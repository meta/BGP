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

#include <boost/regex.hpp>

#include <folly/IPAddress.h>
#include <folly/SocketAddress.h>
#include <folly/io/async/AsyncSocket.h>
#include "configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h"
#include "configerator/structs/neteng/fboss/bgp/if/gen-cpp2/bgp_attr_types.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/Structs.h"
#include "neteng/fboss/bgp/cpp/common/Types.h"
#include "neteng/fboss/bgp/cpp/common/Utils.h"
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::bgp {

using neteng::fboss::bgp::thrift::TBgpSessionConnectMode;
using neteng::fboss::bgp_attr::AddPath;
using neteng::fboss::bgp_attr::AdvertiseLinkBandwidth;
using neteng::fboss::bgp_attr::ReceiveLinkBandwidth;

/*
 * Set tunable properties of various BGP modules
 * These are typically not explicitly configured, but set by various users of
 * BGP like Bgp++, OpenR-BGP need slighlty different behaviors
 */
struct BgpSettings {
  BgpSettings(
      const ValidateRemoteAs validateRemoteAs = ValidateRemoteAs{true},
      const SupportStatefulGr supportStatefulGr = SupportStatefulGr{true},
      const EnableServerSocket enableServerSocket = EnableServerSocket{true},
      const AllowLoopbackReflection allowLoopbackReflection =
          AllowLoopbackReflection{false})
      : validateRemoteAs(validateRemoteAs),
        supportStatefulGr(supportStatefulGr),
        enableServerSocket(enableServerSocket),
        allowLoopbackReflection(allowLoopbackReflection) {}

  const ValidateRemoteAs validateRemoteAs{true};
  const SupportStatefulGr supportStatefulGr{true};
  const EnableServerSocket enableServerSocket{true};
  const AllowLoopbackReflection allowLoopbackReflection{false};
};

struct BgpUcmpQuantizer {
  BgpUcmpQuantizer(
      uint64_t minStepBps,
      double errorPctThreshold,
      const std::vector<uint64_t>& fixedQuantizedBpsList);

  // genrate quantizedBytesPerSec for a given inputBytesPerSec
  float quantize(float inputBytesPerSec) const;

  const uint64_t minStepBps{0};
  const double errorPctThreshold{0};
  const std::vector<uint64_t> fixedQuantizedBpsList{};

  std::map<uint64_t, uint64_t> quantizedBpsMap{};
};

/*
 * Configuration for update group behavior including serialization mode
 * and slow peer detection/detachment thresholds.
 *
 * Default values follow the thrift definition in:
 *   configerator/source/neteng/fboss/bgp/bgp_config.thrift (struct
 * UpdateGroupConfig) and are overridden by user input from
 * Config::populateConfigDatabase().
 */
struct UpdateGroupConfig {
  bool enableSerializeGroupPdu{false};
  bool allowSlowPeerDetach{false};
  std::chrono::milliseconds slowPeerTimeThreshold{50000};
  /*
   * C++-only (not in the thrift UpdateGroupConfig): gates block-count
   * (frequency) slow peer detection. When false, the two slowPeerBlockCount
   * fields are ignored and only duration-based detection runs.
   */
  bool enableSlowPeerBlockCountDetection{false};
  uint32_t slowPeerBlockCountThreshold{10};
  std::chrono::milliseconds slowPeerBlockCountWindow{1000};
};

struct BgpGlobalConfig {
  BgpGlobalConfig(
      const uint32_t localAsn,
      const folly::IPAddress& routerId,
      const folly::IPAddress& clusterId,
      const std::chrono::seconds holdTime,
      const std::optional<folly::SocketAddress>& listenAddr,
      const std::optional<std::chrono::seconds> grRestartTime,
      std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork> networksV4,
      std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork> networksV6,
      const std::optional<uint32_t>& localConfedAsn = std::nullopt,
      const ComputeUcmpFromLbwComm computeUcmpFromLbwComm =
          ComputeUcmpFromLbwComm{false},
      const uint32_t ucmpWidth = 0,
      const std::optional<BgpUcmpQuantizer>& ucmpQuantizer = std::nullopt,
      const ValidateRemoteAs validateRemoteAs = ValidateRemoteAs{true},
      const SupportStatefulGr supportStatefulGr = SupportStatefulGr{true},
      const EnableServerSocket enableServerSocket = EnableServerSocket{true},
      const AllowLoopbackReflection allowLoopbackReflection =
          AllowLoopbackReflection{false},
      const CountConfedsInAsPathLen countConfedsInAsPathLen =
          CountConfedsInAsPathLen{false},
      const std::optional<std::string>& deviceName = std::nullopt,
      const std::optional<thrift::BgpSwitchLimitConfig>& switchLimitConfig =
          std::nullopt,
      const std::optional<uint32_t> dynamicPeerLimit = std::nullopt,
      const std::optional<uint32_t> streamSubscriberLimit = std::nullopt,
      const EnableNexthopTracking enableNextHopTracking =
          EnableNexthopTracking{false},
      const std::vector<std::string>& includeInterfaceRegexes = {},
      const EnableDynamicPolicyEvaluation enableDynamicPolicyEvaluation =
          EnableDynamicPolicyEvaluation{false},
      const std::optional<facebook::bgp::thrift::ThriftServerConfig>&
          thriftServerConfig = std::nullopt,
      const bool enableEgressQueueBackpressure = false,
      const bool enableUpdateGroup = false,
      const UpdateGroupConfig& updateGroupConfig = {},
      const bool enableRibAllocatedPathId = false,
      const bool enableOptimizedGR = false,
      const bool enablePolicyDefaultAction = false,
      const bool enableAddPathGrReconcile = false,
      const bool enableLegacyV4NlriEncoding = false,
      const std::optional<bool> enableStreamSubscriberBackpressure =
          std::nullopt,
      /*
       * Last parameter on purpose. Every caller of this constructor passes
       * arguments positionally, so a new parameter in the middle breaks all of
       * them. Keep new parameters here.
       */
      const EnableNetlinkDampening enableNetlinkDampening =
          EnableNetlinkDampening{false},
      const bool enableRouteRefresh = false,
      const bool enableFibOutTracking = false,
      const bool enableGrNotification = false)
      : localAsn(localAsn),
        routerId(routerId),
        clusterId(clusterId),
        holdTime(holdTime),
        listenAddr(listenAddr),
        grRestartTime(grRestartTime),
        networksV4(std::move(networksV4)),
        networksV6(std::move(networksV6)),
        localConfedAsn(localConfedAsn),
        computeUcmpFromLbwComm(computeUcmpFromLbwComm),
        ucmpWidth(ucmpWidth),
        ucmpQuantizer(ucmpQuantizer),
        validateRemoteAs(validateRemoteAs),
        supportStatefulGr(supportStatefulGr),
        enableServerSocket(enableServerSocket),
        allowLoopbackReflection(allowLoopbackReflection),
        countConfedsInAsPathLen(countConfedsInAsPathLen),
        deviceName(deviceName),
        switchLimitConfig(switchLimitConfig),
        dynamicPeerLimit(dynamicPeerLimit),
        streamSubscriberLimit(streamSubscriberLimit),
        enableNextHopTracking(enableNextHopTracking),
        includeInterfaceRegexes(includeInterfaceRegexes),
        enableNetlinkDampening(enableNetlinkDampening),
        enableDynamicPolicyEvaluation(enableDynamicPolicyEvaluation),
        thriftServerConfig(thriftServerConfig),
        enableEgressQueueBackpressure(enableEgressQueueBackpressure),
        enableUpdateGroup(enableUpdateGroup),
        updateGroupConfig(updateGroupConfig),
        /* Update groups require RIB allocated path IDs for add-path. */
        enableRibAllocatedPathId(enableRibAllocatedPathId || enableUpdateGroup),
        enableOptimizedGR(enableOptimizedGR),
        enablePolicyDefaultAction(enablePolicyDefaultAction),
        enableAddPathGrReconcile(enableAddPathGrReconcile),
        enableLegacyV4NlriEncoding(enableLegacyV4NlriEncoding),
        enableRouteRefresh(enableRouteRefresh),
        enableStreamSubscriberBackpressure(enableStreamSubscriberBackpressure),
        enableFibOutTracking(enableFibOutTracking),
        enableGrNotification(enableGrNotification) {}

  const uint32_t localAsn;
  const folly::IPAddress routerId;
  const folly::IPAddress clusterId;
  const std::chrono::seconds holdTime;
  const std::optional<folly::SocketAddress> listenAddr;
  const std::optional<std::chrono::seconds> grRestartTime;
  const std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork> networksV4;
  const std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork> networksV6;
  const std::optional<uint32_t> localConfedAsn;

  /*
   * UCMP related settings
   */
  const ComputeUcmpFromLbwComm computeUcmpFromLbwComm =
      ComputeUcmpFromLbwComm{false};
  const uint32_t ucmpWidth{0};
  const std::optional<BgpUcmpQuantizer> ucmpQuantizer;

  /*
   * BGP feature control knobs
   */
  const ValidateRemoteAs validateRemoteAs = ValidateRemoteAs{true};
  const SupportStatefulGr supportStatefulGr = SupportStatefulGr{true};
  const EnableServerSocket enableServerSocket = EnableServerSocket{true};
  const AllowLoopbackReflection allowLoopbackReflection =
      AllowLoopbackReflection{false};
  const CountConfedsInAsPathLen countConfedsInAsPathLen =
      CountConfedsInAsPathLen{false};

  /*
   * Local device name - read from netwhoami
   */
  std::optional<std::string> deviceName{std::nullopt};

  /*
   * BGP switch limit config
   */
  std::optional<thrift::BgpSwitchLimitConfig> switchLimitConfig{std::nullopt};

  /*
   * Max number of dynamic peers allowed.
   */
  std::optional<uint32_t> dynamicPeerLimit{std::nullopt};

  /*
   * Max number of stream subscribers allowed.
   */
  std::optional<uint32_t> streamSubscriberLimit{std::nullopt};

  /*
   * Enable Next Hop Tracking and IGP cost comparison in path selection
   */
  const EnableNexthopTracking enableNextHopTracking =
      EnableNexthopTracking{false};

  /*
   * Interface regex patterns for nexthop tracking
   */
  const std::vector<std::string> includeInterfaceRegexes{};

  /*
   * Enable link-flap dampening in nexthop tracking. A link-down publishes
   * immediately; a link-up is held until the interface is stable. Works only
   * when enableNextHopTracking is also true and includeInterfaceRegexes
   * matches at least one interface.
   */
  const EnableNetlinkDampening enableNetlinkDampening =
      EnableNetlinkDampening{false};

  /**
   * Enable dynamic policy evaluation to support runtime policy changes for
   *  1. Ingress Route Filter Policy
   *  2. Ingress and Egress routing policy
   */
  const EnableDynamicPolicyEvaluation enableDynamicPolicyEvaluation =
      EnableDynamicPolicyEvaluation{false};

  /*
   * BGP thrift server config (TLS settings)
   */
  const std::optional<facebook::bgp::thrift::ThriftServerConfig>
      thriftServerConfig{std::nullopt};

  /**
   * Enable egress queue backpressure in bgp++ and bgplib.
   * NOTE!
   *
   * Please never change the default value of this flag!
   * In the constructor default argument and also the
   * default value on the struct! bgplib is a shared
   * serialization/IO library used by not only bgp++. However,
   * we only expect bgp++ to be capable of handling the
   * backpressure from bgplib. Other users CANNOT enable
   * the backpressure feature unless they can handle the backpressure,
   * so this value cannot be changed or removed.
   */
  const bool enableEgressQueueBackpressure{false};

  /*
   * Enable BGP update-group as a feature knob.
   */
  const bool enableUpdateGroup{false};

  /*
   * Update group configuration: serialization mode, slow peer detection
   * thresholds, and detachment behavior.
   */
  const UpdateGroupConfig updateGroupConfig;

  /**
   * Enable using path IDs allocated upon selection in Rib for outgoing updates,
   * instead of using cached per-nexthop IDs in AdjRibOut. This also includes
   * constructing RibOut messages based on these path IDs instead of nexthops.
   *
   * Read once at startup: RIB and every AdjRib must agree on the path ID
   * scheme, so changing it requires a restart.
   *
   * Forced on when enableUpdateGroup is set. Update groups require RIB
   * allocated path IDs for add-path: an AdjRibOutGroup keys its add-path
   * entries by the RIB's pathIdToSend, so the peer-level entries a group
   * member detaches into or collapses from must use the same IDs, not the
   * per-nexthop IDs from PathIdGenerator.
   */
  const bool enableRibAllocatedPathId{false};

  /**
   * Enable optimized GR (Graceful Restart) logic.
   * When enabled, routes are marked stale in-place using a stale bit rather
   * than moving them to a separate stale tree. A counter tracks the number of
   * stale entries for efficient cleanup.
   */
  const bool enableOptimizedGR{false};

  /**
   * Enable policy default action from BgpPolicyStatement::result field.
   * When enabled, the policy engine respects the configured default action
   * (ACCEPT or DENY) for prefixes that don't match any policy term.
   * When disabled (default), all unmatched prefixes are denied.
   */
  const bool enablePolicyDefaultAction{false};

  /**
   * Enable add-path receive reconciliation across a peer graceful restart.
   */
  const bool enableAddPathGrReconcile{false};

  /**
   * Enable RFC 4271 classic IPv4-unicast NLRI encoding toward capability-less
   * peers (peers that advertised no MP-EXT capability). When enabled, their
   * IPv4-unicast announcements are encoded as classic NLRI + NEXT_HOP instead
   * of MP_REACH_NLRI, and they are keyed into their own update group. Default
   * off: every peer keeps MP_REACH and existing update groups are unchanged.
   */
  const bool enableLegacyV4NlriEncoding{false};

  /**
   * Globally enable RFC 2918 Route Refresh capability advertisement. Peer and
   * peer-group settings can narrow this enablement but cannot bypass it.
   */
  const bool enableRouteRefresh{false};

  /**
   * This value controls the bounded egress path of a thrift stream
   * subscriber. The MP-BGP monitor is such a subscriber.
   *
   * If the value is true, the AdjRib of the subscriber uses a bounded queue.
   * The queue applies backpressure in the same way as the queue of a peer.
   * An AsyncGenerator reads the bounded queue. It reads an item only when the
   * client gives stream credit. If the subscriber does not read the stream,
   * the change-list consumer also stops.
   *
   * DEPRECATED: bgp++ ignores this value and always uses the bounded path.
   */
  const std::optional<bool> enableStreamSubscriberBackpressure;

  // Record the last normalized platform FIB request for each RIB entry.
  const bool enableFibOutTracking{false};

  // Advertise the RFC 8538 Notification (N) bit in the GR capability.
  const bool enableGrNotification{false};
};

struct BgpCommonPeerGroupConfig {
  BgpCommonPeerGroupConfig(
      const uint32_t peerAsn,
      const std::optional<uint16_t>& peerPort,
      const std::optional<uint32_t>& localAsn,
      const std::optional<folly::SocketAddress>& bindAddr,
      const std::optional<TBgpSessionConnectMode>& connectMode,
      const folly::IPAddressV4& nexthopV4,
      const folly::IPAddressV6& nexthopV6,
      const std::optional<thrift::RouteLimit>& preRouteLimitInput =
          std::nullopt,
      const std::optional<thrift::RouteLimit>& postRouteLimitInput =
          std::nullopt,
      const std::optional<std::string>& description = std::nullopt,
      const std::optional<std::chrono::seconds> holdTime = std::nullopt,
      const std::optional<std::chrono::seconds> keepAlive = std::nullopt,
      const std::optional<std::chrono::seconds> outDelay = std::nullopt,
      const std::optional<std::chrono::seconds> gracefulRestartTime =
          std::nullopt,
      const std::optional<bool>& isRrClient = std::nullopt,
      const std::optional<std::string>& peerTag = std::nullopt,
      const std::optional<bool>& nextHopSelf = std::nullopt,
      const std::optional<bool>& disableIpv4Afi = std::nullopt,
      const std::optional<bool>& disableIpv6Afi = std::nullopt,
      const std::optional<std::string>& ingressPolicyName = std::nullopt,
      const std::optional<std::string>& egressPolicyName = std::nullopt,
      const std::optional<bool>& isConfedPeer = std::nullopt,
      const std::optional<std::string>& peerId = std::nullopt,
      const std::optional<AdvertiseLinkBandwidth>& advertiseLinkBandwidth =
          std::nullopt,
      const std::optional<ReceiveLinkBandwidth>& receiveLinkBandwidth =
          std::nullopt,
      const std::optional<float> linkBandwidthBps = std::nullopt,
      const std::optional<bool>& removePrivateAs = std::nullopt,
      const std::optional<ValidateRemoteAs>& validateRemoteAs = std::nullopt,
      const std::optional<bool>& enableStatefulHa = std::nullopt,
      const std::optional<AddPath>& addPath = std::nullopt,
      const std::optional<bool>& v4OverV6Nexthop = std::nullopt,
      const std::optional<bool>& isRedistributePeer = std::nullopt,
      const std::optional<std::string>& peerGroupName = std::nullopt,
      const std::optional<bool>& enforceFirstAs = std::nullopt,
      const std::optional<int32_t>& ttlSecurityHops = std::nullopt,
      bool hasEgressPolicyOverride = false,
      const std::optional<bool>& enhancedRouteRefresh = std::nullopt,
      const std::optional<bool>& routeRefresh = std::nullopt,
      const std::optional<uint32_t>& additionalRemoteAs = std::nullopt)
      : peerAsn(peerAsn),
        peerPort(peerPort),
        localAsn(localAsn),
        bindAddr(bindAddr),
        connectMode(connectMode),
        nexthopV4(nexthopV4),
        nexthopV6(nexthopV6),
        preRouteLimit(preRouteLimitInput),
        postRouteLimit(postRouteLimitInput),
        description(description),
        holdTime(holdTime),
        keepAlive(keepAlive),
        outDelay(outDelay),
        gracefulRestartTime(gracefulRestartTime),
        isRrClient(isRrClient),
        peerTag(peerTag),
        nextHopSelf(nextHopSelf),
        disableIpv4Afi(disableIpv4Afi),
        disableIpv6Afi(disableIpv6Afi),
        ingressPolicyName(ingressPolicyName),
        egressPolicyName(egressPolicyName),
        isConfedPeer(isConfedPeer),
        peerId(peerId),
        advertiseLinkBandwidth(advertiseLinkBandwidth),
        receiveLinkBandwidth(receiveLinkBandwidth),
        linkBandwidthBps(linkBandwidthBps),
        removePrivateAs(removePrivateAs),
        validateRemoteAs(validateRemoteAs),
        enableStatefulHa(enableStatefulHa),
        addPath(addPath),
        v4OverV6Nexthop(v4OverV6Nexthop),
        isRedistributePeer(isRedistributePeer),
        peerGroupName(peerGroupName),
        enforceFirstAs(enforceFirstAs),
        ttlSecurityHops(ttlSecurityHops),
        hasEgressPolicyOverride(hasEgressPolicyOverride),
        enhancedRouteRefresh(enhancedRouteRefresh),
        routeRefresh(routeRefresh),
        additionalRemoteAs(additionalRemoteAs) {}

  const uint32_t peerAsn;
  const std::optional<uint16_t> peerPort;
  const std::optional<uint32_t> localAsn;
  const std::optional<folly::SocketAddress> bindAddr;
  const std::optional<TBgpSessionConnectMode> connectMode;
  const folly::IPAddressV4 nexthopV4;
  const folly::IPAddressV6 nexthopV6;
  const std::optional<thrift::RouteLimit> preRouteLimit;
  const std::optional<thrift::RouteLimit> postRouteLimit;
  const std::optional<std::string> description;
  const std::optional<std::chrono::seconds> holdTime;
  const std::optional<std::chrono::seconds> keepAlive;
  const std::optional<std::chrono::seconds> outDelay;
  const std::optional<std::chrono::seconds> gracefulRestartTime;
  const std::optional<bool> isRrClient;
  const std::optional<std::string> peerTag;
  const std::optional<bool> nextHopSelf;
  const std::optional<bool> disableIpv4Afi;
  const std::optional<bool> disableIpv6Afi;
  const std::optional<std::string> ingressPolicyName;
  const std::optional<std::string> egressPolicyName;
  const std::optional<bool> isConfedPeer;
  const std::optional<std::string> peerId;
  const std::optional<AdvertiseLinkBandwidth> advertiseLinkBandwidth;
  const std::optional<ReceiveLinkBandwidth> receiveLinkBandwidth;
  const std::optional<float> linkBandwidthBps;
  const std::optional<bool> removePrivateAs;
  const std::optional<uint32_t> postWarningThreshold;
  const std::optional<ValidateRemoteAs> validateRemoteAs;
  const std::optional<bool> enableStatefulHa;
  const std::optional<AddPath> addPath;
  const std::optional<bool> v4OverV6Nexthop;
  const std::optional<bool> isRedistributePeer;
  const std::optional<std::string> peerGroupName;
  const std::optional<bool> enforceFirstAs;
  const std::optional<int32_t> ttlSecurityHops;
  const bool hasEgressPolicyOverride{false};
  const std::optional<bool> enhancedRouteRefresh;
  const std::optional<bool> routeRefresh;
  const std::optional<uint32_t> additionalRemoteAs;
};

struct BgpPeerConfig {
  BgpPeerConfig(
      const folly::IPAddress& peerAddr,
      const BgpCommonPeerGroupConfig& commonPeerGroupConfig)
      : peerAddr(peerAddr), commonPeerGroupConfig(commonPeerGroupConfig) {}
  const folly::IPAddress peerAddr;
  const BgpCommonPeerGroupConfig commonPeerGroupConfig;
};

struct BgpDynamicPeerConfig {
  BgpDynamicPeerConfig(
      const folly::CIDRNetwork& peerPrefix,
      const BgpCommonPeerGroupConfig& commonPeerGroupConfig)
      : peerPrefix(peerPrefix), commonPeerGroupConfig(commonPeerGroupConfig) {}

  const folly::CIDRNetwork peerPrefix;
  const BgpCommonPeerGroupConfig commonPeerGroupConfig;
};

// Configuration needed to establish and maintain peering session
struct PeeringParams {
  PeeringParams() = default;

  PeeringParams(
      const folly::IPAddress& peerAddr,
      const std::optional<folly::CIDRNetwork>& peerPrefix,
      const uint32_t globalAs,
      const uint32_t localAs,
      const uint32_t remoteAs,
      const folly::IPAddressV4& localBgpId,
      const std::optional<AddPath>& addPath,
      const std::chrono::seconds& holdTime,
      const std::optional<AdvertiseLinkBandwidth>& advertiseLinkBandwidth =
          std::nullopt,
      const std::optional<ReceiveLinkBandwidth>& receiveLinkBandwidth =
          std::nullopt,
      ValidateRemoteAs validateRemoteAs = ValidateRemoteAs{true})
      : peerAddr(peerAddr),
        peerPrefix(peerPrefix),
        globalAs(globalAs),
        localAs(localAs),
        remoteAs(remoteAs),
        localBgpId(localBgpId),
        localClusterId(localBgpId),
        holdTime(holdTime),
        advertiseLinkBandwidth(advertiseLinkBandwidth),
        receiveLinkBandwidth(receiveLinkBandwidth),
        validateRemoteAs(validateRemoteAs),
        addPath(addPath) {}

  PeeringParams(
      const folly::IPAddress& peerAddr,
      const std::optional<folly::CIDRNetwork>& peerPrefix,
      const uint32_t globalAs,
      const uint32_t localAs,
      const uint32_t remoteAs,
      const folly::IPAddressV4& localBgpId,
      const std::chrono::seconds& holdTime,
      const std::optional<std::chrono::seconds>& grRestartTime,
      const std::optional<AdvertiseLinkBandwidth>& advertiseLinkBandwidth =
          std::nullopt,
      const std::optional<ReceiveLinkBandwidth>& receiveLinkBandwidth =
          std::nullopt,
      const std::optional<float> linkBandwidthBps = std::nullopt,
      ValidateRemoteAs validateRemoteAs = ValidateRemoteAs{true})
      : peerAddr(peerAddr),
        peerPrefix(peerPrefix),
        globalAs(globalAs),
        localAs(localAs),
        remoteAs(remoteAs),
        localBgpId(localBgpId),
        localClusterId(localBgpId),
        holdTime(holdTime),
        grRestartTime(grRestartTime),
        advertiseLinkBandwidth(advertiseLinkBandwidth),
        receiveLinkBandwidth(receiveLinkBandwidth),
        linkBandwidthBps(linkBandwidthBps),
        validateRemoteAs(validateRemoteAs) {}

  PeeringParams(
      const folly::IPAddress& peerAddr,
      const std::optional<folly::CIDRNetwork>& peerPrefix,
      const uint32_t globalAs,
      const uint32_t localAs,
      const uint32_t remoteAs,
      const folly::IPAddressV4& localBgpId,
      const std::chrono::seconds& holdTime,
      const std::optional<std::chrono::seconds>& grRestartTime,
      const AfiIpv4Configured& isAfiIpv4Configured,
      const AfiIpv6Configured& isAfiIpv6Configured,
      const std::optional<AdvertiseLinkBandwidth>& advertiseLinkBandwidth =
          std::nullopt,
      const std::optional<ReceiveLinkBandwidth>& receiveLinkBandwidth =
          std::nullopt,
      const std::optional<float> linkBandwidthBps = std::nullopt,
      ValidateRemoteAs validateRemoteAs = ValidateRemoteAs{true})
      : peerAddr(peerAddr),
        peerPrefix(peerPrefix),
        globalAs(globalAs),
        localAs(localAs),
        remoteAs(remoteAs),
        localBgpId(localBgpId),
        localClusterId(localBgpId),
        holdTime(holdTime),
        grRestartTime(grRestartTime),
        isAfiIpv4Configured(isAfiIpv4Configured),
        isAfiIpv6Configured(isAfiIpv6Configured),
        advertiseLinkBandwidth(advertiseLinkBandwidth),
        receiveLinkBandwidth(receiveLinkBandwidth),
        linkBandwidthBps(linkBandwidthBps),
        validateRemoteAs(validateRemoteAs) {}

  PeeringParams(
      const folly::IPAddress& peerAddr,
      const std::optional<folly::CIDRNetwork>& peerPrefix,
      const uint32_t globalAs,
      const uint32_t localAs,
      const uint32_t remoteAs,
      const folly::IPAddressV4& localBgpId,
      const folly::IPAddressV4& localClusterId,
      const std::chrono::seconds& holdTime,
      const std::optional<std::chrono::seconds>& grRestartTime,
      const uint16_t peerPort,
      const folly::SocketAddress& bindAddr,
      const TBgpSessionConnectMode& connectMode,
      const std::optional<AdvertiseLinkBandwidth>& advertiseLinkBandwidth =
          std::nullopt,
      const std::optional<ReceiveLinkBandwidth>& receiveLinkBandwidth =
          std::nullopt,
      const std::optional<float> linkBandwidthBps = std::nullopt,
      ValidateRemoteAs validateRemoteAs = ValidateRemoteAs{true})
      : peerAddr(peerAddr),
        peerPrefix(peerPrefix),
        globalAs(globalAs),
        localAs(localAs),
        remoteAs(remoteAs),
        localBgpId(localBgpId),
        localClusterId(localClusterId),
        holdTime(holdTime),
        grRestartTime(grRestartTime),
        peerPort(peerPort),
        bindAddr(bindAddr),
        connectMode(connectMode),
        advertiseLinkBandwidth(advertiseLinkBandwidth),
        receiveLinkBandwidth(receiveLinkBandwidth),
        linkBandwidthBps(linkBandwidthBps),
        validateRemoteAs(validateRemoteAs) {}

  PeeringParams(
      const folly::IPAddress& peerAddr,
      const std::optional<folly::CIDRNetwork>& peerPrefix,
      const uint32_t globalAs,
      const uint32_t localAs,
      const uint32_t remoteAs,
      const folly::IPAddressV4& localBgpId,
      const folly::IPAddressV4& localClusterId,
      const std::chrono::seconds& holdTime,
      const std::optional<std::chrono::seconds>& grRestartTime,
      const uint16_t peerPort,
      const folly::SocketAddress& bindAddr,
      const TBgpSessionConnectMode& connectMode,
      const folly::IPAddressV4& nexthopV4,
      const folly::IPAddressV6& nexthopV6,
      const RrClientConfigured& isRrClient = RrClientConfigured{false},
      const NextHopSelfConfigured& nextHopSelf = NextHopSelfConfigured{false},
      const AfiIpv4Configured& isAfiIpv4Configured = AfiIpv4Configured{true},
      const AfiIpv6Configured& isAfiIpv6Configured = AfiIpv6Configured{true},
      const ConfedPeerConfigured& isConfedPeer = ConfedPeerConfigured{false},
      const RemovePrivateAsConfigured& removePrivateAs =
          (RemovePrivateAsConfigured{false}),
      const std::optional<uint32_t>& localConfedAs = std::nullopt,
      const std::optional<uint32_t>& asConfedId = std::nullopt,
      const std::optional<AdvertiseLinkBandwidth>& advertiseLinkBandwidth =
          std::nullopt,
      const std::optional<ReceiveLinkBandwidth>& receiveLinkBandwidth =
          std::nullopt,
      const std::optional<float> linkBandwidthBps = std::nullopt,
      ValidateRemoteAs validateRemoteAs = ValidateRemoteAs{true},
      const std::optional<thrift::RouteLimit>& preRouteLimit = std::nullopt,
      const std::optional<thrift::RouteLimit>& postRouteLimit = std::nullopt,
      bool allowLoopbackReflection = false,
      const EnableStatefulHa& enableStatefulHa = EnableStatefulHa{false},
      const std::optional<AddPath>& addPath = std::nullopt,
      const V4OverV6Nexthop& v4OverV6Nexthop = V4OverV6Nexthop{false},
      const IsRedistributePeer& isRedistributePeer = IsRedistributePeer{false},
      const EnhancedRouteRefreshConfigured& isEnhancedRouteRefreshConfigured =
          EnhancedRouteRefreshConfigured{false},
      const bool enforceFirstAs = false,
      const RouteRefreshConfigured& isRouteRefreshConfigured =
          RouteRefreshConfigured{false})
      : peerAddr(peerAddr),
        peerPrefix(peerPrefix),
        globalAs(globalAs),
        localAs(localAs),
        remoteAs(remoteAs),
        localBgpId(localBgpId),
        localClusterId(localClusterId),
        holdTime(holdTime),
        grRestartTime(grRestartTime),
        peerPort(peerPort),
        bindAddr(bindAddr),
        connectMode(connectMode),
        nexthopV4(nexthopV4),
        nexthopV6(nexthopV6),
        isRrClient(isRrClient),
        nextHopSelf(nextHopSelf),
        isAfiIpv4Configured(isAfiIpv4Configured),
        isAfiIpv6Configured(isAfiIpv6Configured),
        isConfedPeer(isConfedPeer),
        removePrivateAs(removePrivateAs),
        localConfedAs(localConfedAs),
        asConfedId(asConfedId),
        advertiseLinkBandwidth(advertiseLinkBandwidth),
        receiveLinkBandwidth(receiveLinkBandwidth),
        linkBandwidthBps(linkBandwidthBps),
        validateRemoteAs(validateRemoteAs),
        preRouteLimit(preRouteLimit),
        postRouteLimit(postRouteLimit),
        allowLoopbackReflection(allowLoopbackReflection),
        enableStatefulHa(enableStatefulHa),
        addPath(addPath),
        v4OverV6Nexthop(v4OverV6Nexthop),
        isRedistributePeer(isRedistributePeer),
        isEnhancedRouteRefreshConfigured(isEnhancedRouteRefreshConfigured),
        enforceFirstAs(enforceFirstAs),
        isRouteRefreshConfigured(isRouteRefreshConfigured) {}

  folly::IPAddress peerAddr;
  std::optional<folly::CIDRNetwork> peerPrefix; // set if dynamic peer
  uint32_t globalAs{};
  uint32_t localAs{};
  uint32_t remoteAs{};
  std::optional<uint32_t> additionalRemoteAs{std::nullopt};
  folly::IPAddressV4 localBgpId;
  folly::IPAddressV4 localClusterId;
  std::chrono::seconds holdTime{};
  std::optional<std::chrono::seconds> grRestartTime;
  uint16_t peerPort{nettools::bgplib::constants::kBgpPort};
  folly::SocketAddress bindAddr{folly::AsyncSocket::anyAddress()};
  TBgpSessionConnectMode connectMode{TBgpSessionConnectMode::PASSIVE_ACTIVE};
  folly::IPAddressV4 nexthopV4;
  folly::IPAddressV6 nexthopV6;
  RrClientConfigured isRrClient{false};
  NextHopSelfConfigured nextHopSelf{false};
  // indicates if an address family is configured
  AfiIpv4Configured isAfiIpv4Configured{true};
  AfiIpv6Configured isAfiIpv6Configured{true};
  // labeled unicast
  AfiIpv4LUConfigured isAfiIpv4LUConfigured{false};
  AfiIpv6LUConfigured isAfiIpv6LUConfigured{false};
  ConfedPeerConfigured isConfedPeer{false};
  RemovePrivateAsConfigured removePrivateAs{false};
  std::optional<uint32_t> localConfedAs{std::nullopt};
  std::optional<uint32_t> asConfedId{std::nullopt};
  std::optional<std::string> peerTag{std::nullopt};
  /*
   * This controls whether the transitive extended community of Link-Bandwidth
   * is forwarded to peers or not.
   * Note that this does not trigger origination of LBW community.
   */
  std::optional<AdvertiseLinkBandwidth> advertiseLinkBandwidth{std::nullopt};
  std::optional<ReceiveLinkBandwidth> receiveLinkBandwidth{std::nullopt};
  std::optional<float> linkBandwidthBps{std::nullopt};
  // This controls whether the peer session is shutdown
  bool isShutdown{false};
  ValidateRemoteAs validateRemoteAs{true};
  std::optional<thrift::RouteLimit> preRouteLimit;
  std::optional<thrift::RouteLimit> postRouteLimit;
  /*
   * This controls if loopback peer learnt prefixes can be
   * announced(reflected)
   */
  bool allowLoopbackReflection{false};

  // set from config directly
  std::string description;
  std::string peerId;

  EnableStatefulHa enableStatefulHa{false};

  std::optional<AddPath> addPath{std::nullopt};

  V4OverV6Nexthop v4OverV6Nexthop{false};
  IsRedistributePeer isRedistributePeer{false};

  std::optional<std::string> peerGroupName;

  bool acceptsRemoteAs(uint32_t asn) const noexcept {
    return remoteAs == asn || additionalRemoteAs == asn;
  }

  std::string getUniquePeerId() const {
    std::string uniquePeerId = peerId;

    boost::regex r(R"(^([a-z0-9\-]+)\..*(\:[0-9]\:v[46])$)");
    boost::smatch m;
    toLower(uniquePeerId);
    auto matched = boost::regex_search(uniquePeerId, m, r);
    if (matched) {
      uniquePeerId = m[1] + m[2];
    }

    return uniquePeerId;
  }

  EnhancedRouteRefreshConfigured isEnhancedRouteRefreshConfigured{false};

  // enforce-first-as validation for eBGP peers
  bool enforceFirstAs{false};

  RouteRefreshConfigured isRouteRefreshConfigured{false};

  // TTL Security / GTSM (RFC 5082) hop count
  std::optional<int32_t> ttlSecurityHops;

  inline bool operator==(const PeeringParams& other) const {
    return (peerAddr == other.peerAddr) && (peerPrefix == other.peerPrefix) &&
        (globalAs == other.globalAs) && (localAs == other.localAs) &&
        (remoteAs == other.remoteAs) &&
        (additionalRemoteAs == other.additionalRemoteAs) &&
        (localBgpId == other.localBgpId) &&
        (localClusterId == other.localClusterId) &&
        (holdTime == other.holdTime) &&
        (grRestartTime == other.grRestartTime) &&
        (peerPort == other.peerPort) && (bindAddr == other.bindAddr) &&
        (connectMode == other.connectMode) && (nexthopV4 == other.nexthopV4) &&
        (nexthopV6 == other.nexthopV6) && (isRrClient == other.isRrClient) &&
        (nextHopSelf == other.nextHopSelf) &&
        (isAfiIpv4Configured == other.isAfiIpv4Configured) &&
        (isAfiIpv6Configured == other.isAfiIpv6Configured) &&
        (isConfedPeer == other.isConfedPeer) &&
        (removePrivateAs == other.removePrivateAs) &&
        (localConfedAs == other.localConfedAs) &&
        (asConfedId == other.asConfedId) &&
        (description == other.description) && (peerId == other.peerId) &&
        (advertiseLinkBandwidth == other.advertiseLinkBandwidth) &&
        (receiveLinkBandwidth == other.receiveLinkBandwidth) &&
        (linkBandwidthBps == other.linkBandwidthBps) &&
        (validateRemoteAs == other.validateRemoteAs) &&
        (peerTag == other.peerTag) && (preRouteLimit == other.preRouteLimit) &&
        (postRouteLimit == other.postRouteLimit) &&
        (enableStatefulHa == other.enableStatefulHa) &&
        (addPath == other.addPath) &&
        (v4OverV6Nexthop == other.v4OverV6Nexthop) &&
        (isRedistributePeer == other.isRedistributePeer) &&
        (isEnhancedRouteRefreshConfigured ==
         other.isEnhancedRouteRefreshConfigured) &&
        (isRouteRefreshConfigured == other.isRouteRefreshConfigured) &&
        (enforceFirstAs == other.enforceFirstAs) &&
        (ttlSecurityHops == other.ttlSecurityHops);
  }
};

} // namespace facebook::bgp
