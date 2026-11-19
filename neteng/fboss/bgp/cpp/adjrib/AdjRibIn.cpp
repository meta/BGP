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

#include <folly/Overload.h>
#include <folly/logging/xlog.h>

#include "common/network/AddressUtil.h"
#include "magic_enum/magic_enum.hpp"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibCommon.h"
#include "neteng/fboss/bgp/cpp/lib/BgpUtil.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

using namespace facebook::nettools::bgplib;

namespace facebook::bgp {

// Functions that depend on AdjRibPrefixSet are kept here to avoid circular deps

void AdjRibStats::incrementPreInPrefixCount(
    const folly::CIDRNetwork& prefix,
    const bool isVipPrefix,
    const bool isGoldenVip) {
  // increment the statistics
  preInPrefixCount++;
  if (prefix.first.isV4()) {
    preInPrefixCountIpv4++;
  } else {
    preInPrefixCountIpv6++;
  }
  totalRcvdPrefixCount++;
  AdjRibPrefixSet::get()->addPrefix(prefix, isGoldenVip);

  // expose the stats to ThreadCachedServiceData
  PeerStats::setPeerPreInPrefixes(peerIdOdsStr, preInPrefixCount);
  PeerStats::setTotalRcvdPrefixes(totalRcvdPrefixCount);
  PeerStats::setTotalPaths(getTotalSwitchPathCount());
  PeerStats::setTotalUniquePrefixes(AdjRibPrefixSet::get()->size());

  if (preInPrefixCount > maxPeerRcvdPrefixCount) {
    maxPeerRcvdPrefixCount = preInPrefixCount;
    PeerStats::setMaxPeerRcvdPrefixes(maxPeerRcvdPrefixCount);
  }
  if (isVipPrefix) {
    auto [it, inserted] = vipPrefixes.emplace(prefix);
    if (inserted) {
      ++totalVipPrefixesCount;
      PeerStats::setTotalVipPrefixes(totalVipPrefixesCount);
    }
  }
  if (isGoldenVip) {
    PeerStats::setTotalGoldenVipPrefixes(
        AdjRibPrefixSet::get()->goldenVipSize());
  }
}

void AdjRibStats::decrementPreInPrefixCount(const folly::CIDRNetwork& prefix) {
  // decrement the statistics
  preInPrefixCount--;
  if (prefix.first.isV4()) {
    preInPrefixCountIpv4--;
  } else {
    preInPrefixCountIpv6--;
  }
  totalRcvdPrefixCount--;
  AdjRibPrefixSet::get()->delPrefix(prefix);

  // expose the stats to ThreadCachedServiceData
  PeerStats::setPeerPreInPrefixes(peerIdOdsStr, preInPrefixCount);
  PeerStats::setTotalRcvdPrefixes(totalRcvdPrefixCount);
  PeerStats::setTotalPaths(getTotalSwitchPathCount());
  PeerStats::setTotalUniquePrefixes(AdjRibPrefixSet::get()->size());

  if (vipPrefixes.erase(prefix) > 0 && totalVipPrefixesCount > 0) {
    --totalVipPrefixesCount;
  }
  PeerStats::setTotalVipPrefixes(totalVipPrefixesCount);
  PeerStats::setTotalGoldenVipPrefixes(AdjRibPrefixSet::get()->goldenVipSize());
}

folly::coro::Task<void> AdjRib::sendRibInAnnouncement(
    const PrefixPathIds& pfxPathIds,
    const std::shared_ptr<const BgpPath>& attrs) noexcept {
  if (!pfxPathIds.empty()) {
    RibInAnnouncement announcement(
        TinyPeerInfo(
            peeringParams_.peerAddr,
            getRemoteAs(),
            remotePeerId_->remoteBgpId,
            getBgpSessionType(),
            peeringParams_.isRrClient,
            peeringParams_.isRedistributePeer,
            peeringParams_.linkBandwidthBps,
            peeringParams_.description),
        pfxPathIds,
        attrs);
    co_await ribInQ_.push(std::move(announcement));
  }
}

folly::coro::Task<void> AdjRib::sendRibInWithdrawal(
    const PrefixPathIds& pfxPathIds) noexcept {
  if (!pfxPathIds.empty()) {
    RibInWithdrawal withdrawal(
        TinyPeerInfo(
            peeringParams_.peerAddr,
            getRemoteAs(),
            remotePeerId_->remoteBgpId,
            getBgpSessionType(),
            peeringParams_.isRrClient,
            peeringParams_.isRedistributePeer,
            peeringParams_.linkBandwidthBps,
            peeringParams_.description),
        pfxPathIds);
    co_await ribInQ_.push(std::move(withdrawal));
  }
}

folly::coro::Task<void> AdjRib::processPeerMessageLoop(
    std::shared_ptr<folly::coro::Baton> terminateBaton) noexcept {
  /*
   * SCOPE_EXIT runs unconditionally — on normal loop exit, cancellation
   * (OperationCancelled), or any other exception. It is not a coroutine
   * await point, so the cancellation token cannot interrupt it.
   */
  SCOPE_EXIT {
    XLOGF(
        DBG1,
        "[Exit] Successfully stopped processPeerMessageLoop for {}",
        getPeerName());

    logPeerEvent("SHUTDOWN_TERMINATE_BATON_POSTED", BGP_LOG_SRC());

    /* Post the terminate baton LAST to unblock
     * PeerManagerBase::sessionEstablished(). */
    terminateBaton->post();
  };

  XLOGF(INFO, "Starting processPeerMessage coro task for {}", getPeerName());
  logPeerEvent("SESSION_PEER_MSG_LOOP_STARTED", BGP_LOG_SRC());

  auto overload = folly::overload(
      [this](std::shared_ptr<const BgpUpdate2> const& update)
          -> folly::coro::Task<bool> {
        /*
         * Acquire semaphore before modifying trees.
         * SCOPE_EXIT guarantees release even if an exception is thrown,
         * using an internal RAII guard that invokes the lambda on destruction.
         */
        co_await waitForTreeAccessSemaphore();
        SCOPE_EXIT {
          signalTreeAccessSemaphore();
        };

        co_await processPeerUpdate(*update);
        co_return false;
      },
      [this](BgpEndOfRib eor) -> folly::coro::Task<bool> {
        // Acquire semaphore before modifying trees
        co_await waitForTreeAccessSemaphore();
        SCOPE_EXIT {
          signalTreeAccessSemaphore();
        };

        co_await processPeerEoR(std::move(eor));
        co_return false;
      },
      [this](BgpRouteRefresh routeRefresh) -> folly::coro::Task<bool> {
        // No tree modification, no semaphore needed
        processPeerRouteRefresh(routeRefresh);
        co_return false;
      },
      [this](
          FiberBgpPeer::BgpSessionStop sessionStop) -> folly::coro::Task<bool> {
        /*
         * Acquire semaphore before modifying trees (this call can clear stale
         * routes)
         */
        co_await waitForTreeAccessSemaphore();
        SCOPE_EXIT {
          signalTreeAccessSemaphore();
        };

        co_await sessionTerminated(std::move(sessionStop));
        co_return true;
      });

  bool terminate = false;

  // adjRibInQueue_ now has a consumer
  auto consumerScope = adjRibInQueue_->getConsumerScope();

  while (!terminate) {
    const auto& token = co_await folly::coro::co_current_cancellation_token;
    if (token.isCancellationRequested()) {
      XLOGF(
          DBG1,
          "[Exit] processPeerMessageLoop cancellation requested for {}",
          getPeerName());
      break;
    }

    auto msg = co_await co_awaitTry(adjRibInQueue_->pop());
    if (msg.hasException()) {
      XLOGF(
          DBG1,
          "[Exit] processPeerMessageLoop is terminating for {}",
          getPeerName());
      break;
    }

    terminate = co_await std::visit(overload, *msg);
  }
}

std::shared_ptr<const BgpPath> AdjRib::getPostInRouteFilterAndPolicyAttributes(
    const folly::CIDRNetwork& prefix,
    const std::shared_ptr<BgpPath>& prePolicyAttrs,
    std::shared_ptr<BgpPolicyActionData>& policyActionData,
    folly::not_null<AdjRibEntry*> adjRibEntry) {
  /*
   * Apply ingress route filter before routing policy processing if ingress
   * route filter is configured
   *
   * If ingress route filter is not configured, this is a no-op
   */
  if (blockedByIngressRouteFilter(prefix)) {
    adjRibEntry->setPostInPolicy("Denied by Route Filter Policy");
    stats_.incrementIngressRouteFilterDenied();
    return nullptr;
  }

  // Apply routing policy for prefixes allowed by Ingress filter
  return getPostInPolicyAttributes(
      prefix, prePolicyAttrs, policyActionData, adjRibEntry);
}

std::shared_ptr<const BgpPath> AdjRib::getPostInPolicyAttributes(
    const folly::CIDRNetwork& prefix,
    const std::shared_ptr<BgpPath>& prePolicyAttrs,
    std::shared_ptr<BgpPolicyActionData>& policyActionData,
    folly::not_null<AdjRibEntry*> adjRibEntry) {
  CHECK(prePolicyAttrs != nullptr);

  if (ingressPolicyConfigured()) {
    const auto& [policyCachedAttrs, postTermName, _] =
        getPostPolicyAttributesPolicyTermAndInfo(
            *ingressPolicyName_, prefix, prePolicyAttrs, policyActionData);

    adjRibEntry->setPostInPolicy(postTermName);

    if (policyCachedAttrs) {
      auto attrsToOverride = prePolicyAttrs->clone();
      /*
       * The policyCachedAttrs may have been given from the cache. However,
       * we cannot use policyCachedAttrs directly because only the
       * attributes marked true in the mask are guaranteed to have the
       * correct post-policy value (as these are the only attributes touched
       * by the policy engine).
       *
       * Therefore, we need to override the original prePolicyAttrs
       * with the policyCachedAttrs. As prePolicyAttrs is possibly used
       * as a policy cache key, we cannot do this modification
       * to the prePolicyAttrs directly.
       */
      overridePrePolicyAttributesCommon(
          policyManager_->getPolicyAttributesMask(*ingressPolicyName_),
          policyCachedAttrs,
          attrsToOverride);
      replaceZerosInAsPath(attrsToOverride, getRemoteAs());
      attrsToOverride->publish();
      return attrsToOverride;
    } else {
      // This route was rejected by policy.
      return nullptr;
    }
  }
  // Otherwise, there was no policy configured so no policy evaluation.
  adjRibEntry->setPostInPolicy({});

  // Publish since we do not anticipate any more changes to prePolicyAttrs.
  if (!prePolicyAttrs->isPublished()) {
    prePolicyAttrs->publish();
  }
  return prePolicyAttrs;
}

void AdjRib::incrementPreInPrefixCount(
    const folly::CIDRNetwork prefix,
    const bool isVipPrefix,
    const bool isGoldenVip) {
  if (isSafeModeOn() && goldenPrefixPolicy_) {
    goldenPrefixPolicy_->incrementSubnet(prefix);
  }
  stats_.incrementPreInPrefixCount(prefix, isVipPrefix, isGoldenVip);
}

void AdjRib::decrementPreInPrefixCount(const folly::CIDRNetwork prefix) {
  if (isSafeModeOn() && goldenPrefixPolicy_) {
    goldenPrefixPolicy_->decrementSubnet(prefix);
  }
  stats_.decrementPreInPrefixCount(prefix);
}

void AdjRib::incrementPrefixesDroppedByLimit(uint32_t count) {
  totalDroppedPrefixCount += count;
  PeerStats::incrementTotalPrefixesDroppedByLimit(count);
}

bool AdjRib::canAddRibInEntry(
    const folly::CIDRNetwork& prefix,
    const std::shared_ptr<const BgpPath>& attrs) {
  if (dropPrefixForOverloadProtection(
          AdjRibStats::getTotalSwitchPathCount() + 1,
          prefix,
          *attrs,
          switchLimitConfig_)) {
    /*
     * Hit per-switch limit of:
     *  1. total path, or
     *  2. total unique prefix count
     * Do NOT process the new path. Continue here because next item may be
     * an update of existing path, not a new one.
     */
    incrementPrefixesDroppedByLimit();
    return false;
  }
  if (capRoutesPerPeer(
          stats_.getPreInPrefixCount() + 1, true /* prefilter */)) {
    /*
     * Hit per-peer limit of the received route/path. This limit is
     * per-adjRib.
     *
     * Do NOT process the new path. Continue here because next item may be
     * an update of existing path, not a new one.
     */
    incrementPrefixesDroppedByLimit();
    stats_.incrementPreFilterDroppedRouteCount();
    return false;
  }
  return true;
}

folly::coro::Task<void> AdjRib::sendRibInUpdates(
    const PrefixPathIds& withdrawnPfxPathIds,
    const folly::F14NodeMap<std::shared_ptr<const BgpPath>, PrefixPathIds>&
        groupAnnouncedPrefixes) {
  co_await sendRibInWithdrawal(withdrawnPfxPathIds);
  // Pass to Rib, each group of (set of prefixes, attributes)
  for (auto& itr : groupAnnouncedPrefixes) {
    co_await sendRibInAnnouncement(itr.second, itr.first);
  }
}

uint32_t AdjRib::processPrefixWithPolicy(
    const folly::CIDRNetwork& prefix,
    const uint32_t pathId,
    const std::shared_ptr<BgpPath>& postConfigInAttrs,
    std::shared_ptr<BgpPolicyActionData>& policyActionData,
    folly::not_null<AdjRibEntry*> adjRibEntry,
    PrefixPathIds& withdrawnPfxPathIds,
    folly::F14NodeMap<std::shared_ptr<const BgpPath>, PrefixPathIds>&
        groupAnnouncedPrefixes) {
  /*
   * Get policy result for this prefix and update adjRibEntry.
   * Check if attributes need to be updated or we need to filter this prefix
   */
  auto postInAttrs = getPostInRouteFilterAndPolicyAttributes(
      prefix, postConfigInAttrs, policyActionData, adjRibEntry);

  return maybeAnnouncePrefix(
      prefix,
      pathId,
      postInAttrs,
      adjRibEntry,
      withdrawnPfxPathIds,
      groupAnnouncedPrefixes);
}

uint32_t AdjRib::maybeAnnouncePrefix(
    const folly::CIDRNetwork& prefix,
    const uint32_t pathId,
    const std::shared_ptr<const BgpPath>& postInAttrs,
    folly::not_null<AdjRibEntry*> adjRibEntry,
    PrefixPathIds& withdrawnPfxPathIds,
    folly::F14NodeMap<std::shared_ptr<const BgpPath>, PrefixPathIds>&
        groupAnnouncedPrefixes) {
  if (!postInAttrs && !adjRibEntry->getPostAttr()) {
    /*
     * Policy blocked this prefix and we didn't inform Rib before, so no need
     * to inform to rib now.
     */
    XLOGF(
        DBG3,
        "Ignoring announcement {} from {}. "
        "Reason: Blocked prefix by policy. (Not previously announced to Rib)",
        folly::IPAddress::networkToString(prefix),
        getPeerName());
    return 0;
  } else if (!postInAttrs) {
    /*
     * Policy blocked this prefix, but previously it was informed to Rib.
     * This can happen if attributes change for a prefix and policy based on
     * new attributes blocks the prefix. Withdraw it from Rib.
     */
    XLOGF(
        DBG3,
        "Withdrawing {} from {}. "
        "Reason: Blocked prefix by policy. (Previously announced to Rib)",
        folly::IPAddress::networkToString(prefix),
        getPeerName());
    stats_.decrementPostInPrefixCount(prefix.first.isV4());
    withdrawnPfxPathIds.emplace_back(prefix, pathId);
    adjRibEntry->setPostAttr(postInAttrs);
    return 0;
  } else {
    /*
     * Announce the prefix to Rib only if postIn has changed
     * We are doing deep compare to avoid notifying rib in cases where
     * due to policy changes of attributes, we end up with same contents
     * but different BgpPath shared_ptr
     */
    if (adjRibEntry->getPostAttr() &&
        (adjRibEntry->getPostAttr() != postInAttrs) &&
        (*adjRibEntry->getPostAttr() == *postInAttrs)) {
      XLOGF(
          DBG3,
          "Ignoring announcement {} from {}. "
          "Reason: got same post policy in attributes.",
          folly::IPAddress::networkToString(prefix),
          getPeerName());
      return 0;
    }
    // For new added adjRibEntry, check to see if we should to cap this route
    if (!adjRibEntry->getPostAttr() &&
        capRoutesPerPeer(
            stats_.getPostInPrefixCount() + 1, false /* postfilter */)) {
      // If pass limit, lets check the next one.
      incrementPrefixesDroppedByLimit();
      return 0;
    }
    // Updating rec prefix count only if it is newly learned routes.
    if (!adjRibEntry->getPostAttr()) {
      stats_.incrementPostInPrefixCount(prefix.first.isV4());
    }

    /**
     * setPostAttr would retrieve attr from the cache if the content
     * of that cached entry is same as postInAttrs that is passed
     * here. This postInAttrs is not valid anymore in that case.
     * Hence, make sure to call getPostAttr() to get the final
     * selected attr to use
     */
    stats_.updateAttributeSizes(postInAttrs);
    adjRibEntry->setPostAttr(postInAttrs);
    auto& postInAttrsFromEntry = adjRibEntry->getPostAttr();
    // Return 1 for announced prefix
    auto it = groupAnnouncedPrefixes.find(postInAttrsFromEntry);
    if (it == groupAnnouncedPrefixes.end()) {
      groupAnnouncedPrefixes[postInAttrsFromEntry] =
          PrefixPathIds{{prefix, pathId}};
    } else {
      it->second.emplace_back(prefix, pathId);
    }
    return 1;
  }
}

folly::coro::Task<void> AdjRib::processPeerAnnounced(
    const std::vector<nettools::bgplib::RiggedIPPrefix>& prefixes,
    const std::shared_ptr<BgpPath>& attrs) noexcept {
  // NOTE: Shallow hashing just using pointer not contents of BgpPath
  folly::F14NodeMap<std::shared_ptr<const BgpPath>, PrefixPathIds>
      groupAnnouncedPrefixes;

  PrefixPathIds withdrawnPfxPathIds;
  /*
   * route processing as following:
   * 1. preInAttrs: route prior to config and policy
   * (processing per-peer-config)
   * 2. postConfigInAttrs: route after peer-config changes
   * (processing per-route-policy)
   * 3. postInAttrs: route after peer-config and policy changes.
   */
  const std::shared_ptr<const BgpPath> preInAttrs = attrs;

  auto postConfigInAttrs = updateAttributesIn(attrs);

  // snapshot policyActionData
  auto policyActionData = createPolicyActionData(attrs);

  const auto& asPathDedup = attrs->getAsPath();
  const bool isVipPrefix = !asPathDedup.nullOrEmpty() &&
      std::find_if(
          asPathDedup->begin(), asPathDedup->end(), [&](const auto& segment) {
            return segment.hasAsn(kVipAsn);
          }) != asPathDedup->end();
  bool isGoldenVipPrefix = isSafeModeOn() && goldenPrefixPolicy_ &&
      isGoldenVip(attrs->getCommunities().get());

  for (const auto& rigPrefix : prefixes) {
    auto prefix = network::toCIDRNetwork(*rigPrefix.prefix());
    uint32_t pathId = recAddPath_ && rigPrefix.pathId() ? *rigPrefix.pathId()
                                                        : kDefaultPathID;
    // If we have a stale entry for this path, we should put it back in AdjRibIn
    if (enableOptimizedGR_) {
      promoteStaleRibInEntryIfExistsInPlace(prefix, pathId);
    } else {
      promoteStaleRibInEntryIfExists(prefix, pathId);
    }

    // Try to get existing adjRibEntry for prefix, if it exists.
    auto adjRibEntry = getRibEntry(/*ingress=*/true, prefix, pathId);
    if (!adjRibEntry || !adjRibEntry->getPreIn()) {
      if (!canAddRibInEntry(prefix, attrs)) {
        continue;
      }

      /*
       * If it's in safemode and has valid golden policy, increment the PreIn
       * prefix count. Meanwhile, if it's a Golden VIP, mark the prefix in
       * AdjRibPrefixSet as golden
       */
      incrementPreInPrefixCount(prefix, isVipPrefix, isGoldenVipPrefix);

      // Add a new AdjRibEntry since we are learning new route.
      if (!adjRibEntry) {
        adjRibEntry = addRibEntry(/*ingress=*/true, prefix, pathId);
      }
    }

    CHECK(adjRibEntry);
    /*
     * Ignoring any updates without any change to attributes
     * Do we really need this check.
     * We may stop a storm some day. e.g Update of a unsupported attribute
     */
    if (adjRibEntry->getPreIn() &&
        (*(adjRibEntry->getPreIn()) == *preInAttrs)) {
      XLOGF(
          DBG3,
          "Ignoring announcement {} from {}. "
          "Reason: received same attributes.",
          folly::IPAddress::networkToString(prefix),
          getPeerName());
      continue;
    }

    adjRibEntry->setPreIn(preInAttrs);
    stats_.updateAttributeSizes(preInAttrs);

    processPrefixWithPolicy(
        prefix,
        pathId,
        postConfigInAttrs,
        policyActionData,
        adjRibEntry,
        withdrawnPfxPathIds,
        groupAnnouncedPrefixes);
  }

  co_await sendRibInUpdates(withdrawnPfxPathIds, groupAnnouncedPrefixes);
}

void AdjRib::promoteStaleRibInEntryIfExists(
    const folly::CIDRNetwork& prefix,
    uint32_t receivedPathId) {
  /*
   * Check for existing stale RibIn entry for this prefix/pathID for GR. If we
   * are receiving ADD-PATH as a GR helper, we cannot assume that post-restart
   * path IDs will correlate with pre-restart ones. However, if an old path ID
   * is reused, we can treat it as an update and thus need to remove it from
   * stale entries
   */
  auto stalePfxMatch = adjRibInStale_.exactMatch(prefix.first, prefix.second);
  // at least one stale entry exist for the prefix
  if (stalePathExist(stalePfxMatch)) {
    auto stalePathMatch = stalePfxMatch.value().find(receivedPathId);
    // stale path exist, move it to adjRib tree
    if (stalePathMatch != stalePfxMatch.value().end()) {
      /*
       * strange behavior can occur if the adjRibEntry we're moving for this
       * pathId actually has a different pathId, so ensure that's not the case
       */
      XCHECK(receivedPathId == stalePathMatch->second->getPathId());
      // we stale-path match, move that entry in "in" adjrib tree
      if (recAddPath_) {
        auto match = adjRibInPathTree_.exactMatch(prefix.first, prefix.second);
        bool isNew = true;
        if (match.atEnd()) {
          folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>> entry;
          entry[receivedPathId] = std::move(stalePathMatch->second);
          adjRibInPathTree_.insert(
              prefix.first, prefix.second, std::move(entry));
        } else {
          isNew = (match.value().find(receivedPathId) == match.value().end());
          match.value()[receivedPathId] = std::move(stalePathMatch->second);
        }
        if (isNew) {
          adjRibInSize_++;
          RibStats::incrAdjRibInCount();
        }
      } else {
        auto res = adjRibInLiteTree_.insert(
            prefix.first, prefix.second, std::move(stalePathMatch->second));
        if (!res.second) {
          XLOGF(
              WARNING,
              "There's an existing adjRibIn entry for stale entry for prefix {} "
              "in non receive ADD-PATH case. Discarding stale entry.",
              folly::IPAddress::networkToString(prefix));
        } else {
          adjRibInSize_++;
          RibStats::incrAdjRibInCount();
        }
      }
      stalePfxMatch.value().erase(receivedPathId);
      adjRibInStaleSize_--;
      RibStats::decrAdjRibInStaleCount(1);
      if (stalePfxMatch.value().size() == 0) {
        adjRibInStale_.erase(stalePfxMatch);
      }
    }
  }
}

void AdjRib::promoteStaleRibInEntryIfExistsInPlace(
    const folly::CIDRNetwork& prefix,
    uint32_t receivedPathId) {
  /*
   * In optimized GR mode, entries stay in place and are marked with a stale
   * bit. When we receive an update for a stale entry, we simply clear the stale
   * bit.
   */
  AdjRibEntry* adjRibEntry =
      getRibEntry(/*ingress=*/true, prefix, receivedPathId);

  if (adjRibEntry && adjRibEntry->isStale()) {
    adjRibEntry->setStale(false);
    if (staleEntryCount_ > 0) {
      staleEntryCount_--;
    }
    XLOGF(
        DBG3,
        "Promoted stale route {} pathId {} for {}",
        folly::IPAddress::networkToString(prefix),
        receivedPathId,
        getPeerName());
  }
}

folly::coro::Task<void> AdjRib::processPeerWithdrawn(
    const std::vector<nettools::bgplib::RiggedIPPrefix>& prefixes) noexcept {
  PrefixPathIds pfxPathIds;
  for (const auto& rigPrefix : prefixes) {
    auto prefix = network::toCIDRNetwork(*rigPrefix.prefix());
    uint32_t pathId = recAddPath_ && rigPrefix.pathId() ? *rigPrefix.pathId()
                                                        : kDefaultPathID;

    /*
     * We should not ever get a withdrawal for a stale path. But if peer
     * violates protocol and sends one, treat it as an expedited
     * purge: move the stale entry to AdjRibIn and process it as usual
     */
    if (enableOptimizedGR_) {
      promoteStaleRibInEntryIfExistsInPlace(prefix, pathId);
    } else {
      promoteStaleRibInEntryIfExists(prefix, pathId);
    }

    processWithdrawnPrefixRibInEntry(pfxPathIds, prefix, pathId);
  }

  // Pass to Rib
  co_await sendRibInWithdrawal(pfxPathIds);
}

void AdjRib::processWithdrawnPrefixRibInEntry(
    PrefixPathIds& pfxPathIds,
    const folly::CIDRNetwork& prefix,
    const uint32_t& pathId) noexcept {
  /*
   * We never learnt this route. Could have been AS loop and
   * we ignored those updates. Ignoring them.
   */
  auto adjRibEntry = getRibEntry(/*ingress=*/true, prefix, pathId);
  if (!adjRibEntry) {
    XLOGF(
        ERR,
        "Path not exist: {}/{}, pathId: {}",
        prefix.first.str(),
        +prefix.second,
        pathId);
    return;
  }
  XLOGF_IF(
      DBG1,
      stats_.getPreInPrefixCount() == 0,
      "Invalid received prefix count of {} from {}",
      folly::IPAddress::networkToString(prefix),
      getPeerName());
  decrementPreInPrefixCount(prefix);

  adjRibEntry->setPreIn(nullptr);
  adjRibEntry->setPostInPolicy({});
  if (adjRibEntry->getPostAttr()) {
    // Notify Rib only if this prefix was not filtered by policy
    pfxPathIds.emplace_back(prefix, pathId);
    adjRibEntry->setPostAttr(nullptr);

    // Decrement accepted prefix count
    stats_.decrementPostInPrefixCount(prefix.first.isV4());
  } else {
    XLOGF(
        DBG3,
        "Withdrawal {} from {} not notified to Rib."
        "Reason: Policy blocked announcement.",
        folly::IPAddress::networkToString(prefix),
        getPeerName());
  }
  tryDeleteRibInEntry(prefix, adjRibEntry, pathId);
}

/**
 * Process updates (new annoucements, withdraws or prefixes with attributes
 * changed.
 *
 * @param update BGP update message, parsed into thrift format.
 */
folly::coro::Task<void> AdjRib::processPeerUpdate(
    const nettools::bgplib::BgpUpdate2& update) noexcept {
  XLOGF(
      DBG3,
      "Received a BgpUpdate from {} containing: {} announced, {} withdrawn",
      getPeerName(),
      update.v4Announced2()->size() + update.mpAnnounced()->prefixes()->size(),
      update.v4Withdrawn2()->size() + update.mpWithdrawn()->prefixes()->size());
  stats_.incrementRecvUpdateMsgs();

  /*
   * Check both v4 and v6 withdrawn prefixes as v4 and v6 withdrawals can be in
   * the same update
   */
  if (!update.v4Withdrawn2()->empty() ||
      !update.mpWithdrawn()->prefixes()->empty()) {
    stats_.incrementRecvWithdrawals();
  }

  // Process v4 withdrawn prefixes.
  if (!update.v4Withdrawn2()->empty()) {
    std::vector<nettools::bgplib::RiggedIPPrefix> v4Withdrawn2{};
    for (const auto& pfx : *update.v4Withdrawn2()) {
      v4Withdrawn2.emplace_back(pfx);
    }
    co_await processPeerWithdrawn(v4Withdrawn2);
  }

  // Process v6 withdrawn prefixes.
  if (!update.mpWithdrawn()->prefixes()->empty()) {
    std::vector<nettools::bgplib::RiggedIPPrefix> mpWithdrawn{};
    for (const auto& pfx : *update.mpWithdrawn()->prefixes()) {
      mpWithdrawn.emplace_back(pfx);
    }
    co_await processPeerWithdrawn(mpWithdrawn);
  }

  if (update.v4Announced2()->empty() &&
      update.mpAnnounced()->prefixes()->empty()) {
    co_return;
  }

  // Process v4 announced prefixes.
  if (!update.v4Announced2()->empty()) {
    stats_.incrementRecvAnnouncementsIpv4();
    std::vector<nettools::bgplib::RiggedIPPrefix> v4Announced2{};
    for (const auto& pfx : *update.v4Announced2()) {
      v4Announced2.emplace_back(pfx);
    }

    // preIn attributes, should not change after this point
    auto attrs =
        std::make_shared<BgpPath>(BgpPathFields(*BgpUpdate2toBgpPathC(update)));
    attrs->publish();

    if (!validateAttributesIn(attrs, peeringParams_)) {
      co_await processPeerWithdrawn(v4Announced2);
    } else {
      co_await processPeerAnnounced(v4Announced2, attrs);
    }
  }

  // Process v6 announced prefixes.
  if (!update.mpAnnounced()->prefixes()->empty()) {
    stats_.incrementRecvAnnouncementsIpv6();
    std::vector<nettools::bgplib::RiggedIPPrefix> mpAnnounced{};
    for (const auto& pfx : *update.mpAnnounced()->prefixes()) {
      mpAnnounced.emplace_back(pfx);
    }

    // preIn attributes, should not change after this point.
    auto attrs =
        std::make_shared<BgpPath>(BgpPathFields(*BgpUpdate2toBgpPathC(update)));
    attrs->setNexthop(network::toIPAddress(*update.mpAnnounced()->nexthop()));
    attrs->publish();
    if (!validateAttributesIn(attrs, peeringParams_)) {
      co_await processPeerWithdrawn(mpAnnounced);
    } else {
      co_await processPeerAnnounced(mpAnnounced, attrs);
    }
  }
}

folly::coro::Task<void> AdjRib::processPeerEoR(
    const nettools::bgplib::BgpEndOfRib& eor) noexcept {
  /*
   * EoR is piped to AdjRib and then back to PeerManagerBase to make sure
   * AdjRib has done processing updates from this peer.
   * Count the received EoR PDU (per AFI) so the control-plane count converges
   * with the socket-layer socket_rx_eor_msgs.
   */
  stats_.incrementRecvEndOfRibMsgs();

  XLOGF(
      DBG1,
      "Received {} EoR family from {}",
      apache::thrift::util::enumNameSafe(*eor.afi()),
      getPeerName());

  if (pendingIngressEoRAfis_.erase(*eor.afi()) &&
      pendingIngressEoRAfis_.empty()) {
    XLOGF(DBG1, "All EoRs are received from peer {}", getPeerName());

    eorReceivedTime_ = nettools::bgplib::getCurrentTimeMs();
    logPeerEvent("SESSION_EOR_RECEIVED", BGP_LOG_SRC());
    stalePathTimer_.reset();

    // GR helper will cleanup stale path after receiving peer EoR
    co_await cleanupStaleRoutes(true /* GR helper */);
    fromAdjRibQ_.push({peeringParams_.vrfId, *remotePeerId_, EoR{}});
  }
}

void AdjRib::processPeerRouteRefresh(const BgpRouteRefresh& rr) noexcept {
  /*
   * Route Refresh is peer-driven and unbounded, so every log on this path is
   * rate-limited. XLOGF_EVERY_MS throttles per call site, not per peer.
   */
  XLOGF_EVERY_MS(
      INFO,
      5000,
      "Received Route Refresh from peer {} for AFI={} SAFI={}",
      remotePeerId_->str(),
      static_cast<int>(rr.afi().value()),
      static_cast<int>(rr.safi().value()));

  /*
   * Require RR (RFC 2918, cap 2). ERR (RFC 7313, cap 70) is not sufficient:
   * we do not implement ERR, so an ERR-only peer expecting a BoRR/EoRR-
   * delimited re-dump would get a bare one.
   */
  if (!isRouteRefreshNegotiated_) {
    XLOGF_EVERY_MS(
        WARN,
        5000,
        "Route Refresh from peer {} ignored: RR (RFC 2918, cap 2) not negotiated (ERR-only sessions are not served until Phase 2)",
        remotePeerId_->str());
    return;
  }

  /*
   * RFC 2918 §3: v1 is unicast only. triggerOutboundRedump validates that the
   * requested AFI was negotiated.
   */
  const auto safi = rr.safi().value();
  if (safi != nettools::bgplib::BgpUpdateSafi::SAFI_UNICAST) {
    XLOGF_EVERY_MS(
        WARN,
        5000,
        "Route Refresh from peer {} ignored: SAFI={} not supported (only UNICAST in v1)",
        remotePeerId_->str(),
        static_cast<int>(safi));
    return;
  }

  /*
   * RFC 2918 defines this octet as reserved and requires receivers to ignore
   * it. Interpret it as a subtype only when RFC 7313 was negotiated. We do not
   * yet implement BoRR/EoRR processing, so those messages are ignored.
   */
  if (isEnhancedRouteRefreshNegotiated_ &&
      rr.msgSubType().value() !=
          BgpRouteRefreshMessageSubtype::ROUTE_REFRESH_REQUEST) {
    return;
  }

  triggerOutboundRedump(rr.afi().value());
}

bool AdjRib::triggerOutboundRedump(BgpUpdateAfi afi) noexcept {
  if (!isAfiNegotiated(afi)) {
    XLOGF_EVERY_MS(
        WARN,
        5000,
        "Outbound re-dump for peer {} skipped: AFI={} not negotiated for this session",
        remotePeerId_->str(),
        static_cast<int>(afi));
    return false;
  }

  XLOGF_EVERY_MS(
      INFO,
      5000,
      "Triggering route re-announcement for peer {} AFI={}",
      remotePeerId_->str(),
      static_cast<int>(afi));
  fromAdjRibQ_.push(
      {peeringParams_.vrfId, *remotePeerId_, RouteRefreshReceived{afi}});
  return true;
}

/*
 * Update local pref for EBGP routes. Will create new BgpPath if needed
 * else returns the passed attrs unchanged
 */
std::shared_ptr<BgpPath> AdjRib::updateAttributesIn(
    const std::shared_ptr<BgpPath>& attrs) noexcept {
  /*
   * apply per-peer-config for ReceiveLBW
   * lbw-policy is applied afterwards, which takes precedence over
   * per-peer-config
   */
  auto attrsLbw = attrs->clone();
  updateReceiveLbwExtCommunity(attrsLbw);
  attrsLbw->publish();

  // iBGP peer
  if (isIBgpPeer()) {
    return attrsLbw; // No change in attributes
  }

  // Confed eBGP peer
  if (isConfedEBgpPeer()) {
    if (attrsLbw->getLocalPref().has_value()) {
      return attrsLbw; // No change in attributes
    }

    CHECK(attrsLbw->isPublished());

    // set to default localPref if localpref does not exists
    auto newAttrs = attrsLbw->clone();
    newAttrs->setLocalPref(facebook::bgp::kDefaultLocalPref);
    newAttrs->publish();

    return newAttrs; // Attributes got changed
  }

  // eBGP peer
  CHECK(attrsLbw->isPublished());

  // clone attributes
  auto newAttrs = attrsLbw->clone();

  // set to default localPref
  newAttrs->setLocalPref(facebook::bgp::kDefaultLocalPref);

  // strip originatorId and clusterList
  newAttrs->setOriginatorId(0);
  newAttrs->setClusterList({});

  newAttrs->publish();
  return newAttrs; // Attributes got changed
}

/*
 * Method to mark all routes learnt from a peer as stale
 * TODO: We may want to later break it down as we won't be
 *       able to iterate all routes in one loop. For RSW scales
 *       tight loop should be fine.
 */
void AdjRib::markLearntRoutesStale() noexcept {
  XLOGF(INFO, "Mark learnt routes stale for {}", getPeerName());

  RibStats::decrAdjRibInStaleCount(adjRibInStaleSize_);
  adjRibInStaleSize_ = 0;
  adjRibInStale_.clear();
  if (recAddPath_) {
    for (auto itr = adjRibInPathTree_.begin(); itr != adjRibInPathTree_.end();
         itr++) {
      /*
       * create empty entry map ( received pathID -> AdjRibEntry ) for prefix in
       * adjRibInStale_
       */
      folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>> entry;
      auto match = adjRibInStale_
                       .insert(itr.ipAddress(), itr.masklen(), std::move(entry))
                       .first;
      // iterate through (pathId -> AdjRibEntry) in adjRibInPathTree_
      for (auto& [pathId, adjRibEntry] : itr->value()) {
        if (!adjRibEntry->getPreIn()) {
          continue;
        }
        if (match.value().emplace(pathId, std::move(adjRibEntry)).second) {
          adjRibInStaleSize_++;
          RibStats::incrAdjRibInStaleCount();
        }
      }
      itr.value().clear();
    }
    RibStats::decrAdjRibInCount(adjRibInSize_);
    adjRibInSize_ = 0;
    adjRibInPathTree_.clear();
  } else {
    for (auto itr = adjRibInLiteTree_.begin(); itr != adjRibInLiteTree_.end();
         itr++) {
      /*
       * create empty entry map ( received pathID -> AdjRibEntry ) for prefix in
       * adjRibInStale_
       */
      folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>> entry;
      auto match = adjRibInStale_
                       .insert(itr.ipAddress(), itr.masklen(), std::move(entry))
                       .first;
      auto& adjRibEntry = itr->value();
      if (!adjRibEntry->getPreIn()) {
        continue;
      }
      auto pathId = adjRibEntry->getPathId();
      if (match.value().emplace(pathId, std::move(adjRibEntry)).second) {
        adjRibInStaleSize_++;
        RibStats::incrAdjRibInStaleCount();
      }
    }
    RibStats::decrAdjRibInCount(adjRibInSize_);
    adjRibInSize_ = 0;
    adjRibInLiteTree_.clear();
  }
}

/*
 * Method to mark all routes learnt from a peer as stale in-place (optimized GR)
 * Instead of moving entries to a separate stale tree, marks them with a stale
 * bit and increments the stale entry counter.
 */
void AdjRib::markLearntRoutesStaleInPlace() noexcept {
  XLOGF(INFO, "Mark learnt routes stale in-place for {}", getPeerName());

  staleEntryCount_ = 0;

  auto markEntryStale = [this](AdjRibEntry* adjRibEntry) {
    if (!adjRibEntry->getPreIn() || adjRibEntry->isStale()) {
      return;
    }
    adjRibEntry->setStale(true);
    staleEntryCount_++;
  };

  if (recAddPath_) {
    for (auto& node : adjRibInPathTree_) {
      for (auto& [pathId, adjRibEntry] : node.value()) {
        markEntryStale(adjRibEntry.get());
      }
    }
  } else {
    for (auto& node : adjRibInLiteTree_) {
      markEntryStale(node.value().get());
    }
  }

  XLOGF(
      INFO,
      "Marked {} routes as stale in-place for {}",
      staleEntryCount_,
      getPeerName());
}

std::optional<RibInWithdrawal> AdjRib::collectStaleRoutes(
    bool isGrHelperMode) noexcept {
  // I wish RadixTree supported erasing while iterating
  PrefixPathIds pfxPathIds;
  for (auto itr = adjRibInStale_.begin(); itr != adjRibInStale_.end(); itr++) {
    const folly::CIDRNetwork prefix = {itr.ipAddress(), itr.masklen()};
    for (const auto& [pathId, adjRibEntry] : itr->value()) {
      XCHECK(pathId == adjRibEntry->getPathId());
      XLOGF_IF(
          DBG1,
          stats_.getPreInPrefixCount() == 0,
          "Invalid received prefix count of {} from {}",
          folly::IPAddress::networkToString(prefix),
          getPeerName());
      decrementPreInPrefixCount(prefix);
      if (adjRibEntry->getPostAttr()) {
        stats_.decrementPostInPrefixCount(prefix.first.isV4());
      }
      pfxPathIds.emplace_back(prefix, adjRibEntry->getPathId());
    }
  }

  // clear out stale routes cache
  RibStats::decrAdjRibInStaleCount(adjRibInStaleSize_);
  adjRibInStaleSize_ = 0;
  adjRibInStale_.clear();

  if (pfxPathIds.empty()) {
    return std::nullopt;
  }

  XLOGF_IF(
      INFO,
      isGrHelperMode,
      "Peer {}: Purge {} stale routes",
      getPeerName(),
      pfxPathIds.size());

  return RibInWithdrawal(
      TinyPeerInfo(
          peeringParams_.peerAddr,
          getRemoteAs(),
          remotePeerId_->remoteBgpId,
          getBgpSessionType(),
          peeringParams_.isRrClient,
          peeringParams_.isRedistributePeer,
          peeringParams_.linkBandwidthBps,
          peeringParams_.description),
      pfxPathIds);
}

folly::coro::Task<void> AdjRib::pushStaleWithdrawal(
    RibInWithdrawal withdrawal) noexcept {
  co_await ribInQ_.nonCancellablePush(std::move(withdrawal));
}

void AdjRib::schedulePendingRibInPush(RibInWithdrawal withdrawal) noexcept {
  // Trim completed entries
  pendingRibInPushes_.erase(
      std::remove_if(
          pendingRibInPushes_.begin(),
          pendingRibInPushes_.end(),
          [](auto& f) { return f.isReady(); }),
      pendingRibInPushes_.end());
  /*
   * Tracked in pendingRibInPushes_ and drained in stop() before AdjRib
   * destruction so the `this` capture stays valid.
   */
  pendingRibInPushes_.push_back(
      co_withExecutor(&evb_, pushStaleWithdrawal(std::move(withdrawal)))
          .start());
}

folly::coro::Task<void> AdjRib::cleanupStaleRoutes(
    bool isGrHelperMode) noexcept {
  if (auto withdrawal = collectStaleRoutes(isGrHelperMode)) {
    co_await pushStaleWithdrawal(std::move(*withdrawal));
  }
}

/*
 * Clean up stale routes in-place for optimized GR
 * Uses a two-pass approach since RadixTree doesn't support deletion during
 * iteration:
 * Pass 1: Collect all stale entries (prefix + pathId)
 * Pass 2: Process deletions and clear entries
 */
folly::coro::Task<void> AdjRib::cleanupStaleRoutesInPlace(
    bool isGrHelperMode) noexcept {
  // Early exit if no stale entries exist
  if (staleEntryCount_ == 0) {
    XLOGF(
        DBG3,
        "No stale entries to cleanup for {} (optimized GR)",
        getPeerName());
    co_return;
  }

  XLOGF(
      INFO,
      "Cleaning up {} stale routes in-place for {} (optimized GR)",
      staleEntryCount_,
      getPeerName());

  /*
   * Collect stale entries (prefix, pathId) pairs
   * Reserve approximate capacity to reduce allocations
   */
  PrefixPathIds stalePfxPathIds;
  stalePfxPathIds.reserve(staleEntryCount_);

  // Lambda to collect stale entries
  auto collectStaleEntry = [&stalePfxPathIds](
                               const folly::CIDRNetwork& prefix,
                               uint32_t pathId,
                               AdjRibEntry* adjRibEntry) {
    if (adjRibEntry && adjRibEntry->isStale() && adjRibEntry->getPreIn()) {
      XCHECK(pathId == adjRibEntry->getPathId());
      stalePfxPathIds.emplace_back(prefix, pathId);
    }
  };

  // Pass 1: Iterate and collect stale entries
  if (recAddPath_) {
    for (auto& node : adjRibInPathTree_) {
      const folly::CIDRNetwork prefix = {node.ipAddress(), node.masklen()};
      for (auto& [pathId, adjRibEntry] : node.value()) {
        collectStaleEntry(prefix, pathId, adjRibEntry.get());
      }
    }
  } else {
    for (auto& node : adjRibInLiteTree_) {
      const folly::CIDRNetwork prefix = {node.ipAddress(), node.masklen()};
      auto& adjRibEntry = node.value();
      collectStaleEntry(prefix, adjRibEntry->getPathId(), adjRibEntry.get());
    }
  }

  XLOGF_IF(
      INFO,
      isGrHelperMode,
      "Peer {}: Purge {} stale routes (optimized GR)",
      getPeerName(),
      stalePfxPathIds.size());

  // Pass 2: Process deletions and clear entries
  for (const auto& [prefix, pathId] : stalePfxPathIds) {
    // Get the entry and clear it
    auto adjRibEntry = getRibEntry(/*ingress=*/true, prefix, pathId);
    if (!adjRibEntry) {
      continue;
    }

    if (stats_.getPreInPrefixCount() == 0) {
      XLOGF(
          DBG1,
          "Invalid received prefix count of {} from {}",
          folly::IPAddress::networkToString(prefix),
          getPeerName());
    }

    decrementPreInPrefixCount(prefix);
    if (adjRibEntry->getPostAttr()) {
      stats_.decrementPostInPrefixCount(prefix.first.isV4());
    }

    // Delete the entry from the tree
    deleteRibEntry(/*ingress=*/true, prefix, pathId);
  }

  // Send withdrawals for all stale entries
  RibInWithdrawal withdrawal(
      TinyPeerInfo(
          peeringParams_.peerAddr,
          getRemoteAs(),
          remotePeerId_->remoteBgpId,
          getBgpSessionType(),
          peeringParams_.isRrClient,
          peeringParams_.isRedistributePeer,
          peeringParams_.linkBandwidthBps,
          peeringParams_.description),
      stalePfxPathIds);
  co_await ribInQ_.nonCancellablePush(std::move(withdrawal));

  // Reset stale entry count
  staleEntryCount_ = 0;

  XLOGF(
      INFO,
      "Completed cleanup of stale routes in-place for {} (optimized GR)",
      getPeerName());
}

/*
 * Try to delete AdjRibEntry if there is no longer any interest for this prefix
 * i.e. we have withdrawn it and no one is advertising this prefix
 */
void AdjRib::tryDeleteRibInEntry(
    const folly::CIDRNetwork& prefix,
    const AdjRibEntry* adjRibEntry,
    uint32_t pathId) noexcept {
  if (adjRibEntry->getPreIn() || adjRibEntry->getPostAttr()) {
    return;
  }

  deleteRibEntry(/*ingress=*/true, prefix, pathId);
}

const AdjRibEntry* FOLLY_NULLABLE AdjRib::getStaleRibInEntry(
    const folly::CIDRNetwork& prefix,
    uint32_t pathId) noexcept {
  auto match = adjRibInStale_.exactMatch(prefix.first, prefix.second);
  if (match.atEnd() || match.value().find(pathId) == match.value().end()) {
    return nullptr;
  }
  return match.value().find(pathId)->second.get();
}

bool AdjRib::allowGoldenVip(const folly::CIDRNetwork& network) const {
  /*
   * if the golden VIP already exists, accept the preix, otherwise check whether
   * having this new prefix will exceeds Golden VIP limit
   */
  const auto [prefixExists, refCount] =
      AdjRibPrefixSet::get()->getRefCount(network);
  auto goldenVipLimit = switchLimitConfig_
      ? switchLimitConfig_->max_golden_vips().value_or(0)
      : 0;
  if ((prefixExists && refCount.isGoldenVip_) ||
      (goldenVipLimit &&
       (AdjRibPrefixSet::get()->goldenVipSize() <=
        goldenVipLimit - 1) /* under golden vip limit */)) {
    return true;
  }
  return false;
}

void AdjRib::triggerSafeMode() noexcept {
  if (isSafeModeOn()) {
    return;
  }
  /*
   * Set safe mode on this peer's AdjRib and send TriggerSafeMode to
   * PeerManagerBase that saves a safe mode file and initiates the rest
   * of the safe mode purging process.
   * See http://fburl.com/bgp_safe_mode for more details
   */
  setSafeModeOn();
  XLOG(INFO, "Prefixes/paths over limit. Safe mode on.");
  BgpStats::setIsSafeModeOn(true);
  fromAdjRibQ_.push({peeringParams_.vrfId, *remotePeerId_, TriggerSafeMode{}});
}

bool AdjRib::canApplyGoldenPrefixPolicyMode() const noexcept {
  return switchLimitConfig_ &&
      *switchLimitConfig_->overload_protection_mode() ==
      thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY;
}

bool AdjRib::dropPrefixForOverloadProtection(
    uint64_t totalPathCount,
    const folly::CIDRNetwork& network,
    const BgpPath& attrs,
    const std::shared_ptr<thrift::BgpSwitchLimitConfig>& switchLimitConfig) {
  if (!switchLimitConfig) {
    // Skip the limit check if no config provided.
    return false;
  }

  const auto& config = *switchLimitConfig;

  // Check if we've exceeded any of the configured switch prefix limits.
  bool exceededSwitchPrefixLimit = false;
  if (const auto totalPathLimit = config.total_path_limit()) {
    if (totalPathCount > *totalPathLimit) {
      /*
       * Switch level total path limit is reached. Reject update and log every 5
       * seconds.
       */
      XLOGF_EVERY_MS(
          ERR,
          5000,
          "Total path received: {} exceeds max limit {}",
          totalPathCount,
          *totalPathLimit);
      exceededSwitchPrefixLimit = true;
    }
  }
  if (const auto prefixLimit = config.prefix_limit()) {
    if (isOverPrefixLimit(network, *prefixLimit)) {
      // Switch level unique prefix limit is reached.
      exceededSwitchPrefixLimit = true;
    }
  }

  if (exceededSwitchPrefixLimit) {
    XLOGF_EVERY_MS(
        INFO,
        3600000 /* 1 hr */,
        "Using overload protection mode {}.",
        *config.overload_protection_mode() ==
                thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY
            ? "APPLY_GOLDEN_PREFIX_POLICY"
            : "DROP_EXCESS_PREFIXES");
  }

  switch (*config.overload_protection_mode()) {
    case thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY:
      if (exceededSwitchPrefixLimit) {
        triggerSafeMode();
      }
      if (isSafeModeOn() && goldenPrefixPolicy_) {
        // apply golden prefix policy
        if (!goldenPrefixPolicy_->allowPrefix(network, attrs)) {
          // disallowed by goldenPrefixPolicies, try golden VIPs
          if (isGoldenVip(attrs.getCommunities().get())) {
            return !allowGoldenVip(network);
          }
          return true; // drop the prefix
        }
      }
      // Still need to check switch prefix limits.
      [[fallthrough]];
    case thrift::OverloadProtectionMode::DROP_EXCESS_PREFIXES:
    default:
      return exceededSwitchPrefixLimit;
  }
}

bool AdjRib::isOverPrefixLimit(
    const folly::CIDRNetwork& network,
    const uint64_t limit) {
  auto prefixSet = AdjRibPrefixSet::get();
  if (!prefixSet) {
    // defensive check to make sure no nullptr access
    return false;
  }

  const auto size = prefixSet->size();
  if (size > limit) {
    // over the limit. Reject update.
    XLOGF_EVERY_MS(
        ERR,
        5000,
        "Total unique prefix size: {} exceeds the max limit {}. Stop processing.",
        size,
        limit);

    return true;
  } else if (size == limit) {
    // at the limit. Need to check if this is an existing prefix.
    const auto refCount = prefixSet->getRefCount(network);
    if (refCount.first) {
      // this is an exsiting prefix. Prefix set size won't increment.
      return false;
    }

    XLOGF_EVERY_MS(
        ERR,
        5000,
        "Total unique prefix size: {} reaches max limit {}. Stop processing.",
        size,
        limit);
    return true;
  }

  // size < limit. Good to go.
  return false;
}

bool AdjRib::capRoutesPerPeer(uint64_t routeCnt, bool isPrefilter) {
  const auto& filter = isPrefilter ? peeringParams_.preRouteLimit
                                   : peeringParams_.postRouteLimit;
  const auto filterStr = isPrefilter ? "Prefilter" : "Postfilter";

  // if user did not specify filters we will not do any further action
  if (!filter.has_value()) {
    return false;
  }

  // if warning threshold is set and we are at warning limit
  if ((*filter->warning_limit() != 0) && routeCnt >= *filter->warning_limit()) {
    XLOGF(
        WARNING,
        "{} prefixes received from peer {} exceed {} warning limit {} ",
        routeCnt,
        getPeerName(),
        filterStr,
        *filter->warning_limit());
  }

  // routeLimit == 0 means we allow unlimited number of routes for certain peer
  if (*filter->max_routes() == 0 || routeCnt <= *filter->max_routes()) {
    return false;
  }

  // routeCnt > filter->max_routes
  XLOGF(
      ERR,
      "{} prefixes received from peer {} exceed {} max limit {} ",
      routeCnt,
      getPeerName(),
      filterStr,
      *filter->max_routes());

  // with warning-only we will reject route but not tear down bgp session
  if (*filter->warning_only()) {
    return true;
  }

  // we will tear down the session if warning only is not set
  fromAdjRibQ_.push({peeringParams_.vrfId, *remotePeerId_, Shutdown{}});
  XLOGF(
      ERR,
      "Tearing down peer {}, because of exceeding {} max limit {} and warning only is not set",
      getPeerName(),
      filterStr,
      *filter->max_routes());

  return true;
}

bool AdjRib::validateAttributesIn(
    const std::shared_ptr<const facebook::bgp::BgpPath>& attrs,
    const facebook::bgp::PeeringParams& params) {
  // reject IBGP update with no local preference
  if (isIBgpPeer() && !attrs->getLocalPref().has_value()) {
    XLOGF(
        ERR,
        "IBGP update does not have Local Preference set from {}.",
        getPeerName());
    return false;
  }

  if (hasAsPathLoop(attrs, params)) {
    XLOGF(ERR, "As path loop detected in update from {}.", getPeerName());
    return false;
  }

  if (hasRRLoop(attrs, params)) {
    XLOGF(
        ERR, "Route reflector loop detected in update from {}.", getPeerName());
    return false;
  }

  auto asPathValid = validateAsPath(attrs, isConfedEBgpPeer(), isEBgpPeer());
  if (asPathValid.hasError()) {
    XLOGF(
        ERR,
        "Malformed as path in update from {}: {} ",
        getPeerName(),
        asPathValid.error());
    auto notification = nettools::bgplib::buildBgpNotification(
        nettools::bgplib::BgpNotifErrCode::BN_UPDATE_MSG_ERR,
        static_cast<uint16_t>(nettools::bgplib::BgpNotifUpdateMsgErrSubCode::
                                  BN_UM_MALFORMED_AS_PATH),
        "Malformed AS_PATH update error Notification",
        "");
    // UPDATE Message Error Handling
    /*
     * We guarantee space for notification messages because no other
     * producers may write above the high watermark, so we directly
     * write here.
     */
    adjRibOutQueue_->push(std::move(notification));
    return false;
  }

  if (params.enforceFirstAs &&
      !validateEnforceFirstAs(attrs, getRemoteAs(), isIBgpPeer())) {
    stats_.incrementEnforceFirstAsRejects();
    XLOGF(
        ERR,
        "Enforce-first-as validation failed for update from {}.",
        getPeerName());
    return false;
  }

  return true;
}

void AdjRib::applyGoldenPrefixPolicy(
    const folly::CIDRNetwork& prefix,
    const std::unique_ptr<AdjRibEntry>& adjRibEntry,
    folly::F14FastMap<folly::CIDRNetwork, uint32_t>& prefixesPathIdToPurge) {
  if (!adjRibEntry->getPreIn()) {
    return;
  }
  // Check if prefix is allowed by golden prefix policy
  if (goldenPrefixPolicy_->allowPrefix(prefix, *adjRibEntry->getPreIn())) {
    // Count subnet to make sure we stay under the limit
    goldenPrefixPolicy_->incrementSubnet(prefix);
    return;
  }

  if (isGoldenVip(adjRibEntry->getPreIn()->getCommunities().get()) &&
      allowGoldenVip(prefix)) {
    // this VIP should already in AdjRibPrefixSet, set it as golden VIP
    AdjRibPrefixSet::get()->markGoldenVip(prefix);
    return;
  }
  /*
   * Prefix is not allowed by golden prefix policy or golden vip limit, populate
   * prefixesPathIdToPurge with the prefix and pathId
   */
  prefixesPathIdToPurge.emplace(prefix, adjRibEntry->getPathId());
}

bool AdjRib::blockedByIngressRouteFilter(
    const folly::CIDRNetwork& prefix) const {
  // apply ingress route filtering if set
  if (!routeFilterStmt_) {
    return false;
  }
  const auto& [allowedPrefixes, rejectedPrefixes] =
      routeFilterStmt_->applyIngressFilter({prefix});

  /**
   * Ingress filter behavior based on permissive_mode:
   *
   * | Permissive  | filterBlockedPrefix | allowedPrefixes | rejectedPrefixes |
   * |-------------|---------------------|-----------------|------------------|
   * | True        | True                | Populated       | Populated        |
   * | True        | False               | Populated       | Empty            |
   * | False       | True                | Empty           | Populated        |
   * | False       | False               | Populated       | Empty            |
   *
   * if permissive_mode is set to true, and if a prefix is rejected it will be
   * populated both in allowed and rejected for logging purposes. the prefix is
   * not actually blocked in this case.
   */
  if (allowedPrefixes.empty()) {
    if (routeFilterLogger_) {
      routeFilterLogger_->log(
          false /* egress */,
          prefix,
          false /* allow */,
          false /* permissive */,
          {} /* no communities */);
    }
    XLOGF(
        DBG2,
        "Ingress Route Filter Policy blocked prefix {} from {}. ",
        folly::IPAddress::networkToString(prefix),
        getPeerName());
    // blocked
    return true;
  }
  if (!rejectedPrefixes.empty()) {
    // permissive mode where blocked prefixes are logged but not dropped
    if (routeFilterLogger_) {
      routeFilterLogger_->log(
          false /* egress */,
          prefix,
          true /* allow */,
          true /* permissive */,
          {} /* no communities */);
    }
    XLOGF(
        WARNING,
        "Ingress Route Filter Policy permissive-allowed prefix {} from {}. ",
        folly::IPAddress::networkToString(prefix),
        getPeerName());
  }
  return false;
}

folly::coro::Task<void> AdjRib::processAdjRibReEvaluationForSafeMode() {
  if (!goldenPrefixPolicy_) {
    XLOG(WARNING, "No golden prefix policy found. Skipping re-evaluation.");
    co_return;
  }
  PrefixPathIds pfxPathIds;
  folly::F14FastMap<folly::CIDRNetwork, uint32_t> prefixesPathIdToPurge;
  if (recAddPath_) {
    // If recAddPath_ is true, iterate through all prefixes in adjRibInPathTree_
    for (auto itr = adjRibInPathTree_.begin(); itr != adjRibInPathTree_.end();
         itr++) {
      const folly::CIDRNetwork prefix = {itr.ipAddress(), itr.masklen()};
      for (const auto& [_, adjRibEntry] : itr->value()) {
        applyGoldenPrefixPolicy(prefix, adjRibEntry, prefixesPathIdToPurge);
      }
    }
  } else {
    /*
     * If recAddPath_ is false, iterate through all prefixes in
     * adjRibInLiteTree_
     */
    for (auto itr = adjRibInLiteTree_.begin(); itr != adjRibInLiteTree_.end();
         itr++) {
      const folly::CIDRNetwork prefix = {itr.ipAddress(), itr.masklen()};
      applyGoldenPrefixPolicy(prefix, itr->value(), prefixesPathIdToPurge);
    }
  }
  for (const auto& [prefix, pathId] : prefixesPathIdToPurge) {
    // Purge adjRibEntry and collect prefixes to withdraw
    processWithdrawnPrefixRibInEntry(pfxPathIds, prefix, pathId);
  }
  incrementPrefixesDroppedByLimit(pfxPathIds.size());
  // Send RibInWithdrawal
  co_await sendRibInWithdrawal(pfxPathIds);
}

template <typename Coro>
folly::coro::Task<void> AdjRib::forEachAdjRibInEntry(
    Coro&& coroutine,
    bool evaluateStale) {
  if (evaluateStale) {
    // Iterate through stale entries in adjRibInStale_
    for (auto itr = adjRibInStale_.begin(); itr != adjRibInStale_.end();
         itr++) {
      const folly::CIDRNetwork prefix = {itr.ipAddress(), itr.masklen()};
      for (const auto& [pathId, adjRibEntry] : itr->value()) {
        if (!adjRibEntry->getPreIn()) {
          continue;
        }
        co_await coroutine(prefix, pathId, adjRibEntry.get());
      }
    }
  } else {
    // Iterate through active entries
    if (recAddPath_) {
      /*
       * If recAddPath_ is true, iterate through all prefixes in
       * adjRibInPathTree_
       */
      for (auto itr = adjRibInPathTree_.begin(); itr != adjRibInPathTree_.end();
           itr++) {
        const folly::CIDRNetwork prefix = {itr.ipAddress(), itr.masklen()};
        for (const auto& [pathId, adjRibEntry] : itr->value()) {
          if (!adjRibEntry->getPreIn()) {
            continue;
          }
          co_await coroutine(prefix, pathId, adjRibEntry.get());
        }
      }
    } else {
      /*
       * If recAddPath_ is false, iterate through all prefixes in
       * adjRibInLiteTree_
       */
      for (auto itr = adjRibInLiteTree_.begin(); itr != adjRibInLiteTree_.end();
           itr++) {
        const folly::CIDRNetwork prefix = {itr.ipAddress(), itr.masklen()};
        auto& adjRibEntry = itr->value();

        if (!adjRibEntry->getPreIn()) {
          continue;
        }

        auto pathId = adjRibEntry->getPathId();
        co_await coroutine(prefix, pathId, adjRibEntry.get());
      }
    }
  }
}

folly::coro::Task<void> AdjRib::reEvaluateAdjRibEntriesWithUpdatedPolicy(
    bool evaluateStale) {
  PrefixPathIds withdrawnPfxPathIds;
  folly::F14NodeMap<std::shared_ptr<const BgpPath>, PrefixPathIds>
      groupAnnouncedPrefixes;

  /*
   * Counter to track announced prefixes directly instead of iterating every
   * time
   */
  uint32_t announcedPrefixPathIdCount = 0;

  // TODO: Add check for RibInQ backpressure before starting re-evaluation

  co_await forEachAdjRibInEntry(
      [&](const folly::CIDRNetwork& prefix,
          uint32_t pathId,
          AdjRibEntry* adjRibEntry) -> folly::coro::Task<void> {
        // Re-evaluate policy for this prefix
        auto preInAttrs = adjRibEntry->getPreIn();
        auto postConfigInAttrs =
            updateAttributesIn(std::const_pointer_cast<BgpPath>(preInAttrs));
        auto policyActionData = createPolicyActionData(adjRibEntry->getPreIn());

        announcedPrefixPathIdCount += processPrefixWithPolicy(
            prefix,
            pathId,
            postConfigInAttrs,
            policyActionData,
            adjRibEntry,
            withdrawnPfxPathIds,
            groupAnnouncedPrefixes);

        // Send batch when batch size reached for withdrawals and announcements
        if ((withdrawnPfxPathIds.size() + announcedPrefixPathIdCount) >=
            kPolicyReEvaluationBatchSize) {
          co_await sendRibInUpdates(
              withdrawnPfxPathIds, groupAnnouncedPrefixes);

          // Clear for next batch and reset counter
          withdrawnPfxPathIds.clear();
          groupAnnouncedPrefixes.clear();
          announcedPrefixPathIdCount = 0;
        }
        co_return;
      },
      evaluateStale);

  // Send any remaining updates
  if ((withdrawnPfxPathIds.size() + announcedPrefixPathIdCount) > 0) {
    co_await sendRibInUpdates(withdrawnPfxPathIds, groupAnnouncedPrefixes);
  }
}

folly::coro::Task<void> AdjRib::processAdjRibReEvaluationForPolicyChange() {
  /*
   * Acquire semaphore to block peer message processing
   * SCOPE_EXIT guarantees release even if an exception is thrown,
   * using an internal RAII guard that invokes the lambda on destruction.
   */
  co_await waitForTreeAccessSemaphore();
  SCOPE_EXIT {
    signalTreeAccessSemaphore();
  };

  // First process regular (active) entries
  co_await reEvaluateAdjRibEntriesWithUpdatedPolicy();

  /*
   * Then process stale entries
   * TODO: Remove evaluateStale once stale prefixes are identified by
   * a bit in the existing tree instead of using a separate tree
   * adjRibInStale_.
   */
  co_await reEvaluateAdjRibEntriesWithUpdatedPolicy(true /* evaluateStale */);
}

folly::coro::Task<void> AdjRib::waitForTreeAccessSemaphore() {
  if (enableDynamicPolicyEvaluation_) {
    co_await treeAccessSemaphore_->co_wait();
  }
  co_return;
}

void AdjRib::signalTreeAccessSemaphore() {
  if (enableDynamicPolicyEvaluation_) {
    treeAccessSemaphore_->signal();
  }
}

folly::coro::Task<void> AdjRib::processAdjRibReEvaluation(
    RibPauseResumeCause cause) {
  switch (cause) {
    case RibPauseResumeCause::SAFE_MODE:
      XLOGF(
          INFO,
          "Processing AdjRib re-evaluation for safe mode for {}",
          getPeerName());
      co_await processAdjRibReEvaluationForSafeMode();
      break;

    case RibPauseResumeCause::ROUTE_FILTER_POLICY_UPDATE:
    case RibPauseResumeCause::ROUTING_POLICY_UPDATE:
      XLOGF(
          INFO,
          "Processing AdjRib re-evaluation for policy change for {}",
          getPeerName());
      co_await processAdjRibReEvaluationForPolicyChange();
      break;

    default:
      XLOGF(
          ERR,
          "Unknown RibPauseResumeCause: {} for policy re-evaluation for {}",
          magic_enum::enum_name(cause),
          getPeerName());
      break;
  }
}

} // namespace facebook::bgp
