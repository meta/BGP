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

#include <folly/IPAddress.h>
#include <folly/container/F14Set.h>
#include <chrono>

#include "configerator/structs/neteng/fboss/bgp/if/gen-cpp2/bgp_attr_types.h"
#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/common/BgpPath.h"
#include "neteng/fboss/bgp/cpp/common/Types.h"

namespace facebook::neteng::fboss::bgp::thrift {
class TUpdateGroupKey;
} // namespace facebook::neteng::fboss::bgp::thrift

namespace facebook::bgp {

/**
 * UpdateGroupKey defines the criteria for grouping peers into an update group.
 * Peers with identical UpdateGroupKey values belong to the same update group.
 */
struct UpdateGroupKey {
  std::optional<std::string> egressPolicyName;
  std::string routeFilterStmtName; /* peer device regex or peer-group name */
  std::chrono::seconds outDelay{0};
  BgpSessionType sessionType{BgpSessionType::EBGP};
  bool afiIpv4Negotiated{false};
  bool afiIpv6Negotiated{false};
  bool isConfedPeer{false};
  bool isRrClient{false};
  std::optional<neteng::fboss::bgp_attr::AdvertiseLinkBandwidth>
      advertiseLinkBandwidth{std::nullopt};
  std::optional<neteng::fboss::bgp_attr::ReceiveLinkBandwidth>
      receiveLinkBandwidth{std::nullopt};
  uint64_t linkBandwidthBps{0};
  bool removePrivateAsn{false};
  bool sendAddPath{false};
  bool as4ByteCapable{true}; /* Negotiated 4-byte ASN capability */
  bool extNhEncodingCapable{false}; /* Negotiated RFC5549 capability */
  bool legacyV4NlriEncoding{
      false}; /* Peer gets RFC4271 classic v4 NLRI instead of MP_REACH */
  std::string peerGroupName; /* Peer group this peer belongs to */
  bool peerOverride{
      false}; /* Whether peer has per-peer egress policy override */
  /*
   * Local AS advertised to peers in this group. Overridable per peer and per
   * peer-group via the local_as cascade (RFC-7705). The egress attribute
   * transform prepends it to AS_PATH and substitutes it for policy-zeroed
   * ASNs, and it is the ASN in a config-derived LBW ext-community, so peers
   * with different local AS must not share a group.
   */
  uint32_t localAs{0};
  /* Confederation identifier used by the shared AS_PATH transform. */
  std::optional<uint32_t> asConfedId{std::nullopt};
  /* VRF of the group's peers. Peers in different VRFs never share a group. */
  VrfId vrfId{fboss::kDefaultVrf};

  bool operator==(const UpdateGroupKey& other) const;
  size_t hash() const;

  static UpdateGroupKey buildUpdateGroupKey(
      VrfId vrfId,
      std::optional<std::string> policyName,
      std::string routeFilterStmtName,
      std::chrono::seconds outDelay,
      BgpSessionType sessionType,
      bool isAfiIpv4Negotiated,
      bool isAfiIpv6Negotiated,
      bool isConfedPeer,
      bool isRrClient,
      std::optional<neteng::fboss::bgp_attr::AdvertiseLinkBandwidth>
          advertiseLinkBandwidth,
      std::optional<neteng::fboss::bgp_attr::ReceiveLinkBandwidth>
          receiveLinkBandwidth,
      uint64_t linkBandwidthBps,
      bool removePrivateAsn,
      bool sendAddPath,
      bool as4ByteCapable,
      bool extNhEncodingCapable,
      bool legacyV4NlriEncoding,
      std::string peerGroupName,
      bool peerOverride,
      uint32_t localAs,
      std::optional<uint32_t> asConfedId = std::nullopt);

  static std::string toString(const UpdateGroupKey& key);

  facebook::neteng::fboss::bgp::thrift::TUpdateGroupKey toThrift() const;
};

/**
 * Hash specialization for UpdateGroupKey to be used with F14 maps
 */
struct UpdateGroupKeyHash {
  size_t operator()(const UpdateGroupKey& key) const {
    return key.hash();
  }
};

/**
 * AdjRibOutOwnerType explicitly identifies whether a RIB-OUT entry belongs
 * to an individual peer or to an update group. This type-safe approach avoids
 * relying on magic number ID spaces and makes code intent clear.
 */
enum class AdjRibOutOwnerType : uint8_t {
  PEER = 0, // Entry belongs to an individual peer (normal or detached)
  GROUP = 1 // Entry belongs to an update group (shared by in-sync peers)
};

/**
 * AdjRibOutOwnerKey uniquely identifies the owner of a RIB-OUT entry.
 * Uses std::variant to support dual-mode operation where both individual peers
 * and update groups can coexist in the same RIB-OUT tree.
 *
 * For PEER: Uses shared_ptr<BgpPeerId> for proper identity semantics
 * For GROUP: Uses uint64_t group ID
 *
 * Example usage:
 *   - forPeer(peerIdPtr) - Entry for peer
 *   - forGroup(1) - Entry for update group with ID 1
 */
struct AdjRibOutOwnerKey {
  /*
   * A wrapper to hold either 1) peer type key or 2) group type key
   */
  std::variant<std::shared_ptr<nettools::bgplib::BgpPeerId>, uint64_t> value;

  /**
   * Static factory method to create a peer owner key
   */
  static AdjRibOutOwnerKey forPeer(
      std::shared_ptr<nettools::bgplib::BgpPeerId> peerId) {
    return {peerId};
  }

  /**
   * Static factory method to create a group owner key
   */
  static AdjRibOutOwnerKey forGroup(uint64_t groupId) {
    return {groupId};
  }

  /**
   * Check if this key represents a peer
   */
  bool isPeer() const {
    return std::holds_alternative<std::shared_ptr<nettools::bgplib::BgpPeerId>>(
        value);
  }

  /**
   * Check if this key represents a group
   */
  bool isGroup() const {
    return std::holds_alternative<uint64_t>(value);
  }

  /**
   * Get peer ID (must be a peer)
   */
  std::shared_ptr<nettools::bgplib::BgpPeerId> getPeerId() const {
    return std::get<std::shared_ptr<nettools::bgplib::BgpPeerId>>(value);
  }

  /**
   * Get group ID (must be a group)
   */
  uint64_t getGroupId() const {
    return std::get<uint64_t>(value);
  }

  /**
   * Equality comparison - required for F14 map lookup
   */
  bool operator==(const AdjRibOutOwnerKey& other) const {
    return value == other.value;
  }

  /**
   * Inequality comparison
   */
  bool operator!=(const AdjRibOutOwnerKey& other) const {
    return !(*this == other);
  }

  /**
   * Hash function - combines variant index and value
   */
  size_t hash() const {
    if (isPeer()) {
      auto peerId = getPeerId();
      return folly::hash::hash_combine(
          AdjRibOutOwnerType::PEER,
          peerId ? std::hash<nettools::bgplib::BgpPeerId>{}(*peerId) : 0);
    }
    return folly::hash::hash_combine(AdjRibOutOwnerType::GROUP, getGroupId());
  }
};

/**
 * Hash specialization for AdjRibOutOwnerKey to be used with F14 maps
 */
struct AdjRibOutOwnerKeyHash {
  size_t operator()(const AdjRibOutOwnerKey& key) const {
    return key.hash();
  }
};

/**
 * =================== PostPolicyResultCacheT ==========================
 */

/**
 * Below we define two types:
 *   PostPolicyResultT to represent post policy result string
 *   PostPolicyResultCacheT, a global store of PostPolicyResultT
 *   across all AdjRibs.
 *
 * @postPolicyResultCache_ is set of shared_ptrs pointing to
 * strings; both shared_ptr and the string value are owned by the set.
 * Items (policy terms) are pruned when ptr ref count is effectively 0.
 *
 * All AdjRibEntries' postPolicyResult_ will point to an item
 * in @postPolicyResultCache_, reducing the memory footprint from
 * a variable sized string to the size of a shared_ptr.
 *
 * Set lookup requires custom hash and comparator to check
 * string equality, as two string literals are not guaranteed
 * to be equal in memory address even if they are equal in value.
 */
using PostPolicyResultT = std::shared_ptr<const std::string>;

struct PostPolicyResultHash {
  size_t operator()(const PostPolicyResultT& p) const {
    return std::hash<std::string>{}(*p);
  }
};

struct PostPolicyResultCmp {
  bool operator()(const PostPolicyResultT& lhs, const PostPolicyResultT& rhs)
      const {
    return lhs && rhs && *lhs == *rhs;
  }
};

using PostPolicyResultCacheT = folly::
    F14NodeSet<PostPolicyResultT, PostPolicyResultHash, PostPolicyResultCmp>;

/**
 * =================== AttrToPrefixMap ==========================
 */

/**
 * Packing list key: groups prefixes by (attrs, AFI, isNexthopSetByPolicy).
 * The isNexthopSetByPolicy field ensures prefixes with different policy
 * nexthop override flags go into separate packing list buckets.
 *
 * NOTE: This flag is intentionally duplicated with AdjRibEntry::flags_
 * (kNexthopSetByPolicyBit). The two serve different purposes:
 *
 *   - BgpPathWithAfi::isNexthopSetByPolicy (here): Captures the flag at
 *     packing list insertion time, consistent with the attrs snapshot.
 *     Used for advertisement correctness at send time.
 *
 *   - AdjRibEntry::isNexthopSetByPolicy: Reflects the latest state of the
 *     entry. Used for CLI display (AdjRibShow) only.
 *
 * These can diverge under backpressure: when a peer queue is full, the
 * packing list is not drained. If the route is re-announced with a
 * different policy result before the old entry is sent, AdjRibEntry's
 * flag updates to the new value while the pending packing list entry
 * still holds the old flag (correct for that entry's attrs). Reading
 * from AdjRibEntry at send time would use the wrong flag for the
 * pending entry. The key-based flag avoids this race.
 */
struct BgpPathWithAfi {
  std::shared_ptr<const BgpPath> attrs;
  nettools::bgplib::BgpUpdateAfi afi;
  bool isNexthopSetByPolicy{false};
};

/*
 * After T228125215, all BgpPaths stored in AdjRibEntry are deduplicated
 * via DeDuplicatedBgpPath, so pointer identity is sufficient for
 * hashing and comparison.
 */
struct BgpPathHashWithNull {
  size_t operator()(std::shared_ptr<const BgpPath> const& attr) const {
    return std::hash<const BgpPath*>{}(attr.get());
  }
};

struct BgpPathCompareWithNull {
  bool operator()(
      std::shared_ptr<const BgpPath> const& attr1,
      std::shared_ptr<const BgpPath> const& attr2) const {
    return attr1 == attr2;
  }
};

struct BgpPathWithAfiHash {
  size_t operator()(const BgpPathWithAfi& p) const {
    return folly::hash::hash_combine(
        BgpPathHashWithNull{}(p.attrs), p.afi, p.isNexthopSetByPolicy);
  }
};

struct BgpPathWithAfiCmp {
  bool operator()(const BgpPathWithAfi& lhs, const BgpPathWithAfi& rhs) const {
    return BgpPathCompareWithNull{}(lhs.attrs, rhs.attrs) &&
        lhs.afi == rhs.afi &&
        lhs.isNexthopSetByPolicy == rhs.isNexthopSetByPolicy;
  }
};

struct PrefixPathIdHash {
  size_t operator()(const std::pair<folly::CIDRNetwork, uint32_t>& p) const {
    return folly::hash::hash_combine(p.first, p.second);
  }
};

using PrefixSet = folly::
    F14NodeSet<std::pair<folly::CIDRNetwork, uint32_t>, PrefixPathIdHash>;

using AttrToPrefixMap = folly::F14NodeMap<
    BgpPathWithAfi,
    PrefixSet,
    BgpPathWithAfiHash,
    BgpPathWithAfiCmp>;

} // namespace facebook::bgp
