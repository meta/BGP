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

#include <limits>
#include <memory>
#include <optional>
#include <set>

#include <boost/noncopyable.hpp>
#include <fboss/lib/RadixTree.h>
#include <fmt/core.h>
#include <folly/CancellationToken.h>
#include <folly/Function.h>
#include <folly/IPAddress.h>
#include <folly/Singleton.h>
#include <folly/container/F14Map.h>
#include <folly/coro/AsyncScope.h>
#include <folly/coro/Baton.h>
#include <folly/coro/Sleep.h>
#include <folly/fibers/BatchSemaphore.h>
#include <folly/futures/Future.h>
#include <folly/memory/not_null.h>
#include <re2/re2.h>

#include "neteng/fboss/bgp/cpp/adjrib/AdjRibCommon.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibEntry.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibGroup.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibPolicyCache.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibStats.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibStructs.h"
#include "neteng/fboss/bgp/cpp/adjrib/PathIdGenerator.h"
#include "neteng/fboss/bgp/cpp/adjrib/RouteFilterLogger.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ChangeTracker.h"
#include "neteng/fboss/bgp/cpp/changeTracker/Consumer.h"
#include "neteng/fboss/bgp/cpp/common/BgpPath.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/config/Config.h"
#include "neteng/fboss/bgp/cpp/config/ConfigManager.h"
#include "neteng/fboss/bgp/cpp/lib/coro/BackPressuredQueue.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberBgpPeer.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyManager.h"
#include "neteng/fboss/bgp/cpp/rib/RibPolicy.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/watchdog/MonitoredModule.h"
#include "neteng/fboss/bgp/cpp/watchdog/MonitoredQueue.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

DECLARE_bool(enable_peer_status_logging);
DECLARE_bool(enable_rib_path_data_exporting);

// Captures caller source location for structured log messages.
#define BGP_LOG_SRC() fmt::format("{}:{}", __FILE__, __LINE__)

namespace facebook::bgp {

// Data structure to store all unique prefixes received from all adjribs
class AdjRibPrefixSet : boost::noncopyable {
  friend class folly::Singleton<AdjRibPrefixSet>;

 public:
  /*
   * @brief: get the singleton instance of the object.
   * @return: the shared_ptr of the singleton.
   */
  static std::shared_ptr<AdjRibPrefixSet> get();

  /*
   * @brief: increment the ref count for a particular prefix.
   * @details: if this is a new prefix, create a new entry for it.
   * @return: none
   */
  void addPrefix(
      const folly::CIDRNetwork& network,
      const bool isGoldenVip = false);

  /*
   * @brief: decrement the ref count for a particular prefix.
   * @details: if the refcount of the prefix drops to/below zero, remove it.
   * @return: none
   */
  void delPrefix(const folly::CIDRNetwork& network);

  struct PrefixRef {
    explicit PrefixRef(int64_t refCount, bool isGoldenVip)
        : refCount_(refCount), isGoldenVip_(isGoldenVip) {}
    // The number of reference to the prefix.
    uint64_t refCount_;
    /*
     * Whether it's a golden VIP. Normally this is false.
     * When safe mode is triggered, ingress dropping and adjrib purging will
     * start to mark golden vips
     */
    bool isGoldenVip_;
  };

  /*
   * @brief: search and get the reference count of a particular prefix.
   * @param: prefix in folly::CIDRNetwork format.
   * @return: a pair of the followings:
   *  1. boolean flag of prefix found or not.
   *  2. ref count of the specified prefix, 0 will be returned if not found.
   */
  std::pair<bool, PrefixRef> getRefCount(const folly::CIDRNetwork& network);

  /*
   * @brief: mark the prefix as golden VIP.
   * @param: prefix in folly::CIDRNetwork format.
   * if the prefix doesn't exist, do nothing.
   */
  void markGoldenVip(const folly::CIDRNetwork& network);

  /*
   * @brief: get the size of the radixTree.
   * @return: number of unique prefixes.
   */
  uint64_t size() {
    return uniquePrefixes_.size();
  }

  uint64_t goldenVipSize() {
    return totalGoldenVipPrefixesCount_;
  }

  /*
   * @brief: remove all entries in the radixTree.
   * @return: none
   */
  void clear() {
    uniquePrefixes_.clear();
    totalGoldenVipPrefixesCount_ = 0;
  }

 private:
  AdjRibPrefixSet() {}

  /*
   * This is the data structure to store the unique prefixes in the form of
   * folly::IPAddress received from all peers. Since this is a shared map
   * between multiple adjribs, we need to manage a reference count in order to
   * properly add remove entries from the map.
   *
   * TODO: Make it folly::synchronized if singleton access is made
   * multi-threaded.
   */
  facebook::network::RadixTree<
      folly::IPAddress, /* ip address */
      PrefixRef>
      uniquePrefixes_;

  uint64_t totalGoldenVipPrefixesCount_{0};
};

/*
 * Captures the associated prefixes and eor flag which are to be processed
 * upon expiry of this timer (indicated by the expiryTimeStamp).
 */
struct AdjRibOutDelayEntry {
  // Expiry timestamp
  std::chrono::time_point<std::chrono::system_clock> expiryTimeStamp;

  /*
   * All prefixes which are to be processed at the end of time delay timer.
   * Note that a stale prefix might hang around in this list even though
   * prefix has been purged from AdjRib::deferredUpdates_ list. This happens
   * if a withdraw is processed while the timer is still in progress.
   */
  std::vector<folly::CIDRNetwork> deferredPrefixes;

  // Ctor()
  AdjRibOutDelayEntry(
      std::chrono::time_point<std::chrono::system_clock> expiryTimeStamp,
      const std::vector<folly::CIDRNetwork>& deferredPrefixes)
      : expiryTimeStamp(expiryTimeStamp), deferredPrefixes(deferredPrefixes) {}

  // Used by the min-heap (priority queue)
  bool operator>(const AdjRibOutDelayEntry& other) const {
    return (expiryTimeStamp > other.expiryTimeStamp);
  }
};

class AdjRibOutConsumer;
class AdjRibOutGroup;
class AdjRibOutGroupConsumer;

/*
 * A single route that policy denied, as reported by
 * AdjRib::getRejectedNetworks. Deliberately minimal: the caller wants to know
 * which prefix was dropped and why, not the route's attributes, so no
 * TBgpPath is built.
 */
struct RejectedNetwork {
  neteng::fboss::bgp_attr::TIpPrefix prefix;
  /*
   * Name of the policy that denied the route, e.g. "Denied by CRF". Empty
   * when the entry records no policy term for the rejection.
   */
  std::string policyName;
};

/*
 * Filters for AdjRib::getRejectedNetworks. An empty/unset filter matches
 * everything. Filters are applied during the tree walk so that excluded
 * entries never reach the result vector.
 */
struct RejectedNetworksQuery {
  /*
   * Which Adj-RIB the walk resolves against: INGRESS reads AdjRibIn (routes
   * from this peer the ingress policy denied), EGRESS reads AdjRibOut (routes
   * to this peer the egress policy or CRF denied).
   *
   * The PRE_/POST_ axis is not a free variable here, which is why this is not
   * a RouteFilterType: an entry is rejected precisely when the pre-policy
   * attributes exist and the post-policy ones do not, so rejection is a
   * relation between the two views rather than a point on that axis. BOTH is
   * likewise not servable -- a walk resolves against one Adj-RIB -- and the
   * thrift handler refuses it before ever building a query.
   */
  neteng::fboss::bgp::thrift::TDirectionFilter direction{
      neteng::fboss::bgp::thrift::TDirectionFilter::EGRESS};
  /*
   * Partial-matched against the rejecting policy name. A pointer rather than
   * a value because re2::RE2 is neither copyable nor movable; null matches
   * every rejection reason.
   *
   * NON-OWNING: the RE2 must outlive the query. Do not point it at a
   * temporary. The thrift handler keeps the compiled regex in a shared_ptr
   * that the event-base lambda captures by value, precisely because
   * co_runOnEvbWithTimeout can leave a lambda running after the coroutine
   * that built the query has returned.
   */
  const re2::RE2* policyNameFilter{nullptr};
  /*
   * Restricts the walk to these prefixes; empty matches every prefix.
   * std::set rather than F14FastSet because TIpPrefix is ordered-comparable
   * but not hashable, and this set is caller-sized (a short explicit prefix
   * list), not RIB-sized.
   */
  std::set<neteng::fboss::bgp_attr::TIpPrefix> prefixFilter;
  std::optional<neteng::fboss::bgp_attr::TBgpAfi> afi;
  /*
   * Caps RejectedNetworks::entries. A deny-all policy rejects a peer's whole
   * Adj-RIB, and the walk runs on the PeerManager event base, so the thrift
   * handler sets this to keep one peer's result bounded no matter how much
   * policy denied. Rejections past it are still counted, not listed.
   */
  size_t maxEntries{std::numeric_limits<size_t>::max()};
  /* Exclude optimized-GR entries awaiting a fresh announcement. */
  bool excludeStale{false};
};

/*
 * One peer's result from AdjRib::getRejectedNetworks.
 */
struct RejectedNetworks {
  /* The first RejectedNetworksQuery::maxEntries rejections, in walk order. */
  std::vector<RejectedNetwork> entries;
  /*
   * Every rejection that passed the filters, per rejecting policy name,
   * including those past maxEntries. Sized by the number of policies.
   */
  folly::F14FastMap<std::string, int64_t> pathCountByPolicy;
  /* True when rejections past maxEntries were counted but not listed. */
  bool truncated{false};
};

// Adjacency Rib
class AdjRib : boost::noncopyable,
               public MonitoredModule,
               public std::enable_shared_from_this<AdjRib> {
 public:
  using AdjRibInQueueT = nettools::bgplib::FiberBgpPeer::OutputQueueT;
  using AdjRibOutQueueT = nettools::bgplib::FiberBgpPeer::BoundedInputQueueT;

  template <typename T>
  using AdjRibTreeIterator = network::RadixTreeIterator<
      folly::IPAddress,
      folly::F14ValueMap<T, std::unique_ptr<AdjRibEntry>>>;
  template <typename T>
  using AdjRibTree = facebook::network::RadixTree<
      folly::IPAddress,
      folly::F14ValueMap<T, std::unique_ptr<AdjRibEntry>>>;

  using AdjRibPathTree = facebook::network::RadixTree<
      folly::IPAddress,
      folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>>>;
  using AdjRibLiteTree = facebook::network::
      RadixTree<folly::IPAddress, std::unique_ptr<AdjRibEntry>>;

  struct Shutdown {};
  struct EoR {};
  struct EgressEoR {};
  struct DetachedInitDumpCompleted {};
  /*
   * Used to notify PeerManagerBase to trigger safe mode, See
   * http://fburl.com/bgp_safe_mode for more details
   */
  struct TriggerSafeMode {};
  /* Route Refresh from a peer: triggers a shadow-RIB re-dump (RFC 2918).
   * requestedAfi scopes it to the family the request named (§3). The AFI is a
   * required ctor argument: BgpUpdateAfi has no zero enumerator, so a
   * default-constructed one would reach processRibDumpReq as an AFI matching
   * neither family and silently re-dump v6 only. */
  struct RouteRefreshReceived {
    explicit RouteRefreshReceived(nettools::bgplib::BgpUpdateAfi afi)
        : requestedAfi(afi) {}
    nettools::bgplib::BgpUpdateAfi requestedAfi;
  };
  /*
   * Message to PeerManagerBase:
   * 1. EoR indicates peer EoR receipt for all negotiated address families.
   * 2. Shutdown requests peer shutdown.
   * 3. EgressEoR indicates that an egress EoR was sent.
   * 4. DetachedInitDumpCompleted retries work deferred behind a detached
   *    initial dump after its private sender drains.
   * 5. TriggerSafeMode indicates that a path-scale or prefix-limit threshold
   *    was reached.
   * 6. RouteRefreshReceived carries an RFC 2918 request from the peer.
   */
  using MessageToPeerManager = std::variant<
      Shutdown,
      EoR,
      EgressEoR,
      DetachedInitDumpCompleted,
      TriggerSafeMode,
      RouteRefreshReceived>;
  // Used to pass message from adjRib to PeerManagerBase
  struct ObservableMessageT {
    nettools::bgplib::BgpPeerId peerId;
    MessageToPeerManager message;
  };

  AdjRib(
      const nettools::bgplib::BgpPeerId& peerId,
      const PeeringParams& peeringParams,
      folly::EventBase& evb,
      nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ,
      MonitoredMPMCQueue<ObservableMessageT>& fromAdjRibQ,
      const std::shared_ptr<folly::coro::Baton>& sessionTerminateBaton,
      const std::shared_ptr<PolicyManager>& policyManager,
      folly::not_null_shared_ptr<std::atomic<bool>> isSafeModeOn,
      const std::optional<std::string>& ingressPolicyName = std::nullopt,
      const std::optional<std::string>& egressPolicyName = std::nullopt,
      const std::shared_ptr<AdjRibOutGroup> adjRibOutGroup = nullptr,
      const std::optional<std::chrono::seconds>& outDelay = std::nullopt,
      const std::shared_ptr<ConfigManager>& configManager = nullptr)
      : remotePeerId_(std::make_shared<nettools::bgplib::BgpPeerId>(peerId)),
        peeringParams_(peeringParams),
        formattedPeerName_(
            fmt::format(
                "[{}, {}]",
                remotePeerId_->str(),
                peeringParams_.description)),
        evb_(evb),
        ribInQ_(ribInQ),
        fromAdjRibQ_(fromAdjRibQ),
        sessionTerminateBaton_(sessionTerminateBaton),
        policyManager_(policyManager),
        isSafeModeOn_(isSafeModeOn),
        ingressPolicyName_(ingressPolicyName),
        egressPolicyName_(egressPolicyName),
        policyCache_(AdjRibPolicyCache::get()),
        adjRibOutGroup_(adjRibOutGroup),
        // log VIPs from dynamic peers to ODS as well as static peers
        stats_(
            peeringParams.acceptsRemoteAs(kVipAsn)
                ? peerId.toOdsKey()
                : peeringParams.getUniquePeerId()),
        outDelay_(outDelay.has_value() ? *outDelay : std::chrono::seconds(0)),
        configManager_(configManager) {
    if (configManager_) {
      auto config = configManager_->getConfig();
      if (auto flag = config->getConfig().sender_suppress_as_loop()) {
        sender_suppress_as_loop_ = *flag;
      }
      switchLimitConfig_ = config->getBgpSwitchLimitConfig();
      if (auto globalConfig = config->getBgpGlobalConfig()) {
        enableUpdateGroup_ = globalConfig->enableUpdateGroup;
        enableOptimizedGR_ = globalConfig->enableOptimizedGR;
        enableAddPathGrReconcile_ = globalConfig->enableAddPathGrReconcile;
        enableLegacyV4NlriEncoding_ = globalConfig->enableLegacyV4NlriEncoding;
        enableDynamicPolicyEvaluation_ =
            globalConfig->enableDynamicPolicyEvaluation;
      }
    }
  }

  virtual ~AdjRib();

  virtual folly::coro::Task<void> stop() noexcept;

  /*
   * Clean up GR state (timers and stale routes) without joining the
   * asyncScope_. Use this when a neighbor goes DOWN while in GR state
   * (Double Failure / DSF fast teardown) — the coroutines have already
   * exited during normal session teardown, so cancelAndJoinAsync() is
   * unnecessary and would cause a double-join hang if stop() is later
   * called again during daemon shutdown.
   *
   * @param isDaemonShutdown When true, skips cleanupStaleRoutes() because
   *        RIB is already stopped and nonCancellablePush would hang.
   */
  virtual folly::coro::Task<void> cleanupGrState(
      bool isDaemonShutdown) noexcept;

  /*
   * Whether a rib dump is currently scheduled or in flight for this peer: a
   * cancellation source exists and has not been cancelled.
   */
  bool isRibDumpScheduled() const noexcept {
    return ribDumpCancellationSource_.has_value() &&
        !ribDumpCancellationSource_->isCancellationRequested();
  }

  /*
   * Create a fresh cancellation source for a newly scheduled rib dump and
   * return its token to pass into PeerManagerBase's asyncScope_ when scheduling
   * the dump.
   */
  folly::CancellationToken getCancellationTokenForNewRibDump() noexcept {
    ribDumpCancellationSource_.emplace();
    return ribDumpCancellationSource_->getToken();
  }

  /*
   * Cancel an in-flight rib dump (e.g. on session teardown, or when superseding
   * it with a newer dump) and clear the source so a subsequent dump can be
   * scheduled. The in-flight task's token still observes the cancellation (the
   * underlying state outlives the source via the token).
   */
  void cancelRibDump() noexcept {
    if (ribDumpCancellationSource_) {
      ribDumpCancellationSource_->requestCancellation();
      ribDumpCancellationSource_.reset();
    }
  }

  /*
   * Clear rib-dump tracking on normal completion so a subsequent dump can be
   * scheduled. The caller MUST only invoke this when its own task was not
   * cancelled (a superseded/stopped task must not overwrite a newer one).
   */
  void resetRibDumpCancellationSource() noexcept {
    ribDumpCancellationSource_.reset();
  }

  // Called when BGP session gets established with new queue details
  void sessionEstablished(
      const std::optional<uint16_t>& remoteGrRestartTime,
      std::shared_ptr<AdjRibInQueueT> adjRibInQueue,
      std::shared_ptr<AdjRibOutQueueT> adjRibOutQueue,
      const AfiIpv4Negotiated& isAfiIpv4Negotiated = AfiIpv4Negotiated(true),
      const AfiIpv6Negotiated& isAfiIpv6Negotiated = AfiIpv6Negotiated(true),
      const V4OverV6Nexthop& isV4OverV6NexthopNegotiated =
          V4OverV6Nexthop(false),
      const EnhancedRouteRefreshNegotiated& isEnhancedRouteRefreshNegotiated =
          EnhancedRouteRefreshNegotiated(false),
      const RouteRefreshNegotiated& isRouteRefreshNegotiated =
          RouteRefreshNegotiated(false),
      const std::optional<nettools::bgplib::BgpAddPathSendRec>& addPathCapas =
          std::nullopt,
      bool as4ByteCapable = true,
      bool extNhEncodingCapable = false,
      bool remoteMpExtExist = true) noexcept;

  /* Establish a session using the remote ASN received from the BGP FSM. */
  void sessionEstablished(
      uint32_t remoteAs,
      const std::optional<uint16_t>& remoteGrRestartTime,
      std::shared_ptr<AdjRibInQueueT> adjRibInQueue,
      std::shared_ptr<AdjRibOutQueueT> adjRibOutQueue,
      const AfiIpv4Negotiated& isAfiIpv4Negotiated = AfiIpv4Negotiated(true),
      const AfiIpv6Negotiated& isAfiIpv6Negotiated = AfiIpv6Negotiated(true),
      const V4OverV6Nexthop& isV4OverV6NexthopNegotiated =
          V4OverV6Nexthop(false),
      const EnhancedRouteRefreshNegotiated& isEnhancedRouteRefreshNegotiated =
          EnhancedRouteRefreshNegotiated(false),
      const RouteRefreshNegotiated& isRouteRefreshNegotiated =
          RouteRefreshNegotiated(false),
      const std::optional<nettools::bgplib::BgpAddPathSendRec>& addPathCapas =
          std::nullopt,
      bool as4ByteCapable = true,
      bool extNhEncodingCapable = false,
      bool remoteMpExtExist = true) noexcept;

  /*
   * Called when session established with a peer (in PeerManagerBase)
   * to start fibers processing peer messages and Rib messages
   */
  void startMessageProcessingLoop() noexcept;

  /*
   * delete AdjRibEntry from the in/out adjrib tree
   */
  void deleteRibEntry(
      bool ingress,
      const folly::CIDRNetwork& prefix,
      uint32_t pathId = kDefaultPathID) noexcept;

  /*
   * add AdjRibEntry to the in/out adjrib tree
   */
  AdjRibEntry* FOLLY_NULLABLE addRibEntry(
      bool ingress,
      const folly::CIDRNetwork& prefix,
      uint32_t pathId = kDefaultPathID) noexcept;

  /*
   * get AdjRibEntry from the in/out adjrib tree
   */
  AdjRibEntry* FOLLY_NULLABLE getRibEntry(
      bool ingress,
      const folly::CIDRNetwork& prefix,
      uint32_t pathId = kDefaultPathID) noexcept;

  /*
   * Look up an egress RIB-OUT entry when update groups are enabled.
   * Checks peer-owned entries first, then falls back to group-owned entries.
   * This ensures detached peers sharing group entries can find them during
   * update processing.
   *
   * If the entry is a shared group entry, it is cloned to a per-peer entry
   * before being returned so the caller can safely modify it.
   */
  AdjRibEntry* FOLLY_NULLABLE getRibEntryWithUpdateGroup(
      const folly::CIDRNetwork& prefix,
      uint32_t pathId = kDefaultPathID) noexcept;

  /**
   * Create an AdjRibOutOwnerKey for this peer.
   * Used when accessing RIB-OUT tree entries.
   */
  inline AdjRibOutOwnerKey getPeerOwnerKey() const noexcept {
    return AdjRibOutOwnerKey::forPeer(remotePeerId_);
  }

  /*
   * @brief: the RIB version up to which this peer shares the group's RIB-OUT
   * entries, used to version-gate per-peer show/count lookups via
   * AdjRibOutGroup::resolve{Lite,Path}EntriesForPeer.
   * - Detached peer: its frozen detach version (getDetachedRibVersion()) — it
   *   shares only group entries created at/before detachment; entries with a
   *   greater ribVersion were never seen by this peer.
   * - In-sync peer (has a group): the group's current version, so every
   *   current group entry is shared.
   * - Standalone peer / update-group disabled (no group): its own
   *   lastSeenRibVersion_ — there are no group entries to gate, entries live
   *   under the peer key, so the gate never trips on this value.
   */
  inline uint64_t getRibOutSharingVersion() const noexcept {
    if (isDetachedPeer()) {
      return getDetachedRibVersion();
    }
    if (adjRibOutGroup_) {
      return adjRibOutGroup_->getLastSeenRibVersion();
    }
    return lastSeenRibVersion_;
  }

  /*
   * @brief  get size of the in/out tree. The tree instance
   *         is determined based on the parameters passed in.
   *         If isAddPathEnabled then look at adjRibInPathTree_
   *         If isAddPathEnabled is false then return size of the
   *           adjRibInLiteTree_ tree
   *
   * @param  bool  indicates in or out tree to look at
   * @param  bool  indicates add-path or Lite tree to look at
   *
   * @return uint32_t size of the tree
   */
  size_t getRibTreeSize(bool ingress, bool isAddPathEnabled) noexcept {
    /*
     * Check which tree to work with
     */
    if (isAddPathEnabled) {
      return (
          (ingress) ? adjRibInPathTree_.size()
                    : adjRibOutGroup_->PathTree_.size());
    }

    return (
        (ingress) ? adjRibInLiteTree_.size()
                  : adjRibOutGroup_->LiteTree_.size());
  }

  /*
   * @brief  get number of peer entries for a given direction
   *         If isAddPathEnabled then look at adjRibInPathTree_
   *         If isAddPathEnabled is false then return size of the
   *           adjRibInLiteTree_ tree
   *
   * @param  bool  indicates in or out tree to look at
   * @param  bool  indicates add-path or Lite tree to look at
   * @param  bool  indicates peer to look for entries
   *
   * @return uint32_t number of peer entries
   */
  size_t getRibTreePeerEntriesCount(
      bool ingress,
      bool isAddPathEnabled) noexcept {
    /*
     * Check which tree to work with
     */
    if (isAddPathEnabled) {
      uint32_t size = 0;
      if (ingress) {
        for (auto itr = adjRibInPathTree_.begin();
             itr != adjRibInPathTree_.end();
             itr++) {
          size += itr->value().size();
        }
      } else {
        size = adjRibOutGroup_->getPeerEntriesCountFromPathTree(
            adjRibOutGroup_->PathTree_, getPeerOwnerKey());
      }
      return size;
    }

    return (ingress) ? adjRibInLiteTree_.size()
                     : adjRibOutGroup_->getPeerEntriesCountFromLiteTree(
                           adjRibOutGroup_->LiteTree_, getPeerOwnerKey());
  }

  /*
   * @brief  get size of the RibIn Stale tree. The tree instance
   *         is determined based on the parameter passed in.
   *
   * @param  bool  indicates which tree to look at
   *
   * @return uint32_t size of the tree
   */
  uint32_t getRibInStaleTreeSize() noexcept {
    return (adjRibInStale_.size());
  }

  /*
   * @brief  get total path elements in the Stale tree
   *         This includes all the paths associated with every prefix
   *         from the tree
   *
   * @param  bool  indicates which tree to look at
   *
   * @return uint32_t number of path elements
   */
  uint32_t getRibInStaleTreePaths() noexcept {
    uint32_t size = 0;
    for (auto itr = adjRibInStale_.begin(); itr != adjRibInStale_.end();
         itr++) {
      size += itr->value().size();
    }

    return size;
  }

  // Returns the list of prefixes this adjRib is tracking.
  std::vector<folly::CIDRNetwork> getAllPrefixes() noexcept;

  /*******************************************************************************
   *             Start   -    AdjRib show functionality
   *******************************************************************************/
  // Get pre/post, in/out networks for fboss thrift service (show CLI)
  void getNetworks(
      std::map<
          neteng::fboss::bgp_attr::TIpPrefix,
          neteng::fboss::bgp::thrift::TBgpPath>& prefixToPath,
      const RouteFilterType& type) noexcept;

  // Get pre/post, in/out networks for fboss thrift service (show CLI)
  void getNetworks2(
      std::map<
          neteng::fboss::bgp_attr::TIpPrefix,
          std::vector<neteng::fboss::bgp::thrift::TBgpPath>>& prefixToPath,
      const RouteFilterType& type) noexcept;

  /*
   * Routes this peer's policy denied, for the getRejectedNetworks thrift API.
   *
   * Walks the same trees as getNetworks2, but instead of materializing pre-
   * and post-policy views for the caller to diff, it tests each entry
   * directly: an entry is rejected when its pre-policy attributes are set and
   * its post-policy attributes are not. The rejecting policy name is already
   * stored on the entry. Accumulates into `rejected` (does not clear it).
   *
   * EGRESS caveats for a peer in an update group. Both are properties of
   * how the group RIB-OUT is built, not of this walk, and both are surfaced
   * on the thrift API so callers can reason about them:
   *   - No CRF rejections exist to report. AdjRibOutGroup::
   *     getPostOutPolicyAttributesAndInfo applies the egress policy but skips
   *     the egress route filter ("TODO: Add egress route filter (CRF) check
   *     for group level"), so a group entry never carries "Denied by CRF".
   *     Egress policy rejections are recorded on the group entry and are
   *     reported normally.
   *   - Group entries are shared, so one rejection recorded for the group is
   *     reported once per member peer that resolves it -- which is the
   *     per-neighbor answer the API promises. For a detached peer,
   *     resolveLiteEntryForPeer gates on its detach version, so rejections
   *     recorded after it detached are not visible for that peer.
   */
  void getRejectedNetworks(
      RejectedNetworks& rejected,
      const RejectedNetworksQuery& query) noexcept;

  /*
   * Records one entry in `rejected` if it was denied by policy and passes
   * every filter in `query`. Shared by all four tree walks above.
   */
  static void maybeAddRejectedNetwork(
      RejectedNetworks& rejected,
      const folly::CIDRNetwork& prefix,
      const AdjRibEntry& adjRibEntry,
      const RejectedNetworksQuery& query) noexcept;

  /*******************************************************************************
   *             End   -    AdjRib show functionality
   *******************************************************************************/

  /*
   * set route filter statement for this peer
   * return tuple of (ingressChanged, egressChanged) flags
   */
  std::tuple<bool, bool> setRouteFilterStatement(
      std::shared_ptr<const RouteFilterStatement> stmt,
      std::unique_ptr<RouteFilterLogger> logger = nullptr);

  // Helper methods to set pending policy update flags based on changes
  void setPendingIngressPolicyUpdate(bool ingressChanged);
  void setPendingEgressPolicyUpdate(bool egressChanged);

  /*
   * set golden prefix policy for this peer
   * return if the policy is different from the current one
   */
  bool setGoldenPrefixPolicy(std::shared_ptr<GoldenPrefixPolicy> policy);

  // Helper methods to check pending policy update flags
  bool isPendingIngressPolicyUpdate() const {
    return pendingIngressPolicyUpdate_;
  }

  bool isEgressPolicyUpdateRequired() const {
    return egressPolicyUpdateRequired_;
  }

  // Helper methods to clear pending policy update flags
  void clearPendingIngressPolicyUpdate() {
    pendingIngressPolicyUpdate_ = false;
  }

  void clearPendingEgressPolicyUpdate() {
    egressPolicyUpdateRequired_ = false;
  }

  /**
   * @brief Update ingress and egress policy names and detect changes
   *
   * @param directionToPolicyName Map of policy direction to policy name
   *                              (DIRECTION::IN for ingress, DIRECTION::OUT for
   * egress)
   * @return std::tuple<bool, bool> A tuple containing (ingressChanged,
   * egressChanged)
   *         - first element: true if ingress policy name was changed, false
   * otherwise
   *         - second element: true if egress policy name was changed, false
   * otherwise
   *
   */
  std::tuple<bool, bool> updateIngressEgressPolicyNames(
      const folly::F14FastMap<
          facebook::bgp::bgp_policy::DIRECTION,
          std::optional<std::string>>& directionToPolicyName) noexcept;

  // Helper method to get the current route filter statement
  std::shared_ptr<const RouteFilterStatement> getRouteFilterStatement() const {
    return routeFilterStmt_;
  }

  /**
   * @brief: Re-evaluate all prefixes (stale and non-stale) in the adjRib.
   *
   * @details: All the prefixes (stale and non-stale) in an adjRib is
   * re-evaluated against configured ingress policies. Triggered under any of
   * the following cases:
   * 1. BGP++ enters safe mode during prefix overload scenarios.
   * 2. Ingress RouteFilterPolicy is updated.
   * 3. Ingress routing policy is updated.
   */
  folly::coro::Task<void> processAdjRibReEvaluation(RibPauseResumeCause cause);

  /*
   * Build and enqueue a Route Refresh message for an explicit AFI. Returns
   * false if the AFI is not negotiated or the egress queue rejects it.
   */
  bool buildAndSendRouteRefresh(
      const nettools::bgplib::BgpRouteRefreshMessageSubtype& subtype,
      nettools::bgplib::BgpUpdateAfi afi) noexcept;

  bool isAfiNegotiated(nettools::bgplib::BgpUpdateAfi afi) const noexcept {
    return (afi == nettools::bgplib::BgpUpdateAfi::AFI_IPv4 &&
            isAfiIpv4Negotiated_) ||
        (afi == nettools::bgplib::BgpUpdateAfi::AFI_IPv6 &&
         isAfiIpv6Negotiated_);
  }

  // Get AdjRibStats for this peer
  const AdjRibStats& getStats() const {
    return stats_;
  }

  neteng::fboss::bgp::thrift::TAdjRibInPeerStats getRibInStatsSnapshot() const;
  neteng::fboss::bgp::thrift::TAdjRibOutPeerStats getRibOutStatsSnapshot()
      const;

  void incrementSentUpdateMsgs(uint64_t bgpMessageCnt = 1) {
    stats_.incrementSentUpdateMsgs(bgpMessageCnt);
  }

  void incrementSentAnnouncementsIpv4() {
    stats_.incrementSentAnnouncementsIpv4();
  }

  void incrementSentAnnouncementsIpv6() {
    stats_.incrementSentAnnouncementsIpv6();
  }

  void incrementSentWithdrawals() {
    stats_.incrementSentWithdrawals();
  }

  /*
   * Cumulative detach/rejoin bookkeeping recorded against this peer by the
   * group (see AdjRibOutGroup::detachPeer / tryAcceptPeersToGroup).
   */
  void incrementTimesDetachedByBlocking() noexcept {
    stats_.incrementTimesDetachedByBlocking();
  }
  void incrementTimesDetachedByPolicy() noexcept {
    stats_.incrementTimesDetachedByPolicy();
  }
  void incrementTimesDetachedByRouteRefresh() noexcept {
    stats_.incrementTimesDetachedByRouteRefresh();
  }
  void incrementTimesRejoined() noexcept {
    stats_.incrementTimesRejoined();
  }

  void copyEgressPrefixCountsFrom(const AdjRibStats& other) {
    stats_.copyEgressPrefixCountsFrom(other);
  }

  void clearEgressPrefixCounts() {
    stats_.clearEgressPrefixCounts();
  }

  void incrementPreOutPrefixCount(bool isIpv4) {
    stats_.incrementPreOutPrefixCount(isIpv4);
  }
  void decrementPreOutPrefixCount(bool isIpv4) {
    stats_.decrementPreOutPrefixCount(isIpv4);
  }
  void incrementPostOutPrefixCount(bool isIpv4, uint32_t numPeers = 1) {
    stats_.incrementPostOutPrefixCount(isIpv4, numPeers);
  }
  void decrementPostOutPrefixCount(bool isIpv4, uint32_t numPeers = 1) {
    stats_.decrementPostOutPrefixCount(isIpv4, numPeers);
  }

  void setInInitialAnnouncement() noexcept {
    inInitialAnnouncement_ = true;
  }
  void resetInInitialAnnouncement() noexcept {
    inInitialAnnouncement_ = false;
  }

  bool inInitialAnnouncement() {
    return inInitialAnnouncement_;
  }

  bool egressEoRsSent() {
    return egressEoRsSent_;
  }

  /**
   * @brief Set the per-AFI egress EoR pending flags.
   * Used by AdjRibOutGroup to mark which EoR markers a peer still owes. The
   * flags are preserved if the peer detaches mid-EoR, so it sends only the
   * AFIs it has not committed yet.
   */
  void setEgressEoRsPending(bool v4Pending, bool v6Pending) {
    if (v4Pending) {
      setAdjRibFlag(EGRESS_EOR_PENDING_V4);
    } else {
      clearAdjRibFlag(EGRESS_EOR_PENDING_V4);
    }

    if (v6Pending) {
      setAdjRibFlag(EGRESS_EOR_PENDING_V6);
    } else {
      clearAdjRibFlag(EGRESS_EOR_PENDING_V6);
    }
  }

  /* Clear the per-AFI egress EoR pending flag once that AFI's EoR is queued. */
  void clearEgressEoRPendingV4() {
    clearAdjRibFlag(EGRESS_EOR_PENDING_V4);
  }
  void clearEgressEoRPendingV6() {
    clearAdjRibFlag(EGRESS_EOR_PENDING_V6);
  }

  bool egressEoRPendingV4() const {
    return isAdjRibFlagSet(EGRESS_EOR_PENDING_V4);
  }
  bool egressEoRPendingV6() const {
    return isAdjRibFlagSet(EGRESS_EOR_PENDING_V6);
  }

  bool egressEoRsPending() const {
    return egressEoRPendingV4() || egressEoRPendingV6();
  }

  bool sendAddPath() {
    return sendAddPath_;
  }

  bool recAddPath() {
    return recAddPath_;
  }

  int64_t eorSentTime() {
    return eorSentTime_;
  }

  int64_t eorReceivedTime() {
    return eorReceivedTime_;
  }

  /**
   * @brief Send all pending End-of-RIB markers
   * Returns pair of bool (whether write was blocked) and uint16_t (number of
   * EoRs sent) Called by AdjRibOutGroup after group packing list is drained
   */
  folly::coro::Task<std::pair<bool, uint16_t>> sendPendingEoRs() noexcept;

  /**
   * @brief Per-peer bookkeeping after all of the peer's egress EoR PDUs are
   * pushed to queue.
   *
   * Sets egressEoRsSent_ to true, records the time, logs the
   * event, and notifies the peer manager. Does not touch the egress queue.
   * One-time per session (guarded by egressEoRsSent_).
   */
  void onEgressEoRSent() noexcept;

  /**
   * @brief Finalize a single AFI's egress EoR once its PDU has landed in the
   * peer's egress queue. The group's EoR push supplies this as the onResolved
   * continuation to tryPushToPeer, so it runs on immediate push or from
   * deferredPushToPeer for a backpressured peer. The continuation carries the
   * AFI, so this clears exactly this AFI's EGRESS_EOR_PENDING flag, increments
   * this peer's sent-EoR count, then fires one-time bookkeeping if it was the
   * last.
   */
  void markEgressEoRSent(nettools::bgplib::BgpUpdateAfi afi) noexcept;

  int64_t flapCounter() {
    return flapCounter_;
  }

  /*
   * Emit a structured BGP_PEER_EVENT log line for FSM tracking.
   * Pass BGP_LOG_SRC() as src to capture the caller's source location.
   */
  void logPeerEvent(const std::string& phase, const std::string& src);

  // Get peer IP address of this AdjRib
  const folly::IPAddress& getPeerAddress() const noexcept {
    return peeringParams_.peerAddr;
  }

  // Get peeringParams of this AdjRib
  const PeeringParams getPeeringParams() const noexcept {
    return peeringParams_;
  }

  uint32_t getRemoteAs() const noexcept {
    return remoteAs_;
  }

  // Get PeerConfig for this AdjRib (used for attribute updates)
  PeerConfig getPeerConfig() const noexcept {
    return PeerConfig{
        peeringParams_,
        getBgpSessionType(),
        egressPolicyName_,
        policyManager_.get()};
  }

  // Get the state of adjrib for this peer (Established/Terminated)
  bool isStateEstablished() const noexcept {
    return (isSessionEstablished_ == true);
  }

  virtual bool isPeerGracefulRestarting() const noexcept {
    return (!isSessionEstablished_ && remoteGrRestartTimer_ != nullptr);
  }

  std::chrono::seconds getRemoteGrRestartTime() const noexcept {
    return remoteGrRestartTime_;
  }

  bool isV4OverV6NexthopNegotiated() const noexcept {
    return isV4OverV6NexthopNegotiated_;
  }

  bool isSafeModeOn() const noexcept {
    return *isSafeModeOn_;
  }

  void setSafeModeOn() noexcept {
    *isSafeModeOn_ = true;
  }

  bool enableAddPathGrReconcile() const noexcept {
    return enableAddPathGrReconcile_;
  }

  /**
   * @brief Mark this AdjRib for BGP daemon shutdown
   *
   * @details When set, expensive O(n) cleanup operations in
   * sessionTerminated() are skipped to prevent systemd timeout during shutdown.
   * Called by PeerManagerBase::stop() before initiating shutdown sequence.
   * This is a one-time flag that is not expected to be reset to false.
   */
  void setDaemonShutdown() noexcept {
    isDaemonShutdown_ = true;
  }

  void clearPackingList() {
    attrToPrefixMap_.clear();
  }

  /**
   * @brief Update packing list with attribute-to-prefix mapping for this peer
   *
   * @details Wrapper around tryUpdateAttrToPrefixMapImpl() that automatically
   * provides the peer's context (attrToPrefixMap_, getPeerName(),
   * stats_).
   *
   * @param prefixPathId - Prefix and path ID pair
   * @param oldPath - Previous attributes (nullptr if new prefix)
   * @param newPath - New attributes (nullptr for withdrawal)
   * @param isNexthopSetByPolicy - Whether the nexthop was set by an egress
   *   policy (affects bookkeeping in tryUpdateAttrToPrefixMapImpl)
   */
  inline void tryUpdateAttrToPrefixMap(
      const std::pair<folly::CIDRNetwork, uint32_t>& prefixPathId,
      const std::shared_ptr<const BgpPath>& oldPath,
      const std::shared_ptr<const BgpPath>& newPath,
      bool isNexthopSetByPolicy = false) {
    tryUpdateAttrToPrefixMapImpl(
        prefixPathId,
        oldPath,
        newPath,
        attrToPrefixMap_,
        getPeerName(),
        stats_,
        isNexthopSetByPolicy);
  }

  void activateChangeListConsumer() noexcept;
  void deactivateChangeListConsumer() noexcept;

  /*
   * Save initialized changeListConsumer for this adjrib
   */
  void setChangeListConsumer(
      std::shared_ptr<AdjRibOutConsumer>& changeListConsumer) noexcept {
    changeListConsumer_ = changeListConsumer;
  }

  void resetChangeListConsumer() noexcept {
    if (changeListConsumer_) {
      /*
       * Ideally deactivating when session (adjRib) terminated is good
       * However, some-times UTs bypass certain methods, hence safety
       * call to deactivate when AdjRib is destroyed
       */
      deactivateChangeListConsumer();
      changeListConsumer_.reset();
    }
  }

  /*
   * Get saved changeListConsumer for this adjrib
   */
  const std::shared_ptr<AdjRibOutConsumer>& getChangeListConsumer()
      const noexcept {
    return changeListConsumer_;
  }

  /*
   * Consume all available peer change-list items. If the consumer reaches the
   * tail, advance the peer's cached version to the Shadow RIB's current
   * maximum.
   */
  void tryIterateChangesToEnd() noexcept;

  bool isEnhancedRouteRefreshNegotiated() const noexcept {
    return isEnhancedRouteRefreshNegotiated_;
  }

  bool isRouteRefreshNegotiated() const noexcept {
    return isRouteRefreshNegotiated_;
  }

  /*
   * NOTE: These states are modified only by events which peerManager see's.
   * As terminate event is seen by both adjRib and peerManager, only
   * peerManager events will trigger this.
   * There could be lag between events.
   */
  void markStateEstablished() noexcept {
    CHECK_EQ(false, isSessionEstablished_);
    isSessionEstablished_ = true;
    stats_.addPeerSessionStateChanges();
    stats_.exportPeerStatus(true);
  }

  void markStateTerminated() noexcept {
    CHECK_EQ(true, isSessionEstablished_);
    isSessionEstablished_ = false;
    stats_.addPeerSessionStateChanges();
    stats_.exportPeerStatus(false);
  }

  /**
   * @brief  Get group name of AdjRibOutGroup associated with this adjRib
   */
  inline std::string getAdjRibOutGroupName() {
    if (!adjRibOutGroup_) {
      return "";
    }

    return adjRibOutGroup_->getAdjRibGroupName();
  }

  /**
   * @brief  Get the update-group ID assigned by UpdateGroupManager
   */
  std::optional<uint64_t> getUpdateGroupId() const {
    if (!adjRibOutGroup_) {
      return std::nullopt;
    }
    return adjRibOutGroup_->getGroupId();
  }

  /**
   * @brief  Get the group-level post-out prefix count from the update-group
   */
  std::optional<uint32_t> getUpdateGroupPostOutPrefixCount() const {
    if (!adjRibOutGroup_) {
      return std::nullopt;
    }
    return adjRibOutGroup_->getStats().getPostOutPrefixCount();
  }

  std::optional<uint32_t> getUpdateGroupPostOutPrefixCountIpv4() const {
    if (!adjRibOutGroup_) {
      return std::nullopt;
    }
    return adjRibOutGroup_->getStats().getPostOutPrefixCountIpv4();
  }

  std::optional<uint32_t> getUpdateGroupPostOutPrefixCountIpv6() const {
    if (!adjRibOutGroup_) {
      return std::nullopt;
    }
    return adjRibOutGroup_->getStats().getPostOutPrefixCountIpv6();
  }

  std::optional<uint32_t> getUpdateGroupPreOutPrefixCountIpv4() const {
    if (!adjRibOutGroup_) {
      return std::nullopt;
    }
    return adjRibOutGroup_->getStats().getPreOutPrefixCountIpv4();
  }

  std::optional<uint32_t> getUpdateGroupPreOutPrefixCountIpv6() const {
    if (!adjRibOutGroup_) {
      return std::nullopt;
    }
    return adjRibOutGroup_->getStats().getPreOutPrefixCountIpv6();
  }

  /**
   * @brief  Effective advertised (egress / post-out) prefix count for this
   *         peer, independent of whether update-group is enabled.
   *
   * Egress accounting lives in two places depending on update-group state: an
   * in-sync group member's advertisements are counted on the shared
   * AdjRibOutGroup (its own per-peer stats_.postOutPrefixCount is cleared by
   * AdjRibGroup::markPeerInSync), whereas a peer with no group (update-group
   * disabled) or a detached/diverged member counts on its own per-peer stats_.
   * Return the larger of the two so callers get the peer's true egress without
   * having to know which accounting is currently active. (Ingress needs no such
   * accessor -- adj-rib-in is inherently per-peer.)
   */
  uint32_t getEffectivePostOutPrefixCount() const {
    const uint32_t perPeerPostOut = stats_.getPostOutPrefixCount();
    if (!adjRibOutGroup_) {
      return perPeerPostOut;
    }
    const uint32_t groupPostOut =
        adjRibOutGroup_->getStats().getPostOutPrefixCount();
    return groupPostOut > perPeerPostOut ? groupPostOut : perPeerPostOut;
  }

  /**
   * @brief  Get the group-level count of UPDATE PDUs generated by the
   * update-group. This is a group diagnostic; peer APIs use per-peer counts.
   */
  std::optional<uint64_t> getUpdateGroupSentUpdateMsgs() const {
    if (!adjRibOutGroup_) {
      return std::nullopt;
    }
    return adjRibOutGroup_->getStats().getSentUpdateMsgs();
  }

  /**
   * @brief Get the group-level count of EoR PDUs generated by the update-group.
   * This is a group diagnostic; peer APIs use per-peer counts.
   */
  std::optional<uint64_t> getUpdateGroupSentEndOfRibMsgs() const {
    if (!adjRibOutGroup_) {
      return std::nullopt;
    }
    return adjRibOutGroup_->getStats().getSentEndOfRibMsgs();
  }

  /**
   * @brief  Get the group-level count of IPv4 announcement UPDATE PDUs
   * generated by the update-group.
   */
  std::optional<uint64_t> getUpdateGroupSentAnnouncementsIpv4() const {
    if (!adjRibOutGroup_) {
      return std::nullopt;
    }
    return adjRibOutGroup_->getStats().getSentAnnouncementsIpv4();
  }

  /**
   * @brief  Get the group-level count of IPv6 announcement UPDATE PDUs
   * generated by the update-group.
   */
  std::optional<uint64_t> getUpdateGroupSentAnnouncementsIpv6() const {
    if (!adjRibOutGroup_) {
      return std::nullopt;
    }
    return adjRibOutGroup_->getStats().getSentAnnouncementsIpv6();
  }

  /**
   * @brief  Get the group-level count of withdrawal UPDATE PDUs generated by
   * the update-group.
   */
  std::optional<uint64_t> getUpdateGroupSentWithdrawals() const {
    if (!adjRibOutGroup_) {
      return std::nullopt;
    }
    return adjRibOutGroup_->getStats().getSentWithdrawals();
  }

  /**
   * @brief  Reset cumulative egress (sent) message counts for this peer, and
   * the shared update-group's count if this peer is a group member. Idempotent
   * across members (each member clears the shared group's count to 0).
   */
  void clearEgressMessageCounts() {
    stats_.clearEgressMessageCounts();
    if (adjRibOutGroup_) {
      adjRibOutGroup_->clearEgressMessageCounts();
    }
  }

  /**
   * @brief  Reset cumulative ingress (recv) message counts for this peer.
   * Ingress is inherently per-peer (adj-rib-in), so there is no shared group.
   */
  void clearIngressMessageCounts() {
    stats_.clearIngressMessageCounts();
  }

  void processShadowRibEntryChange(ShadowRibEntry& srEntry) noexcept;

  /*
   * process a single Rib Update. virtual so tests can spy on rib-dump delivery
   * via MockAdjRib.
   */
  virtual void processRibMessage(const RibOutMessage& update) noexcept;

  /*
   * This function enables or disables the update-group state machine for this
   * AdjRib. It is independent of the global enableUpdateGroup knob.
   *
   * Some AdjRibs are never registered with UpdateGroupManager. They have no
   * update group, so they must not run the update-group transitions. The
   * AdjRib of a thrift stream subscriber is such an AdjRib. createAdjRib()
   * builds its AdjRibOutGroup with enableUpdateGroup=false.
   */
  void setEnableUpdateGroup(bool enable) {
    enableUpdateGroup_ = enable;
  }

  bool isUpdateGroupEnabled() const {
    return enableUpdateGroup_;
  }

  /*
   * Schedule sendBgpUpdates on the AdjRib's asyncScope_.
   * When tryPullNewChangeItems is true, sendBgpUpdates may exit early
   * on backpressure to pull the newest updates from CL, after which
   * sendBgpUpdates will be rescheduled.
   * When tryPullNewChangeItems is false, we drain the full packing list
   * before exiting sendBgpUpdates.
   */
  void scheduleSendBgpUpdates(bool tryPullNewChangeItems) noexcept;

  /*
   * Schedule deferredPushToPeer on the AdjRib's asyncScope_.
   * Called by AdjRibOutGroup::tryPushToPeer when the peer's queue is blocked.
   * Returns false if scheduling failed (asyncScope null or cancelled),
   * so the caller can avoid leaving the blocked bitmap bit set.
   * onResolved (optional) is forwarded to deferredPushToPeer.
   */
  bool scheduleDeferredPushToPeer(
      nettools::bgplib::FiberBgpPeer::InputMessageT message,
      folly::Function<void() noexcept> onResolved = {}) noexcept;

  /*
   * Get the peer's bit position within the update group
   * Returns the invalid bit-position sentinel if the peer is not associated
   * with any group.
   */
  uint64_t getGroupBitPosition() const {
    return groupBitPosition_;
  }

  /*
   * Clear the peer's bit position.
   * Called when peer is unregistered from group
   */
  void clearGroupBitPosition() {
    groupBitPosition_ = kInvalidGroupBitPosition;
  }

  /*
   * Set the peer's bit position within the update group
   */
  void setGroupBitPosition(uint64_t bitPos) {
    groupBitPosition_ = bitPos;
  }

  void resetSlowPeerDurationTimer();

  void cancelSlowPeerDurationTimer();

  void scheduleSlowPeerDurationTimer(
      folly::EventBase& evb,
      std::chrono::milliseconds timeout,
      const std::shared_ptr<AdjRib>& self);

  /*
   * Set the update group this peer belongs to
   */
  void setUpdateGroup(std::shared_ptr<AdjRibOutGroup> group) {
    adjRibOutGroup_ = std::move(group);
  }

  /*
   * Get the update group this peer belongs to
   */
  std::shared_ptr<AdjRibOutGroup> getUpdateGroup() const {
    return adjRibOutGroup_;
  }

  /*
   * Get the update group this peer belongs to
   */
  const UpdateGroupKey& getUpdateGroupKey() const {
    return updateGroupKey_;
  }

  /*
   * Build and set the UpdateGroupKey from the peer's current state.
   */
  const UpdateGroupKey& buildAndSetUpdateGroupKey();

  /*
   * Get the bounded output queue (for backpressure-aware distribution)
   */
  std::shared_ptr<AdjRibOutQueueT> getAdjRibOutQueue() const {
    return adjRibOutQueue_;
  }

  /*
   * Bookkeeping about the peer's update group state transitions, kept
   * alongside (not inside) peerState_.
   */
  struct PeerUpdateStateInfo {
    // Epoch time (ms) when peerState_ last changed; 0 until the first one.
    int64_t lastModifiedTimeMs{0};
    // Reason for the most recent detachment; retained after the peer rejoins.
    std::optional<AdjRibOutGroup::DetachReason> lastDetachReason;
    // Epoch time (ms) of the most recent detachment; 0 if never detached.
    int64_t lastDetachTimeMs{0};
  };

  /*
   * Set the peer's state in update group state machine. Stamps
   * peerUpdateStateInfo_.lastModifiedTimeMs when the state actually changes.
   */
  void setPeerState(PeerUpdateState state);

  /*
   * Get the peer's current state in update group
   */
  PeerUpdateState getPeerState() const {
    return peerState_;
  }

  const PeerUpdateStateInfo& getPeerUpdateStateInfo() const {
    return peerUpdateStateInfo_;
  }

  /*
   * Persist the reason for a detachment from the update group and stamp the
   * detach time.
   */
  void setDetachReason(AdjRibOutGroup::DetachReason reason);

  /*
   * Per-peer blocking metadata for frequency-based slow peer detection.
   * Tracks the number of times a peer gets blocked within a rolling window.
   */
  struct PeerBlockInfo {
    std::chrono::steady_clock::time_point windowStart{
        std::chrono::steady_clock::now()};
    uint32_t blockCount{0};
  };

  /*
   * Get mutable reference to peer block info for slow peer detection
   */
  PeerBlockInfo& getPeerBlockInfo() {
    return peerBlockInfo_;
  }

  /*
   * Reset peer block info (e.g., on peer down)
   */
  void resetPeerBlockInfo() {
    peerBlockInfo_ = PeerBlockInfo{};
  }

  /*
   * RIB_OUT_DISCREPANCY — peer's RIB-OUT entries diverged from
   *        the group during collapse, preventing rejoin.
   * IS_DETACHED_FAST_PEER — peer caught up before the group moved on
   *        CL (DFP). No collapse needed on rejoin.
   * DETACHED_ON_REGISTRATION — peer entered the group already detached
   *        either via initial dump (DETACHED_INIT_DUMP) or policy re-eval,
   *        but not via detachSlowPeer. During collapse,
   *        all group-only entries must be announced since the peer has no
   *        meaningful detachedRibVersion. Such peers cannot use isDFP() check.
   * EGRESS_EOR_PENDING_V4 — the peer still owes an IPv4 egress EoR
   *        marker. Set at RIB-dump intake (or copied from the group on detach);
   *        cleared by clearEgressEoRPendingV4() in sendPendingEoRs once the
   * EoRs have been pushed to the peer's egress queue (sendPendingEoRs also sets
   * the one-time egressEoRsSent_ flag). EGRESS_EOR_PENDING_V6 — same as
   * EGRESS_EOR_PENDING_V4 for IPv6, cleared by clearEgressEoRPendingV6().
   */
  enum AdjRibFlag : uint32_t {
    RIB_OUT_DISCREPANCY = 0,
    IS_DETACHED_FAST_PEER,
    DETACHED_ON_REGISTRATION,
    EGRESS_EOR_PENDING_V4,
    EGRESS_EOR_PENDING_V6,
  };

  bool isAdjRibFlagSet(AdjRibFlag flag) const {
    return (adjRibFlags_ & (1 << flag)) != 0;
  }

  void setAdjRibFlag(AdjRibFlag flag) {
    adjRibFlags_ |= (1 << flag);
  }

  void clearAdjRibFlag(AdjRibFlag flag) {
    adjRibFlags_ &= ~(1 << flag);
  }

  void resetAdjRibFlags() {
    adjRibFlags_ = 0;
  }

  /**
   * Get the cached RIB version - the max version this peer has consumed.
   * Used for backpressure visibility to see how caught up this peer is
   * with the current RIB state. A value of 0 indicates the peer is new
   * or down (displayed as "N/A" in CLI).
   *
   * If this peer is part of an update group AND is in-sync with the group,
   * returns the group's cached version since all group members consume updates
   * as a unit. If peer is in group but not in-sync (detached or not yet
   * completed initial dump), returns individual cached version.
   * Standalone peers (no group) return their individual cached version.
   */
  uint64_t getLastSeenRibVersion() const {
    if (useGroupStats()) {
      return adjRibOutGroup_->getLastSeenRibVersion();
    }
    return lastSeenRibVersion_;
  }

  /**
   * Set the cached RIB version for this peer.
   * Only updates if version is greater than current (tracks max consumed).
   * Used during initial dump when group sets the version on all members,
   * and when processing RIB announcements/withdrawals.
   */
  void setLastSeenRibVersion(uint64_t version) {
    if (version > lastSeenRibVersion_) {
      lastSeenRibVersion_ = version;
      stats_.setPeerTableVersion(version);
    }
  }

  /**
   * Clears the cached RIB version for this peer. This should be called
   * when the peer goes down.
   */
  void clearLastSeenRibVersion() {
    lastSeenRibVersion_ = 0;
    stats_.setPeerTableVersion(0);
  }

  /*
   * Set the detached packing list (deep copy from group on detachment).
   */
  void setDetachedPackingList(const AttrToPrefixMap& pl) {
    attrToPrefixMap_ = pl;
  }

  /*
   * Get the detached RIB version (set at detachment time).
   */
  uint64_t getDetachedRibVersion() const {
    return detachedRibVersion_;
  }

  /*
   * Set the detached RIB version (called once at detachment time).
   */
  void setDetachedRibVersion(uint64_t version) {
    detachedRibVersion_ = version;
  }

  /*
   * Get the detached consumer (for detached mode processing).
   */
  std::shared_ptr<AdjRibOutConsumer> getDetachedConsumer() const {
    return changeListConsumer_;
  }

  /*
   * Register a detached consumer to tracker with null marker (implicitly
   * means it is at the end of the CL).
   * Called for new peers entering a running group (DETACHED_INIT_DUMP).
   * @param changeListTracker - The global change list tracker
   * @param addPathBitmap - The add-path consumer bitmap
   * @param nonAddPathBitmap - The non-add-path consumer bitmap
   * @return true if a new consumer was registered; false if registration was
   *         skipped (a consumer already exists or changeListTracker is null).
   */
  bool registerDetachedConsumer(
      std::shared_ptr<ChangeTracker<ShadowRibEntry>>& changeListTracker,
      ConsumerBitmap& addPathBitmap,
      ConsumerBitmap& nonAddPathBitmap);

  /*
   * Register a detached consumer and join it at the group consumer's position.
   * Called during slow peer detachment so the peer starts consuming CL
   * from where the group left off.
   * @param changeListTracker - The global change list tracker
   * @param groupConsumer - The group's consumer to join at its position
   * @param addPathBitmap - The add-path consumer bitmap
   * @param nonAddPathBitmap - The non-add-path consumer bitmap
   */
  void registerDetachedConsumerAtGroupPosition(
      std::shared_ptr<ChangeTracker<ShadowRibEntry>>& changeListTracker,
      const std::shared_ptr<AdjRibOutGroupConsumer>& groupConsumer,
      ConsumerBitmap& addPathBitmap,
      ConsumerBitmap& nonAddPathBitmap);

  /*
   * DFP (Detached Fast Peer) check.
   * Returns true if peer finished draining PL before the group moved on CL.
   * DFP skips CL consumption and transitions directly to
   * DETACHED_READY_TO_JOIN with IS_DETACHED_FAST_PEER flag set.
   *
   * Conditions:
   *   1. attrToPrefixMap_ is empty (PL fully drained)
   *   2. Group's lastSeenRibVersion == peer's lastSeenRibVersion (group hasn't
   *      moved)
   *   3. Group's packing list is non-empty (group hasn't finished sending its
   *      PL yet, but the peer already has)
   */
  bool isDFP() const;

  /*
   * DSP (Detached Slow Peer) readiness check.
   * Returns true only when both the peer and group have consumed the entire
   * change list, peer has drained packing list, and peer and group have
   * matching materialized RIB versions.
   *
   * Conditions:
   *   1. The peer packing list is empty
   *   2. The peer and group change-list consumers are at the end
   *   3. The peer and group last-seen RIB versions match
   */
  bool canWaitForGroupToRejoin() const;

  /*
   * Rejoin readiness at the acceptance boundary: every condition
   * canWaitForGroupToRejoin() checks, plus an empty group packing list.
   */
  bool isReadyToRejoinGroup() const;

  /*
   * Returns true if the peer is in any DETACHED_* state.
   */
  bool isDetachedPeer() const;

  /*
   * Start the detached peer's independent processing loop.
   * Called when a DETACHED_BLOCKED peer unblocks.
   * Creates CL consumption timer and schedules sendBgpUpdates to drain PL.
   * DFP/DSP transitions are handled at the end of sendBgpUpdates.
   */
  void activateDetachedModeProcessing();

  /*
   * Clean up detached mode processing state on rejoin or peer down.
   */
  void deactivateDetachedModeProcessing();

  /**
   * Get nexthop the route to be advertised with for given post
   * policy attributes. However, does not set nexthop in that
   * attribute copy.
   */
  /**
   * Determine whether nexthop-self should be applied.
   * Returns false if policy explicitly set the nexthop (honors policy intent).
   * Zero nexthop is always overridden (invalid per RFC 4271).
   */
  bool shouldApplyNexthopSelf(
      const std::shared_ptr<const BgpPath>& postOutAttrs,
      bool isNexthopSetByPolicy) noexcept;

  folly::IPAddress getNewNexthopFromAttributesOut(
      const bool isV4Prefix,
      const std::shared_ptr<const BgpPath>& postOutAttrs,
      bool isNexthopSetByPolicy = false) noexcept;

  // Ensure asyncScope_ is initialized
  folly::coro::Task<void> ensureAsyncScopeInitialized() noexcept;

  // Get the ingress policy name
  inline const std::optional<std::string>& getIngressPolicyName() const {
    return ingressPolicyName_;
  }

  // Get the egress policy name
  inline const std::optional<std::string>& getEgressPolicyName() const {
    return egressPolicyName_;
  }

  inline const std::string& getPeerName() const noexcept {
    return formattedPeerName_;
  }

  inline const nettools::bgplib::BgpPeerId& getRemotePeerId() const noexcept {
    return *remotePeerId_;
  }

  /* Used to resume any updates to attrToPrefixMap_. */
  void reschedulePackingTimers() noexcept;

  /*
   * Test-only: when true, PeerManagerBase::processRibDumpReq re-buffers the
   * request for this peer instead of walking the shadow RIB, keeping the
   * peer in DETACHED_INIT_DUMP. Read/written on the EventBase thread.
   */
  bool testOnlyDeferInitDump{false};

  /*
   * Test-only: when true, AdjRibOutGroup skips this peer in
   * checkAndAcceptReadyToJoinPeers / maybeAcceptDSPPeer, keeping
   * it in DETACHED_READY_TO_JOIN. Read/written on the EventBase thread.
   */
  bool testOnlyDeferDrjAcceptance{false};

  /*
   * Queue an AFI-scoped local RIB redump without applying peer capability
   * gates. Returns false if the AFI was not negotiated.
   */
  bool triggerOutboundRedump(nettools::bgplib::BgpUpdateAfi afi) noexcept;

 private:
  static constexpr uint64_t kInvalidGroupBitPosition =
      std::numeric_limits<uint64_t>::max();

  /**
   * Return whether effective RIB-OUT statistics, packing, and progress state
   * should come from this peer's update group.
   */
  bool useGroupStats() const noexcept {
    return adjRibOutGroup_ && groupBitPosition_ != kInvalidGroupBitPosition &&
        adjRibOutGroup_->isPeerInSync(groupBitPosition_);
  }

  /** Return the effective RIB-OUT statistics for this peer. */
  const AdjRibStats& getGroupPeerRibOutStats() const noexcept {
    return useGroupStats() ? adjRibOutGroup_->getStats() : stats_;
  }

  /** Return the effective RIB-OUT packing list for this peer. */
  const AttrToPrefixMap& getGroupPeerPackingList() const noexcept {
    return useGroupStats() ? adjRibOutGroup_->getAttrToPrefixMap()
                           : attrToPrefixMap_;
  }

  /*
   * Check if a detached peer should transition to DETACHED_READY_TO_JOIN.
   * Sets IS_DETACHED_FAST_PEER flag if DFP (only for DETACHED_RUNNING peers;
   * DETACHED_INIT_DUMP peers are never DFP since they were never in sync).
   * Called at end of sendBgpUpdates() when update groups are enabled.
   */
  void transitionPeerUpdateState() noexcept;

  // Called when BGP session is terminated
  folly::coro::Task<void> sessionTerminated(
      const nettools::bgplib::FiberBgpPeer::BgpSessionStop&
          sessionStop) noexcept;

  // Clean up out-dealy related data-structures.
  void cleanUpOutDelay();

  /*
   * For a given prefix and policy out message, get postOutAttrs
   * and the corresponding policy term name. prefix must be in policyOut
   */
  const std::shared_ptr<routing::AttributesAndPolicy<BgpPath>>
  getPostPolicyOutAttrsAndPolicyFromMessage(
      const folly::CIDRNetwork& prefix,
      const PolicyOutMessage& policyOut) const noexcept {
    auto search = policyOut.result.find(prefix);
    // prefix MUST be in policyOut
    CHECK(search != policyOut.result.end());
    return search->second;
  }

  /**
   * Update Link-Bandwidth extended community as per the peer configuration.
   * See `configerator/structs/neteng/fboss/bgp/bgp_config.thrift` for details
   * on various control knobs for receiving and advertising link-bandwidth
   * community.
   */
  void updateAdvertiseLbwExtCommunity(
      const RibOutAnnouncementEntry& update,
      const std::shared_ptr<BgpPath> postPolicyAttrs) noexcept;
  void updateReceiveLbwExtCommunity(
      const std::shared_ptr<BgpPath> postPolicyAttrs) const noexcept;

  /**
   * @brief Update a policy name and detect if it changed
   *
   * @param currentName Reference to the current policy name (may be nullopt)
   * @param newName The new policy name to set. If nullopt, clears the policy.
   * @return bool True if the policy name was changed, false if it remained the
   * same
   *
   */
  bool updatePolicyName(
      std::optional<std::string>& currentName,
      const std::optional<std::string>& newName);

  /*******************************************************************************
   *             Start   -    AdjRibIn functionality
   *******************************************************************************/
  folly::coro::Task<void> processPeerMessageLoop(
      std::shared_ptr<folly::coro::Baton> terminateBaton) noexcept;

  const AdjRibEntry* FOLLY_NULLABLE getStaleRibInEntry(
      const folly::CIDRNetwork& prefix,
      const uint32_t pathId) noexcept;

  void promoteStaleRibInEntryIfExists(
      const folly::CIDRNetwork& prefix,
      uint32_t receivedPathId);

  /*
   * Promote a stale entry in-place (optimized GR)
   * clears the stale bit and decrements the stale entry count.
   * Used when enableOptimizedGR_ is true.
   */
  void promoteStaleRibInEntryIfExistsInPlace(
      const folly::CIDRNetwork& prefix,
      uint32_t receivedPathId);

  /**
   * In certain scenarios, we will not allow a new adjRibEntry
   * to be created due to a per-switch limit or a requirement
   * to cap routes per peer. This method checks whether or not
   * we can add a new entry according to those limits.
   */
  bool canAddRibInEntry(
      const folly::CIDRNetwork& prefix,
      const std::shared_ptr<const BgpPath>& attrs);

  /**
   * This method captures logic invoked during processPeerAnnounced
   * to determine
   *  (1) if we should announce this prefix to Rib,
   *  (2) if we need to withdraw this prefix from Rib
   *
   * and updates the @withdrawnPfxPathIds
   * or @groupAnnouncedPrefixes accordingly.
   *
   * @return uint32_t Returns 1 if the prefix was announced to RIB, 0 otherwise.
   */
  uint32_t maybeAnnouncePrefix(
      const folly::CIDRNetwork& prefix,
      const uint32_t pathId,
      const std::shared_ptr<const BgpPath>& postInAttrs,
      folly::not_null<AdjRibEntry*> adjRibEntry,
      PrefixPathIds& withdrawnPfxPathIds,
      folly::F14NodeMap<std::shared_ptr<const BgpPath>, PrefixPathIds>&
          groupAnnouncedPrefixes);

  // Send bgp of a set of prefixes and attrs
  folly::coro::Task<void> sendRibInAnnouncement(
      const PrefixPathIds& pfxPathIds,
      const std::shared_ptr<const BgpPath>& attrs) noexcept;

  // Send RibInWithdrawal of a set of prefixes
  folly::coro::Task<void> sendRibInWithdrawal(
      const PrefixPathIds& pfxPathIds) noexcept;

  /*
   * For a given prefix, preInAttrs, and policy action data, get
   * postInAttrs after route filter and policy processing. Nullptr will be
   * returned if route filter or policy blocks the prefix.
   * Returned attributes are published and adjRibEntry is updated
   * for postPolicyResult_.
   */
  std::shared_ptr<const BgpPath> getPostInRouteFilterAndPolicyAttributes(
      const folly::CIDRNetwork& prefix,
      const std::shared_ptr<BgpPath>& prePolicyAttrs,
      std::shared_ptr<BgpPolicyActionData>& policyActionData,
      folly::not_null<AdjRibEntry*> adjRibEntry);

  /*
   * For a given prefix, preInAttrs, and policy action data, get
   * postInAttrs. Nullptr will be returned if policy blocks the prefix.
   * Returned attributes are published and adjRibEntry is updated
   * for postPolicyResult_.
   */
  std::shared_ptr<const BgpPath> getPostInPolicyAttributes(
      const folly::CIDRNetwork& prefix,
      const std::shared_ptr<BgpPath>& prePolicyAttrs,
      std::shared_ptr<BgpPolicyActionData>& policyActionData,
      folly::not_null<AdjRibEntry*> adjRibEntry);

  // Process v4, v6 announced route
  folly::coro::Task<void> processPeerAnnounced(
      const std::vector<nettools::bgplib::RiggedIPPrefix>& prefixes,
      const std::shared_ptr<BgpPath>& attrs) noexcept;

  // Process v4, v6 withdrawn route
  folly::coro::Task<void> processPeerWithdrawn(
      const std::vector<nettools::bgplib::RiggedIPPrefix>& prefixes) noexcept;

  // process a single Bgp update2
  folly::coro::Task<void> processPeerUpdate(
      const nettools::bgplib::BgpUpdate2& update) noexcept;

  // process BgpEndOfRib
  folly::coro::Task<void> processPeerEoR(
      const nettools::bgplib::BgpEndOfRib& eor) noexcept;

  // Process BgpRouteRefresh received from the peer.
  void processPeerRouteRefresh(
      const nettools::bgplib::BgpRouteRefresh& rr) noexcept;

  /*
   * Update attributes after receiving from this peer (nexthop, as-path)
   * Same shared_ptr will be returned if no changes are needed
   */
  std::shared_ptr<BgpPath> updateAttributesIn(
      const std::shared_ptr<BgpPath>& attrs) noexcept;

  // Mark all routes learnt from this peer stale
  void markLearntRoutesStale() noexcept;

  /*
   * Mark all routes learnt from this peer stale in-place (optimized GR)
   * a stale bit is set for the entries and total number of stale entries count
   * is incremented
   */
  void markLearntRoutesStaleInPlace() noexcept;

  /*
   * Cleanup all stale routes — runs collectStaleRoutes()
   * + pushStaleWithdrawal().
   */
  folly::coro::Task<void> cleanupStaleRoutes(
      bool isGrHelperMode = true) noexcept;

  /*
   * Split out from cleanupStaleRoutes() so detached timer callbacks can do
   * the AdjRib state mutation synchronously.
   */
  std::optional<RibInWithdrawal> collectStaleRoutes(
      bool isGrHelperMode = true) noexcept;

  /*
   * Async push of a previously-collected stale-route withdrawal. Used by the
   * detached timer callbacks; tracked via pendingRibInPushes_ and drained in
   * stop() before AdjRib tear-down so this->ribInQ_ access is always valid.
   */
  folly::coro::Task<void> pushStaleWithdrawal(
      RibInWithdrawal withdrawal) noexcept;

  /*
   * Helper used by GR timer callbacks to schedule a detached push of a
   * previously-collected withdrawal.
   */
  void schedulePendingRibInPush(RibInWithdrawal withdrawal) noexcept;

  /*
   * Cleanup stale routes in-place (optimized GR)
   * Iterates over the entries in the tree and clears entries with stale bit set
   * Uses a two-pass approach since RadixTree doesn't support deletion during
   * iteration: first collects stale prefixes, then deletes them in batches.
   */
  folly::coro::Task<void> cleanupStaleRoutesInPlace(
      bool isGrHelperMode = true) noexcept;

  // Try deleting a ribInEntry
  void tryDeleteRibInEntry(
      const folly::CIDRNetwork& prefix,
      const AdjRibEntry* adjRibEntry,
      uint32_t pathId) noexcept;

  /**
   * @brief: Process withdrawn prefix in AdjRibIn.
   *
   * @details: This function gets RibInEntry based on input prefix and pathId.
   * If the entry exists,
   * 1. preIn, postInPolicy is set to nullptr.
   * 2. If prefix was announced to RIB already, prefix is added to
   * pfxPathIds which is eventually sent to RIB.
   * 3. Prefix is deleted from AdjRibIn(Even for prefixes not announced to RIB).
   * If the entry does not exist, it is a no-op.
   */
  void processWithdrawnPrefixRibInEntry(
      PrefixPathIds& pfxPathIds,
      const folly::CIDRNetwork& prefix,
      const uint32_t& pathId) noexcept;

  /**
   * @brief: Determine to process route update based on per-switch limit.
   *
   * @details: This function checks whether:
   *  1. the `per-switch` total received ingress path limit is reached.
   *  2. the `per-switch` total unique prefix limit is reached.
   * If either of the above 2 limits is reached, we will not process
   * depending on overload protection mode:
   *  1. If DROP_EXCESS_PREFIXES, simply drop the prefix.
   *  2. If APPLY_GOLDEN_PREFIX_POLICY, the golden prefix policy decides
   * whether to drop the prefix.
   *
   * @param: toalPathCount - the total ingress+egress path count across ALL
   * peers.
   * @param: network - the prefix to be checked in the radix tree.
   * @param: switchLimitConfig - switch limit config contains the limit.
   *
   * @note: switchLimitConfig_ is a private member of adjRib. However,
   * passing it in as an argument list is for easy unit-testing purpose.
   *
   * @return: True if max-cap limit is reached and update is disregarded.
   */
  bool dropPrefixForOverloadProtection(
      uint64_t totalPathCount,
      const folly::CIDRNetwork& network,
      const BgpPath& attrs,
      const std::shared_ptr<thrift::BgpSwitchLimitConfig>& switchLimitConfig);

  /*
   * Triggers persistent safe mode by notifying PeerManager, which writes
   * the safe mode file and kicks off RIB-IN re-evaluation.
   *
   * Shared by the ingress trigger in dropPrefixForOverloadProtection and the
   * egress trigger in handleRibAnnouncedEntry. No-op once safe mode is already
   * on. Note that currently only APPLY_GOLDEN_PREFIX_POLICY enters safe mode.
   */
  void triggerSafeMode() noexcept;

  bool canApplyGoldenPrefixPolicyMode() const noexcept;

  /*
   * check whether the current number of unique VIPs is under limit
   * Two cases to allow adding this golden VIP:
   * 1. the VIP is alredy in AdjRibPrefixSet
   * 2. golden VIP limit is set and there is space to accept this VIP.
   * if golden VIP limit isn't set, we regard it as no VIPs are allowed, even
   * for golden VIPs
   */
  bool allowGoldenVip(const folly::CIDRNetwork& network) const;

  /*
   * @brief: Determine if prefix set limit will be breached.
   *
   * @details: This function checks the refcount of a prefix and determine if
   * it is over the pre-configured limit.
   *
   * @param: network - the prefix to be checked in the radix tree.
   * @param: prefixSetSize - the pre-configured limit of unique prefix set.
   *
   * @return: True is prefix set size will over the limit by adding the
   * prefix.
   */
  bool isOverPrefixLimit(
      const folly::CIDRNetwork& network,
      const uint64_t prefixSetSize);

  /**
   * @brief: Determine to process route update based on per-peer limit.
   *
   * @details: This function checks whether the `per-peer` received route or
   * received path(with add-path enabled) limit is reached.
   *
   * @param: routeCnt - per peer receuved route(or path) count
   * @param: isPrefilter - boolean flag to indicate this is preIn/postIn
   * filter
   *
   * @return: True if max-cap limit is reached and update is disregarded.
   */
  bool capRoutesPerPeer(uint64_t routeCnt, bool isPrefilter);

  /*
   * Increment the fb303 counter peer.totalDroppedPrefixes by `count`. Called
   * from every capacity/overload drop path (switch-level overload protection,
   * per-peer route caps, and safe-mode golden-prefix purge) so the counter is
   * populated consistently regardless of which protection mechanism dropped
   * the route.
   *
   * Note: despite the "Prefix" name (kept for ODS continuity), `count` is a
   * number of dropped routes (prefix + path), not unique prefixes.
   */
  void incrementPrefixesDroppedByLimit(uint32_t count = 1);

  /**
   * @brief Validate incoming attributes.
   * @details Checks for valid attributes. In the case for invalid AsPath, it
   * will generate and send a notification per RFC5065 section 6.
   * @param attrs BGP attributes to validate.
   * @param params Peering parameters for the peer.
   * @return True if attributes are valid, false otherwise.
   */
  bool validateAttributesIn(
      const std::shared_ptr<const BgpPath>& attrs,
      const PeeringParams& params);

  /**
   * @brief: Apply golden prefix policy to prefix in adjRibEntry.
   * @details: This function applies golden prefix policy to prefix in
   * adjRibEntry. If the prefix is not golden, it will be added to
   * prefixesPathIdToPurge.
   *
   * @param adjRibEntry: AdjRibEntry to apply golden prefix policy.
   * @param prefixesPathIdToPurge: Map of prefixes to pathId to purge.
   */
  void applyGoldenPrefixPolicy(
      const folly::CIDRNetwork& prefix,
      const std::unique_ptr<AdjRibEntry>& adjRibEntry,
      folly::F14FastMap<folly::CIDRNetwork, uint32_t>& prefixesPathIdToPurge);

  /**
   * @brief: Helper method to iterate through all AdjRibIn (stale and non-stale)
   * entries.
   *
   * @details: This is a template method that accepts a callable function/lambda
   * which is invoked for each prefix entry. It handles the
   * underlying tree iteration differences between addPath and lite trees
   * transparently.
   *
   * @tparam Coro: Coroutine that accepts parameters:
   *                   (prefix, pathId, adjRibEntry*)
   * @param evaluateStale: If true, processes stale entries from adjRibInStale_.
   *                 If false, processes active entries from the main trees.
   *
   * TODO: Remove evaluateStale once stale prefixes are identified by
   * a bit in the existing tree instead of using a separate tree adjRibInStale_.
   */
  template <typename Coro>
  folly::coro::Task<void> forEachAdjRibInEntry(
      Coro&& coroutine,
      bool evaluateStale = false);

  /**
   * @brief: Re-evaluate all prefixes (stale and non-stale) in AdjRibIn against
   * the golden prefix policy when BGP++ enters safe mode during prefix overload
   * scenarios.
   */
  folly::coro::Task<void> processAdjRibReEvaluationForSafeMode();

  /**
   * @brief: Re-evaluates all prefixes (stale and non-stale) in AdjRibIn when
   * ingress policies change.
   *
   * @details: When BGP++ receives either an update to ingress policies such as
   * route filter policy or ingress routing policy, all
   * prefixes (stale and non-stale) in AdjRibIn are re-evaluated against the new
   * policies.
   *
   * Evaluation order for ingress policy change:
   * 1. Ingress route filter policy (if configured)
   * 2. Ingress routing policy (if configured and route passes filter)
   */
  folly::coro::Task<void> processAdjRibReEvaluationForPolicyChange();

  /**
   * @brief: Re-evaluate AdjRibIn entries with updated policy configuration.
   *
   * @details: This method contains the core logic for re-evaluating prefixes
   * in AdjRibIn when ingress policies change. It can process either active
   * entries or stale entries based on the isStale parameter and applies
   * the updated policy configuration to determine announcements/withdrawals.
   *
   * @param evaluateStale: If true, processes stale entries from adjRibInStale_.
   *                 If false, processes active entries from the main trees.
   *
   * TODO: Remove evaluateStale once stale prefixes are identified by
   * a bit in the existing tree instead of using a separate tree adjRibInStale_.
   */
  folly::coro::Task<void> reEvaluateAdjRibEntriesWithUpdatedPolicy(
      bool evaluateStale = false);

  /**
   * @brief Send accumulated withdrawal and announcement updates to RIB.
   *
   * @details This helper method processes and sends batched updates to the RIB
   * after policy evaluation is complete. It handles both withdrawal and
   * announcement updates.
   */
  folly::coro::Task<void> sendRibInUpdates(
      const PrefixPathIds& withdrawnPfxPathIds,
      const folly::F14NodeMap<std::shared_ptr<const BgpPath>, PrefixPathIds>&
          groupAnnouncedPrefixes);

  /**
   * @brief Process a prefix through policy evaluation and batch announcements
   * and withdrawals it to RIB.
   *
   * @details This helper method evaluates a single prefix against the
   * configured ingress policies (route filter and routing policy) and
   * determines the appropriate action. If the prefix passes policy evaluation,
   * it is added to the batched announcements. If
   * the prefix is blocked by policy, any existing announcement to RIB is
   * withdrawn.
   *
   * @return uint32_t Returns the count of prefixes announced to RIB (0 or 1).
   */
  uint32_t processPrefixWithPolicy(
      const folly::CIDRNetwork& prefix,
      const uint32_t pathId,
      const std::shared_ptr<BgpPath>& postConfigInAttrs,
      std::shared_ptr<BgpPolicyActionData>& policyActionData,
      folly::not_null<AdjRibEntry*> adjRibEntry,
      PrefixPathIds& withdrawnPfxPathIds,
      folly::F14NodeMap<std::shared_ptr<const BgpPath>, PrefixPathIds>&
          groupAnnouncedPrefixes);

  /*******************************************************************************
   *             End   -    AdjRibIn functionality
   *******************************************************************************/

  /*******************************************************************************
   *             Start   -    AdjRib show functionality
   *******************************************************************************/
  std::optional<std::pair<
      neteng::fboss::bgp_attr::TIpPrefix,
      neteng::fboss::bgp::thrift::TBgpPath>>
  convertEntryToPath(
      const folly::CIDRNetwork& prefix,
      const AdjRibEntry& adjRibEntry,
      const RouteFilterType& type,
      const std::optional<uint32_t>& pathId = std::nullopt) noexcept;

  /*******************************************************************************
   *             End   -    AdjRib show functionality
   *******************************************************************************/
  // ----------------------- AdjRib processing --------------------------

  inline bool stalePathExist(AdjRibTreeIterator<uint32_t> itr) {
    /*
     * A stale prefix may have empty stale path mapping stored.
     * To avoid de-referencing empty map iterator, make sure at least one
     * stale entry exist for the prefix iterator.
     */
    return (!itr.atEnd()) && (!itr.value().empty());
  }

  inline bool pathExistForPrefix(
      AdjRibTreeIterator<uint32_t> itr,
      uint32_t pathId) {
    return (!itr.atEnd()) && (itr.value().find(pathId) != itr.value().end());
  }

  // Determine if this peer is IBGP
  inline bool isIBgpPeer() {
    return (getBgpSessionType() == BgpSessionType::IBGP);
  }

  // Determine if this peer is non-Confed EBGP
  inline bool isEBgpPeer() {
    return (getBgpSessionType() == BgpSessionType::EBGP);
  }

  // Determine if this peer is Confed EBGP
  inline bool isConfedEBgpPeer() {
    return (getBgpSessionType() == BgpSessionType::ConfedEBGP);
  }

  inline BgpSessionType getBgpSessionType() const {
    /*
     * remoteAs_ is the ASN accepted for the current session, while
     * peeringParams_.remoteAs is the configured primary ASN and can differ
     * during ASN migration.
     */
    if (peeringParams_.isConfedPeer &&
        peeringParams_.localAs != getRemoteAs()) {
      return BgpSessionType::ConfedEBGP;
    }
    if (peeringParams_.localAs != getRemoteAs()) {
      return BgpSessionType::EBGP;
    }
    return BgpSessionType::IBGP;
  }

  // Check if ingress policy is configured
  inline bool ingressPolicyConfigured() const {
    return ingressPolicyName_ != std::nullopt;
  }

  // Check if egress policy is configured
  inline bool egressPolicyConfigured() const {
    return egressPolicyName_ != std::nullopt;
  }

  // Check if nexthop afi match prefix afi, or if v4OverV6Nexthop is enabled
  inline bool isNexthopSupported(
      const folly::CIDRNetwork& prefix,
      const folly::IPAddress& nexthop) const {
    bool ret{false};
    ret |= prefix.first.isV4() && nexthop.isV4();
    ret |= prefix.first.isV6() && nexthop.isV6();
    ret |=
        isV4OverV6NexthopNegotiated_ && prefix.first.isV4() && nexthop.isV6();
    return ret;
  }

  /*
   * create policy action data
   * currently only captures "link bandwidth" policy data:
   * - original asn & lbw, peer-bps-configs, aggregatged recv/local weight
   * etc.
   *
   * add more other types of policy-action data
   */
  inline std::shared_ptr<BgpPolicyActionData> createPolicyActionData(
      const std::shared_ptr<const BgpPath>& attrs,
      const std::optional<size_t> switchId = std::nullopt,
      const std::optional<size_t> multiPathSize = std::nullopt,
      const std::optional<float> aggregateReceivedUcmpWeight = std::nullopt,
      const std::optional<float> aggregateLocalUcmpWeight = std::nullopt,
      const std::optional<float> ribPolicyUcmpWeight = std::nullopt) {
    return createPolicyActionDataCommon(
        peeringParams_,
        attrs,
        switchId,
        multiPathSize,
        aggregateReceivedUcmpWeight,
        aggregateLocalUcmpWeight,
        ribPolicyUcmpWeight);
  }

  /*
   * Return the post policy attribute, the accept/deny policy term name (if
   * applicable) and the post policy information
   */
  std::tuple<std::shared_ptr<const BgpPath>, std::string, PostPolicyInfo>
  getPostPolicyAttributesPolicyTermAndInfo(
      const std::string& policyName,
      const folly::CIDRNetwork& prefix,
      const std::shared_ptr<const BgpPath>& prePolicyAttrs,
      const std::shared_ptr<BgpPolicyActionData>& policyActionData,
      bool isPartialDrain = false);

  /***********************************
   * AdjRibOut functions
   ***********************************/

  /**
   * ------------------- Sending to IO thread -----------------------
   * =================== WITH BACKPRESSURE ======================
   */

  /**
   * @brief: Cancels all packing timers if adjRibOutQueue_
   * is blocked. Waits for adjRibOutQueue_ to unblock before resuming.
   *
   * @details: Returns true if the queue was blocked when we called this method.
   * Returns false if the queue was not blocked when we called this method.
   */
  folly::coro::Task<bool> waitForQueueSpace() noexcept;

  /**
   * @brief: Write UPDATE and maybe EOR to adjRibOutQueue_ by draining
   * AttrToPrefixMap.
   *
   * @details: This coro may return early if the AttrToPrefixMap is determined
   * to be stale. This can occur if the queue blocked before we constructed
   * the next update, and the changelist received updates from RIB during
   * the time that we yielded.
   *
   * This task can only be run one at a time.
   */
  folly::coro::Task<void> sendBgpUpdates(bool tryPullNewChangeItems) noexcept;

  /*
   * Deferred push coroutine — waits for queue space then pushes.
   * Runs on the peer's asyncScope_ (not the group's).
   * RAII guard calls markPeerUnblocked on completion.
   * onResolved (optional) runs once, only on a successful push.
   */
  folly::coro::Task<void> deferredPushToPeer(
      nettools::bgplib::FiberBgpPeer::InputMessageT message,
      std::shared_ptr<AdjRib> self,
      folly::Function<void() noexcept> onResolved = {}) noexcept;

  /*
   * Returns true if we should break out of sendBgpUpdates early due to
   * backpressure. Called only when backpressured.
   */
  inline bool shouldExitEarlyOnBackPressure(
      bool tryPullNewChangeItems) const noexcept {
    /*
     * If egressEoR is pending, the packing list constitutes entries from a
     * full RIB walk and we must send it all with EoR. Otherwise,
     * sendBgpUpdates may choose to defer sending the packing list in favor
     * of processing newer items from the changelist.
     */
    return tryPullNewChangeItems && !egressEoRsPending();
  }

  /* Used to pause any updates to attrToPrefixMap_. */
  void cancelPackingTimers() noexcept;

  /**
   * Returns number of prefixes drained from prefixPathIds into the
   * RiggedIPPrefix container for a BgpUpdate2. Stops before the approximate
   * size of serialized BgpUpdate2 exceeds the 4k limit.
   * The 4k limit also must account for the bgp header length and approximate
   * serialized attr length,
   */
  uint32_t packPrefixesWithLimit(
      const uint32_t approximateSerializedAttrLen,
      PrefixSet& prefixPathIds,
      std::vector<nettools::bgplib::RiggedIPPrefix>& bgpUpdatePrefixes);

  /**
   * @brief: Build an update message with the given attrs and PrefixSet.
   *
   * @details: The BgpUpdate2 generated is roughly within the 4k size limit of
   * ~1 serialized PDU. This method does not write the generated update
   * to queue.
   */
  std::shared_ptr<nettools::bgplib::BgpUpdate2> buildUpdateWithSizeEstimation(
      const BgpPathWithAfi& attrsWithAfi,
      PrefixSet& prefixPathIds) noexcept;

  /*
   * ----------------------- Announcement --------------------------
   * process RibOutAnnouncement update message
   */
  void processRibOutAnnouncement(
      const RibOutAnnouncement& announcement) noexcept;
  void markEgressEoRsPendingFor(
      const RibOutAnnouncement& announcement) noexcept;

  /*
   * Route Refresh re-dump state transitions. Called from
   * processRibOutAnnouncement at the natural emission boundaries:
   *   maybeBeginRrDump -> Idle to InProgress on the first announcement of a
   *     re-dump (announcement.routeRefreshAfi && state == Idle). Phase 2 ERR
   *     hook: emit BoRR here when isEnhancedRouteRefreshNegotiated_.
   *   maybeEndRrDump -> InProgress to Idle when the re-dump's terminal
   *     announcement fires (announcement.routeRefreshAfi &&
   *     announcement.sendWithEoR && state == InProgress). Phase 2 ERR hook:
   *     emit EoRR here when
   * isEnhancedRouteRefreshNegotiated_. Both are no-ops outside their guard
   * conditions, so callers can invoke them unconditionally on every
   * announcement.
   */
  void maybeBeginRrDump(const RibOutAnnouncement& announcement) noexcept;
  void maybeEndRrDump(const RibOutAnnouncement& announcement) noexcept;
  bool isRrDumpInProgress() const noexcept {
    return rrDumpState_ == RrDumpState::InProgress;
  }

  /*
   * Determines if we need to announce this prefix to peer
   * Considers IBGP, EBGP, Route reflector settings
   */
  bool canAnnounce(const RibOutAnnouncementEntry& update) noexcept;

  /*
   * Delegates to canAnnounceForGroup() when update-group is enabled,
   * otherwise falls back to canAnnounce().
   */
  bool canAnnounceEntry(const RibOutAnnouncementEntry& update) noexcept;

  bool suppressLoopedAdvertisements(
      const std::shared_ptr<const BgpPath>& attrs) noexcept;

  void scheduleOutDelayTimer(void) noexcept;

  void handleRibAnnouncedEntry(
      const RibOutAnnouncementEntry& entry,
      bool initialDump) noexcept;

  /*
   * Egress counterpart of the ingress limit check in canAddRibInEntry: true
   * when admitting one more RIB-OUT entry would breach total_path_limit.
   *
   * Only total_path_limit applies here, and we have excluded
   * prefix_limit on egress side. This is because prefix_limit is measured
   * against AdjRibPrefixSet, which is populated solely by the prefixes learned
   * from peers in AdjRibIn. We also do not drop local routes (prefixes
   * originated by ourselves).
   */
  bool willExceedEgressSwitchLimit() const noexcept;

  /*
   * Process single prefix best path announcement
   * update: the update to be processed with multiple prefixes
   * deferred: is true if the processing is due to out-delay time out.
   */
  void processRibAnnouncedEntry(const RibOutAnnouncementEntry& update) noexcept;

  /*
   * Try inserting a ribOutEntry based on the prefix and next hop.
   * Return the existing one if the entry already exists; Otherwise,
   * create a new one and return it.
   */
  AdjRibEntry* FOLLY_NULLABLE tryInsertRibOutEntry(
      const folly::CIDRNetwork& prefix,
      const folly::IPAddress& nexthop,
      bool isLocalRoute) noexcept;

  /*
   * Get post policy attributes (including CRF filtering)
   * Look up in the cache - if found just return the result
   * Else run thru policy and store result in cache.
   */
  const std::shared_ptr<const BgpPath> getPostOutPolicyAttributes(
      const RibOutAnnouncementEntry& update,
      AdjRibEntry* adjRibEntry,
      const std::shared_ptr<const BgpPath>& prePolicyAttrs,
      const std::string& updatePeerIdStr);

  // Get post policy attributes and along with post policy information
  std::pair<const std::shared_ptr<const BgpPath>, const PostPolicyInfo>
  getPostOutPolicyAttributesAndInfo(
      const RibOutAnnouncementEntry& update,
      AdjRibEntry* adjRibEntry,
      const std::shared_ptr<const BgpPath>& prePolicyAttrs,
      const std::string& updatePeerIdStr);

  // ------------- Centralized Route Filtering (CRF) ----------------
  bool blockedByEgressRouteFilter(
      const RibOutAnnouncementEntry& update,
      const std::string& peerId) const;

  bool blockedByIngressRouteFilter(const folly::CIDRNetwork& prefix) const;

  /*
   * ----------------------- Out delay --------------------------
   * Process update for out-delay feature.
   * Return pair<>.first : whether to continue processing with
   *                       processRibAnnounced
   *        pair<>.second: whether a new prefix got deferred or not.
   */
  std::pair<bool, bool> processOutDelay(const RibOutAnnouncementEntry& update);

  // Starts an async timer for out-delay.
  void programOutDelayTimer() noexcept;

  /*
   * ----------------------- Withdrawal --------------------------
   * process RibOutWithdrawal update message
   */
  void processRibOutWithdrawal(const RibOutWithdrawal& withdrawal) noexcept;

  /*
   * Handles case where we receive RIB announcement (due to bestpath change)
   * which cannot be announced. This may lead to implicit withdrawal of prefix
   * without any explicit withdrawal from RIB
   */
  void handleImplicitWithdrawal(
      const folly::CIDRNetwork& prefix,
      const folly::IPAddress& nextHop) noexcept;

  // Process single prefix best path withdrawal
  void processRibWithdraw(
      const folly::CIDRNetwork& prefix,
      uint32_t pathId) noexcept;

  /*
   * Try to update the prefix/path id pair of the adjRibEntry
   * to withdrawal (nullptr) attr. Also updates attrToPrefixMap_
   * to reflect withdrawal.
   */
  void tryInsertWithdrawal(
      const folly::CIDRNetwork& prefix,
      AdjRibEntry* adjRibEntry,
      const std::string& insertedMsg,
      const std::string& notInsertedMsg);

  // Try deleting a ribOutEntry
  void tryDeleteRibOutEntry(
      const folly::CIDRNetwork& prefix,
      const AdjRibEntry* adjRibEntry,
      uint32_t pathId) noexcept;

  void incrementPreInPrefixCount(
      const folly::CIDRNetwork prefix,
      const bool isVipPrefix,
      const bool isGoldenVip);

  void decrementPreInPrefixCount(const folly::CIDRNetwork prefix);

  /****************************************************************************
   *             Start   -    AdjRib Utility Function
   ****************************************************************************/
  /*
   * @brief: check if path attributes indicate as-path loop.
   * @param: attrs - shared_ptr of BgpPath including as_path.
   * @param: params - BGP peering parameters with local/global ASN.
   * @return: True if path attributes indicate Asloop.
   */
  static bool hasAsPathLoop(
      const std::shared_ptr<const facebook::bgp::BgpPath>& attrs,
      const facebook::bgp::PeeringParams& params);

  /*
   * @brief: check if path attributes indicate route-reflector loop.
   * @param: attrs - shared_ptr of BgpPath including as_path.
   * @param: params - BGP peering parameters with local/global ASN.
   * @return: True if path attributes indicate a loop.
   */
  static bool hasRRLoop(
      const std::shared_ptr<const facebook::bgp::BgpPath>& attrs,
      const facebook::bgp::PeeringParams& params);

  /*
   * @brief: validate as_path(as_seq or as_set) based on confed boolean flag.
   * @param: attrs - shared_ptr of BgpPath including as_path.
   * @param: isConfedEBgpPeer - boolean flag for confed eBGP.
   * @param: isEBgpPeer - boolean flag for eBGP.
   * @return: folly::Unit if all validation passed. Otherwise return string
   *          with failure reason.
   */
  static folly::Expected<folly::Unit, std::string> validateAsPath(
      const std::shared_ptr<const facebook::bgp::BgpPath>& attrs,
      bool isConfedEBgpPeer,
      bool isEBgpPeer);

  /*
   * @brief: check whether prefix is golden VIP. If a prefix carry
   * kGoldenVipCommunity, it's regarded as golden VIP
   * @param: comms, the communities that a prefix carries.
   */
  static bool isGoldenVip(
      const std::vector<nettools::bgplib::BgpAttrCommunityC>& comms) {
    for (const auto comm : comms) {
      if (comm == kGoldenVipCommunity) {
        return true;
      }
    }
    return false;
  }

  /**
   * Check if this peer has a per-peer egress policy override in the config
   * (as opposed to inheriting from the peer group).
   */
  bool hasPeerEgressPolicyOverride() const noexcept;

  /****************************************************************************
   *             End   -    AdjRib Utility Function
   ****************************************************************************/

  // Peering parameters to which this AdjRib belongs to
  std::shared_ptr<nettools::bgplib::BgpPeerId> remotePeerId_;
  PeeringParams peeringParams_;
  /* Remote ASN received from the BGP FSM for the current session. */
  uint32_t remoteAs_{0};
  std::string formattedPeerName_;
  AfiIpv4Negotiated isAfiIpv4Negotiated_{false};
  AfiIpv6Negotiated isAfiIpv6Negotiated_{false};
  V4OverV6Nexthop isV4OverV6NexthopNegotiated_{false};
  EnhancedRouteRefreshNegotiated isEnhancedRouteRefreshNegotiated_{false};
  RouteRefreshNegotiated isRouteRefreshNegotiated_{false};
  /*
   * Remote (pre-negotiation) MP-EXT-exists bit, used to derive
   * legacyV4NlriEncoding in the update-group key. Default true = assume
   * MP-capable peer, preserving today's MP_REACH behavior for callers that do
   * not plumb it through.
   */
  bool remoteMpExtExist_{true};
  /*
   * Session-level state machine for Route Refresh re-dumps (RFC 2918 §4).
   *
   * Idle           : no re-dump in progress; per-entry dedup active.
   * InProgress     : re-dump active; per-entry dedup bypassed so byte-identical
   *                  attrs still reach the wire. Set on the first announcement
   *                  with `routeRefreshAfi`; cleared when an announcement with
   *                  `routeRefreshAfi` and `sendWithEoR=true` completes,
   *                  or defensively on session teardown if a re-dump aborts
   *                  mid-flight.
   *
   * Phase 2 (RFC 7313 ERR) hook: maybeBeginRrDump / maybeEndRrDump are the
   * natural emission points for BoRR / EoRR markers when ERR is negotiated.
   * Promoting from a bare bool to a typed state machine documents the
   * transition semantics and centralizes the marker-emission hook.
   */
  enum class RrDumpState { Idle, InProgress };
  RrDumpState rrDumpState_{RrDumpState::Idle};
  bool as4ByteCapable_{true}; /* Negotiated 4-byte ASN capability */
  bool extNhEncodingCapable_{false}; /* Negotiated RFC5549 capability */

  /*
   * NOTE: deliberately use reference to share the SAME folly::coro primitive
   * across multiple DIFF adjribs to schedule corotine task.
   */
  folly::EventBase& evb_;
  std::optional<folly::coro::CancellableAsyncScope> asyncScope_{std::in_place};

  /*
   * Cancellation source for the rib-dump task scheduled on PeerManagerBase's
   * asyncScope_. Its presence (and not-yet-cancelled state) indicates a rib
   * dump is currently scheduled or in flight for this peer; cancelled in stop()
   * so teardown cancels an in-flight dump.
   */
  std::optional<folly::CancellationSource> ribDumpCancellationSource_;

  // message queue to rib
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ_;

  /*
   * fromAdjRibQ_ is the MPSC (Multiple-Producer-Single-Consumer) queue
   * between multiple adjribs and peer-manager to transmit multiple adjRib
   * events.
   *
   * Each adjRib will have a reference to write to for event publication.
   */
  MonitoredMPMCQueue<ObservableMessageT>& fromAdjRibQ_;

  /*
   * Session Terminate baton. Used to do sequential synchronization between
   * adjRib and peerManager. Posted when both message processing loops complete.
   * Latch semantics: passes through between post() and reset().
   */
  std::shared_ptr<folly::coro::Baton> sessionTerminateBaton_;

  // Policy Manager
  std::shared_ptr<PolicyManager> policyManager_;

  folly::not_null_shared_ptr<std::atomic<bool>> isSafeModeOn_;

  // Ingress and egress configured policies
  std::optional<std::string> ingressPolicyName_;
  std::optional<std::string> egressPolicyName_;

  /*
   * Policy cache to avoid duplicate policy evaluation
   * TODO: remove the shared_ptr access to use try_get from folly::Singleton
   */
  std::shared_ptr<AdjRibPolicyCache> policyCache_;

  //  Ptr to the AdjRibOutGroup, aka, update-group this adjRib belongs to.
  std::shared_ptr<AdjRibOutGroup> adjRibOutGroup_{nullptr};

  // Route Filter Statement
  std::shared_ptr<const RouteFilterStatement> routeFilterStmt_{nullptr};

  // Route Filter Logger
  std::unique_ptr<RouteFilterLogger> routeFilterLogger_{nullptr};

  // Golden Prefix Policy
  std::shared_ptr<GoldenPrefixPolicy> goldenPrefixPolicy_{nullptr};

  /**
   * Policy update flags for asynchronous AdjRib re-evaluation
   *
   * These boolean flags are used to track which AdjRibs require re-evaluation
   * when route filter/routing policies are updated. The update is marked
   * pending since the re-evaluation of ingress and egress routes happens
   * asynchronously and cannot be completed in one uninterrupted execution.
   * The operation is staggered across different context switches, making
   * state tracking necessary to handle cases where other events may intervene
   * during the re-evaluation process.
   *
   * @pendingIngressPolicyUpdate_: When set to true, indicates that entries in
   * the adjRibIn tree are pending re-evaluation with the updated ingress route
   * filter policy. This triggers policy re-evaluation for all prefixes in this
   * AdjRib to determine which routes should be announced to or withdrawn from
   * the RIB based on the new policy.
   *
   * @egressPolicyUpdateRequired_:  When set to true, indicates that a
   * RibDumpReq is yet to be sent for this adjRibOut to process the updated
   * egress route filter policy.
   *
   */
  bool pendingIngressPolicyUpdate_{false};
  bool egressPolicyUpdateRequired_{false};

  /*
   * [AdjRibIn]
   *
   * Read-only queue to process messages from FiberBgpPeer with types of:
   *  - BgpUpdate2
   *  - BgpEndOfRib
   *  - BgpSessionStop
   *
   * NOTE: FiberBgpPeer(oqueue) is the write of this queue.
   */
  std::shared_ptr<AdjRibInQueueT> adjRibInQueue_;

  /*
   * [AdjRibOut]
   *
   * Write-only queue to send messages to FiberBgpPeer with types of:
   *  - BgpUpdate2
   *  - BgpEndOfRib
   *  - BgpNotification
   */
  std::shared_ptr<AdjRibOutQueueT> adjRibOutQueue_;

  /*
   * Ingress EoR
   * For each supported address family by peer, we should receive one EoR
   * This indicates pending EoR afis that are not yet received
   */
  std::set<nettools::bgplib::BgpUpdateAfi> pendingIngressEoRAfis_{};

  /*
   * Egress EoR
   * Flag to indicate whether we have sent eor to peer or not
   * TODO: This should be two flags if we decide to split v4 and v6 logic
   */
  bool egressEoRsSent_{false};

  int64_t eorSentTime_{0};
  int64_t eorReceivedTime_{0};

  /**
   * Boolean indicating if we have a pending sendBgpUpdates coro
   * already scheduled on asyncScope_ to ensure only one can be
   * scheduled at a time.
   */
  bool sendCoroScheduled_{false};

  // TCP session termination counter
  int64_t flapCounter_{0};

  /*
   * Peer-specific packing list, populated when peer detaches from update group.
   * The detached peer drains this independently. Cleared on rejoin or peer
   * down.
   *
   * The packing list is represented as a map of BgpPath attributes to a set of
   * prefixes. This map is updated whenever withdrawals and announcements come
   * in and represents a packing list.
   *
   * Announcements are keyed to a shared_ptr<BgpPath>.
   *
   * Withdrawals are keyed to nullptr.
   */
  AttrToPrefixMap attrToPrefixMap_;

  /*
   * Rib radix tree for AdjRib supporting recvAddPath capability.
   * key is prefix, value is a map of {path id: AdjRibEntry}
   * Each prefix can have multiple path id.
   */
  AdjRibPathTree adjRibInPathTree_;
  /*
   * Rib radix tree for AdjRib not supporting recvAddPath capability.
   * key is prefix, value is AdjRibEntry
   * There is considerable memory consumption when unordered-map is
   * used to maintain path to relevant entry. It can be optimized
   * considerably for AdjRib instances that are not enabled with
   * add-path capability
   */
  AdjRibLiteTree adjRibInLiteTree_;

  /*
   * This is used in GR.
   *
   * Rib radix tree. key is prefix, value is a map of {nextHop's IPAddress:
   * AdjRibEntry}
   *
   * When the peer is doing Graceful Restart (GR), as in BGP RFC4724, all
   * routes learnt from a Restarting peer will be marked as STALE. Hence, in
   * our BGP++ implementation, all routes in adjRibInPathTree_ will be moved to
   * adjRibInStale_.
   *
   * Path ID may not be consistent between restarts, but it's still used as key
   * here just to identify individual paths being marked as stale (we can't use
   * nexthop for this since paths can have same nexthop).
   */
  AdjRibPathTree adjRibInStale_;

  // Timers

  /*
   * Grace-period for allowing peer to re-establish connection.
   * Previous instance advertised routes are purged if session does not
   * re-establish within this period. This time-out is declared by peer during
   * session establishement using GR-capability attr.
   */
  std::chrono::seconds remoteGrRestartTime_{};
  std::unique_ptr<folly::AsyncTimeout> remoteGrRestartTimer_;

  /*
   * TODO: Make this configurable (preferrably on a per-peer basis).
   * Amount of time after which all stale routes are cleared. A restarting
   * peer must readvertise a route (that was announced in previous
   * incarnation) before expiry of this timer to avoid black-holing of
   * traffic.
   */
  std::chrono::seconds stalePathTime_{kDefaultStalePathTimeOut};

  std::unique_ptr<folly::AsyncTimeout> stalePathTimer_;

  /*
   * SemiFutures for in-flight detached pushes of RibInWithdrawal payloads
   * (from stalePathTimer_ / remoteGrRestartTimer_ callbacks). Drained in stop()
   * so the AdjRib isn't destroyed while pushes are still suspended on ribInQ_'s
   * back-pressure semaphore.
   */
  std::vector<folly::SemiFuture<folly::Unit>> pendingRibInPushes_;

  // Stats associated to this AdjRib
  class AdjRibStats stats_;

  // Determines if adjRib is in session established or in terminated state
  bool isSessionEstablished_{false};

  /*
   * Specifically in the squence when BGP is re-starting, initial set of peers
   * expected, to send initial announcement, are the ones that are UP before
   * initial fib sync-up is done. Any new adjRib created after initial fib
   * sync, should not be eligible to send those announcements
   *
   * On contrast note, any new adjRib created after initial fib sync should be
   * eligible for RibDumpRequest, where-as initial set of peers should not
   */
  bool inInitialAnnouncement_{false};

  /*
   * Updates which are deferred due to out-delay feature
   * Note that only a prefix which is learned the 1st time is deferred.
   * Subsequent updates do not trigger a new deferral. However an update
   * which is deferred will be overwritten by subsequent updates which arrive
   * before the deferred time out happens.
   */
  folly::F14NodeMap<folly::CIDRNetwork, RibOutAnnouncementEntry>
      deferredUpdates_;

  // out-delay in seconds
  std::chrono::seconds outDelay_{0s};

  // Timer for out-delay
  std::unique_ptr<folly::AsyncTimeout> outDelayTimer_;

  /*
   * This represents set of prefixes which are qualified to be scheduled in
   * the outDelay timer. This is created and cleared in processOutDelay method.
   *
   * The set of prefixes represented by newDeferredPrefixes_ is only for the
   * scope of time taken by consumeChanges() to complete when called during
   * a specific timer expiry. This set then passed to scheduleOutDelayTimer()
   * to schedule in PQ timer which where then these outDelay prefixes are held
   * to be processed on expiry of outDelay timer.
   */
  std::vector<folly::CIDRNetwork> newDeferredPrefixes_;

  // Min-heap to store all the out-delay time entries.
  std::priority_queue<
      AdjRibOutDelayEntry,
      std::vector<AdjRibOutDelayEntry>,
      std::greater<AdjRibOutDelayEntry>>
      outDelayPQ_;

  /*
   * Whether we should prevent sending update to neighbor
   * if prefix's AS-path contains neighbor's ASN
   */
  bool sender_suppress_as_loop_{true};

  // whether we should send additioanl paths to the peer
  bool sendAddPath_{false};

  // whether we should receive additioanl paths from peer
  bool recAddPath_{false};

  // the path id generator used when we want to send additional paths.
  std::unique_ptr<PathIdGenerator> pathIdGenerator_;

  /*
   * Current state of this peer in update group state machine
   */
  PeerUpdateState peerState_{PeerUpdateState::DOWN};

  PeerUpdateStateInfo peerUpdateStateInfo_;

  PeerBlockInfo peerBlockInfo_;

  uint32_t adjRibFlags_{0};

  // Per-peer size tracking for device-level ODS counters
  uint32_t adjRibInSize_{0};
  uint32_t adjRibInStaleSize_{0};
  uint32_t deferredUpdatesSize_{0};

  /*
   * Update group key for this peer, which determines the update group this peer
   * belongs to.
   */
  UpdateGroupKey updateGroupKey_;

  /*
   * SwitchLimitConfig
   *
   * This is the device level limit to make sure BGP operate in a
   * pre-qualified scale without the risk of instability or memory exhaustion.
   */
  std::shared_ptr<thrift::BgpSwitchLimitConfig> switchLimitConfig_;

  /*
   * Feature flag to enable update group feature
   */
  bool enableUpdateGroup_{false};

  /**
   * Enable optimized GR (Graceful Restart) logic.
   * When enabled, routes are marked stale in-place using a stale bit rather
   * than moving them to a separate stale tree. A counter tracks the number of
   * stale entries for efficient cleanup.
   */
  bool enableOptimizedGR_{false};

  /**
   * Enable add-path receive reconciliation across a peer graceful restart
   * (gated off by default). Consumed by the add-path GR reconcile producer
   * gate in a later diff; nothing reads it yet.
   */
  bool enableAddPathGrReconcile_{false};

  /**
   * Enable RFC 4271 classic IPv4-unicast NLRI encoding toward capability-less
   * peers (default off). Read in buildAndSetUpdateGroupKey() to gate the
   * legacyV4NlriEncoding key bit; when false every peer keeps MP_REACH and no
   * legacy update group is formed -- exact pre-feature behavior, for instant
   * rollback via config.
   */
  bool enableLegacyV4NlriEncoding_{false};

  /**
   * Counter tracking the number of stale entries.
   * Used when enableOptimizedGR_ is true to efficiently determine if cleanup
   * is needed and to track progress during GR operations.
   */
  uint64_t staleEntryCount_{0};

  /**
   * ConfigManager for thread-safe, dynamic config access
   * Allows AdjRib to fetch fresh config values when needed, enabling
   * dynamic policy evaluation and config updates during runtime.
   */
  std::shared_ptr<ConfigManager> configManager_;

  /*
   * Bit position within the update group
   * Used for bitmap operations in AdjRibOutGroup
   */
  uint64_t groupBitPosition_{kInvalidGroupBitPosition};

  /*
   * Per-peer timer that fires when peer has been blocked too long.
   * Cached on AdjRib for efficient access (AdjRibOutGroup is friend class).
   * Created/cancelled by AdjRibOutGroup::markPeerBlocked/markPeerUnblocked.
   */
  std::unique_ptr<folly::AsyncTimeout> slowPeerDurationTimer_{nullptr};

  /*
   * A consumer initialized to a global change tracker.
   * This consumer is to be explicitly initialized after AdjRib has been
   * created (it is not initialized as part of AdjRib constructor).
   * When update group is enabled, this is only populated when the peer
   * is in DETACHED_* state.
   */
  std::shared_ptr<AdjRibOutConsumer> changeListConsumer_{nullptr};

  std::unique_ptr<folly::AsyncTimeout> changeListConsumeTimer_{nullptr};
  uint32_t mraiInterval{kDefaultMraiInterval};

  static inline uint64_t policyReEvaluationBatchSize_{
      kPolicyReEvaluationBatchSize};

  /*
   * Semaphore to synchronize tree access between policy re-evaluation
   * and peer message processing
   */
  std::shared_ptr<folly::fibers::Semaphore> treeAccessSemaphore_{
      std::make_shared<folly::fibers::Semaphore>(1)};

  /*
   * Flag to enable dynamic policy evaluation.
   * When enabled, policy re-evaluation for Ingress can be triggered and
   * synchronized tree access is enforced.
   */
  bool enableDynamicPolicyEvaluation_{false};

  /*
   * Cached RIB version - tracks the max version this peer has consumed
   * from RIB updates. Used for backpressure visibility to see how
   * caught up each peer is with the current RIB state.
   * A value of 0 indicates the peer is new or down (displayed as "N/A").
   */
  uint64_t lastSeenRibVersion_{0};

  /*
   * RIB version when peer detached from group.
   * On detach: peer.detachedRibVersion_ = group.lastSeenRibVersion_
   * Not modified until the peer rejoins the group.
   * peer.detachedRibVersion_ <= peer.lastSeenRibVersion_ must always be true.
   * Used by the lazy clone decision algorithm to determine whether a peer
   * was sharing a group entry at detachment time.
   */
  uint64_t detachedRibVersion_{0};

  /**
   * Flag indicating BGP daemon shutdown is in progress.
   * When true, BGP goes through fast exit-path skipping expensive O(n) cleanup
   * operations prevent systemd timeout during shutdown. Set by
   * PeerManagerBase::stop() before initiating shutdown sequence.
   */
  bool isDaemonShutdown_{false};

  /**
   * @brief Conditionally wait for the tree access semaphore
   *
   * @details Waits for the semaphore only if enable_dynamic_policy_evaluation
   * is enabled. This allows synchronization between policy re-evaluation
   * and peer message processing.
   */
  folly::coro::Task<void> waitForTreeAccessSemaphore();

  /**
   * @brief Conditionally signal the tree access semaphore
   *
   * @details Signals the semaphore only if enable_dynamic_policy_evaluation
   * is enabled.
   */
  void signalTreeAccessSemaphore();

/*
 * per class placeholder for test code injection
 * only need to be setup once here
 */
#ifdef AdjRib_TEST_FRIENDS
  AdjRib_TEST_FRIENDS
#endif
};

/*
 * Class for adjRib specific changeListTracker consumer
 */
class AdjRibOutConsumer : public Consumer<ShadowRibEntry> {
 public:
  AdjRibOutConsumer(
      std::shared_ptr<ChangeTracker<ShadowRibEntry>>& changeListTracker_,
      std::shared_ptr<AdjRib> adjRib,
      std::string name,
      folly::EventBase& evb,
      ConsumerBitmap& addPathBitmap,
      ConsumerBitmap& nonAddPathBitmap)
      : Consumer<ShadowRibEntry>(*changeListTracker_, static_cast<size_t>(-1)),
        adjRib_(std::move(adjRib)),
        name_(std::move(name)),
        evb_(evb),
        addPathConsumerBitmap_(addPathBitmap),
        nonAddPathConsumerBitmap_(nonAddPathBitmap) {
    // Set up the callback to process change items
    setProcessChangeItemCallback([this](ChangeItem<ShadowRibEntry>* item) {
      return this->processChangeItem(item);
    });
  }

  virtual ~AdjRibOutConsumer() override;

  /**
   * Copy constructor (deleted).
   */
  AdjRibOutConsumer(const AdjRibOutConsumer&) = delete;

  /**
   * Copy assignment operator (deleted).
   */
  AdjRibOutConsumer& operator=(const AdjRibOutConsumer&) = delete;

  /**
   * Move constructor (deleted).
   */
  AdjRibOutConsumer(AdjRibOutConsumer&&) = delete;

  /**
   * Move assignment operator (deleted).
   */
  AdjRibOutConsumer& operator=(AdjRibOutConsumer&&) = delete;

  ProcessResult processChangeItem(ChangeItem<ShadowRibEntry>* item) {
    ShadowRibEntry& srEntry = item->getTypedObject();
    adjRib_->processShadowRibEntryChange(srEntry);
    return ProcessResult::CONTINUE;
  }

  /*
   * Reset bitmap bit for this consumer
   */
  void resetBitmap() noexcept {
    if (!adjRib_) {
      return;
    }
    size_t bitPosition = getBitPosition();
    XLOGF(DBG2, "Re-Set consumer bit-position: {}", bitPosition);
    // Clear from both bitmaps (consumer will only be in one)
    BitmapUtils::clearBit(addPathConsumerBitmap_, bitPosition);
    BitmapUtils::clearBit(nonAddPathConsumerBitmap_, bitPosition);
  }

  /*
   * Set bitmap bit for this consumer
   */
  void setBitmap() noexcept {
    if (!adjRib_) {
      return;
    }
    size_t bitPosition = getBitPosition();
    XLOGF(DBG2, "Set consumer bit-position: {}", bitPosition);
    if (adjRib_->sendAddPath()) {
      BitmapUtils::setBit(addPathConsumerBitmap_, bitPosition);
    } else {
      BitmapUtils::setBit(nonAddPathConsumerBitmap_, bitPosition);
    }
  }

 private:
  std::shared_ptr<AdjRib> adjRib_;
  std::string name_;
  folly::EventBase& evb_;
  ConsumerBitmap& addPathConsumerBitmap_;
  ConsumerBitmap& nonAddPathConsumerBitmap_;
};
} // namespace facebook::bgp
