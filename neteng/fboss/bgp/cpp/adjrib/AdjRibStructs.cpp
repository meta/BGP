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

#include <fmt/format.h>
#include <folly/hash/Hash.h>

#include "magic_enum/magic_enum.hpp"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibStructs.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::bgp {

bool UpdateGroupKey::operator==(const UpdateGroupKey& other) const {
  return egressPolicyName == other.egressPolicyName &&
      routeFilterStmtName == other.routeFilterStmtName &&
      outDelay == other.outDelay && sessionType == other.sessionType &&
      afiIpv4Negotiated == other.afiIpv4Negotiated &&
      afiIpv6Negotiated == other.afiIpv6Negotiated &&
      isConfedPeer == other.isConfedPeer && isRrClient == other.isRrClient &&
      advertiseLinkBandwidth == other.advertiseLinkBandwidth &&
      receiveLinkBandwidth == other.receiveLinkBandwidth &&
      linkBandwidthBps == other.linkBandwidthBps &&
      removePrivateAsn == other.removePrivateAsn &&
      sendAddPath == other.sendAddPath &&
      as4ByteCapable == other.as4ByteCapable &&
      extNhEncodingCapable == other.extNhEncodingCapable &&
      legacyV4NlriEncoding == other.legacyV4NlriEncoding &&
      peerGroupName == other.peerGroupName &&
      peerOverride == other.peerOverride && localAs == other.localAs &&
      asConfedId == other.asConfedId && vrfId == other.vrfId;
}

size_t UpdateGroupKey::hash() const {
  return folly::hash::hash_combine(
      egressPolicyName.value_or(""),
      routeFilterStmtName,
      outDelay.count(),
      sessionType,
      afiIpv4Negotiated,
      afiIpv6Negotiated,
      isConfedPeer,
      isRrClient,
      advertiseLinkBandwidth,
      receiveLinkBandwidth,
      linkBandwidthBps,
      removePrivateAsn,
      sendAddPath,
      as4ByteCapable,
      extNhEncodingCapable,
      legacyV4NlriEncoding,
      peerGroupName,
      peerOverride,
      localAs,
      asConfedId,
      vrfId);
}

UpdateGroupKey UpdateGroupKey::buildUpdateGroupKey(
    VrfId vrfId,
    std::optional<std::string> policyName,
    std::string routeFilterStmtName,
    std::chrono::seconds outDelay,
    BgpSessionType sessionType,
    bool afiIpv4Negotiated,
    bool afiIpv6Negotiated,
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
    std::optional<uint32_t> asConfedId) {
  return UpdateGroupKey{
      std::move(policyName),
      std::move(routeFilterStmtName),
      outDelay,
      sessionType,
      afiIpv4Negotiated,
      afiIpv6Negotiated,
      isConfedPeer,
      isRrClient,
      advertiseLinkBandwidth,
      receiveLinkBandwidth,
      linkBandwidthBps,
      removePrivateAsn,
      sendAddPath,
      as4ByteCapable,
      extNhEncodingCapable,
      legacyV4NlriEncoding,
      std::move(peerGroupName),
      peerOverride,
      localAs,
      asConfedId,
      vrfId};
}

std::string UpdateGroupKey::toString(const UpdateGroupKey& key) {
  return fmt::format(
      "{}-{}-{}-{}-{}-{}-{}-{}-{}-{}-{}-{}-{}-{}-{}-{}-{}-{}-{}-{}-{}",
      key.egressPolicyName.value_or(""),
      key.routeFilterStmtName,
      key.outDelay.count(),
      magic_enum::enum_name(key.sessionType),
      key.afiIpv4Negotiated,
      key.afiIpv6Negotiated,
      key.isConfedPeer,
      key.isRrClient,
      key.advertiseLinkBandwidth.has_value()
          ? static_cast<int>(*key.advertiseLinkBandwidth)
          : -1,
      key.receiveLinkBandwidth.has_value()
          ? static_cast<int>(*key.receiveLinkBandwidth)
          : -1,
      key.linkBandwidthBps,
      key.removePrivateAsn,
      key.sendAddPath,
      key.as4ByteCapable,
      key.extNhEncodingCapable,
      key.legacyV4NlriEncoding,
      key.peerGroupName,
      key.peerOverride,
      key.localAs,
      key.asConfedId.has_value() ? static_cast<int64_t>(*key.asConfedId) : -1,
      key.vrfId);
}

facebook::neteng::fboss::bgp::thrift::TUpdateGroupKey UpdateGroupKey::toThrift()
    const {
  facebook::neteng::fboss::bgp::thrift::TUpdateGroupKey t;
  t.egress_policy_name() = egressPolicyName.value_or("");
  t.route_filter_stmt_name() = routeFilterStmtName;
  t.out_delay_seconds() = outDelay.count();
  t.session_type() = std::string(magic_enum::enum_name(sessionType));
  t.afi_ipv4_negotiated() = afiIpv4Negotiated;
  t.afi_ipv6_negotiated() = afiIpv6Negotiated;
  t.is_confed_peer() = isConfedPeer;
  t.is_rr_client() = isRrClient;
  if (advertiseLinkBandwidth.has_value()) {
    t.advertise_link_bandwidth() = advertiseLinkBandwidth.value();
  }
  if (receiveLinkBandwidth.has_value()) {
    t.receive_link_bandwidth() = receiveLinkBandwidth.value();
  }
  if (linkBandwidthBps > 0) {
    t.link_bandwidth_bps() = linkBandwidthBps;
  }
  t.remove_private_asn() = removePrivateAsn;
  t.send_add_path() = sendAddPath;
  t.as4_byte_capable() = as4ByteCapable;
  t.ext_nh_encoding_capable() = extNhEncodingCapable;
  t.legacy_v4_nlri_encoding() = legacyV4NlriEncoding;
  t.peer_group_name() = peerGroupName;
  t.peer_override() = peerOverride;
  t.local_as() = localAs;
  if (asConfedId.has_value()) {
    t.as_confed_id() = *asConfedId;
  }
  return t;
}

} // namespace facebook::bgp
