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

#include "common/network/AddressUtil.h"
#include "neteng/fboss/bgp/cpp/BgpServiceUtil.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"

using namespace facebook::neteng::fboss::bgp_attr;
using namespace facebook::neteng::fboss::bgp::thrift;
using namespace facebook::nettools::bgplib;

using facebook::nettools::bgplib::getCurrentTimeMs;
using facebook::network::toBinaryAddress;
using folly::CIDRNetwork;
using folly::IPAddress;
using std::vector;

namespace facebook::bgp {

/*
 * As convention we try to avoid returning using 'pass by reference'
 * instead we return the value as return object. But as thrift
 * service passes us reference to map and not expect return
 * to avoid a copy, follow same style we are passing prefixToPath&
 */
void AdjRib::getNetworks(
    std::map<TIpPrefix, TBgpPath>& prefixToPath,
    const RouteFilterType& type) noexcept {
  switch (type) {
    case RouteFilterType::PRE_FILTER_RECEIVED:
    case RouteFilterType::POST_FILTER_RECEIVED:
      if (!recAddPath_) {
        for (auto itr = adjRibInLiteTree_.begin();
             itr != adjRibInLiteTree_.end();
             itr++) {
          const folly::CIDRNetwork prefix = {itr.ipAddress(), itr.masklen()};
          auto pfxAndPath = convertEntryToPath(prefix, *itr->value(), type);
          if (pfxAndPath.has_value()) {
            prefixToPath[pfxAndPath.value().first] = pfxAndPath.value().second;
          }
        }
      } else {
        for (auto itr = adjRibInPathTree_.begin();
             itr != adjRibInPathTree_.end();
             itr++) {
          const folly::CIDRNetwork prefix = {itr.ipAddress(), itr.masklen()};
          for (const auto& [_, adjRibEntry] : itr->value()) {
            auto pfxAndPath = convertEntryToPath(prefix, *adjRibEntry, type);
            if (pfxAndPath.has_value()) {
              prefixToPath[pfxAndPath.value().first] =
                  pfxAndPath.value().second;
            }
          }
        }
      }
      break;
    case RouteFilterType::PRE_FILTER_ADVERTISED:
    case RouteFilterType::POST_FILTER_ADVERTISED: {
      /*
       * With update groups, an in-sync peer's RIB-OUT entries live under the
       * group owner key; resolve the entry visible to this peer (peer-owned,
       * shared group, or omitted) via the version-gated resolver.
       *
       * A peer unregistered from its update group (session down) has no
       * RIB-OUT left to report. See AdjRibOutGroup::unregisterPeer.
       */
      if (!adjRibOutGroup_) {
        break;
      }
      const auto sharingVersion = getRibOutSharingVersion();
      if (!sendAddPath_) {
        for (auto itr = adjRibOutGroup_->LiteTree_.begin();
             itr != adjRibOutGroup_->LiteTree_.end();
             itr++) {
          const auto* adjRibEntry = adjRibOutGroup_->resolveLiteEntryForPeer(
              itr->value(), getPeerOwnerKey(), sharingVersion);
          if (!adjRibEntry) {
            continue;
          }
          const folly::CIDRNetwork prefix = {itr.ipAddress(), itr.masklen()};
          auto pfxAndPath = convertEntryToPath(prefix, *adjRibEntry, type);
          if (pfxAndPath.has_value()) {
            prefixToPath[pfxAndPath.value().first] = pfxAndPath.value().second;
          }
        }
      } else {
        for (auto itr = adjRibOutGroup_->PathTree_.begin();
             itr != adjRibOutGroup_->PathTree_.end();
             itr++) {
          const folly::CIDRNetwork prefix = {itr.ipAddress(), itr.masklen()};
          adjRibOutGroup_->resolvePathEntriesForPeer(
              itr->value(),
              getPeerOwnerKey(),
              sharingVersion,
              [&](uint32_t /*pathId*/, const AdjRibEntry& adjRibEntry) {
                auto pfxAndPath = convertEntryToPath(prefix, adjRibEntry, type);
                if (pfxAndPath.has_value()) {
                  prefixToPath[pfxAndPath.value().first] =
                      pfxAndPath.value().second;
                }
              });
        }
      }
      break;
    }
    default:
      return;
  }
}

// Get pre/post, in/out networks for fboss thrift service (show CLI)
void AdjRib::getNetworks2(
    std::map<
        neteng::fboss::bgp_attr::TIpPrefix,
        std::vector<neteng::fboss::bgp::thrift::TBgpPath>>& prefixToPath,
    const RouteFilterType& type) noexcept {
  switch (type) {
    case RouteFilterType::PRE_FILTER_RECEIVED:
    case RouteFilterType::POST_FILTER_RECEIVED:
      if (!recAddPath_) {
        for (auto itr = adjRibInLiteTree_.begin();
             itr != adjRibInLiteTree_.end();
             itr++) {
          const folly::CIDRNetwork prefix = {itr.ipAddress(), itr.masklen()};
          auto pfxAndPath =
              convertEntryToPath(prefix, *itr->value(), type, kDefaultPathID);
          if (pfxAndPath.has_value()) {
            prefixToPath[pfxAndPath.value().first].push_back(
                pfxAndPath.value().second);
          }
        }
      } else {
        for (auto itr = adjRibInPathTree_.begin();
             itr != adjRibInPathTree_.end();
             itr++) {
          const folly::CIDRNetwork prefix = {itr.ipAddress(), itr.masklen()};
          for (const auto& [network, adjRibEntry] : itr->value()) {
            auto pfxAndPath =
                convertEntryToPath(prefix, *adjRibEntry, type, network);
            if (pfxAndPath.has_value()) {
              prefixToPath[pfxAndPath.value().first].emplace_back(
                  pfxAndPath.value().second);
            }
          }
        }
      }
      break;
    case RouteFilterType::PRE_FILTER_ADVERTISED:
    case RouteFilterType::POST_FILTER_ADVERTISED: {
      /*
       * With update groups, an in-sync peer's RIB-OUT entries live under the
       * group owner key; resolve the entry visible to this peer (peer-owned,
       * shared group, or omitted) via the version-gated resolver.
       *
       * A peer unregistered from its update group (session down) has no
       * RIB-OUT left to report. See AdjRibOutGroup::unregisterPeer.
       */
      if (!adjRibOutGroup_) {
        break;
      }
      const auto sharingVersion = getRibOutSharingVersion();
      if (!sendAddPath_) {
        for (auto itr = adjRibOutGroup_->LiteTree_.begin();
             itr != adjRibOutGroup_->LiteTree_.end();
             itr++) {
          const auto* adjRibEntry = adjRibOutGroup_->resolveLiteEntryForPeer(
              itr->value(), getPeerOwnerKey(), sharingVersion);
          if (!adjRibEntry) {
            continue;
          }
          const folly::CIDRNetwork prefix = {itr.ipAddress(), itr.masklen()};
          auto pfxAndPath =
              convertEntryToPath(prefix, *adjRibEntry, type, kDefaultPathID);
          if (pfxAndPath.has_value()) {
            prefixToPath[pfxAndPath.value().first].push_back(
                pfxAndPath.value().second);
          }
        }
      } else {
        for (auto itr = adjRibOutGroup_->PathTree_.begin();
             itr != adjRibOutGroup_->PathTree_.end();
             itr++) {
          const folly::CIDRNetwork prefix = {itr.ipAddress(), itr.masklen()};
          adjRibOutGroup_->resolvePathEntriesForPeer(
              itr->value(),
              getPeerOwnerKey(),
              sharingVersion,
              [&](uint32_t pathId, const AdjRibEntry& adjRibEntry) {
                auto pfxAndPath =
                    convertEntryToPath(prefix, adjRibEntry, type, pathId);
                if (pfxAndPath.has_value()) {
                  prefixToPath[pfxAndPath.value().first].emplace_back(
                      pfxAndPath.value().second);
                }
              });
        }
      }
      break;
    }
    default:
      return;
  }
}

namespace {

/*
 * An entry is rejected when policy produced no post-policy attributes for a
 * route that was present pre-policy. getPreIn() and getPreOut() are two names
 * for the same prePolicyAttrs_ member, so this reads correctly for an entry
 * from either tree; which tree it came from is the caller's choice of
 * RejectedNetworksQuery::direction.
 */
bool isRejectedEntry(const AdjRibEntry& adjRibEntry) noexcept {
  return adjRibEntry.getPreIn() != nullptr &&
      adjRibEntry.getPostAttr() == nullptr;
}

/*
 * The policy term that denied the route. getPostInPolicy() and
 * getPostOutPolicy() are likewise two names for the same postPolicyResult_,
 * so the direction does not select between them. Empty when the entry records
 * no term for the rejection.
 */
std::string rejectPolicyName(const AdjRibEntry& adjRibEntry) noexcept {
  const auto& policy = adjRibEntry.getPostOutPolicy();
  return policy ? *policy : std::string{};
}

/*
 * The wire form of the prefix. Split out because it heap-allocates, so the
 * walk can build it only when a filter needs it or the entry is being kept.
 */
TIpPrefix toRejectedPrefix(
    const folly::CIDRNetwork& prefix,
    TBgpAfi afi) noexcept {
  TIpPrefix tPrefix;
  tPrefix.afi() = afi;
  tPrefix.num_bits() = prefix.second;
  tPrefix.prefix_bin() =
      toBinaryAddress(prefix.first).addr().value().toStdString();
  return tPrefix;
}

} // namespace

void AdjRib::maybeAddRejectedNetwork(
    RejectedNetworks& rejected,
    const folly::CIDRNetwork& prefix,
    const AdjRibEntry& adjRibEntry,
    const RejectedNetworksQuery& query) noexcept {
  if (!isRejectedEntry(adjRibEntry) ||
      (query.excludeStale && adjRibEntry.isStale())) {
    return;
  }

  const auto afi = prefix.first.isV4() ? TBgpAfi::AFI_IPV4 : TBgpAfi::AFI_IPV6;
  if (query.afi.has_value() && *query.afi != afi) {
    return;
  }

  /*
   * Both filters allocate to build the key they match on -- the TIpPrefix for
   * the prefix filter, the policy-name string for the regex -- so ordering
   * cannot avoid an allocation for an entry that survives; a kept entry
   * carries both. What ordering can avoid is materializing a key no filter
   * asked for, on an entry that is about to be discarded.
   *
   * An explicit prefix list is the selective filter, so when one is set it
   * runs first and the policy-name string is not built for what it drops.
   * When it is not set, the TIpPrefix build is deferred past the policy-name
   * filter, so a narrow policy regex does not pay it for every rejected entry
   * it then discards.
   */
  TIpPrefix tPrefix;
  const bool hasPrefixFilter = !query.prefixFilter.empty();
  if (hasPrefixFilter) {
    tPrefix = toRejectedPrefix(prefix, afi);
    if (!query.prefixFilter.contains(tPrefix)) {
      return;
    }
  }

  auto policyName = rejectPolicyName(adjRibEntry);
  if (query.policyNameFilter &&
      !re2::RE2::PartialMatch(policyName, *query.policyNameFilter)) {
    return;
  }

  ++rejected.pathCountByPolicy[policyName];
  if (rejected.entries.size() >= query.maxEntries) {
    rejected.truncated = true;
    return;
  }

  if (!hasPrefixFilter) {
    tPrefix = toRejectedPrefix(prefix, afi);
  }

  rejected.entries.emplace_back(
      RejectedNetwork{
          .prefix = std::move(tPrefix), .policyName = std::move(policyName)});
}

void AdjRib::getRejectedNetworks(
    RejectedNetworks& rejected,
    const RejectedNetworksQuery& query) noexcept {
  switch (query.direction) {
    case TDirectionFilter::BOTH:
      /*
       * A walk resolves against a single Adj-RIB, so BOTH has no meaning
       * here. The thrift handler refuses it before building a query; this arm
       * exists so an unchecked caller gets nothing rather than one arbitrary
       * side.
       */
      break;
    case TDirectionFilter::INGRESS:
      if (!recAddPath_) {
        for (auto itr = adjRibInLiteTree_.begin();
             itr != adjRibInLiteTree_.end();
             itr++) {
          const folly::CIDRNetwork prefix = {itr.ipAddress(), itr.masklen()};
          maybeAddRejectedNetwork(rejected, prefix, *itr->value(), query);
        }
      } else {
        for (auto itr = adjRibInPathTree_.begin();
             itr != adjRibInPathTree_.end();
             itr++) {
          const folly::CIDRNetwork prefix = {itr.ipAddress(), itr.masklen()};
          for (const auto& [_, adjRibEntry] : itr->value()) {
            maybeAddRejectedNetwork(rejected, prefix, *adjRibEntry, query);
          }
        }
      }
      break;
    case TDirectionFilter::EGRESS: {
      /*
       * Mirrors getNetworks2: with update groups an in-sync peer's RIB-OUT
       * entries live under the group owner key, so resolve the entry visible
       * to this peer. A null resolve result means the entry is omitted for
       * this peer, which is not the same as being rejected by policy.
       *
       * A peer unregistered from its update group (session down) has no
       * RIB-OUT left to report. See AdjRibOutGroup::unregisterPeer.
       *
       * What a group entry can and cannot carry -- no CRF at group level, and
       * the detached-peer version gate -- is documented on
       * AdjRib::getRejectedNetworks.
       */
      if (!adjRibOutGroup_) {
        break;
      }
      const auto sharingVersion = getRibOutSharingVersion();
      if (!sendAddPath_) {
        for (auto itr = adjRibOutGroup_->LiteTree_.begin();
             itr != adjRibOutGroup_->LiteTree_.end();
             itr++) {
          const auto* adjRibEntry = adjRibOutGroup_->resolveLiteEntryForPeer(
              itr->value(), getPeerOwnerKey(), sharingVersion);
          if (!adjRibEntry) {
            continue;
          }
          const folly::CIDRNetwork prefix = {itr.ipAddress(), itr.masklen()};
          maybeAddRejectedNetwork(rejected, prefix, *adjRibEntry, query);
        }
      } else {
        for (auto itr = adjRibOutGroup_->PathTree_.begin();
             itr != adjRibOutGroup_->PathTree_.end();
             itr++) {
          const folly::CIDRNetwork prefix = {itr.ipAddress(), itr.masklen()};
          adjRibOutGroup_->resolvePathEntriesForPeer(
              itr->value(),
              getPeerOwnerKey(),
              sharingVersion,
              [&](uint32_t /*pathId*/, const AdjRibEntry& adjRibEntry) {
                maybeAddRejectedNetwork(rejected, prefix, adjRibEntry, query);
              });
        }
      }
      break;
    }
  }
}

std::optional<std::pair<TIpPrefix, TBgpPath>> AdjRib::convertEntryToPath(
    const folly::CIDRNetwork& prefix,
    const AdjRibEntry& adjRibEntry,
    const RouteFilterType& type,
    const std::optional<uint32_t>& pathId) noexcept {
  TBgpPath path;
  TIpPrefix tPrefix;
  std::shared_ptr<const std::string> postInPolicy;
  std::shared_ptr<const std::string> postOutPolicy;
  switch (type) {
    case RouteFilterType::PRE_FILTER_RECEIVED:
      if (!adjRibEntry.getPreIn()) {
        // Not a learnt route from this peer
        return std::nullopt;
      }
      path = createTBgpPath(*(adjRibEntry.getPreIn()));
      postInPolicy = adjRibEntry.getPostInPolicy();
      if (postInPolicy) {
        path.policy_name() = *postInPolicy;
      }
      break;
    case RouteFilterType::POST_FILTER_RECEIVED:
      if (!adjRibEntry.getPostAttr()) {
        return std::nullopt;
      }
      path = createTBgpPath(*(adjRibEntry.getPostAttr()));
      postInPolicy = adjRibEntry.getPostInPolicy();
      if (postInPolicy) {
        path.policy_name() = *postInPolicy;
      }
      break;
    case RouteFilterType::PRE_FILTER_ADVERTISED:
      if (!adjRibEntry.getPreOut()) {
        return std::nullopt;
      }
      path = createTBgpPath(*(adjRibEntry.getPreOut()));
      postOutPolicy = adjRibEntry.getPostOutPolicy();
      if (postOutPolicy) {
        path.policy_name() = *postOutPolicy;
      }
      break;
    case RouteFilterType::POST_FILTER_ADVERTISED:
      if (!adjRibEntry.getPostAttr()) {
        return std::nullopt;
      }
      auto postOutAttrs = adjRibEntry.getPostAttr();
      if (postOutAttrs) {
        auto nextHop = getNewNexthopFromAttributesOut(
            prefix.first.isV4(),
            postOutAttrs,
            adjRibEntry.isNexthopSetByPolicy());
        path = createTBgpPath(*postOutAttrs);
        if (!nextHop.empty()) {
          auto tNexthop = createTIpPrefix(nextHop);
          path.next_hop() = std::move(tNexthop);
        }
      }
      postOutPolicy = adjRibEntry.getPostOutPolicy();
      if (postOutPolicy) {
        path.policy_name() = *postOutPolicy;
      }
      break;
  }
  if (pathId.has_value()) {
    path.path_id() = pathId.value();
  }
  path.last_modified_time() = adjRibEntry.getLastUpdateRcvdTime();

  auto binAddr = toBinaryAddress(prefix.first);

  if (prefix.first.family() == AF_INET) {
    tPrefix.afi() = TBgpAfi::AFI_IPV4;
  } else {
    tPrefix.afi() = TBgpAfi::AFI_IPV6;
  }
  tPrefix.num_bits() = prefix.second;
  tPrefix.prefix_bin() = binAddr.addr()->toStdString();
  return std::make_pair(tPrefix, path);
}

} // namespace facebook::bgp
