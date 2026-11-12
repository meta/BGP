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

#include <folly/Singleton.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Collect.h>
#include <folly/coro/WithCancellation.h>
#include <folly/futures/Future.h>
#include <folly/logging/xlog.h>

#include "common/network/AddressUtil.h"
#include "fboss/lib/AlertLogger.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/config/ConfigStructs.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

using namespace facebook::neteng::fboss::bgp_attr;
using namespace facebook::neteng::fboss::bgp::thrift;
using namespace facebook::nettools::bgplib;

using facebook::nettools::bgplib::getCurrentTimeMs;
using folly::CIDRNetwork;
using folly::IPAddress;
using std::vector;

namespace facebook::bgp {

// One-time instantiation of the singleton instances
folly::Singleton<AdjRibPrefixSet> adjRibPrefixSetSingleton;
folly::Singleton<AdjRibPolicyCache> adjRibPolicyCacheSingleton;

/*
 * Cache of all policy terms is now defined in AdjRibCommon.cpp
 * (shared between AdjRib and AdjRibGroup)
 */
extern PostPolicyResultCacheT postPolicyResultCache_;

/******************************************************************************
 *      START   -   AdjRibPrefixSet methods                                   *
 ******************************************************************************/
std::shared_ptr<AdjRibPrefixSet> AdjRibPrefixSet::get() {
  return folly::Singleton<AdjRibPrefixSet>::try_get();
}

void AdjRibPrefixSet::addPrefix(
    const folly::CIDRNetwork& network,
    const bool isGoldenVip) {
  auto match = uniquePrefixes_.exactMatch(network.first, network.second);
  if (match.atEnd()) {
    // first time received this prefix, add the key with initialized refcount 0.
    match = uniquePrefixes_
                .insert(network.first, network.second, PrefixRef(0, false))
                .first;
  }
  // increment the refcount
  match.value().refCount_++;
  /*
   * if prefix exists, we may need to mark the prefix as golden VIP
   * Note: there is no case that a golden vip will be re-marked as non-golden
   */
  if (!match.value().isGoldenVip_ && isGoldenVip) {
    match.value().isGoldenVip_ = isGoldenVip;
    totalGoldenVipPrefixesCount_++;
  }
}

void AdjRibPrefixSet::delPrefix(const folly::CIDRNetwork& network) {
  auto match = uniquePrefixes_.exactMatch(network.first, network.second);
  if (match.atEnd()) {
    // non-existing prefix. Skip processing.
    XLOGF(
        WARNING,
        "Received a delPrefix for a non-existing prefix {}",
        folly::IPAddress::networkToString(network));
    return;
  }

  // decrement the refcount
  match.value().refCount_--;

  /*
   * ref count drops to/below zero. In case we have an unexpected data race,
   * loose the condition to check < 0 to make sure key is removed.
   */
  if (match.value().refCount_ <= 0) {
    if (match.value().isGoldenVip_) {
      totalGoldenVipPrefixesCount_ = (totalGoldenVipPrefixesCount_ == 0)
          ? 0
          : totalGoldenVipPrefixesCount_ - 1;
    }
    uniquePrefixes_.erase(network.first, network.second);
  }
}

std::pair<bool, AdjRibPrefixSet::PrefixRef> AdjRibPrefixSet::getRefCount(
    const folly::CIDRNetwork& network) {
  auto match = uniquePrefixes_.exactMatch(network.first, network.second);
  if (match.atEnd()) {
    // non-existing prefix. Return 0 as the default value.
    return std::make_pair(false, PrefixRef(0, false));
  }

  return std::make_pair(true, match.value());
}

void AdjRibPrefixSet::markGoldenVip(const folly::CIDRNetwork& network) {
  auto match = uniquePrefixes_.exactMatch(network.first, network.second);
  if (UNLIKELY(match.atEnd())) {
    return;
  }
  if (!match.value().isGoldenVip_) {
    match.value().isGoldenVip_ = true;
    totalGoldenVipPrefixesCount_++;
    PeerStats::setTotalGoldenVipPrefixes(totalGoldenVipPrefixesCount_);
  }
  return;
}

/******************************************************************************
 *      END   -   AdjRibPrefixSet methods                                     *
 ******************************************************************************/

AdjRib::~AdjRib() {
  cancelPackingTimers();
  setDetachedRibVersion(0);
  resetChangeListConsumer();
  policyCache_.reset();

  /*
   * Safety net: if stop() was never called (e.g. test fixtures that
   * overwrite adjRib_ without stopping the previous instance), cancel and
   * join the asyncScope_ here to avoid the CHECK failure in ~AsyncScope().
   * In production, stop() is always called before destruction.
   */
  if (asyncScope_) {
    folly::coro::blockingWait(asyncScope_->cancelAndJoinAsync());
    asyncScope_.reset();
  }

  XLOGF(
      INFO, "[Exit] Successfully destructed {}'s adjrib object", getPeerName());
}

void AdjRib::updateAdvertiseLbwExtCommunity(
    const RibOutAnnouncementEntry& update,
    const std::shared_ptr<BgpPath> postPolicyAttrs) noexcept {
  updateAdvertiseLbwExtCommunityCommon(peeringParams_, update, postPolicyAttrs);
}

void AdjRib::updateReceiveLbwExtCommunity(
    const std::shared_ptr<BgpPath> attrs) const noexcept {
  if (!attrs) {
    return;
  }
  if (!peeringParams_.receiveLinkBandwidth.has_value()) {
    return;
  }
  switch (*peeringParams_.receiveLinkBandwidth) {
    case ReceiveLinkBandwidth::DISABLE:
      // ignore receieved LBW
      attrs->pruneNonTransitiveLbwExtCommunity();
      break;
    case ReceiveLinkBandwidth::ACCEPT:
      // do nothing, accept LBW ext community as is
      break;
    case ReceiveLinkBandwidth::SET_LINK_BPS:
      // override LBW ext community with link bw from config
      CHECK(peeringParams_.linkBandwidthBps.has_value());
      attrs->setNonTransitiveLbwExtCommunity(
          peeringParams_.localAs, peeringParams_.linkBandwidthBps.value());
      break;
    default:
      CHECK(0);
  }
}

/**
 * @brief  A utility function to delete AdjRibEntry from the in/out adjrib tree
 *         The function refers to the right tree based on add-path capability
 *         enabled or not
 *
 * @param  ingress - Check ingress adjRib if true, else check egress adjRib
 * @param  prefix  - A prefix of an entry to be deleted
 * @param  pathId  - In case of add-path enabled, path-id of a specific entry
 *
 * @return void
 */
void AdjRib::deleteRibEntry(
    bool ingress,
    const CIDRNetwork& prefix,
    uint32_t pathId) noexcept {
  bool addPath = false;

  /*
   * First check if dealing with add-path or non add-path
   */
  if ((ingress && recAddPath_) || (!ingress && sendAddPath_)) {
    addPath = true;
  }

  if (addPath) {
    /*
     * Ingress
     */
    if (ingress) {
      auto match = adjRibInPathTree_.exactMatch(prefix.first, prefix.second);
      if (match.atEnd() ||
          (match.value().find(pathId) == match.value().end())) {
        /*
         * Prefix does not exist, nothing to delete
         */
        return;
      }
      match.value().erase(pathId);
      if (match.value().size() == 0) {
        adjRibInPathTree_.erase(match);
      }
      adjRibInSize_--;
      RibStats::decrAdjRibInCount(1);

      return;
    }

    /*
     * Egress
     */
    adjRibOutGroup_->deleteFromPathTree(
        adjRibOutGroup_->PathTree_, prefix, getPeerOwnerKey(), pathId);
    return;
  }

  /*
   * No add-path
   */
  if (ingress) {
    if (adjRibInLiteTree_.erase(prefix.first, prefix.second)) {
      adjRibInSize_--;
      RibStats::decrAdjRibInCount(1);
    }
  } else {
    adjRibOutGroup_->deleteFromLiteTree(
        adjRibOutGroup_->LiteTree_, prefix, getPeerOwnerKey());
  }

  return;
}

neteng::fboss::bgp::thrift::TAdjRibInPeerStats AdjRib::getRibInStatsSnapshot()
    const {
  using neteng::fboss::bgp::thrift::TAdjRibInPeerStats;
  using neteng::fboss::bgp::thrift::TAdjRibPeerKey;

  const auto& remotePeerId = getRemotePeerId();
  TAdjRibPeerKey peerKey;
  peerKey.peer_address() = remotePeerId.peerAddr.str();
  peerKey.remote_bgp_id() = static_cast<int64_t>(remotePeerId.remoteBgpId);

  TAdjRibInPeerStats snapshot;
  snapshot.peer_key() = std::move(peerKey);
  snapshot.peer_name() = peeringParams_.description;
  snapshot.pre_policy_path_count() =
      static_cast<int64_t>(stats_.getPreInPrefixCount());
  snapshot.post_policy_path_count() =
      static_cast<int64_t>(stats_.getPostInPrefixCount());
  /**
   * TODO: Populate `active_prefixes`, `active_paths`, `stale_prefixes`, and
   * `stale_paths` from the peer's semantic Adj-RIB-IN storage state.
   */
  return snapshot;
}

neteng::fboss::bgp::thrift::TAdjRibOutPeerStats AdjRib::getRibOutStatsSnapshot()
    const {
  using neteng::fboss::bgp::thrift::TAdjRibGroupKey;
  using neteng::fboss::bgp::thrift::TAdjRibOutPeerStats;
  using neteng::fboss::bgp::thrift::TAdjRibPeerKey;

  const auto& remotePeerId = getRemotePeerId();
  TAdjRibPeerKey peerKey;
  peerKey.peer_address() = remotePeerId.peerAddr.str();
  peerKey.remote_bgp_id() = static_cast<int64_t>(remotePeerId.remoteBgpId);

  const auto& group = getUpdateGroup();
  const auto& ribOutStats = getGroupPeerRibOutStats();
  const auto& packingList = getGroupPeerPackingList();

  TAdjRibOutPeerStats snapshot;
  snapshot.peer_key() = std::move(peerKey);
  if (group) {
    TAdjRibGroupKey groupKey;
    groupKey.egress_policy_name() =
        group->getGroupKey().egressPolicyName.value_or("");
    groupKey.group_id() = static_cast<int64_t>(group->getGroupId());
    snapshot.group_key() = std::move(groupKey);
  }
  snapshot.peer_name() = peeringParams_.description;
  snapshot.peer_state() = fmt::format("{}", getPeerState());
  snapshot.active_paths() =
      static_cast<int64_t>(ribOutStats.getPreOutPrefixCount());
  snapshot.packing_list_size() = static_cast<int64_t>(packingList.size());
  snapshot.out_delay_pending_keys() =
      static_cast<int64_t>(deferredUpdates_.size());
  /** TODO: Populate `active_prefixes` from the effective Adj-RIB-OUT state. */
  return snapshot;
}

/**
 * @brief  A utility function to insert AdjRibEntry in the in/out adjrib tree
 *         The function refers to the right tree based on add-path capability
 *         enabled or not
 *
 * @param  ingress - Check ingress adjRib if true, else check egress adjRib
 * @param  prefix  - A prefix of an entry to be added
 * @param  pathId  - In case of add-path enabled, path-id of a specific entry
 *
 * @return AdjRibEntry*
 */
AdjRibEntry* FOLLY_NULLABLE AdjRib::addRibEntry(
    bool ingress,
    const CIDRNetwork& prefix,
    uint32_t pathId) noexcept {
  bool addPath = false;

  /*
   * First check if dealing with add-path or non add-path
   */
  if ((ingress && recAddPath_) || (!ingress && sendAddPath_)) {
    addPath = true;
  }

  if (addPath) {
    /*
     * Ingress
     */
    if (ingress) {
      auto match = adjRibInPathTree_.exactMatch(prefix.first, prefix.second);
      bool isNew = true;
      if (match.atEnd()) {
        folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>> entry;

        entry[pathId] = std::make_unique<AdjRibEntry>(pathId);
        match = adjRibInPathTree_
                    .insert(prefix.first, prefix.second, std::move(entry))
                    .first;
      } else {
        isNew = (match.value().find(pathId) == match.value().end());
        match.value()[pathId] = std::make_unique<AdjRibEntry>(pathId);
      }
      if (isNew) {
        adjRibInSize_++;
        RibStats::incrAdjRibInCount();
      }

      return match.value().at(pathId).get();
    }

    /*
     * Egress
     */
    return adjRibOutGroup_->addToPathTree(
        adjRibOutGroup_->PathTree_, prefix, getPeerOwnerKey(), pathId);
  }

  /*
   * No add-path
   */
  if (ingress) {
    auto result = adjRibInLiteTree_.insert(
        prefix.first, prefix.second, std::make_unique<AdjRibEntry>(pathId));
    if (result.second) {
      adjRibInSize_++;
      RibStats::incrAdjRibInCount();
    }
    return result.first.value().get();
  }

  return adjRibOutGroup_->addToLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, getPeerOwnerKey(), pathId);
}

/**
 * @brief  A utility function to get AdjRibEntry from the in/out adjrib tree
 *         The function refers to the right tree based on add-path capability
 *         enabled or not
 *
 * @param  ingress - Check ingress adjRib if true, else check egress adjRib
 * @param  prefix  - A prefix of an entry to be retrieved
 * @param  pathId  - In case of add-path enabled, path-id of a specific entry
 *
 * @return AdjRibEntry*
 */
AdjRibEntry* FOLLY_NULLABLE AdjRib::getRibEntry(
    bool ingress,
    const CIDRNetwork& prefix,
    uint32_t pathId) noexcept {
  /*
   * A peer unregistered from its update group owns no RIB-OUT entries
   * (AdjRibOutGroup::unregisterPeer clears the group). Needed because the
   * thrift attribute-stats walk queries every AdjRib in adjRibs_, including
   * peers retained while down.
   */
  if (!ingress && !adjRibOutGroup_) {
    return nullptr;
  }
  bool addPath = false;

  /*
   * First check if dealing with add-path or non add-path
   */
  if ((ingress && recAddPath_) || (!ingress && sendAddPath_)) {
    addPath = true;
  }

  if (addPath) {
    if (ingress) {
      /*
       * Ingress
       */
      auto match = adjRibInPathTree_.exactMatch(prefix.first, prefix.second);
      if (match.atEnd() ||
          (match.value().find(pathId) == match.value().end())) {
        /*
         * Prefix does not exist
         */
        return nullptr;
      }

      return match.value().find(pathId)->second.get();
    }

    /*
     * Egress
     */
    return adjRibOutGroup_->getFromPathTree(
        adjRibOutGroup_->PathTree_, prefix, getPeerOwnerKey(), pathId);
  }

  /*
   * No add-path
   */
  if (ingress) {
    auto match = adjRibInLiteTree_.exactMatch(prefix.first, prefix.second);
    if (match.atEnd()) {
      return nullptr;
    }
    return match.value().get();
  }

  return adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, getPeerOwnerKey());
}

AdjRibEntry* FOLLY_NULLABLE AdjRib::getRibEntryWithUpdateGroup(
    const CIDRNetwork& prefix,
    uint32_t pathId) noexcept {
  return adjRibOutGroup_->getWritableRibEntryForPeer(
      prefix, getPeerOwnerKey(), pathId, detachedRibVersion_);
}

std::vector<CIDRNetwork> AdjRib::getAllPrefixes() noexcept {
  std::vector<CIDRNetwork> prefixes;
  uint32_t size = 0;

  if (recAddPath_) {
    size += adjRibInPathTree_.size();
  } else {
    size += adjRibInLiteTree_.size();
  }

  /*
   * A peer unregistered from its update group owns no RIB-OUT entries; only
   * its AdjRibIn prefixes remain. See AdjRibOutGroup::unregisterPeer.
   */
  if (adjRibOutGroup_) {
    size += sendAddPath_ ? adjRibOutGroup_->PathTree_.size()
                         : adjRibOutGroup_->LiteTree_.size();
  }

  prefixes.reserve(size);

  std::unordered_set<CIDRNetwork> prefixesSet;

  if (recAddPath_) {
    for (auto itr = adjRibInPathTree_.begin(); itr != adjRibInPathTree_.end();
         itr++) {
      prefixesSet.emplace(itr.ipAddress(), itr.masklen());
    }
  } else {
    for (auto itr = adjRibInLiteTree_.begin(); itr != adjRibInLiteTree_.end();
         itr++) {
      prefixesSet.emplace(itr.ipAddress(), itr.masklen());
    }
  }

  if (adjRibOutGroup_) {
    if (sendAddPath_) {
      for (auto itr = adjRibOutGroup_->PathTree_.begin();
           itr != adjRibOutGroup_->PathTree_.end();
           itr++) {
        auto ownerItr = itr->value().find(getPeerOwnerKey());
        if (ownerItr == itr->value().end()) {
          continue;
        }
        prefixesSet.emplace(itr.ipAddress(), itr.masklen());
      }
    } else {
      for (auto itr = adjRibOutGroup_->LiteTree_.begin();
           itr != adjRibOutGroup_->LiteTree_.end();
           itr++) {
        auto ownerItr = itr->value().find(getPeerOwnerKey());
        if (ownerItr == itr->value().end()) {
          continue;
        }
        prefixesSet.emplace(itr.ipAddress(), itr.masklen());
      }
    }
  }

  prefixes.insert(prefixes.cbegin(), prefixesSet.cbegin(), prefixesSet.cend());
  return prefixes;
}

folly::coro::Task<void> AdjRib::ensureAsyncScopeInitialized() noexcept {
  if (!asyncScope_ || asyncScope_->isScopeCancellationRequested()) {
    if (asyncScope_) {
      co_await asyncScope_->joinAsync();
    }
    asyncScope_.emplace();
  }
  co_return;
}

void AdjRib::logPeerEvent(const std::string& phase, const std::string& src) {
  XLOGF(
      INFO,
      "BGP_PEER_EVENT peer={} incarnation={} phase={} src={}",
      formattedPeerName_,
      flapCounter_,
      phase,
      src);
}

// Called when session established with a peer (in PeerManagerBase)
void AdjRib::sessionEstablished(
    const std::optional<uint16_t>& remoteGrRestartTime,
    std::shared_ptr<AdjRibInQueueT> adjRibInQueue,
    std::shared_ptr<AdjRibOutQueueT> adjRibOutQueue,
    const AfiIpv4Negotiated& isAfiIpv4Negotiated,
    const AfiIpv6Negotiated& isAfiIpv6Negotiated,
    const V4OverV6Nexthop& isV4OverV6NexthopNegotiated,
    const EnhancedRouteRefreshNegotiated& isEnhancedRouteRefreshNegotiated,
    const RouteRefreshNegotiated& isRouteRefreshNegotiated,
    const std::optional<BgpAddPathSendRec>& addPathCapa,
    bool as4ByteCapable,
    bool extNhEncodingCapable,
    bool remoteMpExtExist) noexcept {
  sessionEstablished(
      peeringParams_.remoteAs,
      remoteGrRestartTime,
      std::move(adjRibInQueue),
      std::move(adjRibOutQueue),
      isAfiIpv4Negotiated,
      isAfiIpv6Negotiated,
      isV4OverV6NexthopNegotiated,
      isEnhancedRouteRefreshNegotiated,
      isRouteRefreshNegotiated,
      addPathCapa,
      as4ByteCapable,
      extNhEncodingCapable,
      remoteMpExtExist);
}

void AdjRib::sessionEstablished(
    uint32_t remoteAs,
    const std::optional<uint16_t>& remoteGrRestartTime,
    std::shared_ptr<AdjRibInQueueT> adjRibInQueue,
    std::shared_ptr<AdjRibOutQueueT> adjRibOutQueue,
    const AfiIpv4Negotiated& isAfiIpv4Negotiated,
    const AfiIpv6Negotiated& isAfiIpv6Negotiated,
    const V4OverV6Nexthop& isV4OverV6NexthopNegotiated,
    const EnhancedRouteRefreshNegotiated& isEnhancedRouteRefreshNegotiated,
    const RouteRefreshNegotiated& isRouteRefreshNegotiated,
    const std::optional<BgpAddPathSendRec>& addPathCapa,
    bool as4ByteCapable,
    bool extNhEncodingCapable,
    bool remoteMpExtExist) noexcept {
  XLOGF(DBG1, "Starting AdjRib for {}", getPeerName());

  // Stop the restart-gr timer if running and fire the stale-path timer.
  if (remoteGrRestartTimer_) {
    XLOGF(
        INFO,
        "Starting stale path timer of {} secs for {}.",
        stalePathTime_.count(),
        getPeerName());

    remoteGrRestartTimer_.reset();
    stalePathTimer_ = folly::AsyncTimeout::schedule(
        std::chrono::duration_cast<std::chrono::milliseconds>(stalePathTime_),
        evb_,
        [this]() noexcept {
          XLOGF(
              INFO,
              "{}Stale path timer expired for {}. Purging stale paths.",
              facebook::fboss::BGPAlert().str(),
              getPeerName());
          auto withdrawal = collectStaleRoutes(/*isGrHelperMode=*/true);
          stalePathTimer_.reset();
          PeerStats::incrTotalPurgeForStalePathTimer();
          if (withdrawal) {
            schedulePendingRibInPush(std::move(*withdrawal));
          }
        });
  }
  remoteGrRestartTime_ =
      (remoteGrRestartTime ? std::chrono::seconds(*remoteGrRestartTime) : 0s);

  adjRibInQueue_ = std::move(adjRibInQueue);
  adjRibOutQueue_ = std::move(adjRibOutQueue);

  CHECK(adjRibOutQueue_ != nullptr);
  logPeerEvent("SESSION_QUEUES_INITIALIZED", BGP_LOG_SRC());

  isAfiIpv4Negotiated_ = isAfiIpv4Negotiated;
  isAfiIpv6Negotiated_ = isAfiIpv6Negotiated;
  isV4OverV6NexthopNegotiated_ = isV4OverV6NexthopNegotiated;
  isEnhancedRouteRefreshNegotiated_ = isEnhancedRouteRefreshNegotiated;
  isRouteRefreshNegotiated_ = isRouteRefreshNegotiated;
  as4ByteCapable_ = as4ByteCapable;
  extNhEncodingCapable_ = extNhEncodingCapable;
  remoteMpExtExist_ = remoteMpExtExist;
  remoteAs_ = remoteAs;
  std::tie(sendAddPath_, recAddPath_) = getAddPathCapa(addPathCapa);
  pathIdGenerator_ = std::make_unique<PathIdGenerator>(sendAddPath_);

  /*
   * to reset ingress eor according to peer annouced capability
   * peer might only support one address family
   */
  if (isAfiIpv4Negotiated_) {
    pendingIngressEoRAfis_.insert(BgpUpdateAfi::AFI_IPv4);
  }
  if (isAfiIpv6Negotiated_) {
    pendingIngressEoRAfis_.insert(BgpUpdateAfi::AFI_IPv6);
  }

  /* Reset egress eor timestamps and the one-time sent flag. */
  egressEoRsSent_ = false;
  eorSentTime_ = 0;
  eorReceivedTime_ = 0;

  /* Reset pending state from previous session */
  sendCoroScheduled_ = false;

  /*
   * A rejoining peer must start with zeroed egress prefix counts; they are
   * settled to 0 when the peer detaches or unregisters. A non-zero value here
   * means the accounting leaked, so surface it before clearing.
   */
  if (stats_.getPostOutPrefixCount() != 0 ||
      stats_.getPreOutPrefixCount() != 0) {
    XLOGF(
        ERR,
        "Non-zero egress prefix counts on session establishment for {}: "
        "postOutPrefixCount={}, preOutPrefixCount={}",
        getPeerName(),
        stats_.getPostOutPrefixCount(),
        stats_.getPreOutPrefixCount());
  }
  clearEgressPrefixCounts();

  /*
   * The per-AFI EGRESS_EOR_PENDING_V4/V6 pending flags live in adjRibFlags_
   * and are cleared by resetAdjRibFlags() below.
   */
  resetAdjRibFlags();

  /* initialize update group key */
  buildAndSetUpdateGroupKey();

  /*
   * Reset the baton so the next termination cycle starts fresh.
   * Must be after session setup completes and before new loops start.
   */
  sessionTerminateBaton_->reset();
  logPeerEvent("SESSION_ADJRIB_CREATED", BGP_LOG_SRC());
}

const UpdateGroupKey& AdjRib::buildAndSetUpdateGroupKey() {
  updateGroupKey_ = UpdateGroupKey::buildUpdateGroupKey(
      egressPolicyName_,
      /*
       * The peer's egress route filter statement affects egress output and so
       * belongs in the update group key, but it is not wired in yet -- we pass
       * an empty route filter name. As a result, setting or clearing a route
       * filter does not change the key (and thus does not re-group peers).
       * TODO: pass in the route filter statement name.
       */
      "",
      outDelay_,
      getBgpSessionType(),
      isAfiIpv4Negotiated_,
      isAfiIpv6Negotiated_,
      peeringParams_.isConfedPeer,
      peeringParams_.isRrClient,
      peeringParams_.advertiseLinkBandwidth,
      peeringParams_.receiveLinkBandwidth,
      peeringParams_.linkBandwidthBps.has_value()
          ? *peeringParams_.linkBandwidthBps
          : 0,
      peeringParams_.removePrivateAs,
      sendAddPath_,
      as4ByteCapable_,
      extNhEncodingCapable_,
      /*
       * legacyV4NlriEncoding: true for a peer that negotiated v4-unicast but
       * advertised no MP-EXT (capability-less / RFC 1771). isAfiIpv4Negotiated_
       * already excludes the MP-EXT-without-v4-unicast case (v4 is not
       * negotiated there, so no v4 UPDATE is built), so remoteMpExtExist_ alone
       * suffices. Keys capability-less peers into their own update group so
       * they never share a group with MP peers.
       *
       * Gated by the enable_legacy_v4_nlri_encoding thrift config knob (default
       * off): when off this stays false for every peer, so the encoder falls
       * back to MP_REACH and no legacy update group is formed -- exact
       * pre-feature behavior, for instant rollback via config.
       */
      enableLegacyV4NlriEncoding_ && isAfiIpv4Negotiated_ && !remoteMpExtExist_,
      peeringParams_.peerGroupName.value_or(""),
      hasPeerEgressPolicyOverride(),
      /*
       * The group producer transforms attributes once using the first
       * registered member's peering params. Both localAs and asConfedId are
       * consumed by the AS_PATH transform, so peers whose values differ must
       * key into separate groups.
       */
      peeringParams_.localAs,
      peeringParams_.asConfedId);
  return updateGroupKey_;
}

bool AdjRib::hasPeerEgressPolicyOverride() const noexcept {
  if (!configManager_) {
    return false;
  }
  auto peerConfig =
      configManager_->getConfig()->getConfigOfAPeer(peeringParams_.peerAddr);
  return peerConfig && peerConfig->hasEgressPolicyOverride;
}

void AdjRib::setPeerState(PeerUpdateState state) {
  if (peerState_ == state) {
    return;
  }
  peerState_ = state;
  peerUpdateStateInfo_.lastModifiedTimeMs = getCurrentTimeMs();
}

void AdjRib::setDetachReason(AdjRibOutGroup::DetachReason reason) {
  peerUpdateStateInfo_.lastDetachReason = reason;
  peerUpdateStateInfo_.lastDetachTimeMs = getCurrentTimeMs();
}

/*
 * Called when session established with a peer (in PeerManagerBase)
 * to start processing peer messages
 */
void AdjRib::startMessageProcessingLoop() noexcept {
  XLOGF(INFO, "Starting AdjRib message processing loop for {}", getPeerName());
  logPeerEvent("SESSION_MSG_PROCESSING_STARTED", BGP_LOG_SRC());

  asyncScope_->add(
      co_withExecutor(&evb_, processPeerMessageLoop(sessionTerminateBaton_)));
}

void AdjRib::cleanUpOutDelay() {
  while (!outDelayPQ_.empty()) {
    outDelayPQ_.pop();
  }
  if (outDelayTimer_) {
    outDelayTimer_.reset();
  }
  newDeferredPrefixes_.clear();
  RibStats::decrDeferredUpdatesCount(deferredUpdatesSize_);
  deferredUpdatesSize_ = 0;
  deferredUpdates_.clear();
}

// Called when a session terminated with a peer
folly::coro::Task<void> AdjRib::sessionTerminated(
    const FiberBgpPeer::BgpSessionStop& sessionStop) noexcept {
  XLOGF(
      INFO,
      "Processing session stop message for {}, enter GR helper mode = {}",
      getPeerName(),
      static_cast<bool>(sessionStop.gracefulRestart));
  cleanUpOutDelay();
  resetAdjRibFlags();

  /*
   * Defensive InProgress -> Idle transition: if a Route Refresh re-dump
   * aborted before its EoR-bearing announcement could fire (e.g. session
   * teardown mid-dump), the state would otherwise persist on a recycled
   * AdjRib instance.
   */
  rrDumpState_ = RrDumpState::Idle;

  deactivateChangeListConsumer();

  /*
   * Close outbound queues to wake up any coroutines suspended on waitToPush().
   * Calling close() signals the queue's semaphore which wakes up waiters.
   * They will see the queue is closed and exit gracefully.
   * Only needed when the MPMCWatermarkQueue is being used
   * (i.e. when egress backpressure or update group is enabled).
   *
   * After close(), we do not reset the bounded queue to nullptr.
   * The coroutines waiting on waitToPush() will wake up, check the closed flag,
   * and exit before attempting any further operations on the queue.
   *
   * A new queue will be provided when a new session is established,
   * so the old queue on this adjRib will be replaced and destroyed then
   * by shared_ptr handling.
   */
  if (adjRibOutQueue_) {
    adjRibOutQueue_->close();
  }

  if (asyncScope_) {
    asyncScope_->requestCancellation();
    XLOGF(DBG1, "Requested cancellation of async tasks for {}", getPeerName());
  }

  /*
   * Cancel any in-flight rib dump scheduled on PeerManagerBase's asyncScope_
   * (its cancellation source lives on this AdjRib), tying it to session
   * teardown.
   */
  cancelRibDump();

  /**
   * During BGP daemon shutdown, clear per-peer AdjRibIn trees using
   * RadixTree::clear() which is O(1) (just resets root pointer).
   * This prevents systemd timeout that would occur from:
   * 1. The O(n) per-entry iteration in the cleanup code below, or
   * 2. The O(n) recursive destructor in ~AdjRib()
   *
   * clear() is safe to call on empty trees (no-op if root is nullptr).
   */
  if (isDaemonShutdown_) {
    XLOGF(
        INFO,
        "Instance shutdown, clearing adjRibIn trees for {}",
        getPeerName());

    // Clear AdjRibIn trees (per-peer, safe to clear)
    RibStats::decrAdjRibInCount(adjRibInSize_);
    adjRibInSize_ = 0;
    adjRibInPathTree_.clear();
    adjRibInLiteTree_.clear();
    RibStats::decrAdjRibInStaleCount(adjRibInStaleSize_);
    adjRibInStaleSize_ = 0;
    adjRibInStale_.clear();

    /*
     * Note: adjRibOutGroup_ trees are NOT cleared here because they are
     * shared with other peers. They will be cleared from PeerManagerBase
     * destructor.
     */

    deactivateChangeListConsumer();
    ++flapCounter_;
    stats_.clear();
    co_return;
  }

  /*
   * If session flap happens again before stale path timer expiry,
   * we stop the timer before resetting
   */
  if (stalePathTimer_) {
    XLOGF(
        INFO,
        "Double Failure. "
        "Stopping stale path timer from previous termination for {}",
        getPeerName());

    co_await cleanupStaleRoutes();
    stalePathTimer_.reset();
    PeerStats::incrTotalPurgeForGrDoubleFailure();
  }

  /*
   * Cleanup adjRibOut radix tree by freeing all bgp attrs. Only needed when
   * update groups are disabled. With update groups enabled this AdjRib owns no
   * entries under its peer owner key -- in-sync peers share the group owner
   * key, and a detached peer's per-peer entries are erased by
   * AdjRibOutGroup::cleanUpPeerRibOut on unregister. The global
   * totalSentPrefixCount is likewise reconciled in
   * AdjRibOutGroup::unregisterPeer (by the group's postOutPrefixCount for
   * in-sync peers, or the peer's own for detached peers). Running this loop
   * then would risk double-counting the global total, so skip it entirely.
   */
  if (!enableUpdateGroup_) {
    std::vector<folly::CIDRNetwork> deletePrefixSet;
    if (sendAddPath_) {
      auto itr = adjRibOutGroup_->PathTree_.begin();
      while (itr != adjRibOutGroup_->PathTree_.end()) {
        auto ownerItr = itr->value().find(getPeerOwnerKey());
        if (ownerItr == itr->value().end()) {
          itr++;
          continue;
        }

        for (const auto& [_, adjRibEntry] : ownerItr->second) {
          if (adjRibEntry->getPostAttr()) {
            adjRibEntry->setPostAttr(nullptr);
            XLOGF_IF(
                DBG1,
                stats_.getPostOutPrefixCount() == 0,
                "Invalid sent prefix count for {}",
                getPeerName());
            stats_.decrementPostOutPrefixCount(itr.ipAddress().isV4());
          }
          adjRibEntry->setPreOut(nullptr);
        }
        ownerItr->second.clear();
        itr->value().erase(getPeerOwnerKey());
        if (itr->value().size() == 0) {
          if (itr == adjRibOutGroup_->PathTree_.begin()) {
            /**
             * On egress, in the most common case, all peers in the same group
             * share the same to be advertised routes. So, if this was the last
             * remotePeerId_ for this prefix, likely-hood is all the prefixes
             * have only this last peer that is going away. Thus, if we find
             * this to be the case for the first prefix in the tree, then it is
             * most effecient to erase this prefix and start from new begin()
             */
            adjRibOutGroup_->PathTree_.erase(itr);
            itr = adjRibOutGroup_->PathTree_.begin();
            continue;
          } else {
            /**
             * If the prefix is not the first in the tree for which last peer
             * reference is removed then likely hitting very un-common case
             * where only this prefix or few of such prefixes are without any
             * peers. And so very few prefixes to be removed from the tree. In
             * that case save the list of those prefixes, iterate over them,
             * after this loop is complete, to remove them from the tree
             */
            deletePrefixSet.emplace_back(itr.ipAddress(), itr.masklen());
          }
        }

        itr++;
      }
    } else {
      auto itr = adjRibOutGroup_->LiteTree_.begin();
      while (itr != adjRibOutGroup_->LiteTree_.end()) {
        auto ownerItr = itr->value().find(getPeerOwnerKey());
        if (ownerItr == itr->value().end()) {
          itr++;
          continue;
        }

        auto adjRibEntry = ownerItr->second.get();
        if (adjRibEntry->getPostAttr()) {
          adjRibEntry->setPostAttr(nullptr);
          XLOGF_IF(
              DBG1,
              stats_.getPostOutPrefixCount() == 0,
              "Invalid sent prefix count for {}",
              getPeerName());
          stats_.decrementPostOutPrefixCount(itr.ipAddress().isV4());
        }
        adjRibEntry->setPreOut(nullptr);
        itr->value().erase(getPeerOwnerKey());
        if (itr->value().size() == 0) {
          if (itr == adjRibOutGroup_->LiteTree_.begin()) {
            adjRibOutGroup_->LiteTree_.erase(itr);
            itr = adjRibOutGroup_->LiteTree_.begin();
            continue;
          } else {
            deletePrefixSet.emplace_back(itr.ipAddress(), itr.masklen());
          }
        }
        itr++;
      }
    }

    /**
     * If to be deleted prefix set is non-empty, remove them from the
     * relevant radix tree
     */
    if (!deletePrefixSet.empty()) {
      for (const auto& prefix : deletePrefixSet) {
        if (sendAddPath_) {
          adjRibOutGroup_->PathTree_.erase(prefix.first, prefix.second);
        } else {
          adjRibOutGroup_->LiteTree_.erase(prefix.first, prefix.second);
        }
      }
    }
  }

  // Move entries from the "active" adjRibIn radix tree to the "stale" one
  if (enableOptimizedGR_) {
    markLearntRoutesStaleInPlace();
  } else {
    markLearntRoutesStale();
  }

  if (!sessionStop.gracefulRestart || (remoteGrRestartTime_ == 0s)) {
    /*
     * BGP won't wait for peer coming back, hence directly purging away
     * routes. This is because either of the followings:
     *  1) BGP instance is restarting, aka, gracefulRestart flag is false
     *  2) This peer does NOT support graceful-restart.
     */
    co_await cleanupStaleRoutes(sessionStop.gracefulRestart);
    PeerStats::incrTotalPurgeForNonGr();
  } else {
    XLOGF(
        INFO,
        "Starting Remote-GR timer of {} secs for {}.",
        remoteGrRestartTime_.count(),
        getPeerName());

    CHECK(!remoteGrRestartTimer_) << "Remote-GR timer still alive.";
    remoteGrRestartTimer_ = folly::AsyncTimeout::schedule(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            remoteGrRestartTime_),
        evb_,
        [this]() noexcept {
          XLOGF(
              INFO,
              "{}Remote-GR timer expired for {}. Purging stale paths.",
              facebook::fboss::BGPAlert().str(),
              getPeerName());

          remoteGrRestartTimer_.reset();
          auto withdrawal = collectStaleRoutes(/*isGrHelperMode=*/true);
          PeerStats::incrTotalPurgeForGrRestartTimer();
          if (withdrawal) {
            schedulePendingRibInPush(std::move(*withdrawal));
          }
        });
  }

  logPeerEvent("SHUTDOWN_ADJRIB_CLEANUP_COMPLETE", BGP_LOG_SRC());
  ++flapCounter_;
  stats_.clear();

  co_return;
}

folly::coro::Task<void> AdjRib::cleanupGrState(bool isDaemonShutdown) noexcept {
  if (remoteGrRestartTimer_) {
    XLOGF(DBG1, "Stopping remoteGrRestart Timer for {}", getPeerName());
    if (!isDaemonShutdown) {
      co_await cleanupStaleRoutes();
    }
    remoteGrRestartTimer_.reset();
  }
  if (stalePathTimer_) {
    if (!isDaemonShutdown) {
      co_await cleanupStaleRoutes();
    }
    XLOGF(
        DBG1,
        "Stopping stalePathTimer_ from previous termination for {}",
        getPeerName());
    stalePathTimer_.reset();
  }

  /*
   * Drain any in-flight detached pushes from the timer callbacks before
   * letting the AdjRib be destroyed.
   */
  auto pending = std::exchange(pendingRibInPushes_, {});
  if (!pending.empty()) {
    co_await folly::coro::collectAllRange(std::move(pending));
  }
  co_return;
}

folly::coro::Task<void> AdjRib::stop() noexcept {
  co_await cleanupGrState(/*isDaemonShutdown=*/true);

  /*
   * Cancel and join all AdjRib coroutines (processRibMessageLoop,
   * processPeerMessageLoop, postTerminateBaton, sendBgpUpdates) while
   * evb_ is still alive. This is called from PeerManagerBase::stop()'s task
   * running on the evb_. If deferred to the destructor, the evb_ is already
   * dead (terminateLoopSoon), and blockingWait hangs forever because the
   * coroutines can't run on the dead event loop.
   */
  if (asyncScope_) {
    co_await asyncScope_->cancelAndJoinAsync();
    asyncScope_.reset();
  }

  XLOGF(INFO, "[Exit] AdjRib::stop() completed for {}", getPeerName());
  co_return;
}

void AdjRib::setPendingIngressPolicyUpdate(bool ingressChanged) {
  /*
   * Check if adjRib has learned any routes yet in the AdjRibIn radix tree or
   * AdjRibInStale radix tree
   */
  bool hasLearnedRoutes = adjRibInPathTree_.size() != 0 ||
      adjRibInLiteTree_.size() != 0 || adjRibInStale_.size() != 0;

  XLOGF(
      DBG2,
      "setPendingIngressPolicyUpdate for peer {}: ingressChanged={}, hasLearnedRoutes={}",
      peeringParams_.description,
      ingressChanged,
      hasLearnedRoutes);

  /*
   * Only set ingress policy update flag if there are learned routes to
   * re-evaluate.
   * There are no learned routes in the following 2 cases:
   * 1. Newly created AdjRib (no routes to re-evaluate)
   * 2. Session down without graceful restart peers (no routes to re-evaluate)
   */
  pendingIngressPolicyUpdate_ = ingressChanged && hasLearnedRoutes;
}

void AdjRib::setPendingEgressPolicyUpdate(bool egressChanged) {
  XLOGF(
      DBG2,
      "setPendingEgressPolicyUpdate for peer {}: egressChanged={}",
      peeringParams_.description,
      egressChanged);

  /*
   * Only set egress flag for peers not in initial announcement
   * (peers in initial announcement get full dump anyway)
   */
  egressPolicyUpdateRequired_ = egressChanged && !inInitialAnnouncement();
}

std::tuple<bool, bool> AdjRib::setRouteFilterStatement(
    std::shared_ptr<const RouteFilterStatement> stmt,
    std::unique_ptr<RouteFilterLogger> logger) {
  routeFilterLogger_ = std::move(logger);

  if (!routeFilterStmt_) {
    if (stmt) {
      XLOGF(
          DBG1,
          "New RouteFilterStatement for peer {}",
          peeringParams_.description);
      routeFilterStmt_ = std::move(stmt);
      bool ingressChanged = routeFilterStmt_->hasIngressFilter();
      bool egressChanged = routeFilterStmt_->hasEgressFilter();
      return std::make_tuple(ingressChanged, egressChanged);
    }
    return std::make_tuple(false, false);
  }

  if (!stmt) {
    XLOGF(
        DBG1,
        "Purge RouteFilterStatement for peer {}",
        peeringParams_.description);
    bool ingressChanged = routeFilterStmt_->hasIngressFilter();
    bool egressChanged = routeFilterStmt_->hasEgressFilter();
    routeFilterStmt_.reset();
    return std::make_tuple(ingressChanged, egressChanged);
  }

  // pointer comparison before value comparison
  bool changed = (routeFilterStmt_ != stmt && *routeFilterStmt_ != *stmt);
  if (changed) {
    XLOGF(
        DBG1,
        "Update RouteFilterStatement for peer {}",
        peeringParams_.description);

    bool ingressChanged =
        routeFilterStmt_->getIngressFilter() != stmt->getIngressFilter();
    bool egressChanged =
        routeFilterStmt_->getEgressFilter() != stmt->getEgressFilter();

    routeFilterStmt_ = std::move(stmt);
    return std::make_tuple(ingressChanged, egressChanged);
  }
  return std::make_tuple(false, false);
}

bool AdjRib::setGoldenPrefixPolicy(std::shared_ptr<GoldenPrefixPolicy> policy) {
  if (!goldenPrefixPolicy_) {
    if (policy) {
      XLOGF(
          INFO,
          "New GoldenPrefixPolicy for peer {}",
          peeringParams_.description);
      goldenPrefixPolicy_ = std::move(policy);
      return true;
    }
    return false;
  }
  if (!policy) {
    XLOGF(
        WARNING,
        "Purge GoldenPrefixPolicy for peer {}",
        peeringParams_.description);
    goldenPrefixPolicy_.reset();
    return true;
  }
  // pointer comparison before value comparison
  bool changed =
      (goldenPrefixPolicy_ != policy && *goldenPrefixPolicy_ != *policy);
  if (changed) {
    XLOGF(
        WARNING,
        "Update GoldenPrefixPolicy for peer {}",
        peeringParams_.description);
    goldenPrefixPolicy_ = std::move(policy);
  }
  return changed;
}

std::tuple<std::shared_ptr<const BgpPath>, std::string, PostPolicyInfo>
AdjRib::getPostPolicyAttributesPolicyTermAndInfo(
    const std::string& policyName,
    const folly::CIDRNetwork& prefix,
    const std::shared_ptr<const BgpPath>& prePolicyAttrs,
    const std::shared_ptr<BgpPolicyActionData>& policyActionData,
    bool isPartialDrain) {
  std::optional<AdjRibPolicyCache::PolicyCacheValue> cacheResult{std::nullopt};
  std::shared_ptr<const BgpPath> postPolicyAttrs{nullptr};
  std::string policyTermName{""};
  PostPolicyInfo postPolicyInfo;

  if (policyCache_) {
    cacheResult = policyCache_->lookupPolicyCache(
        policyName,
        policyManager_->getPolicyAttributesMask(policyName),
        prefix,
        prePolicyAttrs,
        policyActionData,
        isPartialDrain);
  }

  if (cacheResult == std::nullopt) {
    BgpStats::setPolicyCacheMiss(
        policyCache_ ? policyCache_->getTotalCacheMiss() : 0);

    XLOGF(
        DBG5,
        "Policy Cache Miss for {} {} for peer {}",
        folly::IPAddress::networkToString(prefix),
        policyName,
        getPeerName());

    /*
     * NOTE:
     * After applyPolicy(), the bgpAttributes(communities, as-path, localPref)
     * may have already been modified. For example `prePolicyAttrs` may
     * contain a completely different set of communities pre/post policy.
     *
     * Hence we can't add `modified` attributes to the cache. Otherwise, it
     * tries to match `pre` attributes against `post` attributes. Instead,
     * use the cloned attributes to update cache.
     */

    // no cached copy. Store original attrs in cache
    auto prePolicyAttrsClone = prePolicyAttrs->clone();

    // use prePolicyAttrsClone for policy evaluation
    auto policyOut = policyManager_->applyPolicy(
        policyName,
        PolicyInMessage({prefix}, prePolicyAttrsClone, policyActionData));

    const auto attrsAndPolicy =
        getPostPolicyOutAttrsAndPolicyFromMessage(prefix, policyOut);

    postPolicyAttrs = attrsAndPolicy->attrs;
    policyTermName = attrsAndPolicy->policyName;
    postPolicyInfo.isMedSetByPolicy = policyActionData->isMedSetByPolicy;
    postPolicyInfo.isNexthopSetByPolicy =
        policyActionData->isNexthopSetByPolicy;

    if (policyCache_) {
      policyCache_->addToPolicyCache(
          policyName,
          policyManager_->getPolicyAttributesMask(policyName),
          prefix,
          prePolicyAttrs,
          policyActionData,
          attrsAndPolicy,
          isPartialDrain);

      // Expose to ODS counters
      BgpStats::setPolicyCacheNumEntries(policyCache_->size());
      BgpStats::setPolicyCacheMemoryUsage(policyCache_->getCacheMemUsage());
    }
  } else {
    BgpStats::setPolicyCacheHit(
        policyCache_ ? policyCache_->getTotalCacheHit() : 0);

    XLOGF(
        DBG5,
        "Policy Cache Hit for {} {} for peer {}",
        folly::IPAddress::networkToString(prefix),
        policyName,
        getPeerName());

    if ((*cacheResult).attrsAndPolicy) {
      postPolicyAttrs = (*cacheResult).attrsAndPolicy->attrs;
      policyTermName = (*cacheResult).attrsAndPolicy->policyName;
    }
    postPolicyInfo.isMedSetByPolicy = (*cacheResult).isMedSetByPolicy;
    postPolicyInfo.isNexthopSetByPolicy = (*cacheResult).isNexthopSetByPolicy;
  }
  return {
      std::move(postPolicyAttrs),
      std::move(policyTermName),
      std::move(postPolicyInfo)};
}

void AdjRib::tryIterateChangesToEnd(bool processFullMultipaths) noexcept {
  auto* consumer = changeListConsumer_.get();
  if (processFullMultipaths) {
    changeListConsumer_->setProcessChangeItemCallback(
        [consumer](ChangeItem<ShadowRibEntry>* item) {
          return consumer->processShadowRibEntryFullMultipaths(item);
        });
  }
  changeListConsumer_->iterateChangesToEnd();
  /*
   * The drain reached the end of the change list, so this peer has seen every
   * item it will be offered and can claim the RIB's max version. Items its
   * consumer bit was never set on (e.g. add-path-only changes for a
   * non-add-path peer) are skipped by markProcessed and would otherwise leave
   * the peer trailing the RIB forever.
   *
   * Reaching the tail is guaranteed rather than checked: no boundary marker is
   * passed, and both AdjRibOutConsumer::processShadowRibEntryDelta() and
   * AdjRibOutConsumer::processShadowRibEntryFullMultipaths() always return
   * ProcessResult::CONTINUE, never YIELD -- the only other way the walk can
   * stop short. Nothing tracks the version per item, so introducing a YIELD on
   * this path would silently stall the peer's version at its pre-drain value.
   */
  setLastSeenRibVersion(adjRibOutGroup_->getShadowRibMaxVersion());
  if (processFullMultipaths) {
    changeListConsumer_->setProcessChangeItemCallback(
        [consumer](ChangeItem<ShadowRibEntry>* item) {
          return consumer->processShadowRibEntryDelta(item);
        });
  }
}

/*
 * @brief  Set up this peer's OWN change list consumer to track the FULL change
 *         list. Used by an in-sync peer (and by all peers when update groups
 *         are disabled).
 *
 *         The peer's changeListConsumer_ must already exist; this registers it
 *         with the tracker and schedules a polled timer that, each cycle,
 *         consumes ALL available changes via iterateChangesToEnd() — i.e. the
 * peer independently keeps up with the entire change list and builds/sends its
 * own BGP updates.
 *
 *         To avoid registering twice if accidentally called twice, check for
 *         existence of changeListConsumeTimer_; its presence means the consumer
 *         is already registered.
 *
 *         Contrast with registerDetachedConsumer(), which CREATES a separate
 *         detached consumer that is BOUNDED to the group's marker.
 *
 * @return void
 */
void AdjRib::activateChangeListConsumer() noexcept {
  scheduleSendBgpUpdates(false /* tryPullNewChangeItems */);
  if (!changeListConsumer_) {
    XLOGF(
        ERR,
        "changeListConsumer is not initialized and hence can not be registered for peer {}",
        getPeerName());
    return;
  }

  if (changeListConsumeTimer_) {
    XLOGF(
        DBG1,
        "peer {} has already been registered to changeListTracker",
        getPeerName());
    return;
  }

  XLOGF(DBG1, "Register peer {} to changeListTracker", getPeerName());
  changeListConsumeTimer_ = folly::AsyncTimeout::make(evb_, [this]() noexcept {
    /*
     * Start a polled cycle to consume available changes.
     * We use co_withExecutor().start() (fire-and-forget coroutine) rather
     * than running the work inline in the timer callback. This enqueues
     * the coroutine as a new EventBase callback, allowing already-pending
     * callbacks (peer session events, socket reads, other timers) to drain
     * first — important under scale when many peers flap simultaneously.
     */
    co_withExecutor(
        &evb_, folly::coro::co_invoke([this]() -> folly::coro::Task<void> {
          /*
           * Guard against use-after-reset: the MRAI timer fires and
           * enqueues this coroutine, but between scheduling and execution,
           * other EventBase callbacks may run — including peer rejoin
           * (deactivateDetachedModeProcessing) or session termination
           * paths that call resetChangeListConsumer() and nullify
           * changeListConsumer_. Under scale with mass peer flaps, this
           * window is wide enough to hit consistently.
           */
          if (!changeListConsumer_) {
            XLOGF(
                WARN,
                "Peer {}: CL consume timer fired but changeListConsumer_ "
                "is null, skipping",
                getPeerName());
            co_return;
          }
          // Use iterator-based interface for consuming change items
          auto previousRibVersion = lastSeenRibVersion_;
          tryIterateChangesToEnd();
          if (changeListConsumer_->isStale(kConsumerStalenessThreshold) &&
              !changeListConsumer_->isStalenessLogged()) {
            XLOGF(
                WARN,
                "Peer {}: change list consumer stale for {}ms, marker has not advanced",
                getPeerName(),
                changeListConsumer_->stalenessDuration().count());
            changeListConsumer_->markStalenessLogged();
          }
          XLOGF(
              DBG2,
              "Peer {}: Updating cached RIB version from {} to {}",
              getPeerName(),
              previousRibVersion,
              lastSeenRibVersion_);

          if (changeListConsumeTimer_) {
            changeListConsumeTimer_->scheduleTimeout(mraiInterval);
          }
          if (newDeferredPrefixes_.size()) {
            XLOGF(
                DBG3,
                "Deferred prefixes {} for peer {}",
                newDeferredPrefixes_.size(),
                getPeerName());
            scheduleOutDelayTimer();
          }
          scheduleSendBgpUpdates(true /* tryPullNewChangeItems */);
          co_return;
        }))
        .start();
  });
  changeListConsumer_->registerWithTracker();
  changeListConsumer_->setPolledMode();
  changeListConsumer_->setBitmap();
  if (!egressEoRsSent_) {
    /*
     * We cannot consume from change list until EoR has been sent to peer.
     * sendBgpUpdates will reschedule the timer.
     */
    return;
  }
  changeListConsumeTimer_->scheduleTimeout(mraiInterval);
}

/*
 * @brief  Deactivation may be called multiple times for the
 *         reasons like: Ideally deactivating when session
 *         (adjRib) terminated is good. However, some-times
 *         UTs bypass certain methods, hence safety calls may
 *         take place at other places like when adjRib is
 *         stopped or destroyed
 *
 *         We do not want to deregister consumer twice and so
 *         to safely ignore duplicate call, first validate
 *         existence of timer. If changeListConsumeTimer_ does
 *         not exist then that means timer has been reset as a
 *         result of already deactivated consumer
 *
 * @param  none
 *
 * @return void
 */
void AdjRib::deactivateChangeListConsumer() noexcept {
  clearPackingList();
  /*
   * If it is already deactivated, then no-op
   */
  if (!changeListConsumeTimer_) {
    XLOGF(
        DBG1,
        "peer {} has already been deregistered from changeListTracker",
        getPeerName());
    return;
  }

  changeListConsumeTimer_->cancelTimeout();
  changeListConsumeTimer_.reset();
  if (changeListConsumer_) {
    changeListConsumer_->resetBitmap();
    changeListConsumer_->terminate();
    changeListConsumer_->deregisterFromTracker();
    XLOGF(DBG1, "Deregistered peer {} from changeListTracker", getPeerName());
  }
}

/*
 * @brief  Create and register a separate DETACHED change list consumer for
 *         this peer. Used by a detached peer running independently behind/at
 *         its update group.
 *
 *         Unlike activateChangeListConsumer() (which sets up the peer's
 *         existing consumer to consume the ENTIRE change list), this CREATES a
 *         new "DetachedCL-<peer>" consumer whose polled timer consumes only UP
 *         TO the group consumer's marker via
 *         iterateChangesUntilExcluding(groupMarker): a detached peer must never
 *         advance past the group on the change list, to preserve lazy-clone
 *         correctness. The timer is created unscheduled; sendBgpUpdates()
 *         schedules it after the packing list drains.
 *
 * @return void
 */
bool AdjRib::registerDetachedConsumer(
    std::shared_ptr<ChangeTracker<ShadowRibEntry>>& changeListTracker,
    ConsumerBitmap& addPathBitmap,
    ConsumerBitmap& nonAddPathBitmap) {
  if (changeListConsumer_) {
    XLOGF(
        WARN,
        "Peer {} already has a detached consumer, skipping registration",
        getPeerName());
    return false;
  }

  if (!changeListTracker) {
    XLOGF(
        ERR,
        "Cannot register detached consumer for peer {}: null changeListTracker",
        getPeerName());
    return false;
  }

  changeListConsumer_ = std::make_shared<AdjRibOutConsumer>(
      changeListTracker,
      shared_from_this(),
      fmt::format("DetachedCL-{}", getPeerName()),
      evb_,
      addPathBitmap,
      nonAddPathBitmap);

  changeListConsumer_->registerWithTracker();
  changeListConsumer_->setPolledMode();
  changeListConsumer_->setBitmap();

  /*
   * Create CL consumption timer (unscheduled). sendBgpUpdates will
   * schedule it after PL drain if the peer is a DSP.
   *
   * The callback runs the change list consumption inline in the timer
   * callback. TODO(T274212842): consider running change list consumption as a
   * cancellable coroutine on packing timer expiry instead.
   */
  changeListConsumeTimer_ = folly::AsyncTimeout::make(evb_, [this]() noexcept {
    /*
     * Guard against use-after-reset: DETACHED_INIT_DUMP peers call
     * activateChangeListConsumer() which schedules this timer,
     * then rapidly transition to DETACHED_READY_TO_JOIN and rejoin
     * the group — the rejoin path resets the consumer while this
     * timer callback is still pending on the EventBase.
     */
    if (!changeListConsumer_) {
      XLOGF(
          WARN,
          "Peer {}: detached CL consume timer fired but "
          "changeListConsumer_ is null, skipping",
          getPeerName());
      return;
    }
    /*
     * Consume changes up to the group consumer's marker.
     * Detached peers must not advance past the group on the
     * change list to preserve lazy-clone correctness.
     */
    auto previousRibVersion = lastSeenRibVersion_;
    auto groupConsumer = adjRibOutGroup_->getChangeListConsumer();
    auto* groupMarker = groupConsumer->getMarker();
    changeListConsumer_->iterateChangesUntilExcluding(groupMarker);
    /*
     * isReady() means the marker reached the end of the change list -- only
     * possible when the group itself had consumed everything (groupMarker was
     * null), so this peer has seen every item it will be offered and can claim
     * the RIB's max version. Stopping on a real boundary item is by design and
     * must keep the per-item version, or the peer would advance past the group.
     */
    if (changeListConsumer_->isReady()) {
      setLastSeenRibVersion(adjRibOutGroup_->getShadowRibMaxVersion());
    }
    if (changeListConsumer_->isStale(kConsumerStalenessThreshold) &&
        !changeListConsumer_->isStalenessLogged()) {
      XLOGF(
          WARN,
          "Peer {}: detached change list consumer stale for {}ms, "
          "marker has not advanced",
          getPeerName(),
          changeListConsumer_->stalenessDuration().count());
      changeListConsumer_->markStalenessLogged();
    }
    XLOGF(
        DBG2,
        "Peer {}: Updating cached RIB version from {} to {}",
        getPeerName(),
        previousRibVersion,
        lastSeenRibVersion_);

    /*
     * Drain any new PL entries generated by CL consumption.
     * Because the peer is not allowed to move if it is ahead
     * of the group on the changelist, then exiting on
     * backpressure only to not move forward by that much,
     * is not efficient. We would like detached consumers
     * to send out their packing list.
     */
    scheduleSendBgpUpdates(false /* tryPullNewChangeItems */);

    // Reschedule for next cycle
    if (changeListConsumeTimer_) {
      changeListConsumeTimer_->scheduleTimeout(
          std::chrono::milliseconds(mraiInterval));
    }
  });

  XLOGF(INFO, "Registered detached consumer for peer {}", getPeerName());
  return true;
}

void AdjRib::registerDetachedConsumerAtGroupPosition(
    std::shared_ptr<ChangeTracker<ShadowRibEntry>>& changeListTracker,
    const std::shared_ptr<AdjRibOutGroupConsumer>& groupConsumer,
    ConsumerBitmap& addPathBitmap,
    ConsumerBitmap& nonAddPathBitmap) {
  if (!groupConsumer) {
    XLOGF(
        ERR,
        "Cannot join detached consumer for peer {} at group position: "
        "null groupConsumer",
        getPeerName());
    return;
  }
  if (!registerDetachedConsumer(
          changeListTracker, addPathBitmap, nonAddPathBitmap)) {
    XLOGF(
        ERR,
        "Cannot join detached consumer for peer {} at group position: "
        "failed to register detached consumer",
        getPeerName());
    return;
  }
  changeListTracker->joinConsumer(groupConsumer, changeListConsumer_);
  XLOGF(
      INFO,
      "Joined detached consumer for peer {} at group's CL position",
      getPeerName());
}

AdjRibOutConsumer::~AdjRibOutConsumer() {
  /*
   * this is resetting shared_ptr of adjRib held by AdjRibOutConsumer
   */
  adjRib_.reset();
}

bool AdjRib::updatePolicyName(
    std::optional<std::string>& currentName,
    const std::optional<std::string>& newName) {
  // Case 1: Clear/unset the policy (newName is nullopt)
  if (!newName.has_value()) {
    if (currentName.has_value()) {
      XLOGF(
          DBG1,
          "Clearing policy '{}' for peer {}",
          *currentName,
          getPeerName());
      currentName.reset();
      return true;
    }
    // Both are nullopt, no change
    return false;
  }

  // Case 2: Set to new policy name
  if (!currentName.has_value() || currentName.value() != newName.value()) {
    XLOGF(
        DBG1,
        "Updating policy name from '{}' to '{}' for peer {}",
        currentName.has_value() ? currentName.value() : "(none)",
        newName.value(),
        getPeerName());
    currentName = newName.value();
    return true;
  }

  XLOGF(
      DBG3,
      "Policy name '{}' unchanged for peer {}",
      newName.value(),
      getPeerName());
  return false;
}

std::tuple<bool, bool> AdjRib::updateIngressEgressPolicyNames(
    const folly::F14FastMap<bgp_policy::DIRECTION, std::optional<std::string>>&
        directionToPolicyName) noexcept {
  bool ingressChanged = false;
  bool egressChanged = false;

  for (const auto& [direction, policyNameOpt] : directionToPolicyName) {
    switch (direction) {
      case facebook::bgp::bgp_policy::DIRECTION::IN:
        ingressChanged = updatePolicyName(ingressPolicyName_, policyNameOpt);
        break;
      case facebook::bgp::bgp_policy::DIRECTION::OUT:
        egressChanged = updatePolicyName(egressPolicyName_, policyNameOpt);
        break;
      default:
        break;
    }
  }

  XLOGF(
      DBG1,
      "Policy update result for peer {}: ingressChanged={}, egressChanged={}",
      getPeerName(),
      ingressChanged,
      egressChanged);

  return {ingressChanged, egressChanged};
}

void AdjRib::resetSlowPeerDurationTimer() {
  if (slowPeerDurationTimer_) {
    slowPeerDurationTimer_->cancelTimeout();
    slowPeerDurationTimer_.reset();
  }
}

void AdjRib::cancelSlowPeerDurationTimer() {
  if (slowPeerDurationTimer_) {
    slowPeerDurationTimer_->cancelTimeout();
  }
}

void AdjRib::scheduleSlowPeerDurationTimer(
    folly::EventBase& evb,
    std::chrono::milliseconds timeout,
    const std::shared_ptr<AdjRib>& self) {
  if (!slowPeerDurationTimer_) {
    slowPeerDurationTimer_ = folly::AsyncTimeout::make(evb, [self]() noexcept {
      uint64_t bit = self->getGroupBitPosition();
      auto& group = self->adjRibOutGroup_;
      /*
       * A peer unregistered from its group has nothing to detach from
       * (AdjRibOutGroup::unregisterPeer clears the group). removePeer()
       * destroys this timer before that happens, so this is belt-and-braces
       * against a future reordering inside unregisterPeer.
       */
      if (!group) {
        XLOGF(
            WARN,
            "Peer {}: slow peer duration timer fired with no update group, skipping",
            self->getPeerName());
        return;
      }
      XLOGF(
          INFO,
          "Group {}: Peer at bit {} exceeded block duration threshold",
          group->getGroupDescriptor(),
          bit);
      group->detachSlowPeer(self);
    });
  }
  slowPeerDurationTimer_->scheduleTimeout(timeout);
}

} // namespace facebook::bgp
