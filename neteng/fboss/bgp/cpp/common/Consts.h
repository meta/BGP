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

#include <fmt/format.h>
#include <re2/re2.h>
#include <chrono>

#include "neteng/fboss/bgp/cpp/common/Structs.h"
#include "neteng/fboss/bgp/cpp/common/Types.h"
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"

namespace facebook::bgp {

using namespace std::chrono_literals;

constexpr uint32_t kDefaultLocalPref = 100;
constexpr auto kDefaultHoldTime = std::chrono::seconds(30);
constexpr auto kDefaultGrRestartTime = std::chrono::seconds(60);
constexpr auto kDefaultStalePathTimeOut = std::chrono::seconds(300);
constexpr uint32_t kMedMax = std::numeric_limits<uint32_t>::max();
constexpr uint16_t kWeightMax = std::numeric_limits<uint16_t>::max();
// TTL Security / GTSM (RFC 5082) hop count range
constexpr int32_t kMinTtlSecurityHops = 1;
constexpr int32_t kMaxTtlSecurityHops = 255;

inline const folly::F14FastSet<std::string> kDsfSwitchRoles = {"RDSW", "EDSW"};

inline const auto kLocalRoutePeerAddr = folly::IPAddress("0.0.0.0");
inline const auto kLocalRoutePeerAsn = facebook::bgp::AsNum(0);
inline const uint32_t kLocalRoutePeerRouterId = 0;
inline const auto kLocalRouteV4Nexthop = folly::IPAddress("0.0.0.0");
inline const auto kLocalRouteV6Nexthop = folly::IPAddress("::");
inline const auto kLoopBackAddressV4 = folly::IPAddress("127.0.0.1");
inline const auto kLoopBackAddressV6 = folly::IPAddress("::1");
inline const auto kDefaultRouteV4 =
    folly::CIDRNetwork(folly::IPAddress("0.0.0.0"), 0);
inline const auto kDefaultRouteV6 =
    folly::CIDRNetwork(folly::IPAddress("::"), 0);
inline const auto kDefaultDescription = "";
inline const auto kDefaultPeerId = "UNKNOWN";
inline const uint32_t kDefaultPreMaxRoutes = 12000;
inline const uint32_t kDefaultPostMaxRoutes = 12000;
// persentage fileds, 0 to represent not using this field
inline const uint32_t kDefaultPreWarningThreshold = 0;
inline const uint32_t kDefaultPostWarningThreshold = 0;

inline const auto kV4LocalPeerInfo = TinyPeerInfo(
    kLocalRoutePeerAddr,
    kLocalRoutePeerAsn,
    kLocalRoutePeerRouterId,
    BgpSessionType::IBGP,
    false // isRrClient
);

/*
 * Interval at which ShadowRib updates are advertised
 * Keeping same as RIB bestpath selection time
 */
inline const uint32_t kDefaultMraiInterval = 200;
/*
 * Minimum out-delay deferral timeout in seconds.
 * Anytime out-delay next timer is armed, the timeout is set to maximum of
 * kMinimumOutDelayTimeout and difference between next out-delay timer and
 * current time. The reason for this minimum spacing is to avoid creating
 * too many timers which fire in quick succession esp when sending updates
 * to a peer for the first time.
 */
const double kMinimumOutDelayTimeout = 0.25;

/*
 * Time interval (ms) after which to reschedule out-delay handling if there are
 * still pending items.
 */
const double kRescheduleOutDelayTimeoutMs = 25;

/*
 * When adjRib is backpressured, re-schedule additional time for
 * ShadowRib updates advertisement
 */
inline const uint32_t kBackpressuredMraiInterval = 100;

// Batch programming interval for BGP++ Fib
constexpr auto kFibBatchTimeDefault = std::chrono::milliseconds(200);

/**
 * @brief The amount of time to increase the FIB batch timeout during periods of
 * high FIB churn.
 */
constexpr auto kFibBatchTimeoutChurn = std::chrono::seconds(1);

/*
 * Rib pause timeout for BGP++
 * Set to 2 minutes initially, will be adjusted based on benchmarking data
 * analysis
 */
constexpr auto kRibPauseTimeout = std::chrono::minutes(2);

// Fboss Fib agent constants
constexpr auto kFbossAgentPort = 5909;
constexpr auto kFbossAgentConnTimeout = 1000ms;
constexpr auto kFbossAgentSendTimeout = 5000ms;
constexpr auto kFbossAgentRecvTimeout = 45000ms;
constexpr auto kFbossAgentKeepAliveTimeout = 1000ms;

// Ebb Fib agent constants
constexpr uint16_t kFibEbbPort = 5913;
constexpr std::chrono::milliseconds kFibEbbConnTimeout = 1000ms;
constexpr std::chrono::milliseconds kFibEbbSendTimeout = 5000ms;
constexpr std::chrono::milliseconds kFibEbbRecvTimeout = 45000ms;
constexpr std::chrono::milliseconds kFibEbbKeepAliveTimeout = 1000ms;

// OpenR Fib agent constants
constexpr uint16_t kFibOpenRPort = 5912;
constexpr std::chrono::milliseconds kFibOpenrConnTimeout = 1000ms;
constexpr std::chrono::milliseconds kFibOpenrSendTimeout = 5000ms;
constexpr std::chrono::milliseconds kFibOpenrRecvTimeout = 45000ms;
constexpr std::chrono::milliseconds kFibOpenrKeepAliveTimeout = 1000ms;

// OpenR FibAgent constants
constexpr auto kOpenrFibAgentTimeout = 1000ms;
/*
 * Maximum queue size to prevent unbounded memory growth for pending Updates
 * from NetlinkWrapper to OpenRFibAgent
 */
constexpr size_t kFibOpenrMaxPendingUpdates = 1000;

/*
 * Netlink Wrapper constants
 * Maximum time we wait to read on socket
 */
constexpr auto kNetlinkSyncReadTimeout = std::chrono::milliseconds(1000);
// Maximum number of retries to sync interface
constexpr auto kNetlinkSyncRetries = 5;
// Directly connected next hop weight
constexpr auto kDirectlyConnectedNexthopWeight = 1;
/*
 * The initial and maximum link-up hold-down times passed to
 * InterfaceEntry::recordLinkDown.
 */
constexpr auto kInitialLinkUpHoldDownTime = std::chrono::milliseconds(200);
constexpr auto kMaxLinkUpHoldDownTime = std::chrono::milliseconds(1000);

// Default maximum number of IPs to enumerate from a CIDR network
constexpr uint64_t kDefaultMaxIPsInCIDR = 2;

// Maximum number of entries per RibOutAnnouncement message
constexpr auto kRibChunkSize = 160;

// Ballpark average serialized length of serialized path attributes.
constexpr auto kApproxSerializedAttrLen = 300;

/*
 * Upper limit of number of serialized chained BgpUpdate2 messages.
 * In order to have flow control during backpressure, we do not
 * want to generate a theoretically unbounded chain of
 * serialized Bgp UPDATE messages. However, because we have to
 * send the prefixes across a thread boundary for serialization (PeerMgr
 * -> IO), we need to do approximate packing before hand.
 *
 * If we overestimate the number of prefixes that will end up
 * in a serialized message, then we may generate a chain of length greater
 * than one.
 *
 * This number represents the level of precision we are willing to accept
 * for the estimation; i.e. serialization should be able to put all of the
 * prefixes sent in one batch within this number of PDUs.
 */
constexpr auto kMaxSerializedChainLen = 10;

// Maximum number of local routes injection processed per iteration
constexpr auto kInjectRouteChunkSize = 100;

/*
 * String terminating Stateful GR file.
 * Used as a signature to verify proper termination of saved file.
 */
constexpr auto kGrStateFileTermination = "END GR STATE BGP++";

/*
 * String terminating Rib Policy file.
 * Used as a signature to verify proper termination of saved file.
 */
constexpr auto kRibPolicyFileTermination = "END RIB POLICY BGP++";

/*
 * Rib stores RouteInfos as a map<pathId, RouteInfo>
 * AdjRib radix tree use prefix -> map<pathId, attribute> mapping
 * DefaultPathId is used when add-path is not activated.
 */
inline const uint32_t kDefaultPathID = 0;

/*
 * Hard bound on the number of distinct (prefix, policy) pairs a single
 * getRejectedNetworks call tracks, independent of the caller's max_groups. It
 * also caps the rejections each per-peer Adj-RIB walk lists. A peer with a
 * deny-all egress policy rejects its entire RIB-OUT, so without this an
 * ostensibly read-only call could allocate proportionally to RIB size. Past
 * the cap the handler stops tracking new pairs but keeps counting paths and
 * neighbors, so total_rejected_paths and the by_policy path_count /
 * neighbor_count stay exact and only the per-prefix breakdown goes partial.
 *
 * Sized for a diagnostic, not for a full RIB dump: a real rejection set is a
 * handful of policies over a bounded prefix list, and a response past this
 * many groups costs more to build and serialize than it is worth reading. A
 * caller that hits it should narrow the request rather than raise this.
 */
constexpr size_t kMaxTrackedRejectedGroups = 10000;

/*
 * Hard bound on the neighbor entries a getRejectedNetworksDetails response
 * holds across all of its groups. The group cap above does not bound these:
 * every walked peer can add itself to every tracked group, so without this
 * the details response grows with groups x peers.
 */
constexpr size_t kMaxTrackedRejectedNeighbors = 100000;

/*
 * placeholder value for ADD-PATH send-side changes
 * TODO: remove this var and replace it with generated IDs wherever it's used
 */
inline const uint32_t kPlaceholderPathID = 0;

/*
 * per RFC7911, path ID is a 4 octet field. Thus
 * we cannot send anything that would exceed the max
 * value of an unsigned 32 bit int
 */
inline const uint32_t kMinPathIDToSend = 0;
inline const uint32_t kMaxPathIDToSend = UINT32_MAX;

// Conversion from Bytes to Kilobytes per second
inline const float BpsPerKBps = 1000.0f;
// Conversion from Bytes to Megabytes per second
inline const float BpsPerMBps = BpsPerKBps * 1000.0f;
// Conversion from Bytes to Gigabytes per second
inline const float BpsPerGBps = BpsPerMBps * 1000.0f;
/*
 * LBW is expressed in the Extended Community as Bytes (not bits) per second.
 * For easy reference, convert this to Gigabits per second while programming
 * weights so, for example a 100Gbps value will show up with a weight of 100
 */
inline const float LbwToUcmpWt = BpsPerGBps / 8.0f;
/*
 * when link_bandwidth_bps == auto, bgp calculates that value based on
 * information from the wedge agent
 */
constexpr auto kAutoLbwBps = "auto";

// Policy to block everything from propagation
inline const auto kNothingPolicy = "PROPAGATE_NOTHING";

// VIP Fairness Bytes per second (10G bits per second)
constexpr uint32_t kVipFairnessBps{1250000000};

// All VIPs are advertised with this ASN.
inline const uint32_t kVipAsn = 65000;

/*
 * AS_TRANS (RFC 6793) - Used as a placeholder in contexts that only support
 * 2-byte ASNs when the actual ASN is a 4-byte value (> 65535). For example,
 * Link Bandwidth Extended Communities use 2-byte AS format per RFC 4360,
 * so AS_TRANS is used when the local ASN is a 4-byte ASN.
 */
constexpr uint16_t kAsTrans = 23456;

/*
 * [Cache] BGP++ cache related variable
 */
constexpr uint64_t kMaxPolicyCacheEntries{40000}; /* cache size */
constexpr uint64_t kPolicyCacheClearSize{500}; /* clear cache size */
constexpr uint64_t kDefaultEvictionRunCount{5000}; /* eviction iteration cnt */
constexpr auto kPolicyCachePeriodicEvictionInterval =
    std::chrono::seconds(120); /* periodic eviction interval 2 minutes*/
/*
 * [Platform] BGP++ can run against different platforms
 */
constexpr auto kFbossPlatform{"fboss"};
constexpr auto kDevPlatform{"dev"};
constexpr auto kEbbPlatform{"ebb"};

enum class BgpPlatformType { DC, BB, OSS };

// Default OpenR FIB agent port (matches EBB AgentPort::FIB_AGENT)
constexpr int32_t kDefaultFibAgentPort = 5912;

/*
 * [Initialization] BGP++ Initialization Event Related
 */
constexpr auto kInitEventCounterFormat{"initialization.{}.duration_ms"};

/*
 * [Watchdog] This section defines all of the watchdog related constant vars.
 */

// BGP++ service ports
constexpr uint16_t kBgpThriftPort = 6909;
constexpr uint16_t kBgpStreamPort = 6910;

// Time duration of system metrics monitoring interval in watchdog.
constexpr std::chrono::seconds kWatchdogSystemMetricsInterval =
    std::chrono::seconds(5);

// Heartbeat snapshot interval and history depth for stall detection.
constexpr std::chrono::seconds kWatchdogHeartbeatSnapshotInterval =
    std::chrono::seconds(60);
constexpr size_t kMaxHeartbeatSnapshots = 3;

/*
 * RibOut queue size threshold monitored by backpressure
 * Following thresholds are used by Rib and PeerMgr respectively
 * to pause RIB computation when above high watermark, and to
 * resume RIB computation when goes below low watermark
 */
constexpr uint64_t kRibOutQueueSizePauseThreshold = 200;
constexpr uint64_t kRibOutQueueSizeResumeThreshold = 100;

/*
 * Batch size for prefixPathIds sent to Rib during policy re-evaluation
 * TODO: Tune this threshold based on RibInQ backpressure metrics
 */
constexpr uint64_t kPolicyReEvaluationBatchSize = 1000;

constexpr auto kModuleSessionManager = "session_manager";
constexpr auto kModulePeerManager = "peer_manager";
constexpr auto kModuleRib = "rib";
constexpr auto kModuleNeighborWatcher = "neighbor_watcher";
constexpr auto kModuleNexthopHandler = "nexthop_handler";
constexpr auto kModuleNetlinkWrapper = "netlink_wrapper";
constexpr auto kModuleWatchdog = "watchdog";
constexpr auto kModuleFsdbFibWatcher = "fsdb_fib_watcher";

// Ingress queue names for monitoring
constexpr auto kIngressQueueSuffix = "ingress";
inline const std::string kQueueNameRibIn =
    fmt::format("{}_rib_in", kIngressQueueSuffix);
inline const std::string kQueueNameAdjRibIn =
    fmt::format("{}_adjrib_in", kIngressQueueSuffix);
inline const std::string kQueueNameParserOut =
    fmt::format("{}_parser_out", kIngressQueueSuffix);
/*
 * There is deliberately no ingress socket queue name here, unlike the egress
 * side's kQueueNameSocketOut: nothing sits between the socket and the parser.
 * FiberSocket::readData hands each buffer straight to FiberBgpParser, which
 * pushes decoded messages into rcvdQueue_ (kQueueNameParserOut) -- the first
 * queue on the ingress path. Egress needs its own socket queue because its
 * producers (AdjRib, timers, notification sites) are decoupled from the socket
 * writer; the single ingress reader is not.
 */

// Egress queue names for monitoring
constexpr auto kEgressQueueSuffix = "egress";
inline const std::string kQueueNameRibOut =
    fmt::format("{}_rib_out", kEgressQueueSuffix);
inline const std::string kQueueNameAdjRibOut =
    fmt::format("{}_adjrib_out", kEgressQueueSuffix);
inline const std::string kQueueNameSocketOut =
    fmt::format("{}_socket_out", kEgressQueueSuffix);

// Misc queue name
constexpr auto kQueueNameFromAdjRib = "from_adjrib";
constexpr auto kQueueNameFromNeighborWatcher = "neighbor_route_change";

// Golden VIP community
constexpr nettools::bgplib::BgpAttrCommunityC kGoldenVipCommunity{65446, 400};

// Drain and Live communities for MNH partial drain
constexpr nettools::bgplib::BgpAttrCommunityC kDrainCommunity{65446, 10};
constexpr nettools::bgplib::BgpAttrCommunityC kLiveCommunity{65446, 30};

inline const RE2 kPortNameRegex = "(?P<port>.*)\\/\\d+$";
/*
 * Used in dsfSwitchReachability table to indicate
 * the switch is not reachable.
 */
constexpr int32_t kNoPortGroup = 0;

/*
 * Maximum number of prefixes learnt in an interval(High watermark),
 * beyond this detected as route churn
 */
constexpr uint64_t kHighWaterMarkForRouteChurn{20000};
/*
 * Minimum number of paths to fall to(lower watermark) once
 * route churn is detected
 */
constexpr uint64_t kLowWaterMarkForRouteChurn{5000};
// Interval to check for route churn
constexpr auto kRouteChurnCheckInterval = std::chrono::seconds(5);

/*
 * Staleness threshold for change list consumers: if a consumer's marker
 * has not advanced for this long, it is considered stuck.
 */
constexpr auto kConsumerStalenessThreshold = std::chrono::minutes(10);

// BGP++ platform tag for ODS counters
constexpr auto kBgpcppTag = "bgpcpp";

/*
 * Multiplier on `eor_time_s` for the last-resort `initializedMaxWaitTimer_`
 * window: the outermost deadline (armed once convergence is kicked off) by
 * which BGP++ publishes the INITIALIZED signal even if some peers never send
 * egress EoR.
 */
constexpr uint32_t kInitializedMaxWaitMultiplier = 5;

/*
 * Multiplier on `eor_time_s` for the BB boot-relative RIB-computation max wait,
 * armed when the PeerManager thread starts so initial RIB path computation is
 * force-unblocked even if no peer session ever comes up. MUST stay below
 * kInitializedMaxWaitMultiplier so RIB computation is forced before the
 * INITIALIZED last line of defense.
 */
constexpr uint32_t kRibComputationMaxWaitMultiplier = 3;

} // namespace facebook::bgp
