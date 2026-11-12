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

#include "Config.h"

#include <limits>
#include <string>

#include <folly/FileUtil.h>
#include <folly/String.h>
#include <folly/container/F14Set.h>
#include <folly/json/json.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <fb303/ThreadCachedServiceData.h>
#include "configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h"
#include "configerator/structs/neteng/netwhoami/gen-cpp2/netwhoami_types.h"
#include "neteng/fboss/bgp/cpp/common/BgpError.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/FeatureFlags.h"
#include "neteng/fboss/bgp/cpp/config/ConfigStructs.h"
#include "neteng/fboss/bgp/cpp/config/ConfigUtils.h"
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyManager.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyUtils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

using apache::thrift::TEnumTraits;
using facebook::bgp::thrift::BgpNetwork;
using facebook::nettools::bgplib::constants::kBgpPort;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::SocketAddress;
using std::string;

namespace {

constexpr char netWhoAmIFile[] = "/etc/netwhoami.json";

/*
 * Pre-validate JSON syntax using folly::parseJson() which reports line
 * numbers in error messages (e.g. "json parse error on line 42 near ...").
 * Thrift's SimpleJSONSerializer gives no positional context on parse errors.
 */
void validateJsonSyntax(
    const std::string& contents,
    const std::string& filePath) {
  try {
    folly::parseJson(contents);
  } catch (const std::exception& ex) {
    XLOGF(ERR, "JSON syntax error in {}: {}", filePath, ex.what());
    throw;
  }
}

template <class T>
auto castToOptionalOrForward(apache::thrift::optional_field_ref<T> t) {
  return t.to_optional();
}

template <class T>
T& castToOptionalOrForward(T& t) {
  return t;
}

/*
 * This method is used for setting per peer config based on peer group config
 * and peer specific overrides.
 * @params: Getter - functor that gets member from peer
 *          std::optional<thrift::PeerGroup> - peer group config
 *          BgpPeer - peer specific overrides
 * @returns: std::optional<Field> - peer member
 * E.g. getValue([](auto&& peer) {
 *                 // `peer` can be thrift::PeerGroup or thrift::BgpPeer
 *                 return peer.is_confed_peer();
 *               },
 *               peerGroup,
 *               peer)
 */

template <typename Getter>
auto getValue(
    Getter getter,
    const std::optional<facebook::bgp::thrift::PeerGroup>& peerGroup,
    const facebook::bgp::thrift::BgpPeer& peer)
    -> decltype(std::make_optional(getter(peer).value())) {
  if (getter(peer)) {
    return std::make_optional(getter(peer).value());
  }
  if (peerGroup.has_value()) {
    return castToOptionalOrForward(getter(*peerGroup));
  }
  return {};
}

/*
 * Resolve a peer's two-representation AS field (e.g. remote_as / local_as) with
 * the peer > peer-group cascade where the per-peer value wins as a unit: the
 * peer-group is consulted only when the peer sets neither representation.
 * Within one level the 4-byte field (RFC 6793) overrides the deprecated i32
 * field.
 *
 * Both representations set at the SAME level is an ambiguous config and is
 * rejected with a BgpError. fieldName names the i32 field (the 4-byte field is
 * assumed to be "<fieldName>_4_byte") and is used only in the diagnostic.
 * Returns nullopt when the field is unset at every level.
 *
 * Note: a per-peer value wins outright, so a peer-group that sets both
 * representations is only rejected for peers that do not override it.
 */
template <typename FourByteGetter, typename LegacyGetter>
std::optional<uint32_t> resolveCascadedAsn(
    const facebook::bgp::thrift::BgpPeer& peer,
    const std::optional<facebook::bgp::thrift::PeerGroup>& peerGroup,
    FourByteGetter fourByteGetter,
    LegacyGetter legacyGetter,
    std::string_view fieldName) {
  auto resolveLevel = [&](auto&& cfg,
                          std::string_view level) -> std::optional<uint32_t> {
    const auto legacyAs = legacyGetter(cfg).to_optional();
    const auto fourByteAs = fourByteGetter(cfg).to_optional();
    if (legacyAs.has_value() && fourByteAs.has_value()) {
      throw facebook::bgp::BgpError(
          fmt::format(
              "Both {0}_4_byte and {0} are set at {1} level for peer {2}. Use {0}_4_byte only",
              fieldName,
              level,
              *peer.peer_addr()));
    }
    if (fourByteAs.has_value()) {
      return static_cast<uint32_t>(*fourByteAs);
    }
    if (legacyAs.has_value()) {
      return static_cast<uint32_t>(*legacyAs);
    }
    return std::nullopt;
  };

  if (auto peerLevel = resolveLevel(peer, "peer")) {
    return peerLevel;
  }
  if (peerGroup.has_value()) {
    return resolveLevel(*peerGroup, "peer-group");
  }
  return std::nullopt;
}
} // namespace

namespace facebook::bgp {

namespace {

template <typename PeerMap>
void addReferencedPeerPolicies(
    const PeerMap& peers,
    folly::F14FastSet<std::string>& policyNames) {
  for (const auto& [_, peer] : peers) {
    const auto& peerConfig = peer->commonPeerGroupConfig;
    if (peerConfig.ingressPolicyName) {
      policyNames.insert(*peerConfig.ingressPolicyName);
    }
    if (peerConfig.egressPolicyName) {
      policyNames.insert(*peerConfig.egressPolicyName);
    }
  }
}

/*
 * The policy file is shared across devices, so validating every statement
 * would reject switches because of policies they never apply. The resolved
 * peer maps already include peer-group inheritance and peer overrides; local
 * route policies are stored on the normalized global config.
 */
folly::F14FastSet<std::string> getReferencedPolicyNames(
    const Config& config,
    const BgpGlobalConfig& globalConfig) {
  folly::F14FastSet<std::string> policyNames;
  addReferencedPeerPolicies(config.getPeerToConfig(), policyNames);
  addReferencedPeerPolicies(config.getDynamicPeerToConfig(), policyNames);

  const auto addNetworkPolicies = [&policyNames](const auto& networks) {
    for (const auto& [_, network] : networks) {
      if (network.policy_name()) {
        policyNames.insert(*network.policy_name());
      }
    }
  };
  addNetworkPolicies(globalConfig.networksV4);
  addNetworkPolicies(globalConfig.networksV6);
  return policyNames;
}

} // namespace

std::shared_ptr<Config> Config::createConfigFromFile(
    const std::string& configFileName) const {
  XLOGF(DBG1, "Creating config from file: {}", configFileName);
  return std::make_shared<Config>(configFileName);
}

std::shared_ptr<const Config> Config::createConfig(
    thrift::BgpConfig config) const {
  return std::make_shared<const Config>(
      std::move(config), std::nullopt, splitConfigPolicy());
}

// Create a policy manager from the given config.
std::shared_ptr<PolicyManager> Config::createPolicyManager(
    const std::shared_ptr<Config>& config) {
  if (config == nullptr) {
    XLOG(ERR, "createPolicyManager failed: No config supplied");
    return nullptr;
  }
  // Policy manager will be created only if policies are configured
  std::shared_ptr<PolicyManager> policyManager;
  if (config->arePoliciesConfigured()) {
    // Populate policy config and create policy manager
    auto policies = config->getPolicies();
    const auto globalConfig = config->getBgpGlobalConfig();
    validateSwitchIdEncoding(
        *policies,
        getReferencedPolicyNames(*config, *globalConfig),
        getSwitchId(globalConfig->deviceName));
    policyManager =
        std::make_shared<PolicyManager>(*policies, globalConfig.get());
  } else {
    XLOG(INFO, "policies are not configured.");
  }
  config->verifyIfPoliciesExist(policyManager);
  return policyManager;
}

/*
 * @brief  Reset BgpConfig fields relevant to Policy configuration
 *
 * @param  config  BgpConfig struct from which policy fields to be
 *                 reset
 *
 * @return void
 */
void Config::resetPolicyConfig(thrift::BgpConfig& config) {
  config.communities() = {};
  config.localprefs() = {};
  config.policies() = {};

  return;
}

/*
 * @brief  Read the policy content from file and set them to variable
 *         holding BGP related all configurations
 *
 * @param  configFile  Input file in json format
 *
 * @return void
 */
void Config::setPolicyConfigFromFile(const string& configFile) {
  string contents;
  thrift::BgpConfig config;

  splitConfigPolicy_ = true;
  resetPolicyConfig(config_);

  if (!folly::readFile(configFile.c_str(), contents)) {
    XLOGF(ERR, "Could not read policy config file: {}", configFile);
    return;
  }

  validateJsonSyntax(contents, configFile);

  try {
    apache::thrift::SimpleJSONSerializer().deserialize(contents, config);
  } catch (const std::exception& ex) {
    XLOGF(
        ERR,
        "Could not parse policy config file : {}",
        folly::exceptionStr(ex));
    throw;
  }

  config_.communities().copy_from(config.communities());
  config_.localprefs().copy_from(config.localprefs());
  config_.policies().copy_from(config.policies());
}

void Config::setConfigFromFile(const string& configFile) {
  string contents;
  if (!folly::readFile(configFile.c_str(), contents)) {
    throw BgpError(fmt::format("Could not read config file: {}", configFile));
  }

  validateJsonSyntax(contents, configFile);

  auto jsonSerializer = apache::thrift::SimpleJSONSerializer();
  try {
    jsonSerializer.deserialize(contents, config_);
  } catch (const std::exception& ex) {
    XLOGF(ERR, "Could not parse BgpConfig struct: {}", folly::exceptionStr(ex));
    throw;
  }
}

std::string Config::getRunningConfig() const {
  auto jsonSerializer = apache::thrift::SimpleJSONSerializer();
  std::string configStr;
  thrift::BgpConfig config = folly::copy(getConfig());

  try {
    jsonSerializer.serialize(config, &configStr);
  } catch (const std::exception& ex) {
    XLOGF(ERR, "Could not serialize config: {}", folly::exceptionStr(ex));
  }

  return configStr;
}

/**
 * @brief  getPolicyConfig from a specific config class instance
 *         function specifically picks up tuple of communities,
 *         localprefs and policies in the format of JSON
 *         serializer
 *
 *         The function returns meaningful data only in the case
 *         when BGP daemon is running with split config policy.
 *         Otherwise an empty string is returned (policy from
 *         --config artifact won't be returned in this case)
 *
 * @param  void
 *
 * @return std::string Policy Configuration JSON serialized to string
 */
std::string Config::getPolicyConfig() const {
  std::string configStr;
  thrift::BgpPolicyConfig config = {};

  if (!splitConfigPolicy_) {
    return configStr;
  }

  config.communities().copy_from(config_.communities());
  config.localprefs().copy_from(config_.localprefs());
  config.policies().copy_from(config_.policies());

  try {
    apache::thrift::SimpleJSONSerializer::serialize(config, &configStr);
  } catch (const std::exception& ex) {
    XLOGF(ERR, "Could not serialize policy: {}", folly::exceptionStr(ex));
  }

  return configStr;
}

/*
 * Verify that ingress and egress policies configured for each peer
 * exist in policy config
 */
void Config::verifyIfPoliciesExist(
    const std::shared_ptr<const PolicyManager>& policy) const {
  for (const auto& peer : *config_.peers()) {
    if (peer.ingress_policy_name()) {
      if ((!policy) ||
          (!policy->isPolicyPresent(*peer.ingress_policy_name()))) {
        throw BgpError(
            fmt::format(
                "Missing ingress policy ({}) needed for peer ({})",
                *peer.ingress_policy_name(),
                *peer.peer_addr()));
      }
    }

    if (peer.egress_policy_name()) {
      if ((!policy) || (!policy->isPolicyPresent(*peer.egress_policy_name()))) {
        throw BgpError(
            fmt::format(
                "Missing egress policy ({}) needed for peer ({})",
                *peer.egress_policy_name(),
                *peer.peer_addr()));
      }
    }
  }

  if (config_.peer_groups()) {
    for (const auto& peerGroup : *config_.peer_groups()) {
      if (peerGroup.ingress_policy_name()) {
        if ((!policy) ||
            (!policy->isPolicyPresent(*peerGroup.ingress_policy_name()))) {
          throw BgpError(
              fmt::format(
                  "Missing ingress policy ({}) needed for peer group ({})",
                  *peerGroup.ingress_policy_name(),
                  *peerGroup.name()));
        }
      }

      if (peerGroup.egress_policy_name()) {
        if ((!policy) ||
            (!policy->isPolicyPresent(*peerGroup.egress_policy_name()))) {
          throw BgpError(
              fmt::format(
                  "Missing egress policy ({}) needed for peer group ({})",
                  *peerGroup.egress_policy_name(),
                  *peerGroup.name()));
        }
      }
    }
  }

  verifyPlatformPolicies(policy);
}

std::optional<int64_t> Config::getLinkBandwidthBps(const std::string& lbwStr) {
  // Delegate to PolicyUtils for consistent parsing logic
  return parseLinkBandwidthBps(lbwStr);
}

std::optional<float> Config::getLinkBandwidthBytesPerSec(
    const std::string& lbwStr,
    const thrift::BgpPeer& peer) {
  // if lbw is not auto, delegate to PolicyUtils for consistent parsing
  if (lbwStr != kAutoLbwBps) {
    return parseLinkBandwidthBytesPerSec(lbwStr);
  }

  // if lbw is auto, get it from fsdb
  std::optional<int64_t> autoLbwBps = std::nullopt; // Bytes per second
  auto MBpsToBps = [&autoLbwBps](int mbps, const std::string& peerAddr) {
    auto bits = mbps * BpsPerMBps;
    // from bits to Bytes
    autoLbwBps = bits / 8;
  };

  // get auto lbw from fsdb
  if (!peerSubnetLbwMap_.has_value() && !peerSubnetLbwMapFetched_) {
    peerSubnetLbwMapFetched_ = true;
    peerSubnetLbwMap_ = fetchPeerSubnetLbwMap_();
  }

  if (peerSubnetLbwMap_ && folly::IPAddress::validate(*peer.peer_addr())) {
    // static peer
    auto peerAddr = folly::IPAddress(*peer.peer_addr());
    for (const auto& [peerSubnet, mbps] : *peerSubnetLbwMap_) {
      if (peerAddr.isV4() == peerSubnet.first.isV4() &&
          peerAddr.inSubnet(peerSubnet.first, peerSubnet.second)) {
        MBpsToBps(mbps, peerAddr.str());
        break;
      }
    }
  }
  if (lbwStr == kAutoLbwBps) {
    if (autoLbwBps) {
      return *autoLbwBps;
    }
  }
  return std::nullopt;
}

BgpUcmpQuantizer Config::createBgpUcmpQuantizer(
    const thrift::BgpUcmpQuantizerConfig& quantizerConfig) {
  auto minStepBps = getLinkBandwidthBps(*quantizerConfig.min_step_bps());
  if (!minStepBps.has_value() || *minStepBps == 0) {
    throw BgpError(
        fmt::format(
            "create BgpUcmpQuantizer failed: invalid min_step_bps {}",
            *quantizerConfig.min_step_bps()));
  }

  std::vector<uint64_t> bpsList{};
  if (quantizerConfig.fixed_quantized_bps_list()->size() == 0) {
    throw BgpError(
        "create BgpUcmpQuantizer failed: empty fixed_quantized_bps_list_ref!");
  }
  for (const auto& bpsStr : *quantizerConfig.fixed_quantized_bps_list()) {
    auto bps = getLinkBandwidthBps(bpsStr);
    if (!bps.has_value() || *bps == 0) {
      throw BgpError(
          fmt::format(
              "create BgpUcmpQuantizer failed: fixed_quantized_bps_list {}",
              bpsStr));
    }
    bpsList.emplace_back(std::move(bps.value()));
  }

  if (*quantizerConfig.error_pct_threshold() < 0 ||
      *quantizerConfig.error_pct_threshold() >= 1) {
    throw BgpError(
        fmt::format(
            "create BgpUcmpQuantizer failed: invalid error_pct_threshold {}",
            *quantizerConfig.error_pct_threshold()));
  }
  return BgpUcmpQuantizer(
      minStepBps.value(), *quantizerConfig.error_pct_threshold(), bpsList);
}

// TODO: split the giant function into smaller pieces for UT purpose
BgpCommonPeerGroupConfig Config::createCommonPeerGroupConfig(
    const thrift::BgpPeer& peer) {
  // get peer group config if exists
  std::optional<thrift::PeerGroup> peerGroup = std::nullopt;
  if (peer.peer_group_name().has_value()) {
    auto it = peerGroups_.find(peer.peer_group_name().value());
    if (it == peerGroups_.end()) {
      throw BgpError(
          fmt::format(
              "Unsupported config: peer_group '{}' does not exist for peer {}. ",
              peer.peer_group_name().value(),
              *peer.peer_addr()));
    }
    peerGroup = it->second;
  }

  /*
   * TODO: change required fields in BgpPeer to optional:
   * remote_as, local_addr, next_hop4, next_hop6
   */

  // connectMode
  std::optional<TBgpSessionConnectMode> connectMode(
      TBgpSessionConnectMode::PASSIVE_ACTIVE);
  if (peerGroup && peerGroup->is_passive().has_value()) {
    connectMode = *peerGroup->is_passive()
        ? TBgpSessionConnectMode::PASSIVE_ONLY
        : TBgpSessionConnectMode::PASSIVE_ACTIVE;
  }
  if (peer.is_passive().has_value()) {
    connectMode = *peer.is_passive() ? TBgpSessionConnectMode::PASSIVE_ONLY
                                     : TBgpSessionConnectMode::PASSIVE_ACTIVE;
  }

  // bgp timers
  std::optional<std::chrono::seconds> holdTime;
  std::optional<std::chrono::seconds> keepAliveTime;
  std::optional<std::chrono::seconds> outDelayTime;
  std::optional<std::chrono::seconds> gracefulRestartTime;

  // first from peer group
  if (peerGroup && peerGroup->bgp_peer_timers().has_value()) {
    holdTime = std::chrono::seconds(
        *peerGroup->bgp_peer_timers()->hold_time_seconds());
    keepAliveTime = std::chrono::seconds(
        *peerGroup->bgp_peer_timers()->keep_alive_seconds());
    outDelayTime = std::chrono::seconds(
        *peerGroup->bgp_peer_timers()->out_delay_seconds());
    if (auto grTime =
            peerGroup->bgp_peer_timers()->graceful_restart_seconds()) {
      gracefulRestartTime = std::chrono::seconds(*grTime);
    }
  }

  // then override from peer config
  if (peer.bgp_peer_timers().has_value()) {
    holdTime =
        std::chrono::seconds(*peer.bgp_peer_timers()->hold_time_seconds());
    keepAliveTime =
        std::chrono::seconds(*peer.bgp_peer_timers()->keep_alive_seconds());
    outDelayTime =
        std::chrono::seconds(*peer.bgp_peer_timers()->out_delay_seconds());
    if (auto grTime = peer.bgp_peer_timers()->graceful_restart_seconds()) {
      gracefulRestartTime = std::chrono::seconds(*grTime);
    }
  }

  if (keepAliveTime && holdTime && (*keepAliveTime * 3 != *holdTime)) {
    XLOGF(
        WARNING,
        "Peer [{}] has invalid timer config: keepAliveTime = {}, "
        "holdTime = {}. holdTime != 3 * keepAliveTime",
        *peer.peer_addr(),
        keepAliveTime->count(),
        holdTime->count());
  }

  /*
   * Only log a meaningful (non-zero) out-delay. out_delay_seconds defaults to 0
   * (no delay), so logging it for every peer is pure noise at scale
   * (1000+ peers, on every config load).
   */
  if (outDelayTime && outDelayTime->count() > 0) {
    XLOGF(
        DBG1,
        "Peer [{}] outDelay = {}",
        *peer.peer_addr(),
        outDelayTime->count());
  }

  if (outDelayTime && *outDelayTime > 15s) {
    XLOGF(
        WARNING,
        "Peer [{}] has a very large out-delay time ({}) configured."
        "Recommended <= 15sec",
        *peer.peer_addr(),
        outDelayTime->count());
  }

  // PeerGroup.peer_tag / Peer.type (legacy configs use type) -> peer_tag
  auto peerTag =
      getValue([](auto&& peer) { return peer.peer_tag(); }, peerGroup, peer);
  if (!peer.peer_tag() && peer.type()) {
    /*
     * Prefer legacy field peer.type over peerGroup.peer_tag if
     * peer.peer_tag is not set.
     */
    peerTag = peer.type().to_optional();
  }

  auto isConfedPeer = getValue(
      [](auto&& peer) { return peer.is_confed_peer(); }, peerGroup, peer);
  auto isRrClient = getValue(
      [](auto&& peer) { return peer.is_rr_client(); }, peerGroup, peer);
  auto nextHopSelf = getValue(
      [](auto&& peer) { return peer.next_hop_self(); }, peerGroup, peer);
  auto disableIpv4Afi = getValue(
      [](auto&& peer) { return peer.disable_ipv4_afi(); }, peerGroup, peer);
  auto disableIpv6Afi = getValue(
      [](auto&& peer) { return peer.disable_ipv6_afi(); }, peerGroup, peer);
  auto advertiseLinkBandwidth = getValue(
      [](auto&& peer) { return peer.advertise_link_bandwidth(); },
      peerGroup,
      peer);
  auto receiveLinkBandwidth = getValue(
      [](auto&& peer) { return peer.receive_link_bandwidth(); },
      peerGroup,
      peer);
  auto lbwStr = getValue(
      [](auto&& peer) { return peer.link_bandwidth_bps(); }, peerGroup, peer);
  std::optional<float> linkBandwidthBps = std::nullopt;
  /*
   * lbwEnabled tracks whether the per-peer config actively advertises or
   * receives link bandwidth (UCMP). It gates the per-peer log lines below (to
   * avoid spam at scale), but NOT the value resolution: linkBandwidthBps is
   * also consumed independently of per-peer config by the policy engine
   * (LbwExtCommunityActionType::SET_LINK_BPS, which CHECK-fails on a missing
   * value) and by AGGREGATE_LOCAL UCMP. Resolving it only when lbwEnabled would
   * crash peers that pull LBW in via a route policy instead of per-peer config.
   */
  const bool lbwEnabled =
      (advertiseLinkBandwidth.has_value() &&
       *advertiseLinkBandwidth == AdvertiseLinkBandwidth::SET_LINK_BPS) ||
      (receiveLinkBandwidth.has_value() &&
       *receiveLinkBandwidth == ReceiveLinkBandwidth::SET_LINK_BPS);
  if (lbwEnabled && !lbwStr.has_value()) {
    // LBW is advertised/received, so we expect the lbw field to be set.
    throw BgpError(
        fmt::format("Peer [{}] link_bandwidth_bps not set", *peer.peer_addr()));
  }

  /*
   * Resolve link-bandwidth value whenever it is configured -- downstream
   * consumers (policy engine, AGGREGATE_LOCAL) read it regardless of per-peer
   * advertise/receive config.
   */
  bool staticPeer = folly::IPAddress::validate(*peer.peer_addr());
  if (lbwStr.has_value()) {
    linkBandwidthBps = getLinkBandwidthBytesPerSec(*lbwStr, peer);

    // Only log when LBW is in use per-peer to avoid per-peer spam at scale.
    if (lbwEnabled) {
      if (!linkBandwidthBps.has_value()) {
        XLOGF(
            WARNING,
            "{} peer [{}] did not get a valid link_bandwidth_bps value, configured lbw {}",
            staticPeer ? "Static" : "Dynamic",
            *peer.peer_addr(),
            *lbwStr);
      } else {
        XLOGF(
            INFO,
            "{} peer [{}] link_bandwidth_bps: {} BytesPerSec, configured lbw: {}",
            staticPeer ? "Static" : "Dynamic",
            *peer.peer_addr(),
            *linkBandwidthBps,
            *lbwStr);
      }
    }
  }

  auto removePrivateAs = getValue(
      [](auto&& peer) { return peer.remove_private_as(); }, peerGroup, peer);
  auto ingressPolicyName = getValue(
      [](auto&& peer) { return peer.ingress_policy_name(); }, peerGroup, peer);
  auto egressPolicyName = getValue(
      [](auto&& peer) { return peer.egress_policy_name(); }, peerGroup, peer);
  auto addPath =
      getValue([](auto&& peer) { return peer.add_path(); }, peerGroup, peer);
  auto v4OverV6Nexthop = getValue(
      [](auto&& peer) { return peer.v4_over_v6_nexthop(); }, peerGroup, peer);
  auto isRedistributePeer = getValue(
      [](auto&& peer) { return peer.is_redistribute_peer(); }, peerGroup, peer);
  auto enforceFirstAs = getValue(
      [](auto&& peer) { return peer.enforce_first_as(); }, peerGroup, peer);
  auto enhancedRouteRefresh = getValue(
      [](auto&& peer) { return peer.enhanced_route_refresh(); },
      peerGroup,
      peer);
  auto routeRefresh = getValue(
      [](auto&& peer) { return peer.route_refresh(); }, peerGroup, peer);
  auto ttlSecurityHops = getValue(
      [](auto&& peer) { return peer.ttl_security_hops(); }, peerGroup, peer);

  // TODO: deprecate i32 asns fields T113736668
  /*
   * Resolve the peer's Local-AS (RFC 7705) with the same peer > peer-group
   * cascade as remote AS, including rejecting both representations set at one
   * level. The config compiler emits a single representation, so this throw is
   * a backstop (and closes the gap where the compiler guards remote_as but not
   * local_as).
   */
  std::optional<uint32_t> peerLocalAs = resolveCascadedAsn(
      peer,
      peerGroup,
      [](auto&& cfg) { return cfg.local_as_4_byte(); },
      [](auto&& cfg) { return cfg.local_as(); },
      "local_as");
  /*
   * Resolve the peer's remote AS with the peer > peer-group cascade
   * (4-byte preferred per RFC 6793). Both representations set at one level
   * throws. Stays nullopt when unset at every level.
   */
  std::optional<uint32_t> remoteAs = resolveCascadedAsn(
      peer,
      peerGroup,
      [](auto&& cfg) { return cfg.remote_as_4_byte(); },
      [](auto&& cfg) { return cfg.remote_as(); },
      "remote_as");

  auto additionalRemoteAsValue = getValue(
      [](auto&& peerConfig) {
        return peerConfig.additional_remote_as_4_byte();
      },
      peerGroup,
      peer);
  if (additionalRemoteAsValue.has_value() &&
      (*additionalRemoteAsValue <= 0 ||
       *additionalRemoteAsValue > std::numeric_limits<uint32_t>::max())) {
    throw BgpError(
        fmt::format(
            "Invalid additional_remote_as_4_byte ({}) for peer {}. Valid range is 1-{}.",
            *additionalRemoteAsValue,
            *peer.peer_addr(),
            std::numeric_limits<uint32_t>::max()));
  }
  const auto additionalRemoteAs = additionalRemoteAsValue.has_value()
      ? std::make_optional(static_cast<uint32_t>(*additionalRemoteAsValue))
      : std::nullopt;

  if (peerLocalAs) {
    if (*peerLocalAs == globalConfig_->localAsn) {
      XLOG(ERR, "Peer-Local-AS is configured with same value as global-AS.");
    } else if (remoteAs) {
      if (*peerLocalAs == *remoteAs) {
        throw BgpError(
            "Peer-Local-AS is configured with same value as remote-AS.");
      } else if (*remoteAs == globalConfig_->localAsn) {
        throw BgpError("Peer-Local-AS configured for non-EBGP peer.");
      }
    }
  }

  auto preFilterLimit =
      getValue([](auto&& peer) { return peer.pre_filter(); }, peerGroup, peer);
  auto postFilterLimit =
      getValue([](auto&& peer) { return peer.post_filter(); }, peerGroup, peer);

  // validate warning limit fileds
  if (preFilterLimit.has_value() && *preFilterLimit->warning_limit() < 0) {
    throw BgpError(
        fmt::format(
            "Invalid value ({}) configed for preWarningLimit field. Valid range >= 0.",
            *preFilterLimit->warning_limit()));
  }

  if (postFilterLimit.has_value() && *postFilterLimit->warning_limit() < 0) {
    throw BgpError(
        fmt::format(
            "Invalid value ({}) configed for postWarningLimit field. Valid range >= 0.",
            *postFilterLimit->warning_limit()));
  }

  // validate max route fileds
  if (preFilterLimit.has_value() && *preFilterLimit->max_routes() < 0) {
    throw BgpError(
        fmt::format(
            "Invalid value ({}) configed for prefilter max routes field. Valid range >= 0.",
            *preFilterLimit->max_routes()));
  }

  if (postFilterLimit.has_value() && *postFilterLimit->max_routes() < 0) {
    throw BgpError(
        fmt::format(
            "Invalid value ({}) configed for postfilter max routes field. Valid range >= 0.",
            *postFilterLimit->max_routes()));
  }

  auto enableStatefulHa = getValue(
      [](auto&& peer) { return peer.enable_stateful_ha(); }, peerGroup, peer);

  // common config
  BgpCommonPeerGroupConfig commonPeerGroupConfig(
      remoteAs.value_or(0), // peerAsn (required uint32_t; 0 when unset)
      std::nullopt, // peerPort, not supported in FBOSS config currently
      peerLocalAs,
      (!peer.local_addr()->empty()
           ? std::optional<SocketAddress>(SocketAddress(*peer.local_addr(), 0))
           : std::nullopt), // bindAddr
      connectMode, // connectMode
      IPAddressV4(*peer.next_hop4()), // nextHopV4
      IPAddressV6(*peer.next_hop6()), // nextHopV6
      preFilterLimit,
      postFilterLimit,
      peer.description().to_optional(), // description
      holdTime, // holdTime
      keepAliveTime, // keepAlive
      outDelayTime, // outDelay
      gracefulRestartTime, // gracefulRestartTime
      isRrClient, // isRrClient
      peerTag.value_or(std::string("UNKNOWN")), // peerTag
      nextHopSelf, // nextHopSelf
      disableIpv4Afi, // disableIpv4Afi
      disableIpv6Afi, // disableIpv6Afi
      ingressPolicyName, // ingressPolicyName
      egressPolicyName, // egressPolicyName
      isConfedPeer, // isConfedPeer
      peer.peer_id().to_optional(), // peerId
      advertiseLinkBandwidth, // advertiseLinkBandwidth
      receiveLinkBandwidth, // receiveLinkBandwidth
      linkBandwidthBps, // link bandwidth bytes-per-sec
      removePrivateAs, // removePrivateAs
      getBgpGlobalConfig()->validateRemoteAs,
      enableStatefulHa, // enableStatefulHa
      addPath,
      v4OverV6Nexthop,
      isRedistributePeer,
      peer.peer_group_name().to_optional(), // peerGroupName
      enforceFirstAs, // enforceFirstAs
      ttlSecurityHops, // ttlSecurityHops
      peer.egress_policy_name().has_value(), // hasEgressPolicyOverride
      enhancedRouteRefresh, // enhancedRouteRefresh
      routeRefresh, // routeRefresh
      additionalRemoteAs // additionalRemoteAs
  );

  return commonPeerGroupConfig;
}

void Config::populatePeerConfigCounters(
    const folly::F14NodeMap<folly::IPAddress, std::shared_ptr<BgpPeerConfig>>&
        peerToConfig) {
  folly::F14FastMap<AdvertiseLinkBandwidth, int64_t> peersAdvertiseCount;
  for (auto& action : TEnumTraits<AdvertiseLinkBandwidth>::values) {
    peersAdvertiseCount.emplace(action, 0);
  }
  folly::F14FastMap<ReceiveLinkBandwidth, int64_t> peersReceiveCount;
  for (auto& action : TEnumTraits<ReceiveLinkBandwidth>::values) {
    peersReceiveCount.emplace(action, 0);
  }
  for (const auto& [_, peerConfig] : peerToConfig) {
    const auto advertise =
        peerConfig->commonPeerGroupConfig.advertiseLinkBandwidth;
    const auto receive = peerConfig->commonPeerGroupConfig.receiveLinkBandwidth;
    if (advertise) {
      auto it = peersAdvertiseCount.find(*advertise);
      if (it != peersAdvertiseCount.end()) {
        it->second += 1;
      }
    }
    if (receive) {
      auto it = peersReceiveCount.find(*receive);
      if (it != peersReceiveCount.end()) {
        it->second += 1;
      }
    }
  }

  // populate advertise counts
  for (auto& [action, count] : peersAdvertiseCount) {
    auto actionStr =
        std::string(TEnumTraits<AdvertiseLinkBandwidth>::findName(action));
    folly::toLowerAscii(actionStr);
    fb303::ThreadCachedServiceData::get()->setCounter(
        fmt::format("bgpd.config.peers.ucmp_advertise.{}", actionStr), count);
  }

  // populate receive counts
  for (auto& [action, count] : peersReceiveCount) {
    auto actionStr =
        std::string(TEnumTraits<ReceiveLinkBandwidth>::findName(action));
    folly::toLowerAscii(actionStr);
    fb303::ThreadCachedServiceData::get()->setCounter(
        fmt::format("bgpd.config.peers.ucmp_receive.{}", actionStr), count);
  }
}

void Config::populateConfigDatabase(
    const std::optional<const BgpSettings>& tunables) {
  // add global config

  // TODO(T113736668): deprecate i32 asns fields
  uint32_t local_as = config_.local_as_4_byte().has_value()
      ? static_cast<uint32_t>(*config_.local_as_4_byte())
      : static_cast<uint32_t>(config_.local_as().value_or(0));

  //  TODO: move the sanity check of local_as and local_confed_as together
  if (config_.local_as_4_byte().has_value() && config_.local_as().has_value()) {
    XLOG(
        ERR,
        "Both local_as_4_byte and local_as are set in the config. Use local_as_4_byte only");
  }

  std::optional<uint32_t> localConfedAsn{std::nullopt};
  if (auto localConfedAs4Byte = config_.local_confed_as_4_byte()) {
    localConfedAsn = *localConfedAs4Byte;
  } else if (auto localConfedAs = config_.local_confed_as()) {
    localConfedAsn = static_cast<uint32_t>(*localConfedAs);
  }

  if (config_.local_confed_as_4_byte() && config_.local_confed_as()) {
    throw BgpError(
        "Both local_confed_as_4_byte and local_confed_as are set in the config. Use local_confed_as_4_byte only");
  }

  std::optional<thrift::BgpSwitchLimitConfig> switchLimitConfig;
  if (config_.switch_limit_config()) {
    switchLimitConfig = *config_.switch_limit_config();
    BgpStats::setUniquePrefixLimit(
        switchLimitConfig->prefix_limit().value_or(0));
    BgpStats::setTotalPathLimit(
        switchLimitConfig->total_path_limit().value_or(0));
    BgpStats::setOverloadProtectionMode(
        *switchLimitConfig->overload_protection_mode());
  } else {
    BgpStats::setUniquePrefixLimit(0);
    BgpStats::setTotalPathLimit(0);
    BgpStats::setOverloadProtectionMode(std::nullopt);
  }

  std::optional<thrift::ThriftServerConfig> thriftServerConfig;
  if (config_.thrift_server_config()) {
    thriftServerConfig = *config_.thrift_server_config();
  }

  std::optional<uint32_t> dynamicPeerLimit{std::nullopt};
  if (FeatureFlags::IsFeatureEnabled("dynamic_peer_limit")) {
    dynamicPeerLimit = dynamicPeerLimit_;
  }

  /*
   * The limit is always enforced. The stream_subscriber_limit feature only
   * lets a bgp_setting layer replace the default with its own value.
   */
  uint32_t streamSubscriberLimit = streamSubscriberLimit_;
  if (FeatureFlags::IsFeatureEnabled("stream_subscriber_limit")) {
    if (auto setting = config_.bgp_setting_config()) {
      if (auto configuredLimit = setting->stream_subscriber_limit()) {
        /*
         * The field is a signed i32. A negative value would wrap to a huge
         * unsigned limit and admit every subscriber, and 0 would lock out the
         * monitors needed to debug the device. Keep the default for both.
         */
        if (*configuredLimit > 0) {
          streamSubscriberLimit = static_cast<uint32_t>(*configuredLimit);
        }
      }
    }
  }

  bool enableNextHopTracking{false};
  bool enableDynamicPolicyEvaluation{false};
  bool enableUpdateGroup{false};
  UpdateGroupConfig updateGroupConfig;
  /* bgp++ always uses egress queue backpressure. */
  const bool enableEgressQueueBackpressure{true};
  bool enableRibAllocatedPathId{false};
  bool enableOptimizedGR{false};
  bool enableAddPathGrReconcile{false};
  bool enableLegacyV4NlriEncoding{false};
  bool enableRouteRefresh{false};
  std::optional<bool> enableStreamSubscriberBackpressure;
  std::vector<std::string> includeInterfaceRegexes{};
  bool enableNetlinkDampening{false};
  bool enableFibOutTracking{false};
  bool enableGrNotification{false};

  if (auto setting = config_.bgp_setting_config()) {
    if (auto fibOutTrackingFlag = setting->enable_fib_out_tracking()) {
      enableFibOutTracking = *fibOutTrackingFlag;
    }
    if (auto grNotificationFlag = setting->enable_gr_notification()) {
      enableGrNotification = *grNotificationFlag;
    }
    if (auto nexthopTrackingFlag = setting->enable_next_hop_tracking()) {
      enableNextHopTracking = *nexthopTrackingFlag;
    }
    if (auto dynamicPolicyEvaluationFlag =
            setting->enable_dynamic_policy_evaluation()) {
      enableDynamicPolicyEvaluation = *dynamicPolicyEvaluationFlag;
    }
    if (auto updateGroupFlag = setting->enable_update_group()) {
      enableUpdateGroup = *updateGroupFlag;
    }
    if (auto ribAllocatedPathIdFlag = setting->enable_rib_allocated_path_id()) {
      enableRibAllocatedPathId = *ribAllocatedPathIdFlag;
    }
    if (auto regexes = setting->include_interface_regexes()) {
      includeInterfaceRegexes = *regexes;
    }
    if (auto netlinkDampeningFlag = setting->enable_netlink_dampening()) {
      enableNetlinkDampening = *netlinkDampeningFlag;
    }
    if (auto optimizedGRFlag = setting->enable_optimized_GR()) {
      enableOptimizedGR = *optimizedGRFlag;
    }
    if (auto addPathGrReconcileFlag = setting->enable_addpath_gr_reconcile()) {
      enableAddPathGrReconcile = *addPathGrReconcileFlag;
    }
    if (auto legacyV4NlriEncodingFlag =
            setting->enable_legacy_v4_nlri_encoding()) {
      enableLegacyV4NlriEncoding = *legacyV4NlriEncodingFlag;
    }
    if (auto routeRefreshFlag = setting->enable_route_refresh()) {
      enableRouteRefresh = *routeRefreshFlag;
    }
    if (auto streamSubscriberBackpressureFlag =
            setting->enable_stream_subscriber_backpressure()) {
      enableStreamSubscriberBackpressure = *streamSubscriberBackpressureFlag;
    }
    if (auto ugConfig = setting->update_group_config()) {
      updateGroupConfig.enableSerializeGroupPdu =
          *ugConfig->enableSerializeGroupPdu();
      updateGroupConfig.allowSlowPeerDetach = *ugConfig->allowSlowPeerDetach();
      updateGroupConfig.slowPeerTimeThreshold =
          std::chrono::milliseconds(*ugConfig->slowPeerTimeThresholdMs());
      updateGroupConfig.slowPeerBlockCountThreshold =
          static_cast<uint32_t>(*ugConfig->slowPeerBlockCountThreshold());
      updateGroupConfig.slowPeerBlockCountWindow =
          std::chrono::milliseconds(*ugConfig->slowPeerBlockCountWindowMs());
    }
  }

  /*
   * RFC 4456 section 1.1 defaults the cluster ID to the BGP identifier. It is
   * configured separately only when redundant reflectors serve one cluster,
   * since they need distinct router ids and a shared cluster id. The wire
   * format is four octets, so reject IPv6 during config loading rather than
   * deferring the failure until peers narrow the value.
   */
  std::optional<IPAddress> configuredClusterId;
  if (auto clusterIdStr = config_.cluster_id().as_const();
      clusterIdStr.has_value()) {
    /*
     * A present-but-empty value is a generated config that rendered nothing,
     * not an operator asking for a cluster ID. Fall back rather than failing
     * the whole config load, but say so: silently reflecting under the router
     * ID is the very thing this field exists to avoid.
     */
    if (clusterIdStr->empty()) {
      XLOGF(WARN, "Empty cluster_id in config, falling back to router_id");
    } else {
      configuredClusterId = IPAddressV4(*clusterIdStr);
    }
  }
  const auto clusterId =
      configuredClusterId.value_or(IPAddress(*config_.router_id()));
  XLOGF(
      DBG1,
      "BGP cluster ID {} ({})",
      clusterId.str(),
      configuredClusterId.has_value() ? "configured"
                                      : "defaulted to router_id");

  globalConfig_ = std::make_shared<BgpGlobalConfig>(
      local_as, /* localAsn */
      IPAddress(*config_.router_id()), /* routerId */
      clusterId, /* clusterId */
      std::chrono::seconds(
          static_cast<uint16_t>(*config_.hold_time())), /* holdTime */
      SocketAddress(
          IPAddress(*config_.listen_addr()),
          static_cast<uint16_t>(*config_.listen_port())), /* bindAddr */
      (config_.graceful_restart_convergence_seconds().has_value()
           ? std::optional<std::chrono::seconds>(std::chrono::seconds(
                 static_cast<uint16_t>(
                     config_.graceful_restart_convergence_seconds().value())))
           : std::nullopt), /* GR hold time */
      createLocalRoutes(*config_.networks4()), /* v4 originated routes */
      createLocalRoutes(*config_.networks6()), /* v6 originated routes */
      localConfedAsn, /* localConfedAsn */
      config_.compute_ucmp_from_link_bandwidth_community().value_or(
          false) /* UCMP rib computation flag */,
      static_cast<uint32_t>(config_.ucmp_width().value()), /* UCMP width */
      config_.ucmp_quantizer_config()
          ? std::optional<BgpUcmpQuantizer>(
                createBgpUcmpQuantizer(*config_.ucmp_quantizer_config()))
          : std::nullopt, /* BgpUcmpQuantizer */
      tunables ? tunables->validateRemoteAs : ValidateRemoteAs{true},
      tunables ? tunables->supportStatefulGr : SupportStatefulGr{true},
      tunables ? tunables->enableServerSocket : EnableServerSocket{true},
      tunables ? tunables->allowLoopbackReflection
               : AllowLoopbackReflection{false},
      CountConfedsInAsPathLen{config_.count_confeds_in_as_path_len().value_or(
          false)}, /* boolean flag to count confed ASN in as-path */
      getDeviceName(), /* local device name */
      switchLimitConfig,
      dynamicPeerLimit,
      streamSubscriberLimit,
      enableNextHopTracking,
      includeInterfaceRegexes,
      enableDynamicPolicyEvaluation,
      thriftServerConfig,
      enableEgressQueueBackpressure,
      enableUpdateGroup,
      updateGroupConfig,
      enableRibAllocatedPathId,
      enableOptimizedGR,
      false /* enablePolicyDefaultAction */,
      enableAddPathGrReconcile,
      enableLegacyV4NlriEncoding,
      enableStreamSubscriberBackpressure,
      enableNetlinkDampening,
      enableRouteRefresh,
      enableFibOutTracking,
      enableGrNotification);

  // populate peer groups
  if (config_.peer_groups().has_value()) {
    for (const auto& peerGroup : config_.peer_groups().value()) {
      peerGroups_.emplace(*peerGroup.name(), peerGroup);
    }
  }

  // add peer configs
  for (const auto& peer : *config_.peers()) {
    auto commonPeerGroupConfig = createCommonPeerGroupConfig(peer);

    BgpStats::initNonGraceful(*(commonPeerGroupConfig.peerTag));

    if (IPAddress::validate(*peer.peer_addr())) {
      // static peer
      auto peerAddr = IPAddress(*peer.peer_addr());
      auto peerConfig =
          std::make_shared<BgpPeerConfig>(peerAddr, commonPeerGroupConfig);
      peerToConfig_[peerAddr] = peerConfig;
      continue;
    }
    // dynamic peer
    auto peerPrefix = IPAddress::createNetwork(*peer.peer_addr());
    auto dynamicPeerConfig = std::make_shared<BgpDynamicPeerConfig>(
        peerPrefix, commonPeerGroupConfig);
    dynamicPeerToConfig_[peerPrefix] = dynamicPeerConfig;
  }

  // Populate ODS counters

  // total ucmp enabled peers counters
  populatePeerConfigCounters(peerToConfig_);

  // ucmp enabled or not
  int ucmpEnabled = globalConfig_->computeUcmpFromLbwComm ? 1 : 0;
  fb303::ThreadCachedServiceData::get()->setCounter(
      "bgpd.config.ucmp_enabled", ucmpEnabled);

  // update group enabled or not
  BgpStats::setUpdateGroupEnabled(globalConfig_->enableUpdateGroup);

  // total configured peers
  BgpStats::setConfiguredPeers(config_.peers()->size());
}

std::optional<const BgpCommonPeerGroupConfig> Config::getConfigOfAPeer(
    const folly::IPAddress& peerAddr) const {
  auto itr = peerToConfig_.find(peerAddr);

  if (itr != peerToConfig_.cend()) {
    return itr->second->commonPeerGroupConfig;
  }

  /*
   * Search in dynamic peers. Number of such prefixes currently are small so
   * searching sequentially is fine. Can optimize later if needed.
   */
  for (const auto& [peerPrefix, dynamicPeerConfig] : dynamicPeerToConfig_) {
    if (peerAddr.inSubnet(peerPrefix.first, peerPrefix.second)) {
      return dynamicPeerConfig->commonPeerGroupConfig;
    }
  }

  return std::nullopt;
}

PeeringParams Config::getPeeringParamsHelper(
    const BgpCommonPeerGroupConfig& config) const {
  PeeringParams params;
  params.globalAs = globalConfig_->localAsn;
  params.localAs =
      static_cast<uint32_t>(config.localAsn.value_or(globalConfig_->localAsn));
  params.remoteAs = config.peerAsn;
  params.additionalRemoteAs = config.additionalRemoteAs;
  // Note that this behavior is different from bgpD
  params.localBgpId = globalConfig_->routerId.asV4();
  params.localClusterId = globalConfig_->clusterId.asV4();
  params.holdTime = config.holdTime.value_or(globalConfig_->holdTime);
  // Check for per-peer/per-group graceful restart time, otherwise use global
  if (config.gracefulRestartTime.has_value()) {
    params.grRestartTime = config.gracefulRestartTime;
  } else {
    params.grRestartTime = globalConfig_->grRestartTime;
  }
  params.peerPort = config.peerPort.value_or(kBgpPort);
  params.bindAddr = config.bindAddr.value_or(folly::AsyncSocket::anyAddress());
  params.nexthopV4 = config.nexthopV4;
  params.nexthopV6 = config.nexthopV6;
  params.preRouteLimit = config.preRouteLimit;
  params.postRouteLimit = config.postRouteLimit;
  params.addPath = config.addPath;

  params.description =
      config.description.value_or(std::string(kDefaultDescription));
  params.isRrClient = config.isRrClient.value_or(false);
  params.nextHopSelf = config.nextHopSelf.value_or(false);
  params.isAfiIpv4Configured = !config.disableIpv4Afi.value_or(false);
  params.isAfiIpv6Configured = !config.disableIpv6Afi.value_or(false);
  params.isConfedPeer = config.isConfedPeer.value_or(false);
  if (params.isConfedPeer) {
    if (!globalConfig_->localConfedAsn) {
      throw BgpError(
          "is_confed_peer is configured, but local_confed_as is missing");
    }
    params.localConfedAs = *globalConfig_->localConfedAsn;
    params.asConfedId = config.localAsn.value_or(globalConfig_->localAsn);
    // need to set localAs as the localConfedAs for session bringing up
    params.localAs = *globalConfig_->localConfedAsn;
  }
  params.peerId = config.peerId.value_or(std::string(kDefaultPeerId));
  params.advertiseLinkBandwidth = config.advertiseLinkBandwidth;
  params.receiveLinkBandwidth = config.receiveLinkBandwidth;
  if (config.linkBandwidthBps.has_value()) {
    params.linkBandwidthBps = *config.linkBandwidthBps;
  }
  params.removePrivateAs = config.removePrivateAs.value_or(false);
  params.peerTag = config.peerTag.value_or(std::string(""));
  params.validateRemoteAs =
      config.validateRemoteAs.value_or(ValidateRemoteAs{true});
  params.allowLoopbackReflection = globalConfig_->allowLoopbackReflection;
  params.enableStatefulHa = config.enableStatefulHa.value_or(false);
  params.v4OverV6Nexthop = config.v4OverV6Nexthop.value_or(false);
  params.isRedistributePeer = config.isRedistributePeer.value_or(false);
  params.peerGroupName = config.peerGroupName;
  params.enforceFirstAs = config.enforceFirstAs.value_or(false);
  params.isEnhancedRouteRefreshConfigured = EnhancedRouteRefreshConfigured{
      config.enhancedRouteRefresh.value_or(false)};
  params.isRouteRefreshConfigured = RouteRefreshConfigured{
      globalConfig_->enableRouteRefresh && config.routeRefresh.value_or(true)};
  params.ttlSecurityHops = config.ttlSecurityHops;
  if (params.ttlSecurityHops.has_value()) {
    auto hops = params.ttlSecurityHops.value();
    if (hops < kMinTtlSecurityHops || hops > kMaxTtlSecurityHops) {
      throw BgpError(
          fmt::format(
              "Invalid value ({}) for ttl_security_hops. Valid range {}-{}.",
              hops,
              kMinTtlSecurityHops,
              kMaxTtlSecurityHops));
    }
  }
  return params;
}

PeeringParams Config::getPeeringParamsForPeer(
    const BgpPeerConfig& peerConfig) const {
  auto params = getPeeringParamsHelper(peerConfig.commonPeerGroupConfig);
  params.peerAddr = peerConfig.peerAddr;
  params.connectMode = peerConfig.commonPeerGroupConfig.connectMode.value_or(
      TBgpSessionConnectMode::PASSIVE_ACTIVE);
  return params;
}

PeeringParams Config::getPeeringParamsForDynamicPeer(
    const BgpDynamicPeerConfig& dynamicPeerConfig) const {
  auto params = getPeeringParamsHelper(dynamicPeerConfig.commonPeerGroupConfig);
  params.connectMode = TBgpSessionConnectMode::PASSIVE_ONLY;
  params.peerPrefix = dynamicPeerConfig.peerPrefix;
  return params;
}

std::unordered_map<folly::CIDRNetwork, BgpNetwork> Config::createLocalRoutes(
    const std::vector<BgpNetwork>& networks) {
  std::unordered_map<folly::CIDRNetwork, BgpNetwork> localRoutes;
  for (const auto& network : networks) {
    auto prefix = IPAddress::createNetwork(*network.prefix());
    localRoutes.emplace(prefix, network);
  }
  return localRoutes;
}

std::unordered_map<folly::CIDRNetwork, BgpNetwork> Config::getLocalRoutes() {
  auto localRoutes = globalConfig_->networksV4;
  localRoutes.insert(
      globalConfig_->networksV6.begin(), globalConfig_->networksV6.end());
  return localRoutes;
}

bool Config::arePoliciesConfigured() const {
  return config_.policies().has_value();
}

std::optional<bgp_policy::BgpPolicies> Config::getPolicies() const {
  return config_.policies().to_optional();
}

std::optional<std::string> Config::getDeviceName() const {
  string contents;
  if (!folly::readFile(netWhoAmIFile, contents)) {
    return std::nullopt;
  }
  facebook::netwhoami::NetWhoAmI netWhoAmI;
  auto jsonSerializer = apache::thrift::SimpleJSONSerializer();
  try {
    jsonSerializer.deserialize(contents, netWhoAmI);
  } catch (const std::exception& ex) {
    XLOGF(ERR, "Could not parse netwhoami.json {}", folly::exceptionStr(ex));
    return std::nullopt;
  }

  return netWhoAmI.name().value();
}

bool Config::isThriftServerTlsEnabled() const {
  if (!globalConfig_->thriftServerConfig.has_value()) {
    return false;
  }
  return *globalConfig_->thriftServerConfig->enable_tls();
}

const std::string Config::getThriftServerCaPath() const {
  if (!isThriftServerTlsEnabled()) {
    return "";
  }

  auto caPath = globalConfig_->thriftServerConfig->x509_ca_path();
  if (!caPath) {
    throw BgpError("TLS is enabled but x509_ca_path is not configured");
  }
  return *caPath;
}

const std::string Config::getThriftServerCertPath() const {
  if (!isThriftServerTlsEnabled()) {
    return "";
  }

  auto certPath = globalConfig_->thriftServerConfig->x509_cert_path();
  if (!certPath) {
    throw BgpError("TLS is enabled but x509_cert_path is not configured");
  }
  return *certPath;
}

const std::string Config::getThriftServerKeyPath() const {
  if (!isThriftServerTlsEnabled()) {
    return "";
  }

  auto keyPath = globalConfig_->thriftServerConfig->x509_key_path();
  if (!keyPath) {
    throw BgpError("TLS is enabled but x509_key_path is not configured");
  }
  return *keyPath;
}

const std::string Config::getThriftServerEccCurveName() const {
  if (!isThriftServerTlsEnabled()) {
    return "";
  }

  auto curveName = globalConfig_->thriftServerConfig->ecc_curve_name();
  if (!curveName) {
    throw BgpError("TLS is enabled but ecc_curve_name is not configured");
  }
  return *curveName;
}

bool Config::isPeerGroupConfigured(const std::string& peerGroupName) const {
  return peerGroups_.find(peerGroupName) != peerGroups_.end();
}

bool Config::validatePeerExists(const std::string& peerAddr) const {
  try {
    // Check static peers using direct map access
    if (folly::IPAddress::validate(peerAddr)) {
      auto ipAddr = folly::IPAddress(peerAddr);
      return peerToConfig_.find(ipAddr) != peerToConfig_.end();
    }

    // Check dynamic peers (prefix format) using direct map access
    auto mayBeNetwork = folly::IPAddress::tryCreateNetwork(peerAddr);
    if (mayBeNetwork.hasValue()) {
      auto prefix = mayBeNetwork.value();
      /*
       * For dynamic peers, check if this prefix matches any configured dynamic
       * peer
       */
      for (const auto& [peerPrefix, _] : dynamicPeerToConfig_) {
        if (prefix.first.inSubnet(peerPrefix.first, peerPrefix.second)) {
          return true;
        }
      }
    }

    return false;
  } catch (const std::exception& ex) {
    XLOGF(
        ERR,
        "Error validating peer existence for '{}': {}",
        peerAddr,
        ex.what());
    return false;
  }
}

bool Config::validatePeerGroupExists(const std::string& peerGroupName) const {
  return isPeerGroupConfigured(peerGroupName);
}

} // namespace facebook::bgp
