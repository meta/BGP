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

#include "common/network/AddressUtil.h"
#include "neteng/fboss/bgp/cpp/BgpProfiler.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibCommon.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibGroup.h"
#include "neteng/fboss/bgp/cpp/adjrib/LegacyV4NlriEncoding.h"
#include "neteng/fboss/bgp/cpp/adjrib/WellKnownCommunityFilter.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

using namespace facebook::nettools::bgplib;

DEFINE_bool(
    enable_rib_path_data_exporting,
    false,
    "Enable RibPathData exporting to thrift stream subscriber (e.g. MpBgpMonitor)");

namespace facebook::bgp {

/*
 * @brief  process shadow rib entries as they are consumed from
 *         changeList. Every adjRib instance when it consumes
 *         from changeList would go through this function.
 *
 * @param  srEntry - shadow Rib entry consumed from the changeList
 *
 * @return void
 */
void AdjRib::processShadowRibEntryChange(ShadowRibEntry& srEntry) noexcept {
  /*
   * The adjRib is activated to consume from changeListTracker immediately
   * as RibDumpReq sends EoR. However, it is possible that
   */
  if (!egressEoRsSent_) {
    XLOGF(
        DBG1,
        "Ignoring changeList announcement for {} before sending initial dump",
        getPeerName());
    return;
  }

  /*
   * Track how caught up this peer is on the change list. Every consumed entry
   * -- announcement or withdrawal -- advances the peer's RIB version, so set it
   * once here (mirroring AdjRibOutGroup::processShadowRibEntryChange) rather
   * than per-branch. The version-gated rejoin check relies on this being
   * accurate; handleRibAnnouncedEntry additionally sets it on the direct
   * RIB-dump path (processRibMessage).
   */
  setLastSeenRibVersion(srEntry.ribVersion);

  if (sendAddPath_) {
    for (const auto& [_, multipath] : srEntry.multipaths) {
      if (!multipath) {
        continue;
      }
      if (isShadowRibRouteInUpdate(multipath->flags)) {
        RibOutAnnouncementEntry entry(
            srEntry.prefix,
            multipath->pathIdToSend,
            multipath->peer,
            multipath->attrs,
            srEntry.switchId,
            srEntry.multiPathSize,
            srEntry.aggregateReceivedUcmpWeight,
            srEntry.aggregateLocalUcmpWeight,
            srEntry.ribPolicyUcmpWeight);
        entry.newlyInstalledInLocalRib = srEntry.newlyInstalledInLocalRib;
        entry.installTimeStamp = srEntry.installTimeStamp;
        entry.ribVersion = srEntry.ribVersion;
        entry.isPartialDrain = multipath->isPartialDrain;
        handleRibAnnouncedEntry(entry, false);
      } else if (isShadowRibRouteInWithdraw(multipath->flags)) {
        /*
         * Withdrawal announcements from RIB don't have attrs, however
         * when we are processing withdrawal here, it is a priori
         * shadow RIB entry that was existing with attrs in place (the
         * entry yet has not been reset for multipath) and hence it
         * should have attrs available to get the nexthop value
         *
         * Thus, we would never expect nullptr to attrs here. The check
         * here is simply a safety check
         */
        if (multipath->attrs) {
          // TODO: we don't need a full RibOutWithdrawal entry here, do we?
          RibOutWithdrawalEntry entry(
              srEntry.prefix,
              multipath->pathIdToSend,
              multipath->attrs->getNexthop());
          uint32_t pathId = resolvePathId(
              entry.prefix,
              entry.nh.value_or(folly::IPAddress()),
              entry.pathIdToSend);
          processRibWithdraw(entry.prefix, pathId);
        } else {
          XLOGF(
              ERR,
              "Unexpected null attr for multipath when processing "
              "withdrawal for shadowrib entry. prefix {}, peer {}",
              folly::IPAddress::networkToString(srEntry.prefix),
              getPeerName());
        }
      }
    }
  } else {
    if (!srEntry.bestpath) {
      return;
    }
    if (isShadowRibRouteInUpdate(srEntry.bestpath->flags)) {
      RibOutAnnouncementEntry entry(
          srEntry.prefix,
          kDefaultPathID,
          srEntry.bestpath->peer,
          srEntry.bestpath->attrs,
          srEntry.switchId,
          srEntry.multiPathSize,
          srEntry.aggregateReceivedUcmpWeight,
          srEntry.aggregateLocalUcmpWeight,
          srEntry.ribPolicyUcmpWeight);
      entry.newlyInstalledInLocalRib = srEntry.newlyInstalledInLocalRib;
      entry.installTimeStamp = srEntry.installTimeStamp;
      entry.ribVersion = srEntry.ribVersion;
      entry.isPartialDrain = srEntry.bestpath->isPartialDrain;
      handleRibAnnouncedEntry(entry, false);
    } else if (isShadowRibRouteInWithdraw(srEntry.bestpath->flags)) {
      /*
       * Withdrawal announcements from RIB don't have attrs, however
       * when we are processing withdrawal here, it is a priori
       * shadow RIB entry that was existing with attrs in place (the
       * entry yet has not been reset for bestpath) and hence it
       * should have attrs available to get the nexthop value
       *
       * Thus, we would never expect nullptr to attrs here. The check
       * here is simply a safety check
       */
      if (srEntry.bestpath->attrs) {
        // TODO: we don't need a full RibOutWithdrawal entry here, do we?
        RibOutWithdrawalEntry entry(
            srEntry.prefix,
            kDefaultPathID,
            srEntry.bestpath->attrs->getNexthop());
        uint32_t pathId = resolvePathId(
            entry.prefix,
            entry.nh.value_or(folly::IPAddress()),
            entry.pathIdToSend);
        processRibWithdraw(entry.prefix, pathId);
      } else {
        XLOGF(
            ERR,
            "Unexpected null attr for bestpath when processing "
            "withdrawal for shadowrib entry. prefix {}, peer {}",
            folly::IPAddress::networkToString(srEntry.prefix),
            getPeerName());
      }
    }
  }

  return;
}

void AdjRib::processRibMessage(const RibOutMessage& ribMsg) noexcept {
  folly::variant_match(
      ribMsg,
      [this](const RibOutAnnouncement& announcement) {
        processRibOutAnnouncement(announcement);
      },
      [this](const RibOutWithdrawal& withdrawal) {
        processRibOutWithdrawal(withdrawal);
      },
      [](const ShadowRibOutAnnouncement&) {},
      [](const ShadowRibOutWithdrawal&) {},
      [](const RibInitialAnnouncementStart& /* unused*/) {});
}

void AdjRib::scheduleSendBgpUpdates(bool tryPullNewChangeItems) noexcept {
  if (sendCoroScheduled_) {
    return;
  }
  if (!asyncScope_ || asyncScope_->isScopeCancellationRequested()) {
    return;
  }
  asyncScope_->add(
      co_withExecutor(&evb_, sendBgpUpdates(tryPullNewChangeItems)));
  sendCoroScheduled_ = true;
}

bool AdjRib::scheduleDeferredPushToPeer(
    nettools::bgplib::FiberBgpPeer::InputMessageT message,
    folly::Function<void() noexcept> onResolved) noexcept {
  /*
   * A null asyncScope_ is unexpected here — the scope should exist whenever a
   * push can be scheduled. Log it as an error to surface the bug. A requested
   * cancellation, by contrast, is a normal teardown condition (the peer is
   * going down).
   */
  if (!asyncScope_) {
    XLOGF(
        ERR,
        "Peer {}: Cannot schedule deferredPushToPeer, asyncScope_ is null",
        getPeerName());
    return false;
  }
  if (asyncScope_->isScopeCancellationRequested()) {
    XLOGF(
        DBG2,
        "Peer {}: Not scheduling deferredPushToPeer, asyncScope_ cancellation requested",
        getPeerName());
    return false;
  }
  /* Capture shared_from_this() now and pass it into the coroutine.
   * The coroutine's RAII guard needs a shared_ptr to pass to
   * markPeerUnblocked. Calling shared_from_this() from the guard
   * during teardown would throw bad_weak_ptr since the destructor
   * has already zeroed the refcount. */
  asyncScope_->add(co_withExecutor(
      &evb_,
      deferredPushToPeer(
          std::move(message), shared_from_this(), std::move(onResolved))));
  return true;
}

folly::coro::Task<void> AdjRib::deferredPushToPeer(
    nettools::bgplib::FiberBgpPeer::InputMessageT message,
    std::shared_ptr<AdjRib> self,
    folly::Function<void() noexcept> onResolved) noexcept {
  bool pushed = false;

  /* RAII guard: Always clear blocked state and log when coroutine exits.
   * Uses the captured `self` shared_ptr instead of shared_from_this()
   * to avoid bad_weak_ptr during teardown when the refcount is 0. */
  auto guard = folly::makeGuard([this, &pushed, self] {
    if (adjRibOutGroup_) {
      adjRibOutGroup_->markPeerUnblocked(self);
      XLOGF(
          DBG2,
          "Group {} deferred push for peer {}: pushed={}",
          adjRibOutGroup_->getGroupDescriptor(),
          getPeerName(),
          pushed);
    }
  });

  co_await folly::coro::co_safe_point;

  if (adjRibOutQueue_ && co_await adjRibOutQueue_->waitToPush()) {
    pushed = adjRibOutQueue_->push(message);
    /* Run the continuation only on a successful push. */
    if (pushed && onResolved) {
      onResolved();
    }
  }
}

/**
 * This method is cancellation-aware because of the underlying queue
 * implementation using a folly::fibers::Semaphore with D21655946.
 * When cancellation is requested on the asyncScope_, the co_wait from
 * the semaphore in waitToPush will return with OperationCancelled,
 * and cancellation will be propagated upwards from the semaphore to callers.
 */
folly::coro::Task<bool> AdjRib::waitForQueueSpace() noexcept {
  /* Record if we experienced a write block when checking to wait. */
  bool writeBlocked = adjRibOutQueue_->isBlocked();
  if (writeBlocked) {
    BgpStats::incrementEgressQueueBackpressuredEvents();
    stats_.incrementEgressQueueBackpressuredEvents();

    /*
     * Cancel packing timers if queue is blocked as the peer has not
     * handled the queued updates quickly enough. We cannot take more updates
     * into the packing list when backpressured.
     */
    cancelPackingTimers();
    auto stateBeforeBlock = peerState_;
    if (enableUpdateGroup_ && isDetachedPeer()) {
      XLOGF(
          DBG3,
          "Peer {}: State Transition: {} -> {}",
          getPeerName(),
          peerState_,
          PeerUpdateState::DETACHED_BLOCKED);
      setPeerState(PeerUpdateState::DETACHED_BLOCKED);
    }
    auto beforeWait = getCurrentTimeMs();
    co_await adjRibOutQueue_->waitToPush();

    stats_.setLastEgressQueueBlockTime(beforeWait);
    stats_.addEgressQueueBlockDuration(getCurrentTimeMs() - beforeWait);
    if (enableUpdateGroup_ && peerState_ == PeerUpdateState::DETACHED_BLOCKED) {
      XLOGF(
          DBG3,
          "Peer {}: State Transition: {} -> {}",
          getPeerName(),
          peerState_,
          stateBeforeBlock);
      setPeerState(stateBeforeBlock);
    }
  }
  /* Now we are unblocked; return whether or not we experienced blocking. */
  co_return writeBlocked;
}

void AdjRib::cancelPackingTimers() noexcept {
  if (changeListConsumeTimer_ && changeListConsumeTimer_->isScheduled()) {
    XLOGF(
        DBG2, "Canceled changeListConsumeTimer_ timeout for {}", getPeerName());

    changeListConsumeTimer_->cancelTimeout();
  }

  if (outDelayTimer_ && outDelayTimer_->isScheduled()) {
    XLOGF(DBG2, "Canceled outDelayTimer_ timeout for {}", getPeerName());

    outDelayTimer_->cancelTimeout();
  }
}

void AdjRib::reschedulePackingTimers() noexcept {
  if (changeListConsumeTimer_ && !changeListConsumeTimer_->isScheduled()) {
    XLOGF(
        DBG2,
        "Rescheduled changeListConsumeTimer_ of {} millisecs for {}.",
        mraiInterval,
        getPeerName());

    changeListConsumeTimer_->scheduleTimeout(mraiInterval);
  }

  if (!outDelayPQ_.empty() && outDelayTimer_ &&
      !outDelayTimer_->isScheduled()) {
    XLOGF(
        DBG2,
        "Rescheduled outDelayTimer_ of {} millisecs for {}.",
        kRescheduleOutDelayTimeoutMs,
        getPeerName());

    outDelayTimer_->scheduleTimeout(kRescheduleOutDelayTimeoutMs);
  }
}

folly::coro::Task<void> AdjRib::sendBgpUpdates(
    bool tryPullNewChangeItems) noexcept {
  SCOPE_EXIT {
    sendCoroScheduled_ = false;
  };

  uint64_t bgpMessageCnt = 0;
  uint64_t withdrawPrefixCnt = 0;
  uint64_t announcePrefixCnt = 0;
  uint16_t eorCnt = 0;
  bool backpressured = false;

  while (!attrToPrefixMap_.empty()) {
    /* Check for asyncScope cancellation. */
    co_await folly::coro::co_safe_point;

    /* Wait for queue space before building an update to write to queue. */
    backpressured |= co_await waitForQueueSpace();
    if (backpressured && shouldExitEarlyOnBackPressure(tryPullNewChangeItems)) {
      break;
    }
    /*
     * If we were backpressured and resume here, it is currently possible for
     * the map to be cleared if the peer session flaps. This check can be
     * removed once we properly implement cancellation in sessionTerminated.
     * Here we need to check one more time to make sure that the map is not
     * empty before taking the begin iterator.
     */
    if (attrToPrefixMap_.empty()) {
      break;
    }
    auto it = attrToPrefixMap_.begin();
    auto& [attrWithAfi, pfxSet] = *it;
    const auto& attr = attrWithAfi.attrs;
    const auto afi = attrWithAfi.afi;
    auto update = buildUpdateWithSizeEstimation(attrWithAfi, pfxSet);
    if (update) {
      if (attr) {
        /*
         * Exactly one of mpAnnounced / v4Announced2 is populated per UPDATE
         * (mutually exclusive per buildUpdateWithSizeEstimation), so summing
         * counts each prefix once; DCHECK guards a future mixed-encoding path.
         */
        DCHECK(
            update->mpAnnounced()->prefixes()->empty() ||
            update->v4Announced2()->empty());
        announcePrefixCnt += update->mpAnnounced()->prefixes()->size() +
            update->v4Announced2()->size();
      } else {
        if (afi == BgpUpdateAfi::AFI_IPv4) {
          withdrawPrefixCnt += update->v4Withdrawn2()->size();
        } else if (afi == BgpUpdateAfi::AFI_IPv6) {
          withdrawPrefixCnt += update->mpWithdrawn()->prefixes()->size();
        }
      }
      /*
       * We waited for the queue to be unblocked before building the message;
       * guaranteed to succeed writing.
       */
      adjRibOutQueue_->push(std::move(update));
      /*
       * Queue insertion is the counter commit point. Publish the counter
       * before any subsequent suspension so counter clear and cancellation
       * cannot separate the PDU from its accounting.
       */
      stats_.incrementSentUpdateMsgs(1);
      ++bgpMessageCnt;
    }
    /*
     * Move onto next attr if we sent all of the prefixes for current attr.
     * This is guaranteed to happen because attrToPrefixMap cannot be
     * updated between the start and end of sendBgpUpdates, as we cancel
     * all packing timers. Otherwise we would be in an infinite loop.
     */
    if (pfxSet.empty()) {
      attrToPrefixMap_.erase(it);
    }
  }

  /* Queue EoRs if we just finished sending RIB initial announcement. */
  if (egressEoRsPending()) {
    auto [writeBlocked, numEoRs] = co_await sendPendingEoRs();
    backpressured |= writeBlocked;
    eorCnt = numEoRs;
  }
  const auto totalMessageCnt = bgpMessageCnt + eorCnt;
  XLOGF_IF(
      INFO,
      totalMessageCnt > 0,
      "Sending accumulated changes to {}."
      "({} withdraws, {} announcements, EoR {}) - {} BGP message(s). "
      "Backpressured = {}",
      getPeerName(),
      withdrawPrefixCnt,
      announcePrefixCnt,
      eorCnt,
      totalMessageCnt,
      backpressured);

  /*
   * Exit early if cancellation was requested due to session termination by
   * AdjRib::sessionTerminated.
   */
  co_await folly::coro::co_safe_point;

  if (enableUpdateGroup_ && attrToPrefixMap_.empty()) {
    transitionPeerUpdateState();
  } else {
    reschedulePackingTimers();
  }
  co_return;
}

folly::coro::Task<std::pair<bool, uint16_t>>
AdjRib::sendPendingEoRs() noexcept {
  XLOGF(INFO, "Sending EoR to peer {}", getPeerName());
  /* Check for asyncScope cancellation. */
  co_await folly::coro::co_safe_point;

  uint16_t eorCnt{0};
  bool backpressured = false;
  /*
   * Send only the AFIs still pending. When the group already queued one AFI's
   * EoR before this peer detached, its flag is already cleared, so the detached
   * peer sends just the remaining AFI — no duplicate, no missing EoR. This is
   * the peer's own synchronous send path: every owed AFI is pushed here, so we
   * finalize unconditionally via onEgressEoRSent at the end.
   */
  if (isAdjRibFlagSet(EGRESS_EOR_PENDING_V4)) {
    backpressured |= co_await waitForQueueSpace();
    adjRibOutQueue_->push(buildEndOfRib(BgpUpdateAfi::AFI_IPv4));
    stats_.incrementSentEndOfRibMsgs(1);
    ++eorCnt;
    clearAdjRibFlag(EGRESS_EOR_PENDING_V4);
  }
  if (isAdjRibFlagSet(EGRESS_EOR_PENDING_V6)) {
    backpressured |= co_await waitForQueueSpace();
    adjRibOutQueue_->push(buildEndOfRib(BgpUpdateAfi::AFI_IPv6));
    stats_.incrementSentEndOfRibMsgs(1);
    ++eorCnt;
    clearAdjRibFlag(EGRESS_EOR_PENDING_V6);
  }

  onEgressEoRSent();

  co_return std::make_pair(backpressured, eorCnt);
}

/*
 * One-time per-session bookkeeping once the peer's egress EoR PDUs have been
 * queued. Sets egressEoRsSent_, records the time, logs the event, and notifies
 * the peer manager for init. Does NOT touch the egress queue.
 */
void AdjRib::onEgressEoRSent() noexcept {
  /*
   * One-time per session: if the bookkeeping already ran (e.g. the group
   * finalized this AFI via markEgressEoRSent and the detached peer's
   * sendPendingEoRs then finalizes the rest), skip so we record the time, log,
   * and notify the peer manager at most once.
   */
  if (egressEoRsSent_) {
    return;
  }
  egressEoRsSent_ = true;
  eorSentTime_ = getCurrentTimeMs();
  logPeerEvent("SESSION_EOR_SENT", BGP_LOG_SRC());

  /* Send out egressEoR notification to peer manager for initialization. */
  fromAdjRibQ_.push({*remotePeerId_, EgressEoR{}});
}

/*
 * Finalize a single AFI's egress EoR once its PDU has landed in the peer's
 * egress queue. The group's EoR push supplies this as the onResolved
 * continuation to tryPushToPeer, so it runs on immediate push or from
 * deferredPushToPeer for a backpressured peer. The continuation carries the
 * AFI, so record the committed PDU, clear exactly this AFI's
 * EGRESS_EOR_PENDING flag, increment this
 * peer's sent-EoR count, then fire the one-time bookkeeping if no AFI's EoR
 * is
 * still pending (this was the last).
 *
 * Only AFI_IPv4 and AFI_IPv6 carry egress EoR state. Match each AFI explicitly
 * rather than treating "not IPv4" as IPv6: an unexpected AFI (e.g. AFI_LS, or a
 * future value) must not silently clear the v6 flag and fire onEgressEoRSent
 * early. It is a programming error to reach here with any other AFI.
 */
void AdjRib::markEgressEoRSent(nettools::bgplib::BgpUpdateAfi afi) noexcept {
  if (afi == nettools::bgplib::BgpUpdateAfi::AFI_IPv4) {
    clearAdjRibFlag(EGRESS_EOR_PENDING_V4);
  } else if (afi == nettools::bgplib::BgpUpdateAfi::AFI_IPv6) {
    clearAdjRibFlag(EGRESS_EOR_PENDING_V6);
  } else {
    XLOGF(
        ERR,
        "Peer {}: markEgressEoRSent called with unsupported AFI {}; expected "
        "AFI_IPv4 or AFI_IPv6",
        getPeerName(),
        static_cast<int>(afi));
    return;
  }

  stats_.incrementSentEndOfRibMsgs(1);
  if (!egressEoRsPending()) {
    onEgressEoRSent();
  }
}

/**
 * This can be used to pack all prefixes into the provided RiggedIPPrefix
 * container up to an estimated limit, such that the final BgpUpdate2
 * can be roughly serialized to 1 PDU. Used when backpressure is enabled.
 *
 * Highlighting two scenarios:
 *   1. The approximate serialized attr length is smaller than the actual
 *      length, and we overpacked prefixes.
 *
 *   2. The approximate serialized attr length is larger than the actual
 *      length, and we underpacked prefixes.
 *
 * In both cases, the BgpUpdate2 that carries the @bgpUpdatePrefixes container
 * is passed all the way to the FiberSocket. Serialization only happens
 * after dequeueing from the last egress queue between AdjRibOut and
 * socket. We aim to be within 1-2 messages after serialization; and socket
 * currently writes EVERYTHING it serialized before dequeueing another
 * BgpUpdate2.
 */
uint32_t AdjRib::packPrefixesWithLimit(
    const uint32_t approximateSerializedAttrLen,
    PrefixSet& prefixPathIds,
    std::vector<RiggedIPPrefix>& bgpUpdatePrefixes) {
  return packPrefixesWithLimitCommon(
      approximateSerializedAttrLen,
      prefixPathIds,
      bgpUpdatePrefixes,
      sendAddPath_,
      getPeerName());
}

/**
 * Used in sendBgpUpdates coro to build a BgpUpdate2 that can be serialized
 * to roughly 1 PDU.
 */
std::shared_ptr<BgpUpdate2> AdjRib::buildUpdateWithSizeEstimation(
    const BgpPathWithAfi& attrsWithAfi,
    PrefixSet& prefixPathIds) noexcept {
  if (prefixPathIds.empty()) {
    return nullptr;
  }
  auto& postOutAttrs = attrsWithAfi.attrs;
  auto afi = attrsWithAfi.afi;
  std::shared_ptr<BgpUpdate2> update;
  if (postOutAttrs) {
    /* Case 1: Announcement. */
    /* Set the new nexthop on the BgpUpdate2 message. */
    auto newNexthop = getNewNexthopFromAttributesOut(
        afi == BgpUpdateAfi::AFI_IPv4 /* isV4 */,
        postOutAttrs,
        attrsWithAfi.isNexthopSetByPolicy);

    update = postOutAttrs->getBgpUpdate2();
    /* The NEXT_HOP attribute string is identical for both encodings. */
    update->attrs()->nexthop() = newNexthop.str();
    switch (v4EncodingDecision(
        updateGroupKey_,
        afi == BgpUpdateAfi::AFI_IPv4,
        newNexthop,
        getPeerName())) {
      case V4EncodingDecision::ClassicNlri:
        /* Legacy RFC 4271: classic v4 NLRI + NEXT_HOP (attr 3). */
        update->v4Nexthop() = network::toBinaryAddress(newNexthop);
        packPrefixesWithLimit(
            kApproxSerializedAttrLen, prefixPathIds, *update->v4Announced2());
        if (!update->v4Announced2()->empty()) {
          stats_.incrementSentLegacyV4Announcements();
        }
        break;
      case V4EncodingDecision::Drop:
        /*
         * Capability-less peer with a v6 nexthop: the route cannot be encoded
         * for this peer, so drop it (v4EncodingDecision has already logged).
         * Clear prefixPathIds so sendBgpUpdates erases the map entry instead of
         * re-picking it forever (it only advances once pfxSet is drained).
         */
        prefixPathIds.clear();
        return nullptr;
      case V4EncodingDecision::MpReach:
        update->mpAnnounced()->afi() = afi;
        update->mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
        update->mpAnnounced()->nexthop() = network::toBinaryAddress(newNexthop);
        packPrefixesWithLimit(
            kApproxSerializedAttrLen,
            prefixPathIds,
            *update->mpAnnounced()->prefixes());
        break;
    }

    if (afi == BgpUpdateAfi::AFI_IPv4) {
      stats_.incrementSentAnnouncementsIpv4();
    } else if (afi == BgpUpdateAfi::AFI_IPv6) {
      stats_.incrementSentAnnouncementsIpv6();
    }

  } else {
    /* Case 2: Withdrawal. */
    update = std::make_shared<BgpUpdate2>();
    if (afi == BgpUpdateAfi::AFI_IPv4) {
      packPrefixesWithLimit(
          0 /* approxAttrLen */, prefixPathIds, *update->v4Withdrawn2());
    } else if (afi == BgpUpdateAfi::AFI_IPv6) {
      packPrefixesWithLimit(
          0 /* approxAttrLen */,
          prefixPathIds,
          *update->mpWithdrawn()->prefixes());
      update->mpWithdrawn()->afi() = BgpUpdateAfi::AFI_IPv6;
      update->mpWithdrawn()->safi() = BgpUpdateSafi::SAFI_UNICAST;
    }
    stats_.incrementSentWithdrawals();
  }
  return update;
}

bool AdjRib::buildAndSendRouteRefresh(
    const nettools::bgplib::BgpRouteRefreshMessageSubtype& subtype,
    BgpUpdateAfi afi) noexcept {
  if (!isAfiNegotiated(afi)) {
    return false;
  }

  auto routeRefresh =
      buildRouteRefresh(afi, subtype, BgpUpdateSafi::SAFI_UNICAST);
  if (!adjRibOutQueue_ || adjRibOutQueue_->isBlocked()) {
    return false;
  }
  return adjRibOutQueue_->push(std::move(routeRefresh));
}

void AdjRib::markEgressEoRsPendingFor(
    const RibOutAnnouncement& announcement) noexcept {
  if (!announcement.sendWithEoR) {
    return;
  }

  if (announcement.routeRefreshAfi) {
    if (*announcement.routeRefreshAfi == BgpUpdateAfi::AFI_IPv4) {
      setEgressEoRsPending(isAfiIpv4Negotiated_, egressEoRPendingV6());
      return;
    }
    if (*announcement.routeRefreshAfi == BgpUpdateAfi::AFI_IPv6) {
      setEgressEoRsPending(egressEoRPendingV4(), isAfiIpv6Negotiated_);
      return;
    }
    XLOGF(
        ERR,
        "Peer {}: unexpected Route Refresh AFI {}, sending all negotiated EoRs",
        getPeerName(),
        static_cast<int>(*announcement.routeRefreshAfi));
  }

  setEgressEoRsPending(isAfiIpv4Negotiated_, isAfiIpv6Negotiated_);
}

void AdjRib::processRibOutAnnouncement(
    const RibOutAnnouncement& announcement) noexcept {
  if (!egressEoRsSent_ && !announcement.initialDump) {
    XLOGF(
        DBG1,
        "Ignoring Rib announcement for {} before sending initial dump",
        getPeerName());
    return;
  }

  /*
   * Idle -> InProgress on the first RR announcement; gates per-entry dedup
   * bypass via isRrDumpInProgress() so RFC 2918 §4 re-advertisement reaches
   * the wire even when attrs are unchanged. No-op outside the transition,
   * so safe to call unconditionally.
   */
  maybeBeginRrDump(announcement);

  const auto& entries =
      sendAddPath_ ? announcement.addPathEntries : announcement.entries;

  for (const auto& entry : entries) {
    handleRibAnnouncedEntry(entry, announcement.initialDump);
  }
  scheduleOutDelayTimer();
  markEgressEoRsPendingFor(announcement);

  /*
   * InProgress -> Idle once the terminal Route Refresh announcement
   * (routeRefreshAfi has a value and sendWithEoR=true) completes. Per-entry
   * dedup decisions for this dump are already final; async wire emission needs
   * no further state check.
   */
  maybeEndRrDump(announcement);
}

void AdjRib::maybeBeginRrDump(const RibOutAnnouncement& announcement) noexcept {
  if (announcement.routeRefreshAfi && rrDumpState_ == RrDumpState::Idle) {
    rrDumpState_ = RrDumpState::InProgress;
    /*
     * Phase 2 (RFC 7313 ERR) hook: emit BoRR here when
     * isEnhancedRouteRefreshNegotiated_.
     */
  }
}

void AdjRib::maybeEndRrDump(const RibOutAnnouncement& announcement) noexcept {
  if (announcement.routeRefreshAfi && announcement.sendWithEoR &&
      rrDumpState_ == RrDumpState::InProgress) {
    /*
     * Phase 2 (RFC 7313 ERR) hook: emit EoRR here when
     * isEnhancedRouteRefreshNegotiated_.
     */
    rrDumpState_ = RrDumpState::Idle;

    /*
     * Drain any pending changeList items synchronously now that the RR
     * re-dump has emitted everything from the shadow RIB. The consumer's
     * marker may still point at items the consumer queued before / during
     * the dump — items the RR walk already covered. Without this drain
     * three costs accrue:
     *   - memory: ChangeItems can only evict from the global ChangeList
     *     once all consumers' markers move past them; a stuck marker on
     *     this peer pins items resident until the next periodic tick.
     *   - CPU (deferred): the next periodic iterateChangesToEnd()
     *     walks the now-stale items and generates would-be UPDATEs that
     *     AdjRibOut postAttr dedup suppresses anyway — wasted traversal.
     *   - latency: a real new change arriving immediately after RR sits
     *     behind the stale items in the consumer's processing order.
     *
     * Items the RR walk just emitted hit postAttr-equality dedup here and
     * are silently skipped; items registered while RR was in flight (real
     * shadow-RIB changes that completed during the dump) emerge as
     * post-EoR incremental UPDATEs. Either way, exactly-once on the wire.
     *
     * Threading / re-entrance: same evb as the rest of
     * processRibOutAnnouncement; sync loop with no co_await; mirrors the
     * existing periodic-timer call site at AdjRib.cpp:1224.
     */
    if (changeListConsumer_ && !isUpdateGroupEnabled()) {
      changeListConsumer_->iterateChangesToEnd();
    }
  }
}

bool AdjRib::willExceedEgressSwitchLimit() const noexcept {
  if (!switchLimitConfig_) {
    return false;
  }
  const auto totalPathLimit = switchLimitConfig_->total_path_limit();
  if (!totalPathLimit) {
    return false;
  }
  const auto totalPathCount = AdjRibStats::getTotalSwitchPathCount() + 1;
  if (totalPathCount <= *totalPathLimit) {
    return false;
  }
  XLOGF_EVERY_MS(
      ERR,
      600000 /* 10 min */,
      "Total path advertised: {} exceeds max limit {}",
      totalPathCount,
      *totalPathLimit);
  return true;
}

void AdjRib::handleRibAnnouncedEntry(
    const RibOutAnnouncementEntry& entry,
    bool initialDump) noexcept {
  XLOGF(
      DBG5,
      "Processing Rib announcement of {} for peer {}",
      folly::IPAddress::networkToString(entry.prefix),
      getPeerName());
  // ignore unsupported afi
  if (!AdjRibCommonUtils::isAfiNegotiated(
          entry.prefix, isAfiIpv4Negotiated_, isAfiIpv6Negotiated_)) {
    XLOGF(
        DBG4,
        "Ignore RibAnnouncement of prefix {}: AFI is not supported for {}.",
        folly::IPAddress::networkToString(entry.prefix),
        getPeerName());
    return;
  }
  // sanity check
  if (!canAnnounceEntry(entry)) {
    handleImplicitWithdrawal(entry);
    return;
  }

  /*
   * if processOutDelay returns false we are not deferring the prefix
   * update due to out-delay. if the prefix is not already deferred
   * then newEntryDeferred will be set.
   */
  bool outDelay{false}, newEntryDeferred{false};
  if (!initialDump) {
    std::tie(outDelay, newEntryDeferred) = processOutDelay(entry);
  }
  if (!outDelay) {
    processRibAnnouncedEntry(entry);
  } else if (newEntryDeferred) {
    newDeferredPrefixes_.emplace_back(entry.prefix);
  }

  // Update cached RIB version to track how caught up this peer is
  setLastSeenRibVersion(entry.ribVersion);
}

void AdjRib::scheduleOutDelayTimer(void) noexcept {
  if (newDeferredPrefixes_.empty()) {
    return;
  }

  /*
   * Enqueue the new out-delay timer if newEntryDeferred is set from
   * above.
   */
  auto delayTimeStamp = std::chrono::system_clock::now() + outDelay_;
  outDelayPQ_.emplace(delayTimeStamp, std::move(newDeferredPrefixes_));
  /*
   * if timer is running we don't need to do anything.
   * else fire a new out-delay timer.
   */
  if (!outDelayTimer_ || !outDelayTimer_->isScheduled()) {
    XLOGF(
        DBG4,
        "Starting outDelay timer_ of {} secs for {}.",
        outDelay_.count(),
        getPeerName());

    outDelayTimer_ = folly::AsyncTimeout::schedule(
        std::chrono::duration_cast<std::chrono::milliseconds>(outDelay_),
        evb_,
        [this]() noexcept { this->programOutDelayTimer(); });
  }
  newDeferredPrefixes_.clear();
}

bool AdjRib::canAnnounce(const RibOutAnnouncementEntry& update) noexcept {
  /*
   * Do not send out to the peer from which we learnt this route
   * NOTE Here we only use peer Addr, maybe also need to check the bgpId?
   */
  if ((update.peer.addr == peeringParams_.peerAddr) &&
      ((!peeringParams_.peerAddr.isLoopback()) ||
       (!peeringParams_.allowLoopbackReflection))) {
    XLOGF(
        DBG4,
        "Not advertising prefix {} to {}. Reason: Learnt from the same peer",
        folly::IPAddress::networkToString(update.prefix),
        getPeerName());
    return false;
  }

  if (isIBgpPeer()) {
    if (peeringParams_.isRrClient) {
      XLOGF(
          DBG4,
          "Permit prefix announcement {} to IBGP peer {}. "
          "Reason: Peer is RR client",
          folly::IPAddress::networkToString(update.prefix),
          getPeerName());
      return true;
    }

    /*
     * Announce eBGP learnt routes, local routes and RR client routes to
     * non RR peer
     * Local routes will have addr.isZero()
     */
    if (update.peer.sessionType != BgpSessionType::IBGP ||
        update.peer.addr.isZero() || update.peer.isRrClient) {
      XLOGF(
          DBG4,
          "Permit prefix announcement {} to IBGP peer {}. "
          "Reason: EBGP learnt route | Local Route | RRC Learnt Route",
          folly::IPAddress::networkToString(update.prefix),
          getPeerName());
      return true;
    }

    XLOGF(
        DBG4,
        "Not advertising prefix {} to {}. Reason: Learnt from same AS",
        folly::IPAddress::networkToString(update.prefix),
        getPeerName());
    return false;
  }
  return true;
}

/*
 * When update-group is enabled, use the group-level check to stay
 * consistent with the group processing path, which skips the per-peer
 * same-peer filter (split-horizon is deferred to per-peer distribution).
 */
bool AdjRib::canAnnounceEntry(const RibOutAnnouncementEntry& update) noexcept {
  if (enableUpdateGroup_ && adjRibOutGroup_) {
    return adjRibOutGroup_->canAnnounceForGroup(update);
  }
  if (!canAnnounce(update)) {
    return false;
  }
  if (sender_suppress_as_loop_ && suppressLoopedAdvertisements(update.attrs)) {
    return false;
  }
  /*
   * RFC 1997 well-known community egress filter. Gated by gflag so the
   * rollout is bisectable and rollback is instant. When the flag is off
   * the behavior is unchanged (routes carrying well-known communities
   * continue to be advertised, preserving the historical behavior).
   *
   * The filter wrapper is shared with the group-level path
   * (AdjRibOutGroup::canAnnounceForGroup) so the decision + logging +
   * counter side-effects stay in lock-step regardless of update-grouping.
   */
  if (FLAGS_enable_well_known_community_filter &&
      applyWellKnownCommunityFilter(
          update.attrs, getBgpSessionType(), getPeerName(), update.prefix)) {
    return false;
  }
  return true;
}

bool AdjRib::suppressLoopedAdvertisements(
    const std::shared_ptr<const facebook::bgp::BgpPath>& attrs) noexcept {
  auto remoteAs = facebook::bgp::AsNum(getRemoteAs());
  /*
   * Determine if remote AS exists in the as path (so that remote would reject
   * it)
   * Note: its check is less than hasAsPathLoop it will check confed_as or
   * global_as depending whether it is confed peer, as it has only one remote_as
   * Motivation: https://fburl.com/gdoc/4cm17y05
   */
  if (isIBgpPeer()) {
    return false;
  }

  if (peeringParams_.isConfedPeer) {
    for (const auto& seg : attrs->getAsPath().get()) {
      if (std::find(
              seg.asConfedSequence.cbegin(),
              seg.asConfedSequence.cend(),
              remoteAs) != seg.asConfedSequence.cend()) {
        return true;
      }
      if (seg.asConfedSet.contains(remoteAs)) {
        return true;
      }
    }
  } else {
    for (const auto& seg : attrs->getAsPath().get()) {
      if (seg.asSet.contains(remoteAs)) {
        return true;
      }
      if (std::find(seg.asSequence.cbegin(), seg.asSequence.cend(), remoteAs) !=
          seg.asSequence.cend()) {
        return true;
      }
    }
  }
  return false;
}

/*
 * TODO (D6882628): We are applying policy per Rib announcement entry. We can
 * see if we can optimize by trying to group prefixes based on attributes from
 * RIB message and applyPolicy. (If policy applying cost is more than grouping
 * cost) For incremental announcements as rib aggregates messages from multiple
 * peers and accumulates with delay, it may not be worth it. Even for initial
 * dump of route table as we advertise only couple of routes from each RSW,
 * number of routes sharing preOutAttrs with same values is very small,
 * annecdotally < 4. As we create separate shared_ptr for preInAttrs from
 * multiple peers unless we do deep compare/deep hashing we cannot determine
 * which prefixes to group. Overall I feel with current design and deployments
 * it's not worth it.
 */
void AdjRib::processRibAnnouncedEntry(
    const RibOutAnnouncementEntry& update) noexcept {
  const std::string updatePeerIdStr =
      BgpPeerId(update.peer.addr, update.peer.routerId).str();

  auto* adjRibEntry = tryInsertRibOutEntry(update);
  if (!adjRibEntry) {
    // Refused by the egress switch limit.
    return;
  }
  stats_.updateAttributeSizes(update.attrs);
  adjRibEntry->setPreOut(update.attrs);
  adjRibEntry->setRibVersion(update.ribVersion);

  /*
   * apply per-peer-lbw-config for AdvertiseLBW
   * lbw-policy is applied afterwards, which takes precedence over
   * per-peer-config
   */
  auto prePolicyAttrs = update.attrs->clone();
  if (update.isPartialDrain) {
    applyPartialDrainCommunities(prePolicyAttrs);
  }
  updateAdvertiseLbwExtCommunity(update, prePolicyAttrs);

  /*
   * Get post policy attributes
   * Look up in the cache - if found just clone the result
   * Else run thru policy and store result in cache, clone the cached copy
   */
  const auto& [policyCachedAttrs, postPolicyInfo] =
      getPostOutPolicyAttributesAndInfo(
          update, adjRibEntry, prePolicyAttrs, updatePeerIdStr);

  /* Preserve previous value of the isNexthopSetByPolicy flag for comparison. */
  bool wasNexthopSetByPolicy = adjRibEntry->isNexthopSetByPolicy();

  /* Now update flag on AdjRibEntry for CLI display (AdjRibShow). */
  adjRibEntry->setNexthopSetByPolicy(postPolicyInfo.isNexthopSetByPolicy);

  if (!policyCachedAttrs) {
    PeerStats::incrRejectedOutboundRoutes();
    // Policy blocked this prefix
    tryInsertWithdrawal(
        update.prefix,
        adjRibEntry,
        fmt::format(
            "Withdrawing {} from [{}]. "
            "Reason: Blocked prefix by policy. (Previously announced to peer)",
            folly::IPAddress::networkToString(update.prefix),
            updatePeerIdStr),
        fmt::format(
            "Ignoring Rib announcement {} from [{}]. "
            "Reason: Blocked prefix by policy. (Not previously announced to peer)",
            folly::IPAddress::networkToString(update.prefix),
            updatePeerIdStr));
    return;
  }

  auto newNexthop = getNewNexthopFromAttributesOut(
      update.prefix.first.isV4(),
      policyCachedAttrs,
      postPolicyInfo.isNexthopSetByPolicy);

  // ignore announcement with unsupported nexthop encoding
  if (!newNexthop.empty() && !isNexthopSupported(update.prefix, newNexthop)) {
    PeerStats::incrInvalidNexthopEncoding();
    XLOGF_EVERY_MS(
        ERR,
        600000,
        "Ignore RibAnnouncement of prefix {} with nexthop {}: Nexthop encoding "
        "is not supported for {}.",
        folly::IPAddress::networkToString(update.prefix),
        newNexthop.str(),
        getPeerName());
    return;
  }

  /*
   * Prefix is permitted from egress policy.
   * Attributes needs to be updated before sending to packing list.
   *
   * NOTE: directly modifying prePolicyAttrs will make cache miss since it is
   * used as the policy cache key. Clone it for modification.
   */
  auto postOutAttrsNew = prePolicyAttrs->clone();
  updateAttributesOutWithoutNexthopCommon(
      getPeerConfig(),
      update,
      policyCachedAttrs,
      postOutAttrsNew,
      postPolicyInfo);

  /* Attributes will not change any more. Publish. */
  postOutAttrsNew->publish();

  // Check if we announced the prefix before; postAttr is set if so.
  if (adjRibEntry->getPostAttr()) {
    /*
     * Previously announced. Skip if attrs are byte-identical (deep compare
     * because policy may produce equal contents from a different BgpPath
     * shared_ptr). Route Refresh (RFC 2918) bypasses the skip so all
     * requested-AFI routes are re-advertised to the requesting peer even
     * when attributes are unchanged.
     */
    if (!isRrDumpInProgress() &&
        *adjRibEntry->getPostAttr() == *postOutAttrsNew &&
        (wasNexthopSetByPolicy == adjRibEntry->isNexthopSetByPolicy())) {
      XLOGF(
          DBG3,
          "Skip announcing {} from [{}] to {}. "
          "Reason: Previously announced to peer",
          folly::IPAddress::networkToString(update.prefix),
          updatePeerIdStr,
          getPeerName());
      return;
    }
  } else {
    /*
     * First-time announcement to this peer; bump sentPrefixCount. RR flag
     * is irrelevant here — a never-announced prefix always goes out.
     */
    stats_.incrementPostOutPrefixCount(update.prefix.first.isV4());
  }

  XLOGF(
      DBG3,
      "Announcing {} from [{}] to {}.",
      folly::IPAddress::networkToString(update.prefix),
      updatePeerIdStr,
      getPeerName());

  auto prefixPathId = std::make_pair(update.prefix, adjRibEntry->getPathId());

  auto oldPostAttr = adjRibEntry->getPostAttr();

  /*
   * Set the post out
   * Majority of the cases we update attributes with nexthop/AS-Path changes
   * or policy changes, but in few cases where there is no change between
   * preOut and postOut, this deep compare will save memory.
   */
  if (*postOutAttrsNew == *adjRibEntry->getPreOut()) {
    auto preOutPostAttr = adjRibEntry->getPreOut();
    stats_.updateAttributeSizes(preOutPostAttr);
    adjRibEntry->setPostAttr(preOutPostAttr);
  } else {
    stats_.updateAttributeSizes(postOutAttrsNew);
    adjRibEntry->setPostAttr(postOutAttrsNew);
  }
  tryUpdateAttrToPrefixMap(
      prefixPathId,
      oldPostAttr,
      adjRibEntry->getPostAttr(),
      postPolicyInfo.isNexthopSetByPolicy);
}

uint32_t AdjRib::resolvePathId(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& nexthop,
    uint32_t pathIdToSend) noexcept {
  return (enableRibAllocatedPathId_ || enableUpdateGroup_)
      ? pathIdToSend
      : pathIdGenerator_->getPathId(prefix, nexthop);
}

std::optional<uint32_t> AdjRib::findResolvedPathId(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& nexthop,
    uint32_t pathIdToSend) const noexcept {
  return (enableRibAllocatedPathId_ || enableUpdateGroup_)
      ? std::optional<uint32_t>(pathIdToSend)
      : pathIdGenerator_->findPathId(prefix, nexthop);
}

/*
 * Try inserting a ribOutEntry for the update's prefix and next hop.
 * Return the existing one if the entry already exists; Otherwise,
 * create a new one and return it.
 */
AdjRibEntry* FOLLY_NULLABLE
AdjRib::tryInsertRibOutEntry(const RibOutAnnouncementEntry& update) noexcept {
  const auto& prefix = update.prefix;
  const auto& nexthop = update.attrs->getNexthop();
  /*
   * Resolve the path id without issuing one where we can: the generator
   * allocates on a miss, and the limit check below should not leave a path id
   * behind for a route it goes on to refuse. A (prefix, nexthop) the generator
   * has never issued an id for cannot have a peer-owned entry.
   *
   * With RIB allocated path IDs, and with update groups (which key group-owned
   * entries by pathIdToSend), the path ID comes from the update instead.
   */
  const auto resolvedPathId =
      findResolvedPathId(prefix, nexthop, update.pathIdToSend);

  auto adjRibEntry = resolvedPathId
      ? (enableUpdateGroup_
             ? getRibEntryWithUpdateGroup(prefix, *resolvedPathId)
             : getRibEntry(/*ingress=*/false, prefix, *resolvedPathId))
      : nullptr;
  if (adjRibEntry) {
    XLOGF(
        DBG4,
        "Updating preOut attributes of prefix {} for {}",
        folly::IPAddress::networkToString(prefix),
        getPeerName());
    return adjRibEntry;
  }

  /*
   * Learning a new route. The switch limit only applies here: updating an
   * existing entry adds no rib entry, and refusing it would leave the peer
   * holding stale attributes. Mirrors canAddRibInEntry, which ingress consults
   * only when the AdjRibIn entry does not already exist.
   *
   * Before S696431 the switch limits were only
   * enforced at RIB-IN, now we also enforce the total_path_limit on egress.
   * This is not enforced under update group because total_path_limit
   * is a heuristic to estimate the memory consumption of total rib entries
   * created for prefixes for all peers.
   *
   * With update group enabled, this is no longer linearly proportional.
   * Defining the limit for update group is tracked separately.
   */
  if (!enableUpdateGroup_ && willExceedEgressSwitchLimit()) {
    if (canApplyGoldenPrefixPolicyMode()) {
      triggerSafeMode();
    }
    /*
     * Locally originated routes are advertised regardless; we only drop
     * routes learned from peers.
     */
    if (!update.isLocalRoute()) {
      incrementPrefixesDroppedByLimit();
      return nullptr;
    }
  }

  XLOGF(
      DBG4,
      "Learning new prefix from Rib for {}: {}",
      getPeerName(),
      folly::IPAddress::networkToString(prefix));
  stats_.incrementPreOutPrefixCount(prefix.first.isV4());
  const auto pathId = resolvedPathId
      ? *resolvedPathId
      : pathIdGenerator_->getPathId(prefix, nexthop);
  return addRibEntry(/*ingress=*/false, prefix, pathId);
}

std::pair<const std::shared_ptr<const BgpPath>, const PostPolicyInfo>
AdjRib::getPostOutPolicyAttributesAndInfo(
    const RibOutAnnouncementEntry& update,
    AdjRibEntry* adjRibEntry,
    const std::shared_ptr<const BgpPath>& prePolicyAttrs,
    const std::string& updatePeerIdStr) {
  std::shared_ptr<const BgpPath> policyResultAttrs{nullptr};
  PostPolicyInfo postPolicyResultInfo;

  /*
   * adjRibEntry must not be nullptr
   * This check allows UT to surface assumption violation early
   */
  CHECK(adjRibEntry != nullptr);

  /*
   * TODO: if policy has no prefix terms we can store a separate cache of
   *       egress-policy and attrs which can then be applied across all prefixes
   *       thus saving cache space.
   */

  /*
   * Even in presence of local-as policy cache lookup is fine.
   * Because as-path-update is done on post-policy attrs.
   */
  if (egressPolicyConfigured()) {
    /*
     * snapshot policyActionData
     * This captures the original data, before update.attrs is
     * updated to prePolicyAttrs
     */
    auto policyActionData = createPolicyActionData(
        update.attrs,
        update.switchId,
        update.multiPathSize,
        update.aggregateReceivedUcmpWeight,
        update.aggregateLocalUcmpWeight,
        update.ribPolicyUcmpWeight);

    const auto& [attrs, postTermName, postPolicyInfo] =
        getPostPolicyAttributesPolicyTermAndInfo(
            *egressPolicyName_,
            update.prefix,
            prePolicyAttrs,
            policyActionData,
            update.isPartialDrain);

    policyResultAttrs = attrs;
    postPolicyResultInfo = postPolicyInfo;
    adjRibEntry->setPostOutPolicy(postTermName);
  } else {
    policyResultAttrs = prePolicyAttrs;
  }

  // see if egress route filter hard blocks this prefix
  if (policyResultAttrs != nullptr &&
      blockedByEgressRouteFilter(update, updatePeerIdStr)) {
    policyResultAttrs = nullptr;
    /*
     * this prefix was allowed by policy but blocked by crf, we update
     * PostOutPolicy accordingly
     */
    adjRibEntry->setPostOutPolicy("Denied by CRF");
  }
  return {policyResultAttrs, postPolicyResultInfo};
}

const std::shared_ptr<const BgpPath> AdjRib::getPostOutPolicyAttributes(
    const RibOutAnnouncementEntry& update,
    AdjRibEntry* adjRibEntry,
    const std::shared_ptr<const BgpPath>& prePolicyAttrs,
    const std::string& updatePeerIdStr) {
  return getPostOutPolicyAttributesAndInfo(
             update, adjRibEntry, prePolicyAttrs, updatePeerIdStr)
      .first;
}

bool AdjRib::shouldApplyNexthopSelf(
    const std::shared_ptr<const BgpPath>& postOutAttrs,
    bool isNexthopSetByPolicy) noexcept {
  /**
   * Zero nexthop (0.0.0.0 / ::) is always invalid in a BGP UPDATE.
   * Always apply nexthop-self regardless of policy intent.
   */
  if (postOutAttrs->getNexthop().isZero()) {
    return true;
  }

  /**
   * If the egress policy explicitly set the nexthop via SetNexthop
   * action, honor the policy's intent and skip nexthop-self.
   */
  if (isNexthopSetByPolicy) {
    return false;
  }

  return isEBgpPeer() || peeringParams_.nextHopSelf;
}

folly::IPAddress AdjRib::getNewNexthopFromAttributesOut(
    const bool isV4Prefix,
    const std::shared_ptr<const BgpPath>& postOutAttrs,
    bool isNexthopSetByPolicy) noexcept {
  CHECK(postOutAttrs != nullptr);
  /**
   * Set nexthop as the configured nexthop when shouldApplyNexthopSelf()
   * returns true. Policy-set nexthop takes precedence over nexthop-self
   * (except for zero nexthop which is always invalid).
   */
  folly::IPAddress newNexthop = postOutAttrs->getNexthop();
  if (shouldApplyNexthopSelf(postOutAttrs, isNexthopSetByPolicy)) {
    if (!isV4OverV6NexthopNegotiated_ && isV4Prefix) {
      newNexthop = peeringParams_.nexthopV4.isZero()
          ? folly::IPAddress(peeringParams_.localBgpId)
          : peeringParams_.nexthopV4;
    } else {
      newNexthop = peeringParams_.nexthopV6.isZero()
          ? folly::IPAddress::createIPv6(peeringParams_.localBgpId)
          : peeringParams_.nexthopV6;
    }
  }

  return newNexthop;
}

bool AdjRib::blockedByEgressRouteFilter(
    const RibOutAnnouncementEntry& update,
    const std::string& peerId) const {
  // apply egress route filtering if set
  if (routeFilterStmt_) {
    std::vector<std::string> communityStrs;
    for (const auto& community : update.attrs->getCommunities().get()) {
      communityStrs.emplace_back(community.to_string());
    }

    auto routeFilterOut = routeFilterStmt_->applyEgressFilter({update.prefix});
    const auto& remainingPrefixes = std::get<0>(routeFilterOut);
    const auto& filteredPrefixes = std::get<1>(routeFilterOut);
    if (remainingPrefixes.empty()) {
      if (routeFilterLogger_) {
        routeFilterLogger_->log(
            true /* egress */,
            update.prefix,
            false /* blocked */,
            false /* non-permissive */,
            communityStrs);
      }
      XLOGF(
          WARNING,
          "Egress Route Filter Policy blocked prefix {} from [{}]. ",
          folly::IPAddress::networkToString(update.prefix),
          peerId);
      // blocked
      return true;
    } else if (!filteredPrefixes.empty()) {
      if (routeFilterLogger_) {
        routeFilterLogger_->log(
            true /* egress */,
            update.prefix,
            true /* allowed */,
            true /* permissive */,
            communityStrs);
      }
      XLOGF(
          WARNING,
          "Egress Route Filter Policy permissive-allowed prefix {} from [{}]. ",
          folly::IPAddress::networkToString(update.prefix),
          peerId);
    }
  }
  return false;
}

/*
 * Check if the update is to be deferred due to out-delay or not.
 * Whenever a new AdjRibEntry is created for the first time we will apply
 * out-delay. Any subsequent update won't trigger out-delay.
 * Returns true if update is deferred or dropped.
 * Else, return false which invokes processRibAnnouncedEntry on the update
 * to send the update out without any delay.
 */
std::pair<bool, bool> AdjRib::processOutDelay(
    const RibOutAnnouncementEntry& update) {
  if (outDelay_ == 0s) {
    return std::make_pair<bool, bool>(false, false);
  }

  /*
   * if prefix is currently in deferredUpdates_ list just modify it.
   * Note such a prefix might not have newlyInstalledInLocalRib marked.
   */
  if (deferredUpdates_.erase(update.prefix) > 0) {
    XLOGF(
        DBG3,
        "Modifying already deferred prefix received from Rib for {}: {}",
        getPeerName(),
        folly::IPAddress::networkToString(update.prefix));
    /*
     * Replace the existing update entry and return true to indicate deferred
     * processing will be in effect. No counter change: erase+emplace is a
     * replace, net size stays the same.
     */
    deferredUpdates_.emplace(update.prefix, update);
    return std::make_pair<bool, bool>(true, false);
  }
  /*
   * if prefix is seen firstime with newlyInstalledInLocalRib marked then add to
   * deferred list. Also if the entry's RIB install time stamp is within the
   * outdelay window (i.e. install time is < (currentTime - outdelay)) apply
   * outdelay.
   * TODO: The current logic defers the entry for outdelay seconds not the
   * delta. Technically sepaking delta is the right deferral period but that
   * compicates the overall timer mgmt logic too much in terms of (potentially)
   * tracking a timer per entry and later creating multiple timers per batch.
   * For now I have taken the simpler approach.
   */
  auto curTimeStamp = std::chrono::system_clock::now();
  std::chrono::duration<double> timeDiff =
      curTimeStamp - update.installTimeStamp - outDelay_;
  if ((timeDiff.count() <= 0) || (update.newlyInstalledInLocalRib)) {
    XLOGF(
        DBG3,
        "Adding new deferred prefix {} new {} time-diff {}sec "
        "received from Rib for {}",
        folly::IPAddress::networkToString(update.prefix),
        update.newlyInstalledInLocalRib,
        timeDiff.count(),
        getPeerName());
    if (deferredUpdates_.emplace(update.prefix, update).second) {
      deferredUpdatesSize_++;
      RibStats::incrDeferredUpdatesCount();
    }
    return std::make_pair<bool, bool>(true, true);
  }
  XLOGF(
      DBG4,
      "Not deferring the prefix for [{}] for peer {}",
      folly::IPAddress::networkToString(update.prefix),
      getPeerName());
  return std::make_pair<bool, bool>(false, false);
}

void AdjRib::programOutDelayTimer() noexcept {
  XLOGF(DBG2, "Out-Delay timer expired for {}.", getPeerName());

  /*
   * Pop the min-heap for all out-delay which are in the past. For each enqueued
   * out-delay entry, process the prefixes ONLY present in the deferredUpdates_
   * map. If not, just ignore since the "deferredUpdates_" serves the state
   * compression purpose with prefix withdrawals.
   */
  uint64_t processedCnt{0};
  auto now = std::chrono::system_clock::now();

  /* Iterate through the priority_queue until the top expires in the future */
  while (!outDelayPQ_.empty()) {
    const auto& topEntry = outDelayPQ_.top();
    if (topEntry.expiryTimeStamp > now) {
      /* Not yet, not yet */
      break;
    }
    for (const auto& pfx : topEntry.deferredPrefixes) {
      const auto& match = deferredUpdates_.find(pfx);
      if (match != deferredUpdates_.cend()) {
        processRibAnnouncedEntry(match->second);
        /* deferred update is processed. Purge it. */
        if (deferredUpdates_.erase(pfx) > 0) {
          deferredUpdatesSize_--;
          RibStats::decrDeferredUpdatesCount(1);
        }
      }
    }
    scheduleSendBgpUpdates(true /* tryPullNewChangeItems */);
    outDelayPQ_.pop();

    /*
     * TODO: the cnt does not reflect the real number of prefixes processed as
     * one entry may contain high number of prefixes. Refactor this.
     */
    if (!outDelayPQ_.empty() &&
        ++processedCnt >= nettools::bgplib::kMsgBatchSizeToYield) {
      outDelayTimer_->scheduleTimeout(kRescheduleOutDelayTimeoutMs);
      return;
    }
  }

  if (!outDelayPQ_.empty()) {
    /*
     * For the next timer to be scheduled, we want to be a bit loose about the
     * next timeout and fire with timeout after at least 250ms so as to club
     * multiple outdelay updates in one batch. This will be helpful during
     * intitial update after reboot as we will be getting a lot of rib-update
     * batches and those will come within O(50msec) of each other.
     */
    std::chrono::duration<double> delta =
        outDelayPQ_.top().expiryTimeStamp - std::chrono::system_clock::now();
    double timeout =
        std::max(ceil(delta.count()), kMinimumOutDelayTimeout) * 1000;
    outDelayTimer_->scheduleTimeout(timeout); /* unit in milliseconds */

    XLOGF(
        DBG4,
        "Schedule outDelay timer of {} millisecs for {}.",
        timeout,
        getPeerName());
  }
}

void AdjRib::processRibOutWithdrawal(
    const RibOutWithdrawal& withdrawal) noexcept {
  if (!egressEoRsSent_) {
    XLOGF(
        DBG1,
        "Ignoring Rib withdrawal for {} before sending initial dump",
        getPeerName());
    return;
  }
  const auto& entries =
      sendAddPath_ ? withdrawal.addPathEntries : withdrawal.entries;
  for (const auto& entry : entries) {
    XLOGF(
        DBG5,
        "Processing Rib withdrawal of {} for peer {}",
        folly::IPAddress::networkToString(entry.prefix),
        getPeerName());

    auto nh = entry.nh.has_value() ? entry.nh.value() : folly::IPAddress();
    processRibWithdraw(
        entry.prefix, resolvePathId(entry.prefix, nh, entry.pathIdToSend));

    // Update cached RIB version to track how caught up this peer is
    setLastSeenRibVersion(entry.ribVersion);
  }
}

/*
 * Check if we need to withdraw previously announced prefix due to
 * bestpath change and latest prefix/path no longer qualifies for announcing
 */
void AdjRib::handleImplicitWithdrawal(
    const RibOutAnnouncementEntry& entry) noexcept {
  const auto& prefix = entry.prefix;
  auto pathId =
      resolvePathId(prefix, entry.attrs->getNexthop(), entry.pathIdToSend);

  XLOGF(
      DBG3,
      "Implicit withdrawing prefix {} from {}.",
      folly::IPAddress::networkToString(prefix),
      getPeerName());

  processRibWithdraw(prefix, pathId);
}

void AdjRib::processRibWithdraw(
    const folly::CIDRNetwork& prefix,
    uint32_t pathId) noexcept {
  if (!AdjRibCommonUtils::isAfiNegotiated(
          prefix, isAfiIpv4Negotiated_, isAfiIpv6Negotiated_)) {
    XLOGF(
        DBG4,
        "Ignore RibWithdrawal of prefix {}: AFI is not supported for {}.",
        folly::IPAddress::networkToString(prefix),
        getPeerName());
    return;
  }

  /*
   * Note we remove the prefix from the deferredUpdates_ but don't cancel the
   * deferred timer. we let the timer run out and ignore any entry that is
   * not present in the deferredUpdates_ list.
   */
  if (deferredUpdates_.erase(prefix)) {
    deferredUpdatesSize_--;
    RibStats::decrDeferredUpdatesCount(1);
    XLOGF(
        DBG3,
        "Removed deferred entry of prefix {} for {} as we "
        "received withdrawal before out-delay expiry",
        folly::IPAddress::networkToString(prefix),
        getPeerName());
  }

  auto adjRibEntry = enableUpdateGroup_
      ? getRibEntryWithUpdateGroup(prefix, pathId)
      : getRibEntry(/*ingress=*/false, prefix, pathId);
  if (!adjRibEntry || !adjRibEntry->getPreOut()) {
    XLOGF(
        DBG3,
        "Received withdraw of prefix {} without announcement for {}",
        folly::IPAddress::networkToString(prefix),
        getPeerName());
    return;
  }

  adjRibEntry->setPreOut(nullptr);
  adjRibEntry->setPostOutPolicy({});
  stats_.decrementPreOutPrefixCount(prefix.first.isV4());
  if (adjRibEntry) {
    tryInsertWithdrawal(
        prefix,
        adjRibEntry,
        fmt::format(
            "Withdrawing prefix {} from {}",
            folly::IPAddress::networkToString(prefix),
            getPeerName()),
        fmt::format(
            "Ignoring Rib withdraw of prefix {} for unannounced route for {}",
            folly::IPAddress::networkToString(prefix),
            getPeerName()));
  }

  tryDeleteRibOutEntry(prefix, adjRibEntry, pathId);
}

void AdjRib::tryInsertWithdrawal(
    const folly::CIDRNetwork& prefix,
    AdjRibEntry* adjRibEntry,
    const std::string& insertedMsg,
    const std::string& notInsertedMsg) {
  auto oldPostAttr = adjRibEntry->getPostAttr();
  if (oldPostAttr) {
    /*
     * Withdraw the prefix only if we previously announced it to this peer
     * This can happen if attributes change for a prefix and policy based on
     * new attributes blocks the prefix. Withdraw it from peer.
     */
    XLOGF_IF(
        DBG1,
        stats_.getPostOutPrefixCount() == 0,
        "Invalid sent prefix count of {} for {}",
        folly::IPAddress::networkToString(prefix),
        getPeerName());
    stats_.decrementPostOutPrefixCount(prefix.first.isV4());

    adjRibEntry->setPostAttr(nullptr);

    auto prefixPathId = std::make_pair(prefix, adjRibEntry->getPathId());

    /*
     * Update attrToPrefixMap to move prefixPathId from the previous postAttrs
     * to the new postAttr (nullptr).
     */
    tryUpdateAttrToPrefixMap(
        prefixPathId, oldPostAttr, adjRibEntry->getPostAttr());

    XLOG(DBG3, insertedMsg);
  } else {
    // we din't inform peer before, so no need of informing to peer now.
    XLOG(DBG3, notInsertedMsg);
  }
}

/*
 * Try to delete AdjRibEntry if there is no longer any interest for this prefix
 * i.e. we have withdrawn it and no one is advertising this prefix
 */
void AdjRib::tryDeleteRibOutEntry(
    const folly::CIDRNetwork& prefix,
    const AdjRibEntry* adjRibEntry,
    uint32_t pathId) noexcept {
  if (adjRibEntry->getPreOut() || adjRibEntry->getPostAttr()) {
    return;
  }
  deleteRibEntry(/*ingress=*/false, prefix, pathId);
}

/*
 * DFP (Detached Fast Peer) check.
 * Returns true if peer finished draining PL before the group moved on CL.
 * DFP skips CL consumption and transitions directly to
 * DETACHED_READY_TO_JOIN with IS_DETACHED_FAST_PEER flag (no collapse needed).
 */
bool AdjRib::isDFP() const {
  return attrToPrefixMap_.empty() && adjRibOutGroup_ &&
      detachedRibVersion_ == lastSeenRibVersion_ &&
      adjRibOutGroup_->getLastSeenRibVersion() == lastSeenRibVersion_ &&
      !adjRibOutGroup_->getAttrToPrefixMap().empty();
}

/*
 * DSP (Detached Slow Peer) readiness check.
 * Returns true only after both the peer and group have consumed the entire
 * change list, peer packing list is empty, and their materialized RIB
 * versions match. Equal non-null markers and versions are insufficient: the
 * consumers can reach the same boundary after materializing different earlier
 * entries.
 */
bool AdjRib::canWaitForGroupToRejoin() const {
  if (!attrToPrefixMap_.empty()) {
    XLOGF(
        DBG2,
        "Peer {} not ready to rejoin group: packing list not empty",
        getPeerName());
    return false;
  }
  if (!changeListConsumer_) {
    XLOGF(
        DBG2,
        "Peer {} not ready to rejoin group: changeListConsumer is null",
        getPeerName());
    return false;
  }
  if (!adjRibOutGroup_) {
    XLOGF(
        DBG2,
        "Peer {} not ready to rejoin group: adjRibOutGroup is null",
        getPeerName());
    return false;
  }
  const auto groupConsumer = adjRibOutGroup_->getChangeListConsumer();
  if (!groupConsumer) {
    XLOGF(
        DBG2,
        "Peer {} not ready to rejoin group: group changeListConsumer is null",
        getPeerName());
    return false;
  }
  if (!changeListConsumer_->isReady()) {
    XLOGF(
        DBG2,
        "Peer {} not ready to rejoin group: peer change list is not fully consumed",
        getPeerName());
    return false;
  }
  if (!groupConsumer->isReady()) {
    XLOGF(
        DBG2,
        "Peer {} not ready to rejoin group: group change list is not fully consumed",
        getPeerName());
    return false;
  }
  if (adjRibOutGroup_->getLastSeenRibVersion() != lastSeenRibVersion_) {
    XLOGF(
        DBG2,
        "Peer {} not ready to rejoin group: peer lastSeenRibVersion {} != "
        "group lastSeenRibVersion {}",
        getPeerName(),
        lastSeenRibVersion_,
        adjRibOutGroup_->getLastSeenRibVersion());
    return false;
  }
  return true;
}

bool AdjRib::isReadyToRejoinGroup() const {
  /*
   * Shared conditions first; it also validates adjRibOutGroup_, so the group
   * dereference below is safe.
   */
  if (!canWaitForGroupToRejoin()) {
    return false;
  }
  if (!adjRibOutGroup_->getAttrToPrefixMap().empty()) {
    XLOGF(
        DBG2,
        "Peer {} not ready to rejoin group: group packing list not empty",
        getPeerName());
    return false;
  }
  return true;
}

bool AdjRib::isDetachedPeer() const {
  return peerState_ == PeerUpdateState::DETACHED_INIT_DUMP ||
      peerState_ == PeerUpdateState::DETACHED_READY_TO_JOIN ||
      peerState_ == PeerUpdateState::DETACHED_BLOCKED ||
      peerState_ == PeerUpdateState::DETACHED_RUNNING;
}

/*
 * Check if a DETACHED_RUNNING peer should transition to
 * DETACHED_READY_TO_JOIN. Called at the end of sendBgpUpdates() after PL
 * drain when update groups are enabled and peer is in DETACHED_RUNNING state.
 *
 * DFP: group hasn't moved since detach -> sets IS_DETACHED_FAST_PEER flag,
 *      no collapse needed on rejoin.
 * DSP: peer caught up independently -> collapse verification needed on rejoin.
 *      DSP can initiate rejoin if the group is ready/IDLE.
 * Neither: reschedule packing timers to continue processing.
 */
void AdjRib::transitionPeerUpdateState() noexcept {
  if (!adjRibOutGroup_ ||
      (peerState_ != PeerUpdateState::DETACHED_INIT_DUMP &&
       peerState_ != PeerUpdateState::DETACHED_RUNNING)) {
    return;
  }
  if (isRibDumpScheduled()) {
    XLOGF(
        DBG1,
        "Peer {}: deferring detached rejoin while a RIB dump is scheduled",
        getPeerName());
    return;
  }
  const bool completedDetachedInitDump =
      peerState_ == PeerUpdateState::DETACHED_INIT_DUMP;
  /*
   * DETACHED_ON_REGISTRATION peers were never in sync with the group, so they
   * can never be DFP — they must always go through the DSP rejoin path
   * with collapse verification.
   */
  if (!isAdjRibFlagSet(DETACHED_ON_REGISTRATION) && isDFP()) {
    setAdjRibFlag(IS_DETACHED_FAST_PEER);
    XLOGF(
        DBG1,
        "Peer {}: State Transition: {} (DFP) -> {}",
        getPeerName(),
        peerState_,
        PeerUpdateState::DETACHED_READY_TO_JOIN);

    setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
    cancelPackingTimers();
  } else if (canWaitForGroupToRejoin()) {
    XLOGF(
        DBG1,
        "Peer {}: State Transition: {} (DSP) -> {}",
        getPeerName(),
        peerState_,
        PeerUpdateState::DETACHED_READY_TO_JOIN);

    setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
    cancelPackingTimers();

    adjRibOutGroup_->maybeAcceptDSPPeer(shared_from_this());
  } else if (!adjRibOutGroup_->hasEntrySharingPeers()) {
    /*
     * If the group has no SYNC peers and no sharing DSP, it is frozen waiting
     * for a detached peer to promote itself (handleNoSyncPeers). This peer can
     * re-seed the group. Only do so when no detached peer still shares the
     * group's entries (numPeersDetachedAfterJoin_ == 0).
     */
    XLOGF(
        DBG1,
        "Group {}: Promoting peer {} at bit {} in group with no sharing peers at rib version {}, "
        "State Transition: {} -> {}",
        adjRibOutGroup_->getGroupDescriptor(),
        getPeerName(),
        getGroupBitPosition(),
        lastSeenRibVersion_,
        peerState_,
        PeerUpdateState::DETACHED_READY_TO_JOIN);
    setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
    cancelPackingTimers();
    adjRibOutGroup_->promoteDetachedPeerToSync(shared_from_this());
    adjRibOutGroup_->scheduleChangeListConsumeTimer();
  } else if (lastSeenRibVersion_ > adjRibOutGroup_->getLastSeenRibVersion()) {
    /*
     * Peer is ahead of the group on the CL (DEP-A). Park it in
     * DETACHED_READY_TO_JOIN; checkAndAcceptReadyToJoinPeers picks it up on a
     * later group tick. The group reaching the peer's version is not enough:
     * acceptance needs both consumers at the end of the CL with their packing
     * lists drained (isReadyToRejoinGroup).
     */
    XLOGF(
        DBG1,
        "Group {}: Peer {} at bit {} with lastSeenRibVersion {} ahead of group {}, "
        "State Transition: {} -> {}",
        adjRibOutGroup_->getGroupDescriptor(),
        getPeerName(),
        getGroupBitPosition(),
        lastSeenRibVersion_,
        adjRibOutGroup_->getLastSeenRibVersion(),
        peerState_,
        PeerUpdateState::DETACHED_READY_TO_JOIN);
    setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
    cancelPackingTimers();
  } else {
    /*
     * Otherwise, the peer is behind the group on the changelist. Let the peer
     * continue consuming behind and up to the group.
     */
    reschedulePackingTimers();
  }
  if (completedDetachedInitDump &&
      peerState_ != PeerUpdateState::DETACHED_INIT_DUMP) {
    /* remotePeerId_ is installed by the constructor and is never reset. */
    fromAdjRibQ_.push({getRemotePeerId(), DetachedInitDumpCompleted{}});
  }
}

/*
 * Start the detached peer's independent processing loop.
 * Called when a DETACHED_BLOCKED peer unblocks.
 *
 * Schedules sendBgpUpdates to drain PL. sendBgpUpdates handles DFP/DSP
 * transitions and timer scheduling at the end of the drain.
 */
void AdjRib::activateDetachedModeProcessing() {
  XLOGF(INFO, "Peer {}: Activating detached mode processing", getPeerName());

  /*
   * Schedule PL drain. DFP/DSP checks happen at end of sendBgpUpdates.
   * CL consumption will also be pumped through scheduleSendBgpUpdates entry
   * point by scheduling the packing timers (changeListConsumeTimer_).
   */
  scheduleSendBgpUpdates(false /* tryPullNewChangeItems */);
}

/*
 * Clean up detached mode processing state on rejoin or peer down.
 */
void AdjRib::deactivateDetachedModeProcessing() {
  XLOGF(DBG1, "Peer {}: Deactivating detached mode processing", getPeerName());
  resetChangeListConsumer();
  cancelPackingTimers();
  if (adjRibOutGroup_ && getDetachedRibVersion() > 0) {
    adjRibOutGroup_->decrementPeersDetachedAfterJoin();
  }
  setDetachedRibVersion(0);
  clearAdjRibFlag(DETACHED_ON_REGISTRATION);
}

} // namespace facebook::bgp
