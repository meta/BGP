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

#include <map>

#include <folly/container/F14Set.h>
#include <folly/logging/xlog.h>

#include "common/network/AddressUtil.h"
#include "magic_enum/magic_enum.hpp"
#include "neteng/fboss/bgp/cpp/BgpServiceUtil.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibGroup.h"
#include "neteng/fboss/bgp/cpp/config/ConfigManager.h"
#include "neteng/fboss/bgp/cpp/lib/BgpUtil.h"
#include "neteng/fboss/bgp/cpp/peer/PeerManagerBase.h"
#include "neteng/fboss/bgp/cpp/rib/canonical/CanonicalRibBuilder.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

using namespace facebook::neteng::fboss::bgp_attr;
using namespace facebook::neteng::fboss::bgp::thrift;
using namespace facebook::nettools::bgplib;
using namespace facebook::bgp::BgpStats;

namespace facebook::bgp {

namespace {
/*
 * Build CanonicalPathInput vector from a ShadowRibEntry.
 * Extracts bestpath (if present, flagged in kBestPathGroup) and all multipaths
 * (in kMultiPathGroup), mirroring the legacy getter's path attribution.
 */
std::vector<CanonicalPathInput> buildCanonicalPathInputsFromShadowEntry(
    const ShadowRibEntry& entry) {
  std::vector<CanonicalPathInput> inputs;
  /*
   * Shadow entries expose only bestpath and multipaths, not all paths.
   * Build CanonicalPathInput for each available path. Peer attribution
   * is present in ShadowRibRouteInfo.
   */
  if (entry.bestpath) {
    CanonicalPathInput in;
    in.path = entry.bestpath->attrs;
    in.peerAddr = entry.bestpath->peer.addr;
    in.peerRouterId = entry.bestpath->peer.routerId;
    in.peerDescription = entry.bestpath->peer.description;
    /*
     * Advertised (shadow/changelist) paths carry a pathIdToSend (the TX
     * path id), not a received path id -- mirror the legacy getter and set
     * only path_id_to_send. igp_cost / last_modified_time /
     * bestpath_filter_descr are not available on ShadowRibRouteInfo. The
     * legacy getter also stamps in_update / in_withdraw from the shadow
     * flags, but the canonical schema has no field for them and no consumer
     * renders them, so they are intentionally dropped.
     */
    in.pathIdToSend = entry.bestpath->pathIdToSend;
    in.isBestPath = true;
    in.group = facebook::bgp::kBestPathGroup;
    inputs.push_back(std::move(in));
  }
  /* Mirror PeerManagerBase::createTRibEntryWithFilter: every multipath goes in
   * kMultiPathGroup, including the bestpath when it is part of the
   * multipath set (it also appears, flagged, in kBestPathGroup above).
   */
  for (const auto& [pathId, routeinfo] : entry.multipaths) {
    CanonicalPathInput in;
    in.path = routeinfo->attrs;
    in.peerAddr = routeinfo->peer.addr;
    in.peerRouterId = routeinfo->peer.routerId;
    in.peerDescription = routeinfo->peer.description;
    in.pathIdToSend = routeinfo->pathIdToSend;
    in.group = facebook::bgp::kMultiPathGroup;
    inputs.push_back(std::move(in));
  }
  return inputs;
}

} // namespace

/* Get egress statistics on all peers. */
std::vector<TPeerEgressStats> PeerManagerBase::getPeerEgressStats(
    std::unordered_multimap<
        folly::IPAddress,
        std::shared_ptr<BgpPeerDisplayInfo>> allPeers) {
  std::vector<TPeerEgressStats> allStats;

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    for (const auto& [addr, peerInfo] : allPeers) {
      TPeerEgressStats peerStats;

      peerStats.session() = getSessionInfo(addr, peerInfo);
      peerStats.send_queue_blocks() = peerInfo->sendQueueBlocks;
      peerStats.send_queue_total_block_duration() =
          peerInfo->sendQueueTotalBlockDurationMs;
      peerStats.total_async_socket_buffered() =
          peerInfo->totalSocketEgressBufferedEvents;
      peerStats.last_send_queue_block_time() =
          peerInfo->lastSendQueueBlockTimeMs;
      peerStats.last_socket_buffered_time() =
          peerInfo->lastSocketEgressBufferedTimeMs;

      BgpPeerId bgpPeerId{addr, peerInfo->remoteBgpId};
      if (!peerInfo->peeringParams.peerPrefix) {
        if (adjRibs_.contains(bgpPeerId)) {
          const auto& adjRib = adjRibs_.at(bgpPeerId);
          /*
           * Group name is used to compute percentiles across any grouping of
           * interest; does not have to be limited to adjRibOutGroup.
           * For example, we could consider update group name.
           */
          peerStats.group_name() = adjRib->getAdjRibOutGroupName();

          const auto& adjRibStats = adjRib->getStats();
          peerStats.transient_route_updates_suppressed() =
              adjRibStats.getTransientRouteUpdatesSuppressed();
          peerStats.adjribout_queue_blocks() =
              adjRibStats.getEgressQueueBackpressuredEvents();
          peerStats.adjribout_queue_total_block_duration() =
              adjRibStats.getEgressQueueTotalBlockDuration();
          peerStats.last_adjribout_queue_block_time() =
              adjRibStats.getLastEgressQueueBlockTime();
        }
      }
      allStats.emplace_back(peerStats);
    }
  });

  return allStats;
}

/* Get lightweight update group summaries without walking peers or RIB trees. */
std::vector<TUpdateGroupSummary> PeerManagerBase::getUpdateGroupSummaries() {
  std::vector<TUpdateGroupSummary> result;

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    if (!updateGroupManager_) {
      return;
    }

    const auto& allGroups = updateGroupManager_->getAllGroups();
    result.reserve(allGroups.size());

    for (const auto& [key, group] : allGroups) {
      TUpdateGroupSummary summary;
      summary.group_id() = group->getGroupId();
      summary.egress_policy_name() = key.egressPolicyName.value_or("");
      summary.group_state() = fmt::format("{}", group->getState());
      summary.member_count() = group->getMemberCount();
      summary.in_sync_peer_count() = group->getNumInSyncPeers();
      summary.detached_peer_count() = group->getDetachedPeers().size();
      summary.last_seen_rib_version() = group->getLastSeenRibVersion();

      const auto& stats = group->getStats();
      summary.post_out_prefix_count() = stats.getPostOutPrefixCount();
      summary.post_out_prefix_count_ipv4() = stats.getPostOutPrefixCountIpv4();
      summary.post_out_prefix_count_ipv6() = stats.getPostOutPrefixCountIpv6();

      result.emplace_back(std::move(summary));
    }
  });

  return result;
}

/* Get detailed update group information for CLI/thrift. */
std::vector<TUpdateGroupInfo> PeerManagerBase::getUpdateGroupInfo(
    int64_t groupId) {
  std::vector<TUpdateGroupInfo> result;

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    if (!updateGroupManager_) {
      return;
    }

    const auto& allGroups = updateGroupManager_->getAllGroups();
    result.reserve(1);

    for (const auto& [key, group] : allGroups) {
      if (static_cast<int64_t>(group->getGroupId()) != groupId) {
        continue;
      }

      TUpdateGroupInfo info;
      const auto& groupKey = group->getGroupKey();

      info.group_id() = group->getGroupId();
      info.group_key() = groupKey.toThrift();

      info.group_state() = fmt::format("{}", group->getState());
      info.member_count() = group->getMemberCount();
      info.detached_peer_count() = group->getDetachedPeers().size();
      info.last_seen_rib_version() = group->getLastSeenRibVersion();

      TUpdateGroupStats thriftStats;

      /*
       * TODO: Add per-group counters to AdjRibOutGroup and
       * wire them up. These events currently have no per-group tracking.
       */
      thriftStats.slow_peer_detachments() = 0;
      thriftStats.dfp_rejoin_events() = 0;
      thriftStats.collapse_entries_corrected() = 0;
      thriftStats.dsp_rejoin_events() = 0;
      thriftStats.lazy_clone_events() = 0;

      /*
       * The per-AFI group_update_messages_ipv4/ipv6 stay 0: AdjRibStats tracks
       * sent UPDATE messages as a single counter, not per-AFI.
       */
      thriftStats.group_update_messages_ipv4() = 0;
      thriftStats.group_update_messages_ipv6() = 0;

      int64_t inSyncCount = 0;
      int64_t blockedCount = 0;
      std::map<std::string, int64_t> peerStateCounts;
      int64_t totalQueueWaitMs = 0;
      int64_t totalQueueBlocks = 0;
      int64_t lastQueueBlockTime = 0;

      const auto& bitToAdjRibs = group->getBitToAdjRibs();
      std::vector<TUpdateGroupPeerInfo> peers;
      peers.reserve(bitToAdjRibs.size());

      for (const auto& [bitPos, adjRib] : bitToAdjRibs) {
        if (!adjRib) {
          continue;
        }

        auto state = adjRib->getPeerState();
        bool isInSync = group->isPeerInSync(bitPos);
        bool isBlocked =
            (state == PeerUpdateState::JOINED_BLOCKED ||
             state == PeerUpdateState::DETACHED_BLOCKED);

        if (isInSync) {
          ++inSyncCount;
        }
        if (isBlocked) {
          ++blockedCount;
        }
        ++peerStateCounts[std::string(magic_enum::enum_name(state))];

        const auto& peerStats = adjRib->getStats();
        totalQueueWaitMs += peerStats.getEgressQueueTotalBlockDuration();
        totalQueueBlocks += peerStats.getEgressQueueBackpressuredEvents();

        auto peerLastBlock =
            static_cast<int64_t>(peerStats.getLastEgressQueueBlockTime());
        if (peerLastBlock > lastQueueBlockTime) {
          lastQueueBlockTime = peerLastBlock;
        }

        TUpdateGroupPeerInfo peerInfo;
        peerInfo.peer_addr() = adjRib->getPeerAddress().str();
        peerInfo.peer_state() = fmt::format("{}", state);
        peerInfo.bit_position() = bitPos;
        peerInfo.is_in_sync() = isInSync;
        peerInfo.is_blocked() = isBlocked;
        const bool isDetached = adjRib->isDetachedPeer();
        peerInfo.is_detached() = isDetached;

        if (isDetached) {
          peerInfo.detach_type() =
              adjRib->isAdjRibFlagSet(AdjRib::IS_DETACHED_FAST_PEER) ? "DFP"
                                                                     : "DSP";
          peerInfo.detached_rib_version() = adjRib->getDetachedRibVersion();
        }

        const auto& peerUpdateStateInfo = adjRib->getPeerUpdateStateInfo();
        if (auto reason = peerUpdateStateInfo.lastDetachReason) {
          peerInfo.last_detach_reason() =
              std::string(magic_enum::enum_name(*reason));
          peerInfo.last_detach_time_ms() = peerUpdateStateInfo.lastDetachTimeMs;
        }

        /*
         * TODO: session_state and description should come from
         * BgpPeer/SessionManager, not AdjRib. AdjRib only has the update-group
         * state machine state and formatted peer name. remote_as needs config
         * data not available from AdjRib.
         */
        peerInfo.session_state() = TBgpPeerState::IDLE;
        peerInfo.description() = adjRib->getPeerName();
        peerInfo.remote_as() = 0;

        peerInfo.last_seen_rib_version() = adjRib->getLastSeenRibVersion();
        auto queue = adjRib->getAdjRibOutQueue();
        peerInfo.queue_size() = queue ? static_cast<int64_t>(queue->size()) : 0;
        /*
         * In-sync peers use group-owned RIB-OUT entries and therefore have no
         * per-peer entries to count. Only detached peers can own entries under
         * their peer key; avoid a full group-tree scan for every in-sync peer.
         */
        peerInfo.entry_count() = isDetached
            ? static_cast<int64_t>(adjRib->getRibTreePeerEntriesCount(
                  /*ingress=*/false, groupKey.sendAddPath))
            : 0;

        auto eorTime = adjRib->eorSentTime();
        if (eorTime > 0) {
          peerInfo.eor_sent_time_ms() = eorTime;
        }

        auto peerStateTime = peerUpdateStateInfo.lastModifiedTimeMs;
        if (peerStateTime > 0) {
          peerInfo.last_modified_peer_update_state_time_ms() = peerStateTime;
        }

        peers.emplace_back(std::move(peerInfo));
      }

      info.in_sync_peer_count() = inSyncCount;
      info.blocked_peer_count() = blockedCount;
      info.peer_state_counts() = std::move(peerStateCounts);

      /* Announcement/withdrawal PDU counts come from the group's own
       * AdjRibStats: the group builds each UPDATE once for its in-sync members
       * (whose per-peer counters stay 0), so read the group directly -- uniform
       * with post_out_prefix_count below. A detached member's independent
       * per-peer sends are surfaced in its own `show bgp neighbor` detail and
       * the peer table, not rolled up into the group total. */
      const auto& groupStats = group->getStats();
      thriftStats.total_sent_announcement_msgs_ipv4() =
          static_cast<int64_t>(groupStats.getSentAnnouncementsIpv4());
      thriftStats.total_sent_announcement_msgs_ipv6() =
          static_cast<int64_t>(groupStats.getSentAnnouncementsIpv6());
      thriftStats.total_sent_withdrawal_msgs() =
          static_cast<int64_t>(groupStats.getSentWithdrawals());
      thriftStats.group_total_queue_wait_ms() = totalQueueWaitMs;
      thriftStats.group_total_queue_blocks() = totalQueueBlocks;
      if (lastQueueBlockTime > 0) {
        thriftStats.last_group_queue_block_time() = lastQueueBlockTime;
      }

      thriftStats.post_out_prefix_count() = groupStats.getPostOutPrefixCount();
      thriftStats.post_out_prefix_count_ipv4() =
          groupStats.getPostOutPrefixCountIpv4();
      thriftStats.post_out_prefix_count_ipv6() =
          groupStats.getPostOutPrefixCountIpv6();
      info.stats() = std::move(thriftStats);

      if (auto ts = group->getInitialDumpCompletionTimeMs()) {
        info.initial_dump_completion_time_ms() = *ts;
      }
      info.total_discrepancies() = group->getTotalDiscrepancies();
      info.peers() = std::move(peers);

      result.emplace_back(std::move(info));
      return;
    }
  });

  return result;
}

/**
 * @brief  return collection of all the paths associated to the specific
 *         prefix in the shadow Rib. The returned result will have one
 *         "best" path and another set representing all ECMP (multipaths)
 *
 * @param  Prefix  const folly::CIDRNetwork, a specific prefix reprsenting
 *                 a specific entry in the shadow RIB data-base
 *
 * @param  ShadowRibEntry  a specific shadow RIB entry content associated
 *                         to that prefix
 *
 * @return TRibEntry  the result populated to be passed to the thrift API
 *                    caller. It includes both bestpath and multipaths
 */
std::optional<neteng::fboss::bgp::thrift::TRibEntry>
PeerManagerBase::createTRibEntryWithFilter(
    const std::pair<const folly::CIDRNetwork, facebook::bgp::ShadowRibEntry>&
        entry,
    const std::function<bool(const RouteInfo&)>& /* pathFilter */) {
  TRibEntry tRibEntry;
  TBgpAfi afi =
      entry.first.first.isV4() ? TBgpAfi::AFI_IPV4 : TBgpAfi::AFI_IPV6;
  tRibEntry.prefix()->afi() = afi;
  tRibEntry.prefix()->num_bits() = entry.first.second;
  tRibEntry.prefix()->prefix_bin() =
      facebook::network::toBinaryAddress(entry.first.first)
          .addr()
          ->toStdString();

  std::map<std::string, std::vector<TBgpPath>> pathGrps;
  std::vector<TBgpPath> tBestPaths{};
  std::vector<TBgpPath> tMultiPaths{};
  const auto& srEntry = entry.second;
  const auto& bestpath = srEntry.bestpath;
  /*
   * Don't exit pre-maturely because we can have bestpath == null but
   * multipath not null in the case of bgp_native_path_selection_min_nexthop
   */
  if (bestpath) {
    auto bestNexthop = bestpath->attrs->getNexthop();
    tRibEntry.best_next_hop() = createTIpPrefix(bestNexthop);

    auto tPath = createTBgpPath(*(bestpath->attrs));
    tPath.router_id() = bestpath->peer.routerId;
    tPath.peer_id() = createTIpPrefix(bestpath->peer.addr);
    tPath.peer_description() = bestpath->peer.description;
    tPath.in_update() = isShadowRibRouteInUpdate(bestpath->flags);
    tPath.in_withdraw() = isShadowRibRouteInWithdraw(bestpath->flags);
    tPath.path_id_to_send() = bestpath->pathIdToSend;
    tBestPaths.emplace_back(tPath);
    pathGrps.emplace(facebook::bgp::kBestPathGroup, tBestPaths);
    /*
     * best_group labels the selected group only when a best path exists;
     * left empty otherwise so it reads as a presence signal, not a constant.
     */
    tRibEntry.best_group() = facebook::bgp::kBestPathGroup;
  }

  for (const auto& routeinfo : srEntry.multipaths) {
    auto tPath = createTBgpPath(*(routeinfo.second->attrs));
    tPath.router_id() = routeinfo.second->peer.routerId;
    tPath.peer_id() = createTIpPrefix(routeinfo.second->peer.addr);
    tPath.peer_description() = routeinfo.second->peer.description;
    tPath.in_update() = isShadowRibRouteInUpdate(routeinfo.second->flags);
    tPath.in_withdraw() = isShadowRibRouteInWithdraw(routeinfo.second->flags);
    tPath.path_id_to_send() = routeinfo.second->pathIdToSend;
    tMultiPaths.emplace_back(tPath);
  }
  if (tMultiPaths.size()) {
    pathGrps.emplace(facebook::bgp::kMultiPathGroup, tMultiPaths);
  }
  tRibEntry.paths() = pathGrps;

  return tRibEntry;
}

TRibEntry PeerManagerBase::createTRibEntry(
    const std::pair<const folly::CIDRNetwork, facebook::bgp::ShadowRibEntry>&
        entry) {
  return *(
      createTRibEntryWithFilter(entry, [](const RouteInfo&) { return true; }));
}

/**
 * @brief  retrieve all the entries currently present in the shadow RIB
 *
 * @param  AFI - Address family for which entries to be retrieved
 *
 * @return vector<TRibEntry>  return a full list of entries for a given
 *                            AFI
 */
std::vector<TRibEntry> PeerManagerBase::getShadowRibEntries(TBgpAfi afi) {
  std::vector<TRibEntry> tRibEntries;

  if (afi != TBgpAfi::AFI_IPV4 && afi != TBgpAfi::AFI_IPV6) {
    return tRibEntries;
  }

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    auto expectIPv4 = afi == TBgpAfi::AFI_IPV4;
    for (const auto& srEntry : shadowRibEntries_) {
      const auto& prefix = srEntry.first;
      auto isIPv4 = prefix.first.family() == AF_INET;
      if ((expectIPv4 && !isIPv4) || (!expectIPv4 && isIPv4)) {
        continue;
      }
      tRibEntries.emplace_back(
          createTRibEntry(std::make_pair(prefix, srEntry.second->get())));
    }
  });
  return tRibEntries;
}

neteng::fboss::bgp::thrift::TCanonicalRibState
PeerManagerBase::getShadowRibEntriesCanonical(TBgpAfi afi) {
  CanonicalRibBuilder builder;

  if (afi != TBgpAfi::AFI_IPV4 && afi != TBgpAfi::AFI_IPV6) {
    return builder.build();
  }

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    auto expectIPv4 = afi == TBgpAfi::AFI_IPV4;
    for (const auto& srEntry : shadowRibEntries_) {
      const auto& prefix = srEntry.first;
      auto isIPv4 = prefix.first.family() == AF_INET;
      if (expectIPv4 != isIPv4) {
        continue;
      }
      const auto& entry = srEntry.second->get();
      auto inputs = buildCanonicalPathInputsFromShadowEntry(entry);
      builder.addEntry(prefix, entry.ribVersion, inputs);
    }
  });
  return builder.build();
}

/**
 * @brief  retrieve all the entries currently present in the changeList
 *
 * @param  AFI - Address family for which entries to be retrieved
 *
 * @return vector<TRibEntry>  return a full list of entries for a given
 *                            AFI
 */
std::vector<TRibEntry> PeerManagerBase::getChangeListEntries(TBgpAfi afi) {
  std::vector<TRibEntry> tRibEntries;

  if (afi != TBgpAfi::AFI_IPV4 && afi != TBgpAfi::AFI_IPV6) {
    return tRibEntries;
  }

  if (!changeListTracker_) {
    return tRibEntries;
  }

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    auto expectIPv4 = afi == TBgpAfi::AFI_IPV4;

    auto changeItem = changeListTracker_->getHead();
    while (changeItem) {
      auto& srEntry = changeItem->getTypedObject();
      changeItem = get_next(changeItem, changeListTracker_->getChangeList());
      const auto& prefix = srEntry.prefix;
      auto isIPv4 = prefix.first.family() == AF_INET;
      if ((expectIPv4 && !isIPv4) || (!expectIPv4 && isIPv4)) {
        continue;
      }
      tRibEntries.emplace_back(
          createTRibEntry(std::make_pair(srEntry.prefix, srEntry)));
    }
  });
  return tRibEntries;
}

neteng::fboss::bgp::thrift::TCanonicalRibState
PeerManagerBase::getChangeListEntriesCanonical(TBgpAfi afi) {
  CanonicalRibBuilder builder;

  if (afi != TBgpAfi::AFI_IPV4 && afi != TBgpAfi::AFI_IPV6) {
    return builder.build();
  }

  if (!changeListTracker_) {
    return builder.build();
  }

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    auto expectIPv4 = afi == TBgpAfi::AFI_IPV4;

    auto changeItem = changeListTracker_->getHead();
    while (changeItem) {
      auto& srEntry = changeItem->getTypedObject();
      changeItem = get_next(changeItem, changeListTracker_->getChangeList());
      const auto& prefix = srEntry.prefix;
      auto isIPv4 = prefix.first.family() == AF_INET;
      if (expectIPv4 != isIPv4) {
        continue;
      }
      auto inputs = buildCanonicalPathInputsFromShadowEntry(srEntry);
      builder.addEntry(prefix, srEntry.ribVersion, inputs);
    }
  });
  return builder.build();
}

std::vector<TBgpSession> PeerManagerBase::getSessionInfos(
    const std::unordered_multimap<
        folly::IPAddress,
        std::shared_ptr<BgpPeerDisplayInfo>>& allPeers) noexcept {
  std::vector<TBgpSession> sessions;
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    for (const auto& [addr, peerInfo] : allPeers) {
      sessions.emplace_back(getSessionInfo(addr, peerInfo));
    }
  });
  return sessions;
}

/*
 * TODO: Unused: peer_id, next_hop4, next_hop6 are not being currently used in
 *       display
 */
std::vector<TBgpStreamSession> PeerManagerBase::getBgpStreamSummary() noexcept {
  std::vector<TBgpStreamSession> stream_sessions;
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    for (const auto& [subscriberName, subscriber] : streamSubscribers_) {
      TBgpStreamSession tBgpStreamSession;
      auto peerId = subscriber.peerId;
      tBgpStreamSession.peer_id() = peerId.remoteBgpId;
      tBgpStreamSession.peer_addr() = peerId.peerAddr.str();
      tBgpStreamSession.subscriber_name() = subscriberName;
      tBgpStreamSession.state() = subscriber.state;
      if (subscriber.state == TBgpPeerState::ESTABLISHED) {
        auto currentTime = std::chrono::steady_clock::now();
        auto duration_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                currentTime - subscriber.upSince);
        tBgpStreamSession.uptime() = duration_time.count();
      } else {
        tBgpStreamSession.uptime() = 0;
      }
      if (adjRibs_.contains(peerId)) {
        const auto& stats = adjRibs_.at(peerId)->getStats();
        tBgpStreamSession.sent_prefix_count() = stats.getPostOutPrefixCount();
      } else {
        tBgpStreamSession.sent_prefix_count() = 0;
      }
      tBgpStreamSession.num_flaps() = subscriber.numFlaps;
      stream_sessions.emplace_back(tBgpStreamSession);
    }
  });
  return stream_sessions;
}

std::vector<TBgpSession> PeerManagerBase::getDetailSessionInfos(
    const std::unordered_multimap<
        folly::IPAddress,
        std::shared_ptr<BgpPeerDisplayInfo>>& allPeers) noexcept {
  std::vector<TBgpSession> sessionInfos;
  auto bgpGlobalConfig = configManager_->getConfig()->getBgpGlobalConfig();
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    for (const auto& [peerAddr, bgpDisplayInfo] : allPeers) {
      sessionInfos.emplace_back(
          getDetailSessionInfo(peerAddr, bgpDisplayInfo, bgpGlobalConfig));
    }
  });
  return sessionInfos;
}

std::vector<THoldTimerInfo> PeerManagerBase::getHoldTimerInfos(
    const std::unordered_multimap<
        folly::IPAddress,
        std::shared_ptr<BgpPeerDisplayInfo>>& allPeers) noexcept {
  std::vector<THoldTimerInfo> holdTimerInfos;
  auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();

  for (const auto& [peerAddr, peerInfo] : allPeers) {
    // Skip dynamic peer IDLE entries (unattached prefix-range templates)
    if (peerInfo->peeringParams.peerPrefix) {
      continue;
    }

    THoldTimerInfo info;
    info.peer_address() = peerAddr.str();

    // Compute remaining hold time for peers with an active timer
    if (peerInfo->negotiatedHoldTime &&
        peerInfo->negotiatedHoldTime->count() > 0 &&
        peerInfo->lastResetHoldTimer > 0) {
      auto negotiatedMs = peerInfo->negotiatedHoldTime->count() * 1000;
      auto elapsedMs = nowMs - peerInfo->lastResetHoldTimer;
      auto remainingMs = negotiatedMs - elapsedMs;
      info.hold_time_remaining_ms() =
          remainingMs > 0 ? static_cast<int32_t>(remainingMs) : 0;
    } else {
      info.hold_time_remaining_ms() = 0;
    }

    holdTimerInfos.emplace_back(std::move(info));
  }
  return holdTimerInfos;
}

/**
 * @brief Helper function to get detail information of a BGP neighbor/session
 *
 * @details This function is the superset of `getSessionInfo` function, i.e.
 * it will call the `getSessionInfo` to get basic information of the queried
 * session and this function itself will populate the detail information of
 * it.
 *
 * This function is called by the getBgpNeighbor function.
 *
 * @param peerAddr queried peer addresses to get information on.
 *
 * @param peerInfo is the peer information of the queried peer. The
 * information is relying heavily in the peeringParams baked inside the
 * peerInfo
 *
 * @param bgpGlobalConfig the global config of the BGP daemon
 *
 * @return TBgpSession structs containing information on the queried peers.
 */
TBgpSession PeerManagerBase::getDetailSessionInfo(
    const folly::IPAddress& peerAddr,
    const std::shared_ptr<BgpPeerDisplayInfo>& peerInfo,
    const std::shared_ptr<BgpGlobalConfig>& bgpGlobalConfig) noexcept {
  TBgpSession tBgpSession = getSessionInfo(peerAddr, peerInfo);

  TBgpSessionDetail tBgpSessionDetail;
  tBgpSessionDetail.peer_port() = htons(peerInfo->peeringParams.peerPort);
  tBgpSessionDetail.local_router_id() = bgpGlobalConfig->routerId.str();
  tBgpSessionDetail.local_port() = htons(peerInfo->localAddr.getPort());
  tBgpSessionDetail.confed_peer() = peerInfo->peeringParams.isConfedPeer;
  tBgpSessionDetail.remote_bgp_id() = htonl(peerInfo->remoteBgpId);
  tBgpSessionDetail.ipv4_unicast() =
      peerInfo->negotiatedCapabilities.mpExtV4Unicast().value();
  tBgpSessionDetail.ipv6_unicast() =
      peerInfo->negotiatedCapabilities.mpExtV6Unicast().value();
  if (peerInfo->peeringParams.grRestartTime.has_value()) {
    tBgpSessionDetail.gr_restart_time() =
        peerInfo->peeringParams.grRestartTime.value().count();
  }
  if (peerInfo->remoteGrRestartTime.has_value()) {
    tBgpSessionDetail.gr_remote_restart_time() =
        peerInfo->remoteGrRestartTime.value();
  }
  tBgpSessionDetail.gr_notification_negotiated() =
      nettools::bgplib::isGrNotificationNegotiated(
          peerInfo->negotiatedCapabilities);
  tBgpSessionDetail.rr_client() = peerInfo->peeringParams.isRrClient;
  tBgpSessionDetail.connect_mode() = peerInfo->peeringParams.connectMode;
  if (peerInfo->peeringParams.ttlSecurityHops.has_value()) {
    tBgpSessionDetail.ttl_security_enabled() = true;
    tBgpSessionDetail.ttl_security_hops() =
        peerInfo->peeringParams.ttlSecurityHops.value();
  }
  if (!peerInfo->peeringParams.peerPrefix) {
    BgpPeerId bgpPeerId{peerAddr, peerInfo->remoteBgpId};
    if (adjRibs_.contains(bgpPeerId)) {
      const auto& adjRib = adjRibs_.at(bgpPeerId);
      const auto stats = adjRib->getStats();
      tBgpSessionDetail.sent_update_announcements_ipv4() =
          stats.getSentAnnouncementsIpv4();
      tBgpSessionDetail.sent_update_announcements_ipv6() =
          stats.getSentAnnouncementsIpv6();
      tBgpSessionDetail.recv_update_announcements_ipv4() =
          stats.getRecvAnnouncementsIpv4();
      tBgpSessionDetail.recv_update_announcements_ipv6() =
          stats.getRecvAnnouncementsIpv6();
      tBgpSessionDetail.sent_update_withdrawals() = stats.getSentWithdrawals();
      tBgpSessionDetail.recv_update_withdrawals() = stats.getRecvWithdrawals();
      tBgpSessionDetail.enforce_first_as_rejects() =
          stats.getEnforceFirstAsRejects();
      tBgpSessionDetail.prepolicy_rcvd_prefix_count_ipv4() =
          stats.getPreInPrefixCountIpv4();
      tBgpSessionDetail.prepolicy_rcvd_prefix_count_ipv6() =
          stats.getPreInPrefixCountIpv6();
      tBgpSessionDetail.postpolicy_rcvd_prefix_count_ipv4() =
          stats.getPostInPrefixCountIpv4();
      tBgpSessionDetail.postpolicy_rcvd_prefix_count_ipv6() =
          stats.getPostInPrefixCountIpv6();
      tBgpSessionDetail.prepolicy_sent_prefix_count_ipv4() =
          stats.getPreOutPrefixCountIpv4();
      tBgpSessionDetail.prepolicy_sent_prefix_count_ipv6() =
          stats.getPreOutPrefixCountIpv6();
      tBgpSessionDetail.postpolicy_sent_prefix_count_ipv4() =
          stats.getPostOutPrefixCountIpv4();
      tBgpSessionDetail.postpolicy_sent_prefix_count_ipv6() =
          stats.getPostOutPrefixCountIpv6();
      const auto peerState = adjRib->getPeerState();
      if (peerState == PeerUpdateState::JOINED_RUNNING ||
          peerState == PeerUpdateState::JOINED_BLOCKED) {
        if (auto groupV4 = adjRib->getUpdateGroupPreOutPrefixCountIpv4()) {
          tBgpSessionDetail.prepolicy_sent_prefix_count_ipv4() =
              groupV4.value();
        }
        if (auto groupV6 = adjRib->getUpdateGroupPreOutPrefixCountIpv6()) {
          tBgpSessionDetail.prepolicy_sent_prefix_count_ipv6() =
              groupV6.value();
        }
        if (auto groupV4 = adjRib->getUpdateGroupPostOutPrefixCountIpv4()) {
          tBgpSessionDetail.postpolicy_sent_prefix_count_ipv4() =
              groupV4.value();
        }
        if (auto groupV6 = adjRib->getUpdateGroupPostOutPrefixCountIpv6()) {
          tBgpSessionDetail.postpolicy_sent_prefix_count_ipv6() =
              groupV6.value();
        }
      }
      tBgpSessionDetail.eor_sent_time() = adjRibs_.at(bgpPeerId)->eorSentTime();
      tBgpSessionDetail.eor_received_time() =
          adjRibs_.at(bgpPeerId)->eorReceivedTime();
      tBgpSessionDetail.num_of_flaps() = adjRibs_.at(bgpPeerId)->flapCounter();
      /* Whether this peer is using RFC 4271 legacy v4-unicast NLRI encoding. */
      tBgpSessionDetail.legacy_v4_nlri_encoding() =
          adjRibs_.at(bgpPeerId)->getUpdateGroupKey().legacyV4NlriEncoding;

      /*
       * Control-plane per-type message counts (PeerManager / AdjRib), the
       * counterpart to the socket_* counts below. Both directions are
       * cumulative per-peer counts.
       */
      tBgpSessionDetail.adjrib_recv_update_msgs() =
          static_cast<int64_t>(stats.getRecvUpdateMsgs());
      tBgpSessionDetail.adjrib_recv_eor_msgs() =
          static_cast<int64_t>(stats.getRecvEndOfRibMsgs());
      tBgpSessionDetail.adjrib_sent_update_msgs() =
          static_cast<int64_t>(stats.getSentUpdateMsgs());
      tBgpSessionDetail.adjrib_sent_eor_msgs() =
          static_cast<int64_t>(stats.getSentEndOfRibMsgs());
    }
  }
  /*
   * This is mainly for ACTIVE state peers to see how long the TCP socket has
   * been up
   */
  if (peerInfo->state >= BgpSessionState::ACTIVE) {
    const auto currentTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        currentTime - peerInfo->startTime);
    tBgpSessionDetail.active_time() = duration.count();
  } else {
    tBgpSessionDetail.active_time() = 0;
  }

  // Populate negotiated add-path capabilities
  for (auto& c :
       peerInfo->negotiatedCapabilities.addPathCapabilities().value()) {
    TBgpAddPathNegotiated nc; // negotiated-capability
    switch (c.afi().value()) {
      case BgpUpdateAfi::AFI_IPv4:
        nc.afi().emplace(TBgpAfi::AFI_IPV4);
        break;
      case BgpUpdateAfi::AFI_IPv6:
        nc.afi().emplace(TBgpAfi::AFI_IPV6);
        break;
      case BgpUpdateAfi::AFI_LS:
      default:
        DCHECK(false) << "Unexpected AFI " << static_cast<int>(c.afi().value());
    }
    nc.add_path() = static_cast<AddPath>(c.sor().value());
    tBgpSessionDetail.add_path_capabilities()->emplace_back(nc);
  }

  /*
   * Layer-2 data-plane: PDUs actually written to / read from the socket, from
   * the I/O-thread snapshot. These converge with the AdjRib control counts.
   */
  {
    const auto& tx = peerInfo->txMsgs;
    tBgpSessionDetail.socket_tx_open_msgs() = static_cast<int64_t>(tx.open);
    tBgpSessionDetail.socket_tx_update_msgs() = static_cast<int64_t>(tx.update);
    tBgpSessionDetail.socket_tx_keepalive_msgs() =
        static_cast<int64_t>(tx.keepAlive);
    tBgpSessionDetail.socket_tx_notification_msgs() =
        static_cast<int64_t>(tx.notification);
    tBgpSessionDetail.socket_tx_route_refresh_msgs() =
        static_cast<int64_t>(tx.routeRefresh);
    tBgpSessionDetail.socket_tx_eor_msgs() = static_cast<int64_t>(tx.endOfRib);

    const auto& rx = peerInfo->rxMsgs;
    tBgpSessionDetail.socket_rx_open_msgs() = static_cast<int64_t>(rx.open);
    tBgpSessionDetail.socket_rx_update_msgs() = static_cast<int64_t>(rx.update);
    tBgpSessionDetail.socket_rx_keepalive_msgs() =
        static_cast<int64_t>(rx.keepAlive);
    tBgpSessionDetail.socket_rx_notification_msgs() =
        static_cast<int64_t>(rx.notification);
    tBgpSessionDetail.socket_rx_route_refresh_msgs() =
        static_cast<int64_t>(rx.routeRefresh);
    tBgpSessionDetail.socket_rx_eor_msgs() = static_cast<int64_t>(rx.endOfRib);
  }

  tBgpSession.details() = tBgpSessionDetail;
  return tBgpSession;
}

/**
 * @brief Helper function to get fundamental information of a BGP
 * neighbor/session
 *
 * @details This function is used to query basic information of a BGP
 * neighbor/session. This function is called by the getSessionInfos function.
 *
 * @param peerAddr queried peer addresses to get information on.
 *
 * @param peerInfo is the peer information of the queried peer. The
 * information is relying heavily in the peeringParams baked inside the
 * peerInfo
 *
 * @return TBgpSession structs containing information on the queried peers.
 */
TBgpSession PeerManagerBase::getSessionInfo(
    const folly::IPAddress& peerAddr,
    const std::shared_ptr<BgpPeerDisplayInfo>& peerInfo) noexcept {
  const auto currentTime = std::chrono::steady_clock::now();

  TBgpSession tBgpSession;
  TBgpPeer tBgpPeer;

  tBgpPeer.local_as() = peerInfo->peeringParams.localAs;
  tBgpPeer.remote_as() = peerInfo->peeringParams.remoteAs;

  // TODO: deprecate i32 asns T113736668
  tBgpPeer.local_as_4_byte() = peerInfo->peeringParams.localAs;
  tBgpPeer.remote_as_4_byte() = peerInfo->state >= BgpSessionState::OPEN_CONFIRM
      ? peerInfo->remoteAs
      : peerInfo->peeringParams.remoteAs;

  tBgpSession.peer_bgp_id() =
      folly::IPAddressV4::fromLongHBO(peerInfo->remoteBgpId).str();
  /*
   * Display negotiated value if already negotiated (State >= open_confirm)
   * All other cases (IDLE peer, Dynamic prefix peer etc) display configured
   * value
   */
  if (peerInfo->negotiatedHoldTime) {
    tBgpPeer.hold_time() =
        static_cast<int32_t>(peerInfo->negotiatedHoldTime->count());
  } else {
    tBgpPeer.hold_time() =
        static_cast<int32_t>(peerInfo->peeringParams.holdTime.count());
  }

  // Enum numbers are different, can't simply cast
  switch (peerInfo->state) {
    case BgpSessionState::IDLE:
      if (peerInfo->peeringParams.isShutdown) {
        tBgpPeer.peer_state() = TBgpPeerState::IDLE_ADMIN;
      } else {
        tBgpPeer.peer_state() = TBgpPeerState::IDLE;
      }
      break;
    case BgpSessionState::ACTIVE:
      tBgpPeer.peer_state().emplace(TBgpPeerState::ACTIVE);
      break;
    case BgpSessionState::CONNECT:
      tBgpPeer.peer_state().emplace(TBgpPeerState::CONNECT);
      break;
    case BgpSessionState::OPEN_SENT:
      tBgpPeer.peer_state().emplace(TBgpPeerState::OPEN_SENT);
      break;
    case BgpSessionState::OPEN_CONFIRM:
      tBgpPeer.peer_state().emplace(TBgpPeerState::OPEN_CONFIRMED);
      break;
    case BgpSessionState::ESTABLISHED:
      tBgpPeer.peer_state().emplace(TBgpPeerState::ESTABLISHED);
      break;
  }

  if (peerInfo->remoteGrRestartTime) {
    tBgpPeer.graceful() = true;
  } else {
    tBgpPeer.graceful() = false;
  }
  tBgpPeer.lastResetHoldTimer() = peerInfo->lastResetHoldTimer;
  tBgpPeer.lastResetKeepAliveTimer() = peerInfo->lastResetKeepAliveTimer;
  tBgpPeer.lastSentKeepAlive() = peerInfo->lastSentKeepAlive;
  tBgpPeer.lastRcvdKeepAlive() = peerInfo->lastReceivedKeepAlive;

  // Populate configured add-path information
  if (peerInfo->peeringParams.addPath.has_value()) {
    tBgpPeer.add_path() = peerInfo->peeringParams.addPath.value();
  }

  tBgpSession.peer() = std::move(tBgpPeer);
  tBgpSession.my_addr() = getAddressStr(peerInfo->localAddr);

  // Set UCMP configuration knobs
  if (peerInfo->peeringParams.advertiseLinkBandwidth.has_value()) {
    tBgpSession.advertise_link_bandwidth() =
        *peerInfo->peeringParams.advertiseLinkBandwidth;
  }
  if (peerInfo->peeringParams.receiveLinkBandwidth.has_value()) {
    tBgpSession.receive_link_bandwidth() =
        *peerInfo->peeringParams.receiveLinkBandwidth;
  }
  if (peerInfo->peeringParams.linkBandwidthBps) {
    tBgpSession.link_bandwidth_bps() =
        peerInfo->peeringParams.linkBandwidthBps.value();
  }

  /*
   * Please see T126145664 and D54921268
   * For ESTABLISHED peer, the peerInfo->lastResetReason and
   * peerInfo->numResets are from sessionInfo. For IDLE and ACTIVE peers, the
   * peerInfo->lastResetReason and peerInfo->numResets are from
   * connectionInfo->lastSessionInfo weak pointer which points to the latest
   * sessionInfo.
   */
  if (peerInfo->state == BgpSessionState::ESTABLISHED ||
      peerInfo->state == BgpSessionState::ACTIVE ||
      peerInfo->state == BgpSessionState::IDLE) {
    if (peerInfo->state == BgpSessionState::ESTABLISHED) {
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          currentTime - peerInfo->establishedTime);
      tBgpSession.uptime() = duration.count();
    }
    if (peerInfo->numResets > 0) {
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          currentTime - peerInfo->lastResetTime);
      tBgpSession.reset_time() = duration.count();
      if (peerInfo->lastResetReason.has_value()) {
        tBgpSession.last_reset_reason() =
            facebook::nettools::bgplib::getResetReasonName(
                peerInfo->lastResetReason.value());
      }
      /*
       * Carried from the same lastSessionInfo as lastResetReason, and left
       * unset when no NOTIFICATION was exchanged -- a session can reset
       * without one (socket error, hold timer expiry with no send).
       */
      if (const auto& sent = peerInfo->lastNotificationSent) {
        tBgpSession.last_notification_sent() = sent->notification;
        tBgpSession.last_notification_sent_time() = sent->timeMs;
      }
      if (const auto& received = peerInfo->lastNotificationReceived) {
        tBgpSession.last_notification_received() = received->notification;
        tBgpSession.last_notification_received_time() = received->timeMs;
      }
    }
  }
  tBgpSession.num_resets() = peerInfo->numResets;

  // Set default values
  tBgpSession.prepolicy_rcvd_prefix_count() = 0;
  tBgpSession.postpolicy_rcvd_prefix_count() = 0;
  tBgpSession.postpolicy_sent_prefix_count() = 0;
  tBgpSession.recv_update_msgs() = 0;
  tBgpSession.sent_update_msgs() = 0;

  BgpPeerId bgpPeerId{peerAddr, peerInfo->remoteBgpId};
  if (!peerInfo->peeringParams.peerPrefix) {
    // Not a Dynamic Peer IDLE entry
    if (adjRibs_.contains(bgpPeerId)) {
      const auto& adjRib = adjRibs_.at(bgpPeerId);
      const auto& stats = adjRib->getStats();
      tBgpSession.prepolicy_rcvd_prefix_count() = stats.getPreInPrefixCount();
      tBgpSession.postpolicy_rcvd_prefix_count() = stats.getPostInPrefixCount();
      tBgpSession.postpolicy_sent_prefix_count() =
          stats.getPostOutPrefixCount();
      tBgpSession.recv_update_msgs() = stats.getRecvUpdateMsgs();
      tBgpSession.sent_update_msgs() = stats.getSentUpdateMsgs();

      /*
       * Per-peer routes dropped because the peer reached its configured
       * pre-filter max prefix limit. A non-zero value means the peer is
       * actively shedding received (PR) routes; `show bgp summary` flags it and
       * the health validator reuses this count.
       */
      tBgpSession.prepolicy_rcvd_dropped_prefix_count() =
          stats.getPreFilterDroppedRouteCount();

      tBgpSession.rib_version() = adjRib->getLastSeenRibVersion();
      if (adjRib->getIngressPolicyName().has_value()) {
        tBgpSession.ingress_policy_name() =
            adjRib->getIngressPolicyName().value();
      }
      if (adjRib->getEgressPolicyName().has_value()) {
        tBgpSession.egress_policy_name() =
            adjRib->getEgressPolicyName().value();
      }
      // Populate update-group info
      auto groupId = adjRib->getUpdateGroupId();
      if (groupId.has_value()) {
        tBgpSession.update_group_id() = groupId.value();
        /*
         * Use group-level prefix count only when peer is in-sync with
         * the group (JOINED_RUNNING or JOINED_BLOCKED). Detached peers
         * have their own independent RIB-OUT state.
         */
        auto peerState = adjRib->getPeerState();
        if (peerState == PeerUpdateState::JOINED_RUNNING ||
            peerState == PeerUpdateState::JOINED_BLOCKED) {
          auto groupPrefixCount = adjRib->getUpdateGroupPostOutPrefixCount();
          if (groupPrefixCount.has_value()) {
            tBgpSession.postpolicy_sent_prefix_count() =
                groupPrefixCount.value();
          }
        }
      }
      tBgpSession.peer_state_update_group() =
          fmt::format("{}", adjRib->getPeerState());
    }
    tBgpSession.peer_addr() = peerAddr.str();
    if (const auto it = adjRibs_.find(bgpPeerId); it != adjRibs_.end()) {
      tBgpSession.eor_received() = it->second->eorReceivedTime() > 0;
      tBgpSession.eor_sent() = it->second->eorSentTime() > 0;
    } else {
      tBgpSession.eor_received() = false;
      tBgpSession.eor_sent() = false;
    }
  } else {
    // Dynamic peer IDLE entry
    tBgpSession.peer_addr() = folly::IPAddress::networkToString(
        *(peerInfo->peeringParams.peerPrefix));
    tBgpSession.eor_received() = false;
    tBgpSession.eor_sent() = false;
  }
  tBgpSession.description() = peerInfo->peeringParams.description;
  return tBgpSession;
}

void PeerManagerBase::getNetworks(
    std::map<TIpPrefix, TBgpPath>& prefixToPath,
    const std::unique_ptr<std::string>& peer,
    const RouteFilterType& type) noexcept {
  // Invalid input
  if (!peer) {
    return;
  }

  // Invalid neighbor address
  if (!folly::IPAddress::validate(*peer)) {
    return;
  }

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    auto peerAddr = folly::IPAddress(*peer);
    if (peerAddrToIds_.contains(peerAddr)) {
      for (const auto& peerId : peerAddrToIds_.at(peerAddr)) {
        if (!adjRibs_.contains(peerId)) {
          continue;
        }
        const auto adjRib = adjRibs_.at(peerId);
        adjRib->getNetworks(prefixToPath, type);
      }
    }
    // Nothing in AdjRib
    else {
      return;
    }
  });
}

void PeerManagerBase::getNetworks(
    std::map<TIpPrefix, TBgpPath>& prefixToPath,
    const std::unique_ptr<std::string>& peer,
    const std::unique_ptr<std::string>& sessionBgpId,
    const RouteFilterType& type) noexcept {
  // Invalid input
  if (!peer || !sessionBgpId) {
    return;
  }

  // Invalid neighbor address and bgpId
  if (!folly::IPAddress::validate(*peer) ||
      !folly::IPAddress::validate(*sessionBgpId)) {
    return;
  }

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    auto peerAddr = folly::IPAddress(*peer);
    const BgpPeerId peerId{
        peerAddr, folly::IPAddressV4(*sessionBgpId).toLongHBO()};
    // Nothing in AdjRib
    if (!adjRibs_.contains(peerId)) {
      return;
    }
    const auto adjRib = adjRibs_.at(peerId);
    adjRib->getNetworks(prefixToPath, type);
  });
}

void PeerManagerBase::getNetworks2(
    std::map<TIpPrefix, std::vector<TBgpPath>>& prefixToPath,
    const std::unique_ptr<std::string>& peer,
    const RouteFilterType& type) noexcept {
  // Invalid input
  if (!peer) {
    return;
  }

  // Invalid neighbor address
  if (!folly::IPAddress::validate(*peer)) {
    return;
  }

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    auto peerAddr = folly::IPAddress(*peer);
    if (!peerAddrToIds_.contains(peerAddr)) {
      return;
    }
    for (const auto& peerId : peerAddrToIds_.at(peerAddr)) {
      if (!adjRibs_.contains(peerId)) {
        continue;
      }
      const auto adjRib = adjRibs_.at(peerId);
      adjRib->getNetworks2(prefixToPath, type);
    }
  });
}

void PeerManagerBase::getNetworks2(
    std::map<TIpPrefix, std::vector<TBgpPath>>& prefixToPath,
    const std::unique_ptr<std::string>& peer,
    const std::unique_ptr<std::string>& sessionBgpId,
    const RouteFilterType& type) noexcept {
  // Invalid input
  if (!peer || !sessionBgpId) {
    return;
  }

  // Invalid neighbor address and bgpId
  if (!folly::IPAddress::validate(*peer) ||
      !folly::IPAddress::validate(*sessionBgpId)) {
    return;
  }

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    auto peerAddr = folly::IPAddress(*peer);
    const BgpPeerId peerId{
        peerAddr, folly::IPAddressV4(*sessionBgpId).toLongHBO()};
    // Nothing in AdjRib
    if (!adjRibs_.contains(peerId)) {
      return;
    }
    const auto adjRib = adjRibs_.at(peerId);
    adjRib->getNetworks2(prefixToPath, type);
  });
}

std::vector<BgpPeerId> PeerManagerBase::resolveRejectedNetworkPeers(
    const re2::RE2* nameFilter,
    const std::vector<std::string>& peers) noexcept {
  std::vector<BgpPeerId> peerIds;

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    /*
     * The explicit-peers branch below can reach the same neighbor twice --
     * the caller's list is unvalidated and may repeat an address, and two
     * spellings can normalize to one folly::IPAddress. A repeated id would
     * make 3/3 walk that peer twice and count its rejections twice, so
     * de-duplicate here rather than leaving it to the caller. Bounded by the
     * neighbor count, not by RIB size.
     */
    folly::F14FastSet<BgpPeerId> seen;
    auto addPeer = [&](const BgpPeerId& peerId, const AdjRib& adjRib) {
      if (!seen.insert(peerId).second) {
        return;
      }
      auto& added = peerIds.emplace_back(peerId);
      added.peerDescription = adjRib.getPeeringParams().description;
    };

    if (peers.empty()) {
      for (const auto& [peerId, adjRib] : adjRibs_) {
        if (!adjRib) {
          continue;
        }
        if (nameFilter &&
            !re2::RE2::PartialMatch(
                adjRib->getPeeringParams().description, *nameFilter)) {
          continue;
        }
        addPeer(peerId, *adjRib);
      }
      return;
    }

    for (const auto& peer : peers) {
      if (!folly::IPAddress::validate(peer)) {
        continue;
      }
      const auto peerAddr = folly::IPAddress(peer);
      if (!peerAddrToIds_.contains(peerAddr)) {
        continue;
      }
      for (const auto& peerId : peerAddrToIds_.at(peerAddr)) {
        const auto it = adjRibs_.find(peerId);
        if (it == adjRibs_.end() || !it->second) {
          continue;
        }
        addPeer(peerId, *it->second);
      }
    }
  });

  return peerIds;
}

RejectedNetworks PeerManagerBase::getRejectedNetworksForPeer(
    const BgpPeerId& peerId,
    const RejectedNetworksQuery& query) noexcept {
  RejectedNetworks rejected;

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    const auto it = adjRibs_.find(peerId);
    if (it == adjRibs_.end() || !it->second) {
      return;
    }
    it->second->getRejectedNetworks(rejected, query);
  });

  return rejected;
}

void PeerManagerBase::getSubscriberNetworks(
    std::map<TIpPrefix, std::vector<TBgpPath>>& prefixToPath,
    const int32_t peerID,
    const RouteFilterType& type) noexcept {
  BgpPeerId peer_id{streamPeerAddr_, static_cast<uint32_t>(peerID)};

  // Invalid neighbor address
  if (!adjRibs_.contains(peer_id)) {
    XLOGF(INFO, "Did not find adjrib for subscriber {}", peerID);
    return;
  }

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    const auto adjRib = adjRibs_.at(peer_id);
    adjRib->getNetworks2(prefixToPath, type);
  });
}

TGetDeduplicatorStatsResponse PeerManagerBase::getDeduplicatorStats() noexcept {
  TGetDeduplicatorStatsResponse stats;
  stats.bgp_path()->entry_count() = static_cast<int64_t>(
      nettools::bgplib::DeDuplicatedBgpPath::deduplicatorSize());
  stats.bgp_attributes()->entry_count() = static_cast<int64_t>(
      nettools::bgplib::DeDuplicatedBgpAttributesC::deduplicatorSize());
  stats.as_path()->entry_count() = static_cast<int64_t>(
      nettools::bgplib::DeDuplicatedAsPath::deduplicatorSize());
  stats.communities()->entry_count() = static_cast<int64_t>(
      nettools::bgplib::DeDuplicatedCommunities::deduplicatorSize());
  stats.cluster_list()->entry_count() = static_cast<int64_t>(
      nettools::bgplib::DeDuplicatedClusterList::deduplicatorSize());
  stats.ext_communities()->entry_count() = static_cast<int64_t>(
      nettools::bgplib::DeDuplicatedExtCommunities::deduplicatorSize());
  return stats;
}

TGetAdjRibStatsResponse PeerManagerBase::getAdjRibStats(
    TDirectionFilter direction) const {
  DCHECK(evb_.isInEventBaseThread());

  TGetAdjRibStatsResponse response;
  auto& ribInPeers = response.rib_in()->peers().value();
  auto& ribOutPeers = response.rib_out()->peers().value();
  const bool includeRibIn = direction != TDirectionFilter::EGRESS;
  const bool includeRibOut = direction != TDirectionFilter::INGRESS;

  if (includeRibIn) {
    ribInPeers.reserve(adjRibs_.size());
  }
  if (includeRibOut) {
    ribOutPeers.reserve(adjRibs_.size());
  }
  for (const auto& [_, adjRib] : adjRibs_) {
    if (!adjRib) {
      continue;
    }
    if (includeRibIn) {
      ribInPeers.emplace_back(adjRib->getRibInStatsSnapshot());
    }
    if (includeRibOut) {
      ribOutPeers.emplace_back(adjRib->getRibOutStatsSnapshot());
    }
  }
  return response;
}

} // namespace facebook::bgp
