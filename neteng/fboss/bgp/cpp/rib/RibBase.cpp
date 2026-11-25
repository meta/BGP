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

#include <boost/filesystem.hpp>
#include <cmath>
#include <exception>

#include <folly/CppAttributes.h>
#include <folly/FileUtil.h>
#include <folly/container/F14Set.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/CurrentExecutor.h>
#include <folly/coro/Sleep.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/json/json.h>
#include <folly/logging/xlog.h>

#include "magic_enum/magic_enum.hpp"

#include "common/network/AddressUtil.h"
#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/BgpProfiler.h"
#include "neteng/fboss/bgp/cpp/BgpServiceUtil.h"
#include "neteng/fboss/bgp/cpp/common/BgpError.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/Utils.h"
#include "neteng/fboss/bgp/cpp/config/ConfigUtils.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopCache.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopInfo.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyStructs.h"
#include "neteng/fboss/bgp/cpp/rib/FibDev.h"
#include "neteng/fboss/bgp/cpp/rib/FibOutThrift.h"
#include "neteng/fboss/bgp/cpp/rib/RibBase.h"
#include "neteng/fboss/bgp/cpp/rib/RibFileUtils.h"
#include "neteng/fboss/bgp/cpp/rib/RibPolicy.h"
#include "neteng/fboss/bgp/cpp/rib/RouteFilterConfig.h"
#include "neteng/fboss/bgp/cpp/rib/Utils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/if/gen-cpp2/BgpStructs_types.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"
#include "thrift/lib/cpp2/protocol/Serializer.h"

DEFINE_string(
    rp_state_file,
    // patternlint-disable-next-line no-dev-shm-usage
    "/dev/shm/bgp_rp_state.txt",
    "File in which Rib Policy stateful information is stored across bgp restarts");

DEFINE_string(
    rp_change_history_file,
    facebook::bgp::kRpChangeHistoryFilePath,
    "Path to RIB policy change history file");

DEFINE_bool(
    enable_default_route_logging,
    false,
    "Log default route has its path changed");

namespace facebook {
namespace bgp {

namespace {
BgpRouteType getBgpPathType(const RouteInfo* bgpPath) {
  if (bgpPath->getIsRouteLocal()) {
    return BgpRouteType::LOCAL;
  } else if (bgpPath->getIsRouteExternal()) {
    return BgpRouteType::EBGP;
  } else if (bgpPath->getIsRouteConfedExternal()) {
    return BgpRouteType::ConfedEBGP;
  } else {
    return BgpRouteType::IBGP;
  }
}
} // namespace

using namespace neteng::fboss::bgp_attr;
using namespace neteng::fboss::bgp::thrift;

using facebook::network::toBinaryAddress;
using folly::IPAddress;
using nettools::bgplib::BgpPeerId;

RibBase::FibProgrammedPrefixIterator::FibProgrammedPrefixIterator(
    const RibVrf& vrf,
    const Fib::FibProgrammedPfxToNexthops& pfxNhs)
    : vrf_(vrf), pfxNhs_(pfxNhs) {}

Fib::FibProgrammedPfxToNexthops::const_iterator
RibBase::FibProgrammedPrefixIterator::cbegin() {
  return next();
}

Fib::FibProgrammedPfxToNexthops::const_iterator
RibBase::FibProgrammedPrefixIterator::cend() {
  return pfxNhs_.cend();
}

Fib::FibProgrammedPfxToNexthops::const_iterator
RibBase::FibProgrammedPrefixIterator::next() {
  if (!inited_) {
    localRouteIter_ = vrf_.localRoutes().cbegin();
    inited_ = true;
  }
  /*
   * if we are in summaryRoutesPass, check if next
   * localRouteIter_ is presnet in the pfxNhs_ batch.
   * If found return the same else proceed to next localRoute.
   * if all localRoutes are exhausted i.e. summaryRoutesPass_ is false
   * iterate over all routes returned in the map's iterator order
   * and return any which is not present in localRoutes.
   */
  if (summaryRoutesPass_) {
    while (localRouteIter_ != vrf_.localRoutes().cend()) {
      const auto& elem = pfxNhs_.find(localRouteIter_->first);
      ++localRouteIter_;
      // if local route matches fib-batch then return it
      if (elem != pfxNhs_.cend()) {
        return elem;
      }
    }
    nonLocalRouteIter_ = pfxNhs_.cbegin();
    summaryRoutesPass_ = false;
  }
  while (nonLocalRouteIter_ != pfxNhs_.cend()) {
    // Skip prefix that macthes local-route as it is alrady handled above.
    if (vrf_.localRoutes().find(nonLocalRouteIter_->first) ==
        vrf_.localRoutes().cend()) {
      auto ret = nonLocalRouteIter_;
      ++nonLocalRouteIter_;
      return ret;
    } else {
      ++nonLocalRouteIter_;
    }
  }
  return pfxNhs_.cend();
}

RibBase::RibBase(
    const std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>&
        localRoutes,
    const BgpGlobalConfig& globalConfig,
    const std::optional<bgp_policy::BgpPolicies>& policyConfig,
    nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ,
    MonitoredMPMCQueue<RibOutMessage>& ribOutQ,
    const std::string& platform,
    std::shared_ptr<NexthopCache> nexthopCache,
    uint16_t fibAgentPort,
    uint32_t fibAgentRecvTimeout)
    : BgpModuleBase(kModuleRib),
      globalConfig_(globalConfig),
      platform_(platform),
      ribInQ_(ribInQ),
      ribOutQ_(ribOutQ),
      enableNexthopTracking_(globalConfig.enableNextHopTracking),
      nexthopCache_(nexthopCache),
      fibAgentPort_(fibAgentPort),
      fibAgentRecvTimeout_(fibAgentRecvTimeout),
      enableRibAllocatedPathId_(globalConfig.enableRibAllocatedPathId),
      defaultVrf_(createVrf(fboss::kDefaultVrf)) {
  // TODO: re-enable the quarantine VRF.
  // quarantineVrf_(createVrf(fboss::kQuarantineVrfId))
  // init switchId best effort if deviceName is set in global config
  switchId_ = facebook::bgp::getSwitchId(globalConfig_.deviceName);
  // add monitoring for inter-thread queues
  monitorQueue(kQueueNameRibIn, ribInQ_, MonitorableQueueTrace::Direction::IN);
  monitorQueue(
      kQueueNameRibOut, ribOutQ_, MonitorableQueueTrace::Direction::OUT);

  multipathSelector_ = std::make_unique<facebook::bgp::RouteInfoSelector>(
      facebook::bgp::getBaseRouteFilterConfigsMultiPath(
          globalConfig_.countConfedsInAsPathLen));
  bestpathSelector_ = std::make_unique<facebook::bgp::RouteInfoSelector>(
      facebook::bgp::getRouteFilterConfigsBestPath(
          globalConfig_.countConfedsInAsPathLen));

  // policy manager will be created only if policies are configured
  if (policyConfig.has_value()) {
    policyManager_ = std::make_shared<facebook::bgp::PolicyManager>(
        policyConfig.value(), &globalConfig_ /* config */);
  }

  // populate LocalRouteStore
  for (const auto& [prefix, bgpNetwork] : localRoutes) {
    auto localRouteOpt = createLocalRoute(prefix, bgpNetwork);
    if (!localRouteOpt.has_value()) {
      // ATTN: invalid local routes
      continue;
    }
    // TODO_PMTRIZED_VRF: Local routes can only be in the default VRF for now.
    defaultVrf_.localRoutes().emplace(prefix, localRouteOpt.value());
    /*
     * store a set of local routes requiring conditional origination
     * to avoid full walk of localRoutes_ every time nexthop resolution changes
     */
    if (getRequireNhResolution(localRouteOpt->network)) {
      // if require_nexthop_resolution is set, nexthop must be non-empty
      assert(localRouteOpt->network.nexthop());
      auto nexthop = folly::IPAddress(*localRouteOpt->network.nexthop());
      defaultVrf_.conditionalLocalRoutes()[nexthop].push_back(prefix);
    }
  }
  // TODO_NH_VRF: Set the originated route count in the counters of its VRF.
  defaultVrf_.ribCounters().setOriginatedRoutes(
      defaultVrf_.localRoutes().size());

  XLOGF(
      INFO,
      "Created Rib with fibBatchTime_= {}ms, computeUcmpFromLbwComm:{}",
      fibBatchTime_.count(),
      static_cast<bool>(globalConfig_.computeUcmpFromLbwComm));
}

RibVrf& RibBase::createVrf(VrfId vrfId) {
  auto& ribVrf = ribVrfs_[vrfId];
  if (!ribVrf) {
    ribVrf = std::make_unique<RibVrf>(vrfId, *this);
  }
  return *ribVrf;
}

RibVrf* FOLLY_NULLABLE RibBase::findVrf(VrfId vrfId) noexcept {
  if (vrfId == fboss::kDefaultVrf) {
    return &defaultVrf_;
  }
  auto it = ribVrfs_.find(vrfId);
  return it == ribVrfs_.end() ? nullptr : it->second.get();
}

const RibVrf* FOLLY_NULLABLE RibBase::findVrf(VrfId vrfId) const noexcept {
  if (vrfId == fboss::kDefaultVrf) {
    return &defaultVrf_;
  }
  auto it = ribVrfs_.find(vrfId);
  return it == ribVrfs_.end() ? nullptr : it->second.get();
}

RibBase::~RibBase() {
  /*
   * Cancel the nexthop-resolution timer so its callback (which captures `this`)
   * cannot fire after destruction. It is armed only while a deferred initial
   * full-sync is pending; without this, tearing down RibBase with that wait
   * outstanding could invoke onNexthopResolutionTimeout() on a destroyed
   * object (use-after-free).
   */
  for (auto& [_, ribVrf] : ribVrfs_) {
    if (ribVrf->nexthopResolutionTimer()) {
      ribVrf->nexthopResolutionTimer()->cancelTimeout();
    }
  }
  // reset the batch list
  for (auto& [_, ribVrf] : ribVrfs_) {
    ribVrf->fibBatchList().clear();
  }
}

void RibBase::setFibBatchTime(std::chrono::milliseconds d) {
  fibBatchTime_ = d;
  XLOGF(INFO, "Set fibBatchTime_ to {}ms", fibBatchTime_.count());
}

void RibBase::createFib() {
  XLOG(DBG1, "Creating Fib Dev with no actual route programming");
  fib_ = FibDev::createFibDev(fromFibMessageQ_);
}

void RibBase::run() noexcept {
  createFib();

  // folly::coro tasks
  asyncScope_.add(co_withExecutor(&evb_, processRibInMsgLoop()));
  asyncScope_.add(co_withExecutor(&evb_, processFibMsgLoop()));
  asyncScope_.add(co_withExecutor(&evb_, processFibProgrammingMsgLoop()));
  asyncScope_.add(co_withExecutor(&evb_, processRibPolicyMsgLoop()));
  /*
   * Attention: processLocalRoutesRoutine() should be initialized after
   * processRibInMsgLoop()
   */
  asyncScope_.add(co_withExecutor(&evb_, processLocalRoutesRoutine()));
  if (FLAGS_bgp_coro_profiler_export_ods) {
    asyncScope_.add(co_withExecutor(&evb_, co_exportProfilerStatsLoop()));
  }

  XLOG(INFO, "Start Rib event-base loop");
  evb_.loopForever();
  XLOG(INFO, "[Exit] Successfully terminated Rib event-base");
}

/**
 * @brief Shutdown the Rib module. Template method; see header for the fixed
 * step order and rationale.
 */
void RibBase::stop() noexcept {
  cancelAndJoinTasks();
  cleanupCommon();
  cleanupPlatform();

  /*
   * Terminate the evb loop LAST: cleanupCommon() and cleanupPlatform() reset
   * evb-bound timers via runImmediatelyOrRunInEventBaseThreadAndWait, which
   * would deadlock if the loop were already stopped.
   */
  evb_.terminateLoopSoon();
}

/**
 * @brief [Exit] Step 1: cancel and join all Rib coroutines.
 *
 * After this returns no coroutine in asyncScope_ is running, so resources they
 * may access (timers, data structures) can be destroyed safely in the steps
 * that follow.
 */
void RibBase::cancelAndJoinTasks() noexcept {
  XLOG(INFO, "[Exit] Cancel and stop all coroutines");
  folly::coro::blockingWait(asyncScope_.cancelAndJoinAsync());
}

/**
 * @brief [Exit] Step 2: common (platform-agnostic) teardown.
 *
 * Resets core timers and stops the FIB on the evb thread, destroys the FIB,
 * and clears heavy data structures. MUST run only after cancelAndJoinTasks().
 * Does NOT terminate the evb loop — stop() does that after cleanupPlatform().
 */
void RibBase::cleanupCommon() noexcept {
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    for (auto& [_, ribVrf] : ribVrfs_) {
      ribVrf->fibBatchTimer().reset();
    }
    if (fib_) {
      fib_->stop();
    }
  });

  fib_.reset();

  /**
   * Clear heavy data structures during controlled shutdown to prevent
   * systemd timeout during destructor. The ribEntries_ map can contain
   * hundreds of thousands of routes, each with nested shared_ptr<RouteInfo>.
   * Clearing here while nexthopInfoMap_ is still valid lets RouteInfo
   * destructors unlink from their NexthopInfo association lists.
   */
  for (auto& [_, ribVrf] : ribVrfs_) {
    ribVrf->clear();
  }
  XLOG(INFO, "[Exit] ribEntries_ cleared");

  /*
   * kNexthopInfoCount counter does not need to be reset here;
   * device bootup will initialize the ODS counter to 0
   */
  nexthopInfoMap_.clear();
}

/*
 * This is the coro loop to trigger the actual FIB programming.
 *
 * This method will refer to the following:
 *
 *  - fibBatchList_: the shared state can be modified in:
 *    - fibProgrammingLoop();
 *    - prepareFibProgramming();
 *  - fibProgrammingRequests_: the intrusive list of VRFs allowing RIB to
 *                             start the programming cycle;
 *
 * prepareFibProgramming links a VRF onto the list at most once:
 *  - with fullSync_ unset: generated every fibBatchTime_;
 *  - with fullSync_ set: generated by either EOR or fullSync request from FIB;
 *  - Termination: RibBase::stop() cancels asyncScope_, which cancels the
 *    wait in fibProgrammingRequests_.pop() to close the coro task;
 */
folly::coro::Task<void> RibBase::processFibProgrammingMsgLoop() noexcept {
  while (true) {
    // when cancelAndJoinAsync is called, guaranteed to exit
    co_await folly::coro::co_safe_point;

    auto request = co_await co_awaitTry(fibProgrammingRequests_.pop());

    // pop() fails only when asyncScope_ is cancelled.
    if (!request.hasValue()) {
      XLOG(
          INFO,
          "[Exit] Coro task cancelled. Terminating processFibProgrammingMsgLoop.");
      break;
    }
    auto& vrf = request->get();
    const bool fullSync = vrf.fullSync();

    if (!fib_->isConnected()) {
      XLOG(INFO, "Fib agent is not connected. Skipping fib batch programming.");
      continue;
    }

    if (!fib_->isFullSynced() && !fullSync) {
      XLOG(INFO, "Skipping add/delete route calls before syncFib.");
      continue;
    }

    const auto toFibTotal = vrf.populateFibBatch(fib_.get());
    deleteUnusedNexthopInfos();
    if (fullSync || toFibTotal) {
      XLOGF(DBG2, "Added {} prefixes to Fib batch", toFibTotal);
      /*
       * Clear fullSync_ before co_awaiting program(), not after. While
       * program() is suspended, prepareFibProgramming can genuinely mark
       * another full sync for this VRF; clearing afterwards would
       * accidentally reset that request to false.
       */
      vrf.setFullSync(false);
      /*
       * TODO_FIB_VRF: FibFboss::program() still calls the VRF-0 agent APIs
       * (syncFib, addUnicastRoutes, deleteUnicastRoutes); vrfId only tags the
       * completion message. Program each VRF through the vrf-aware agent APIs.
       */
      co_await fib_->program(fullSync, vrf.vrfId());
    }
  }
}

folly::coro::Task<void> RibBase::processFibMsgLoop() noexcept {
  while (true) {
    // when cancelAndJoinAsync is called, guaranteed to exit
    co_await folly::coro::co_safe_point;

    auto msg = co_await co_awaitTry(fromFibMessageQ_.pop());
    if (!msg.hasValue()) {
      XLOG(INFO, "[Exit] Coro task cancelled. Terminating processFibMsgLoop.");
      break;
    }

    folly::variant_match(
        *msg,
        [this](const Fib::FibProgrammedMessage& msg) {
          handleFibProgrammedMessage(msg);
        },
        [this](const Fib::FibSyncReq& req) { handleFibSyncReq(req); });
  }
}

void RibBase::handleRibPolicyClearMsg() noexcept {
  replaceRibPolicy(nullptr);
}

/*
 * Thread-safe: ribPolicyMsgQ_ is a coalescing MergeQueue guarded by its own
 * lock, so callers enqueue directly from any thread (e.g. a thrift handler)
 * rather than hopping onto the RIB evb. Coalescing ahead of the evb keeps a
 * busy consumer (a long RIB walk) from accumulating a backlog of per-call evb
 * tasks.
 */
void RibBase::enqueueRibPolicyMsg(RibPolicyMessage msg) noexcept {
  RibStats::STATS_ribPolicyMsgEnqueued.add(1);
  /*
   * Each sub-policy's set and clear share one slot, so a pending set and a
   * later clear (or vice versa) coalesce to the latest -- a single-sub-policy
   * purge is just an in-place merge. RibPolicyClearMsg maps to
   * kRibPolicyPurgeAllSlot and drops everything queued.
   *
   * Coalescing two route-filter sets can drop an earlier one's forceUpdate
   * flag; this is intended. forceUpdate=true is enqueued only by
   * setCrfPolicyFromFile (FILE_MODE) and forceUpdate=false only by thrift sets,
   * and crfPolicyMutex_ rejects thrift sets while FILE_MODE is on, so the two
   * never coexist in the queue except across a FILE_MODE flip. There the later
   * message is the superseding intent and must keep its own version-check
   * semantics -- carrying an earlier file policy's force onto a later thrift
   * set would wrongly bypass that check.
   */
  const int slot = folly::variant_match(
      msg,
      [](const RibPolicyClearMsg&) { return kRibPolicyPurgeAllSlot; },
      [](const RouteAttributePolicySetMsg&) {
        return kRibPolicyMergeSlotRouteAttribute;
      },
      [](const RouteAttributePolicyClearMsg&) {
        return kRibPolicyMergeSlotRouteAttribute;
      },
      [](const RouteAttributePolicyTimerMsg&) {
        return kRibPolicyMergeSlotRouteAttributeTimer;
      },
      [](const PathSelectionPolicySetMsg&) {
        return kRibPolicyMergeSlotPathSelection;
      },
      [](const PathSelectionPolicyClearMsg&) {
        return kRibPolicyMergeSlotPathSelection;
      },
      [](const RouteFilterPolicySetMsg&) {
        return kRibPolicyMergeSlotRouteFilter;
      },
      [](const RouteFilterPolicyClearMsg&) {
        return kRibPolicyMergeSlotRouteFilter;
      });
  if (slot == kRibPolicyPurgeAllSlot) {
    // A clear-all supersedes everything already queued.
    RibStats::STATS_ribPolicyMsgPurged.add(1);
    ribPolicyMsgQ_.pushPurgeAll(std::move(msg));
  } else if (ribPolicyMsgQ_.pushMerge(std::move(msg), slot)) {
    /*
     * Coalesced into an already-pending same-slot message -- an apply the
     * consumer will now skip.
     */
    RibStats::STATS_ribPolicyMsgCoalesced.add(1);
  }
}

void RibBase::handleRouteFilterPolicySetMsg(
    const RouteFilterPolicySetMsg& msg) noexcept {
  replaceRouteFilterPolicy(
      std::make_unique<RouteFilterPolicy>(msg.policy),
      /*isBootstrap=*/false,
      msg.forceUpdate);
}

void RibBase::handleRouteFilterPolicyClearMsg() noexcept {
  replaceRouteFilterPolicy(nullptr);
}

folly::coro::Task<void> RibBase::processRibInMsgLoop() noexcept {
  XLOG(INFO, "Starting ribInMsg processing coro task...");

  // ribInQ_ now has a consumer
  auto consumerScope = ribInQ_.getConsumerScope();

  while (true) {
    // when cancelAndJoinAsync is called, guaranteed to exit
    co_await folly::coro::co_safe_point;

    auto msg = co_await co_awaitTry(ribInQ_.pop());
    if (!msg.hasValue()) {
      XLOG(INFO, "[Exit] Coro task cancelled. Terminating processRibInMsgLoop");
      break;
    }

    folly::variant_match(
        *msg,
        [this](const RibInAnnouncement& announcement) {
          auto* vrf = findVrf(announcement.vrfId);
          if (!vrf) {
            XLOGF(
                ERR,
                "Ignoring RibInAnnouncement for non-existent VRF {}",
                announcement.vrfId);
            return;
          }
          processRibInAnnouncement(
              *vrf,
              announcement.peer,
              announcement.attrs,
              announcement.pfxPathIds);
        },
        [this](const RibInWithdrawal& withdrawal) {
          auto* vrf = findVrf(withdrawal.vrfId);
          if (!vrf) {
            XLOGF(
                ERR,
                "Ignoring RibInWithdrawal for non-existent VRF {}",
                withdrawal.vrfId);
            return;
          }
          processRibInWithdrawal(*vrf, withdrawal.peer, withdrawal.pfxPathIds);
        },
        [this](const RibInInitialPathComputation& initialPathComputation) {
          processRibInInitialPathComputation(
              initialPathComputation.vrfId,
              initialPathComputation.nexthopResolutionTimeout);
        },
        [this](const RibInNexthopUpdate& nexthopUpdate) {
          processRibInNexthopUpdate(nexthopUpdate);
        },
        [this](
            const PauseBestPathAndFibProgramming&
                pauseBestPathAndFibProgramming) {
          processPauseBestPathAndFibProgramming(pauseBestPathAndFibProgramming);
        },
        [this](
            const ResumeBestPathAndFibProgramming&
                resumeBestPathAndFibProgramming) {
          processResumeBestPathAndFibProgramming(
              resumeBestPathAndFibProgramming);
        },
        [this](const NexthopResolutionUpdate& nexthopResolutionUpdate) {
          processNexthopResolutionUpdate(nexthopResolutionUpdate);
        },
        [](const RibInAddPathGrUpdate& /* addPathGrUpdate */) {
          // RibInAddPathGrUpdate unimplemented
        });

    co_await folly::coro::co_reschedule_on_current_executor;
  }
}

folly::coro::Task<void> RibBase::processLocalRoutesRoutine() noexcept {
  // advertise local routes that does not need minimum upporting routes
  XLOG(INFO, "Starting local route advertising coro task...");

  /*
   * TODO_PMTRIZED_VRF: Advertise the local routes of every VRF. Only the
   * default VRF has local routes for now, because the config does not assign
   * them to a VRF.
   */
  for (const auto& [prefix, localRoute] : defaultVrf_.localRoutes()) {
    /*
     * If min_supporting_route is non 0, or if require_nexthop_resolution is
     * set, we cannot simply originate the local route here. Instead, we need to
     * wait for the corresponding condition to be met and originate on-demand.
     */
    if (getMinSupportRoutes(localRoute.network) == 0 &&
        !getRequireNhResolution(localRoute.network)) {
      PrefixPathIds pfxPathIds{{prefix, kDefaultPathID}};
      co_await ribInQ_.push(RibInAnnouncement(
          fboss::kDefaultVrf, kV4LocalPeerInfo, pfxPathIds, localRoute.attrs));
    }
  }

  XLOG(INFO, "Local route advertisement coro task finished");
  co_return;
}

void RibBase::processRibInAnnouncement(
    RibVrf& vrf,
    const TinyPeerInfo& peer,
    std::shared_ptr<const BgpPath> attrs,
    const PrefixPathIds& pfxPathIds) noexcept {
  XLOGF(
      DBG2,
      "Processing RibInAnnouncement from [{}] with {} prefixes",
      BgpPeerId{peer.addr, peer.routerId}.str(),
      pfxPathIds.size());

  auto unlinkRouteInfo = [this](RouteInfo& routeInfo) {
    return unlinkRouteInfoAndCleanupNexthop(routeInfo);
  };
  for (const auto& pfxPathId : pfxPathIds) {
    /*
     * As per RFC-4724 we must send all our local routes along with
     * peer-received routes before we send our first EoR after graceful-restart.
     * Following code injects the min-supporting local summary prefixes before
     * other received prefixes so that FIB can be programmed and summary routes
     * can be advertised to peers before we send EoR. Process the aggregate
     * routes first
     */
    auto aggs = vrf.processSingleRibInUpdate(
        peer, attrs, pfxPathId, getNexthopInfo(attrs), unlinkRouteInfo);
    for (auto& [network, bgpAttrs] : aggs) {
      /*
       * aggs iterated here MUST be local originated routes, not received
       * routes. Hence they always use default path ID
       */
      PrefixPathId aggPfxPid{network, kDefaultPathID};
      vrf.processSingleRibInUpdate(
          kV4LocalPeerInfo,
          bgpAttrs,
          aggPfxPid,
          getNexthopInfo(bgpAttrs),
          unlinkRouteInfo);
    }
  }
  vrf.ribCounters().publishTotalPathCounters();
  // Subscribe any nexthops newly learned while processing this batch.
  maybeFlushNexthopSubscriptions();
  if (vrf.ribEoRReceived()) {
    // Monitor prefix count to detect route churn
    incrementPrefixCountForRouteChurn(pfxPathIds.size());

    // Schedule incremental fib programming timer
    vrf.schedulePrepareFibProgrammingTimer();
  }
}

void RibBase::maybeFlushNexthopSubscriptions() noexcept {
  if (pendingNexthopSubscriptions_.empty()) {
    return;
  }
  if (nexthopSubscribeRequester_) {
    nexthopSubscribeRequester_(std::move(pendingNexthopSubscriptions_));
  }
  // Ensure the buffer is empty even if it was moved-from or requester unset.
  pendingNexthopSubscriptions_.clear();
}

void RibBase::processRibInWithdrawal(
    RibVrf& vrf,
    const TinyPeerInfo& peer,
    const PrefixPathIds& pfxPathIds) noexcept {
  XLOGF(
      DBG2,
      "Processing RibInWithdrawal from [{}] with {} prefixes",
      BgpPeerId{peer.addr, peer.routerId}.str(),
      pfxPathIds.size());

  auto unlinkRouteInfo = [this](RouteInfo& routeInfo) {
    return unlinkRouteInfoAndCleanupNexthop(routeInfo);
  };
  for (const auto& pfxPathId : pfxPathIds) {
    /*
     * As per RFC-4724 we must send all our local routes along with
     * peer-received routes before we send our first EoR after graceful-restart.
     * Following code injects the min-supporting local summary prefixes before
     * other received prefixes so that FIB can be programmed and summary routes
     * can be advertised to peers before we send EoR. Process the aggregate
     * routes first
     */
    auto aggs = vrf.processSingleRibInUpdate(
        peer, nullptr, pfxPathId, nullptr /* nexthopInfo */, unlinkRouteInfo);

    for (auto& [network, bgpAttrs] : aggs) {
      /*
       * aggs iterated here MUST be local originated routes, not received
       * routes. Hence they always use default path ID
       */
      PrefixPathId aggPfxPid{network, kDefaultPathID};
      vrf.processSingleRibInUpdate(
          kV4LocalPeerInfo,
          bgpAttrs,
          aggPfxPid,
          getNexthopInfo(bgpAttrs),
          unlinkRouteInfo);
    }
  }
  vrf.ribCounters().publishTotalPathCounters();
  // Aggregate/local routes injected above may introduce new nexthops.
  maybeFlushNexthopSubscriptions();
  if (vrf.ribEoRReceived()) {
    // Monitor prefix count to detect route churn
    incrementPrefixCountForRouteChurn(pfxPathIds.size());

    // Schedule incremental fib programming timer
    vrf.schedulePrepareFibProgrammingTimer();
  }
}

void RibBase::processRibInInitialPathComputation(
    VrfId vrfId,
    std::chrono::milliseconds nexthopResolutionTimeout) noexcept {
  XLOG(INFO, "Processing RibInInitialPathComputation");

  auto* vrf = findVrf(vrfId);
  if (!vrf) {
    XLOGF(
        ERR,
        "Ignoring RibInInitialPathComputation for non-existent VRF {}",
        vrfId);
    return;
  }

  // avoid duplicate initial full-sync to program FIB
  if (vrf->ribEoRReceived()) {
    XLOG(
        DBG4,
        "Ignore RibInInitialPathComputation since Rib has already started best-path");
    return;
  }

  /*
   * Nexthop-resolution-gated startup: by the time this message is dequeued,
   * every route announcement enqueued ahead of it (e.g. GR-replayed routes) is
   * already processed, so nexthopInfoMap_ holds the complete set of nexthops.
   * Defer the initial full-sync (and arm nexthopResolutionTimer_ for the
   * remaining budget) until isInitialPathComputationReady(): all registered
   * nexthops resolved AND, when conditional local routes exist, the first
   * NexthopResolutionUpdate has been processed so those routes are advertised.
   * This prevents the sync from wiping FIB routes whose nexthops FSDB has not
   * resolved yet, as well as GR-retained conditional routes. The sync resumes
   * from maybeRunPendingInitialPathComputation() once ready, or from
   * onNexthopResolutionTimeout() on expiry. A zero timeout (no nexthop tracking
   * and no conditional routes, or PeerMgr's EoR timer already expired) runs the
   * sync immediately.
   */
  if (nexthopResolutionTimeout > std::chrono::milliseconds(0) &&
      !isInitialPathComputationReady()) {
    vrf->setPendingInitialPathComputation(true);
    vrf->nexthopResolutionTimer()->scheduleTimeout(nexthopResolutionTimeout);
    XLOGF(
        INFO,
        "[Initialization] Deferring initial path computation up to {}ms: "
        "nexthop resolution / conditional routes not ready yet (vrf={}).",
        nexthopResolutionTimeout.count(),
        vrfId);
    return;
  }

  runInitialPathComputation(vrfId);
}

void RibBase::runInitialPathComputation(VrfId vrfId) noexcept {
  auto* vrf = findVrf(vrfId);
  if (!vrf) {
    XLOGF(
        ERR,
        "Ignoring initial path computation for non-existent VRF {}",
        vrfId);
    return;
  }

  vrf->setPendingInitialPathComputation(false);
  // stop the resolution timer so none is live when the full-sync runs
  vrf->nexthopResolutionTimer()->cancelTimeout();

  // fib batch process list must be empty right now
  CHECK(vrf->fibBatchList().empty());

  /*
   * set one-time flag before prepareFibProgramming so that subclass
   * hooks (called during prepareFibProgramming) can check
   * ribEoRReceived_ as a precondition
   */
  vrf->markRibEoRReceived();

  // best path selection + notify programming FIB
  prepareFibProgramming(vrfId, true /* fullSync */);

  // log BGP++ initialization event
  BgpStats::logInitializationEvent("Rib", BgpInitializationEvent::RIB_COMPUTED);

  /*
   * NOTE: route-churn detection coro task will only be scheduled AFTER the
   * RIB_COMPUTED stage to make sure FIB_SYNC is not accidentally performed to
   * break BGP++ initialization sequence. Bestpath selection happens within
   * prepareFibProgramming() and can't be paused/stopped once called.
   */

  // Schedule periodic task to monitor route churn, once for the process
  if (!routeChurnMonitorStarted_) {
    routeChurnMonitorStarted_ = true;
    asyncScope_.add(co_withExecutor(&evb_, monitorRouteChurn()));
  }
}

void RibBase::maybeRunPendingInitialPathComputation() noexcept {
  for (const auto& [vrfId, ribVrf] : ribVrfs_) {
    if (!ribVrf->pendingInitialPathComputation()) {
      /* Already past the initial path computation stage; nothing pending. */
      continue;
    }
    if (!isInitialPathComputationReady()) {
      /*
       * Not yet ready (nexthop resolution / conditional routes); skip for now.
       */
      return;
    }
    XLOGF(
        INFO,
        "[Initialization] Nexthop resolution / conditional routes ready; "
        "running deferred initial path computation for VRF {}.",
        vrfId);
    runInitialPathComputation(vrfId);
  }
}

void RibBase::onNexthopResolutionTimeout(VrfId vrfId) noexcept {
  const auto* vrf = findVrf(vrfId);
  if (!vrf) {
    XLOGF(
        ERR,
        "Ignoring nexthop resolution timeout for non-existent VRF {}",
        vrfId);
    return;
  }
  if (!vrf->pendingInitialPathComputation()) {
    return;
  }
  XLOGF(
      ERR,
      "[Initialization] Timed out waiting for nexthop resolution for VRF {}; "
      "running initial path computation with unresolved nexthops.",
      vrfId);
  runInitialPathComputation(vrfId);
}

bool RibBase::areAllRegisteredNexthopsResolved() const noexcept {
  for (const auto& [nexthop, nexthopInfo] : nexthopInfoMap_) {
    if (!nexthopInfo.isResolvedForSelection()) {
      return false;
    }
  }
  return true;
}

bool RibBase::isInitialPathComputationReady() const noexcept {
  /*
   * Two independent preconditions gate the initial full-sync:
   *  - Nexthop tracking: every registered nexthop is resolved. Vacuously true
   *    when tracking is off (nexthopInfoMap_ is empty).
   *  - Conditional local routes: the first NexthopResolutionUpdate has been
   *    processed, so conditional routes have been advertised into ribEntries_
   *    and won't be wiped by the initial syncFib. This is independent of
   *    nexthop tracking -- processNexthopResolutionUpdate runs whenever FSDB
   *    delivers neighbor/FIB updates, even with tracking disabled.
   */
  return areAllRegisteredNexthopsResolved() &&
      (!hasConditionalLocalRoutes() || firstNexthopResolutionProcessed_);
}

/**
 * Message to pause local RIB operations that does the following actions:
 * 1. Sets pauseBestPathAndFibProgramming_ which prevents best path selection
 * and FIB programming.
 * 2. Cancels fibBatchTime_ if already scheduled.
 */
void RibBase::processPauseBestPathAndFibProgramming(
    const PauseBestPathAndFibProgramming&
        pauseBestPathAndFibProgrammingMsg) noexcept {
  // Get the task name issuing PauseBestPathAndFibProgramming message
  auto taskName = pauseBestPathAndFibProgrammingMsg.taskName;

  // Avoid duplicate pause to local Rib operations
  if (bestPathAndFibProgrammingPausedBy_.rlock()->contains(taskName)) {
    XLOGF(
        DBG4,
        "Ignore duplicate RibPauseBestPathAndFibProgramming from {} since Rib has already "
        "paused local rib thread operations",
        magic_enum::enum_name(taskName));
    return;
  }

  XLOGF(
      INFO,
      "Insert RibPauseBestPathAndFibProgramming from task {}",
      magic_enum::enum_name(taskName));

  // Record task names to pauseBestPathAndFibProgrammingSentBy_ set
  bestPathAndFibProgrammingPausedBy_.wlock()->insert(taskName);

  // First time detecting rib bestpath pause message. Set the state.
  if (!pauseBestPathAndFibProgramming_) {
    pauseBestPathAndFibProgrammingStartTime_ = std::chrono::steady_clock::now();

    // Set pauseBestPathAndFibProgramming_ to true
    pauseBestPathAndFibProgramming_ = true;

    // Stop scheduled Fib programming timer if any
    for (auto& [_, ribVrf] : ribVrfs_) {
      auto& fibBatchTimer = ribVrf->fibBatchTimer();
      if (fibBatchTimer->isScheduled()) {
        fibBatchTimer->cancelTimeout();
      }
    }
  }

  if (taskName == RibPauseResumeCause::BACKPRESSURE) {
    /*
     * Case 1: BACKPRESSURE pause will not invoke pause timer.
     * Pause from BACKPRESSURE is a hardblocker and will not
     * rely on ribPauseTimer to unblock, whereas the other tasks will have a
     * max-cap timer duration.
     */
    return;
  }

  // Create/extend ribPauseTimer if it is not BACKPRESSURE
  if (ribPauseTimer_) {
    // Case 2: other tasks will extend ribPauseTimer if scheduled.
    XLOGF(
        DBG4,
        "Rib has already been paused, extending Rib pause time for {}",
        magic_enum::enum_name(taskName));

    ribPauseTimer_->scheduleTimeout(ribPauseTime_);
  } else {
    /*
     * Initialize ribPauseTimer_ and schedule timeout to resume best path and
     * fib programming when ribPauseTimer_ expires
     */
    ribPauseTimer_ = folly::AsyncTimeout::make(
        evb_, [this]() noexcept { mayResumeBestPathAndFibProgramming(); });
    ribPauseTimer_->scheduleTimeout(ribPauseTime_);
  }
}

/**
 * Message to resume the paused local Rib operations that does the following:
 * 1. Walks all Rib entries and marks requirePathSelection.
 * 2. Calls prepareFibProgramming with fullsync which takes care of best path
 * selection and programming Fib
 * 2. Unsets pauseBestPathAndFibProgramming_ flag
 * 3. Schedules fibBatchTime_.
 */
void RibBase::processResumeBestPathAndFibProgramming(
    const ResumeBestPathAndFibProgramming&
        resumeBestPathAndFibProgrammingMsg) noexcept {
  // Get the task name issuing ResumeBestPathAndFibProgramming message
  auto taskName = resumeBestPathAndFibProgrammingMsg.taskName;

  XLOGF(
      INFO,
      "Received ResumeBestPathAndFibProgramming from {}",
      magic_enum::enum_name(taskName));

  /*
   * Erase the received task name bestPathAndFibProgrammingPausedBy_ set. It
   * is a no-op if the task name is already removed from the list.
   */
  bestPathAndFibProgrammingPausedBy_.wlock()->erase(taskName);

  if (!bestPathAndFibProgrammingPausedBy_.rlock()->empty()) {
    XLOGF(
        DBG1,
        "Rib has not resumed local Rib thread operations as it is paused by {} tasks",
        bestPathAndFibProgrammingPausedBy_.rlock()->size());
    return;
  }

  /*
   * Resume best path and fib programming once
   * all tasks that sent PauseBestPathAndFibProgramming have sent
   * ResumeBestPathAndFibProgramming
   */
  mayResumeBestPathAndFibProgramming(taskName);
}

void RibBase::mayResumeBestPathAndFibProgramming(
    std::optional<RibPauseResumeCause> cause) noexcept {
  // If we were not paused then there is nothing to resume
  if (!pauseBestPathAndFibProgramming_) {
    XLOG(
        DBG4,
        "Ignore RibResumeBestPathAndFibProgramming since Rib has already "
        "resumed local Rib thread operations");
    return;
  }

  if (!cause.has_value()) {
    /*
     * Case 1: invoked from ribPauseTimer.
     * Best path and FIB programming was resumed due to timeout, clear
     * bestPathAndFibProgrammingPausedBy_ collection except
     * BACKPRESSURE.
     *
     * Case 2: invoked from processResumeBestPathAndFibProgramming.
     * The pause reason has already been removed from the caller.
     */
    if (bestPathAndFibProgrammingPausedBy_.rlock()->contains(
            RibPauseResumeCause::BACKPRESSURE)) {
      XLOG(
          DBG1,
          "Skip resuming bestpath selection/fib programming due to BACKPRESSURE blocking");

      folly::F14NodeSet<facebook::bgp::RibPauseResumeCause> causes;
      causes.insert(RibPauseResumeCause::BACKPRESSURE);
      bestPathAndFibProgrammingPausedBy_.wlock()->swap(causes);
      return;
    } else {
      /*
       * Clear all entries in bestPathAndFibProgrammingPausedBy_ if
       * BACKPRESSURE is not present
       */
      bestPathAndFibProgrammingPausedBy_.wlock()->clear();
    }
  }

  // Unset pauseBestPathAndFibProgramming_
  pauseBestPathAndFibProgramming_ = false;

  // Set ODS stats to record pause time
  auto ribPauseTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() -
      pauseBestPathAndFibProgrammingStartTime_);
  XLOGF(
      INFO,
      "Rib best path and fib programming resumed after {} ms",
      ribPauseTimeMs.count());
  RibStats::STATS_ribBestPathAndFibProgrammingPauseTimeMs.addValue(
      ribPauseTimeMs.count());

  for (auto& [_, ribVrf] : ribVrfs_) {
    for (auto& [prefix, ribEntry] : ribVrf->ribEntries()) {
      ribEntry.requirePathSelection();
    }
  }

  // Capture and reset the pending FibSyncReq flag
  bool fullSync = fibSyncReqPending_;
  fibSyncReqPending_ = false;

  for (const auto& [vrfId, ribVrf] : ribVrfs_) {
    if (!ribVrf->ribEoRReceived()) {
      continue;
    }
    XLOGF(
        DBG2,
        "ResumeBestPathAndFibProgramming is triggering best-path computation and FIB programming for VRF {} with fullSync={}",
        vrfId,
        fullSync);

    // Best path selection + notify programming FIB (fullSync if pending)
    prepareFibProgramming(vrfId, fullSync);
    ribVrf->schedulePrepareFibProgrammingTimer();
  }
}

void RibBase::processRibInNexthopUpdate(
    const RibInNexthopUpdate& nexthopUpdate) noexcept {
  if (!enableNexthopTracking_ || !nexthopCache_) {
    XLOGF(
        WARNING,
        "Nexthop tracking is not enabled or nexthopCache is null, ignoring nexthop update (tracking vrf={})",
        fboss::kDefaultVrf);
    return;
  }
  bool needPathSelection = false;
  // Changed nexthop(s) recorded for the trigger log at the end of this method.
  std::vector<std::string> triggeringNexthops;

  // Process each NexthopStatus in the update
  for (const auto& queuedStatus : nexthopUpdate.nexthopStatuses) {
    const auto& nexthop = queuedStatus.getNexthopKey();
    if (nexthop.nextHopVrfId != fboss::kDefaultVrf) {
      continue;
    }
    const auto& nexthopIp = nexthop.ip;
    XLOGF(
        DBG2,
        "Processing nexthop update for {} (vrf={})",
        nexthopIp.str(),
        nexthop.nextHopVrfId);

    // Check if the nexthop exists in nexthopInfoMap_
    auto it = nexthopInfoMap_.find(nexthopIp);
    if (it != nexthopInfoMap_.end()) {
      /*
       * Skip an update older than the status the NexthopInfo already holds.
       * An equal epoch is re-read. Epoch 0 means NexthopCache did not stamp
       * the update, so it is always re-read.
       */
      if (queuedStatus.getEpoch() != 0 &&
          it->second.getEpoch() > queuedStatus.getEpoch()) {
        XLOGF(
            DBG2,
            "Skipping stale nexthop update for {} (vrf={}): epoch {} is older than {}",
            nexthopIp.str(),
            nexthop.nextHopVrfId,
            queuedStatus.getEpoch(),
            it->second.getEpoch());
        RibStats::incrNhtRibStaleUpdateSkipped();
        continue;
      }
      /*
       * The update names a nexthop whose status changed. The NexthopInfo takes
       * the current status from NexthopCache, not the status in the update.
       */
      const auto cachedStatus = nexthopCache_->getNexthopStatus(nexthop);
      if (!cachedStatus.has_value()) {
        XLOGF(
            ERR,
            "Ignoring nexthop update for {} (vrf={}): RIB has a NexthopInfo but NexthopCache has no entry. The RIB keeps the old status.",
            nexthopIp.str(),
            nexthop.nextHopVrfId);
        continue;
      }
      const NexthopStatus& nexthopStatus = *cachedStatus;
      const bool isReachable = nexthopStatus.isReachable();
      const std::optional<uint32_t> igpCost = nexthopStatus.getIgpCost();
      const bool isResolved = nexthopStatus.isResolvedForSelection();

      // Update the existing NexthopInfo
      const bool isReachabilityChanged =
          it->second.isReachable() != isReachable;
      const bool isIgpCostChanged = it->second.getIgpCost() != igpCost;
      const bool isResolvabilityChanged =
          it->second.isResolvedForSelection() != isResolved;
      const bool needsPathSelection =
          isIgpCostChanged || isResolvabilityChanged;

      // TODO_NH_VRF: Count unresolvable nexthops in the counters of their VRF.
      if (isResolvabilityChanged) {
        if (isResolved) {
          defaultVrf_.ribCounters().decrementUnresolvableNexthopCount();
        } else {
          defaultVrf_.ribCounters().incrementUnresolvableNexthopCount();
        }
      }
      if (isReachabilityChanged) {
        if (isReachable) {
          RibStats::incrNhtRibNexthopReachable();
        } else {
          RibStats::incrNhtRibNexthopUnreachable();
        }
      }

      if (isReachabilityChanged || isIgpCostChanged || isResolvabilityChanged) {
        XLOGF(
            DBG1,
            "Updating nexthop {} (vrf={}) status: reachable={} -> {}, igpCost={} -> {}, resolvedForSelection={} -> {}",
            nexthopIp.str(),
            nexthop.nextHopVrfId,
            it->second.isReachable(),
            isReachable,
            it->second.getIgpCost().has_value()
                ? std::to_string(it->second.getIgpCost().value())
                : "unset",
            igpCost.has_value() ? std::to_string(igpCost.value()) : "unset",
            it->second.isResolvedForSelection(),
            isResolved);
      }

      // Update the existing nexthopInfo with the status
      it->second.updateStatus(nexthopStatus);

      /*
       * The initial full sync consumes status stored before EOR. Afterwards,
       * only cost or selection eligibility changes require path selection.
       */
      // TODO_NH_VRF: Check the EoR of each RouteInfo's VRF.
      if (!needsPathSelection || !defaultVrf_.ribEoRReceived()) {
        continue;
      }

      triggeringNexthops.push_back(
          fmt::format("{} (vrf={})", nexthopIp.str(), nexthop.nextHopVrfId));

      /*
       * Iterate over RouteInfo in the association list to mark
       * requirePathSelection only if
       * ribEoRReceived: True => already fulfilled the initial FULL_SYNC
       */
      for (auto routeInfoIt = it->second.begin();
           routeInfoIt != it->second.end();
           routeInfoIt++) {
        // Get the ribEntry directly from the RouteInfo
        RibEntry& ribEntry = routeInfoIt->getRibEntry();
        if (isResolvabilityChanged) {
          ribEntry.requirePathSelectionForNexthopResolvabilityChange();
        } else {
          ribEntry.requirePathSelection();
        }
        if (!needPathSelection) {
          needPathSelection = true;
        }
      }
    } else {
      /*
       * NexthopCache only queues updates for nexthops registered by the RIB.
       * A missing NexthopInfo therefore means all associated routes were
       * removed after this update was queued. Recreating it here would leave
       * an unregistered, orphaned entry that cannot receive later updates.
       */
      XLOGF(
          INFO,
          "Ignoring stale nexthop update for untracked nexthop {} (vrf={})",
          nexthopIp.str(),
          nexthop.nextHopVrfId);
    }
  }

  if (needPathSelection) {
    /*
     * Schedule FIB programming timer which on expiry performs best-path
     * computation and Fib programming
     */
    XLOGF(
        DBG1,
        "Nexthop update for [{}] is triggering best-path computation and FIB programming",
        folly::join(", ", triggeringNexthops));
    for (const auto& [_, ribVrf] : ribVrfs_) {
      if (ribVrf->ribEoRReceived()) {
        ribVrf->schedulePrepareFibProgrammingTimer();
      }
    }
  }

  /*
   * If initial path computation was waiting on nexthop resolution, check
   * whether the new status completed it.
   */
  maybeRunPendingInitialPathComputation();
}

folly::coro::Task<void> RibBase::monitorRouteChurn() noexcept {
  while (true) {
    // when cancelAndJoinAsync is called, guaranteed to exit
    co_await folly::coro::co_safe_point;

    /*
     * Handle route churn detection and pause/resume best path and Fib
     * programming
     */
    if (isRouteChurnDetected()) {
      XLOG(
          INFO,
          "Route churn detected, pausing best path selection and Fib programming");
      RibStats::incrRouteChurnDetected();
      processPauseBestPathAndFibProgramming(
          PauseBestPathAndFibProgramming(RibPauseResumeCause::ROUTE_CHURN));
    } else if (bestPathAndFibProgrammingPausedBy_.rlock()->contains(
                   RibPauseResumeCause::ROUTE_CHURN)) {
      /*
       * Resume best path and Fib programming if it was already paused due to
       * route churn
       */
      XLOG(
          INFO,
          "Route churn is stable, resuming best path selection and Fib programming");
      processResumeBestPathAndFibProgramming(
          ResumeBestPathAndFibProgramming(RibPauseResumeCause::ROUTE_CHURN));
    }
    // reset prefix count for route churn
    prefixCountForRouteChurn_ = 0;

    // sleep a constant time and yield before next time checking
    co_await folly::coro::sleep(routeChurnCheckInterval_);
  }
}

folly::coro::Task<void> RibBase::co_exportProfilerStatsLoop() noexcept {
  XLOG(INFO, "Starting profiler stats export coro task");
  while (true) {
    co_await folly::coro::co_safe_point;
    try {
      BgpProfiler::getInstance()->writeToFb303();
    } catch (const std::exception& ex) {
      XLOGF(ERR, "Failed to export profiler stats: {}", ex.what());
    }
    co_await folly::coro::sleepReturnEarlyOnCancel(std::chrono::seconds(60));
  }
}

void RibBase::incrementPrefixCountForRouteChurn(uint64_t count) noexcept {
  prefixCountForRouteChurn_ += count;
}

bool RibBase::isRouteChurnDetected() noexcept {
  routeChurnDetected_ =
      prefixCountForRouteChurn_ > highWatermarkForRouteChurn_ ||
      (routeChurnDetected_ &&
       prefixCountForRouteChurn_ >= lowWatermarkForRouteChurn_);
  return routeChurnDetected_;
};

/*
 * prepareFibProgramming() is scheduled every fibBatchTime_ (default 200ms).
 *
 * Its job is NOT to program Fib, but to create or update fibBatchList_, a
 * list of rib entries that have some changes which will result in:
 *
 * (a) Update fib with different set of nexthops / nexhhop weights; and/or
 * (b) Update to neighbors with different path attributes for the route
 *
 * The second point is subtle. There may be some update (for example a
 * community change) that does not result in Fib update, but still results in
 * advertisement to peer.
 *
 * When prepareFibProgramming() is done, it will add a message to
 * fibProgrammingRequests_ in order to trigger the actual procedure to program
 * Fib.
 *
 * In addition to updating fibBatchList_, this function does one more thing.
 * If the prefix is being withdrawn from Fib, we must notify our neighbors
 * before we withdraw the route from Fib.  This will ensure that our neighbor
 * stop sending us traffic to that destination before we withdraw from Fib.
 * Note that this is the reverse for announcements, where we need to update
 * Fib before we announce to neighbors so that our Fib is "warm" before the
 * neighbor sends us traffic to that destination.
 */
RibBase::PathSelectionInput RibBase::snapshotAndResetForPathSelection(
    RibEntry& entry) noexcept {
  PathSelectionInput input{
      .oldBestpath = entry.bestpath_,
      .oldAggregateReceivedUcmpWeight = entry.aggregateReceivedUcmpWeight_,
      .oldAggregateLocalUcmpWeight = entry.aggregateLocalUcmpWeight_,
      .oldMultipathWeightedNexthops = entry.weightedNexthops_,
      .oldNexthopAndTopoInfo = entry.nexthopsAndTopoInfo_,
  };
  entry.needPathSelection_ = false;
  entry.aggregateReceivedUcmpWeight_ = 0;
  entry.aggregateLocalUcmpWeight_ = 0;
  entry.nexthopsAndTopoInfo_ = nullptr;
  return input;
}

std::vector<std::shared_ptr<RouteInfo>> RibBase::prePathSelectionFiltering(
    RibEntry& entry) noexcept {
  const FeatureFlags::BgpBestpathFeatures& bgpBestpathFeatures =
      FeatureFlags::getBgpBestpathFeatures();
  const bool nexthopTrackingEnabled = bgpBestpathFeatures.enableNextHopTracking;
  std::vector<std::shared_ptr<RouteInfo>> routes;
  uint32_t inactivePathCount{0};
  for (const auto& peerIdAndPaths : entry.routeInfos_) {
    for (const auto& pathIdAndRoute : peerIdAndPaths.second) {
      if (nexthopTrackingEnabled &&
          !pathIdAndRoute.second->isResolvedForSelection()) {
        ++inactivePathCount;
        continue;
      }
      routes.emplace_back(pathIdAndRoute.second);
    }
  }
  entry.inactivePathCount_ = inactivePathCount;
  return routes;
}

RibBase::MultiPathSelectionResult RibBase::multiPathSelection(
    RibEntry& entry,
    const std::vector<std::shared_ptr<RouteInfo>>& routes,
    const std::unique_ptr<RouteInfoSelector>& multipathSelector) noexcept {
  /*
   * Run the configured multipath selector over the candidate routes.
   * Required to produce at least one path when called with a non-empty
   * candidate set; an empty result would indicate a bug in the
   * multipath selector itself.
   */
  MultiPathSelectionResult result;
  result.selectedPaths = multipathSelector->selectRoutes(routes);
  XCHECK(!result.selectedPaths.empty());
  for (const auto& path : result.selectedPaths) {
    entry.getOrAssignSendPathId(path);
  }
  return result;
}

void RibBase::accumulateAggregateWeightsAndTopoInfo(
    RibEntry& entry,
    MultiPathSelectionResult& mp,
    const std::shared_ptr<const NexthopTopoInfoMap>& oldNexthopAndTopoInfo,
    const std::optional<BgpUcmpQuantizer>& quantizer) noexcept {
  mp.lbwMultiplier = BpsPerGBps;
  NexthopTopoInfoMap nexthopsAndTopoInfo;
  bool missingReceivedUcmpWeight{false};
  bool missingLocalUcmpWeight{false};

  for (const auto& path : mp.selectedPaths) {
    if (auto topoInfo = path->attrs->getTopologyInfo()) {
      nexthopsAndTopoInfo.emplace(path->attrs->getNexthop(), *topoInfo);
    }

    auto receivedUcmpWeight = path->getUcmpWeight();
    missingReceivedUcmpWeight |= (receivedUcmpWeight == 0);
    entry.aggregateReceivedUcmpWeight_ += receivedUcmpWeight;

    auto localUcmpWeight = path->peer.ucmpWeight;
    missingLocalUcmpWeight |= (!localUcmpWeight.has_value());
    entry.aggregateLocalUcmpWeight_ += localUcmpWeight.value_or(0.0);

    auto receivedLbwBps = receivedUcmpWeight * 8.0f;
    if (receivedLbwBps > BpsPerGBps) {
      mp.lbwMultiplier = std::min(mp.lbwMultiplier, BpsPerGBps);
    } else if (receivedLbwBps > BpsPerMBps) {
      mp.lbwMultiplier = std::min(mp.lbwMultiplier, BpsPerMBps);
    } else {
      mp.lbwMultiplier = 1.0;
    }
  }

  if (nexthopsAndTopoInfo.size() == mp.selectedPaths.size()) {
    entry.nexthopsAndTopoInfo_ =
        std::make_shared<NexthopTopoInfoMap>(std::move(nexthopsAndTopoInfo));
    mp.topoInfoChanged =
        (oldNexthopAndTopoInfo == nullptr ||
         *oldNexthopAndTopoInfo != *entry.nexthopsAndTopoInfo_);
  }

  if (missingReceivedUcmpWeight) {
    entry.aggregateReceivedUcmpWeight_ = 0;
  }
  if (missingLocalUcmpWeight) {
    entry.aggregateLocalUcmpWeight_ = 0;
  } else if (quantizer.has_value()) {
    entry.aggregateLocalUcmpWeight_ =
        quantizer->quantize(entry.aggregateLocalUcmpWeight_);
  }
}

void RibBase::bestPathSelection(
    RibEntry& entry,
    const std::vector<std::shared_ptr<RouteInfo>>& selectedPaths,
    const std::unique_ptr<RouteInfoSelector>& bestpathSelector) noexcept {
  auto bestpath = bestpathSelector->selectRoutes(selectedPaths);
  CHECK(!bestpath.empty());
  entry.bestpath_ = bestpath[0];
  entry.installToFib_ = bestpath[0]->installToFib;
}

WeightedNexthopMap RibBase::buildAndNormalizeWeightedNexthops(
    RibEntry& entry,
    std::vector<std::shared_ptr<RouteInfo>>& selectedPaths,
    bool computeUcmp,
    uint32_t ucmpWidth,
    float lbwMultiplier) noexcept {
  WeightedNexthopMap newNhWtMap;
  uint32_t weightMultiplier{0};
  uint64_t totalUcmpWeight{0};
  const bool setUcmpWeights =
      computeUcmp && entry.aggregateReceivedUcmpWeight_ > 0;
  std::unordered_set<uint32_t> uniqueNexthopWeights;
  entry.multipaths_ = {};
  for (auto& path : selectedPaths) {
    auto pathId = path->pathIdToSend.value();
    entry.multipaths_.emplace(pathId, std::move(path));
    path = entry.multipaths_.at(pathId);

    if (setUcmpWeights) {
      auto ucmpWeight = static_cast<uint32_t>(
          round(path->getUcmpWeight() * 8 / lbwMultiplier));
      totalUcmpWeight += ucmpWeight;
      weightMultiplier = std::gcd(weightMultiplier, ucmpWeight);
      newNhWtMap.emplace(path->attrs->getNexthop(), ucmpWeight);
    } else {
      newNhWtMap.emplace(path->attrs->getNexthop(), 0u);
    }
  }
  if (setUcmpWeights) {
    totalUcmpWeight = totalUcmpWeight / std::max(weightMultiplier, 1u);
    for (auto it = newNhWtMap.begin(); it != newNhWtMap.end();) {
      auto& [nh, ucmpWeight] = *it;
      ucmpWeight = ucmpWeight / std::max(weightMultiplier, 1u);
      if (totalUcmpWeight > ucmpWidth) {
        ucmpWeight = round(ucmpWeight * ucmpWidth * 1.0 / totalUcmpWeight);
      }
      if (ucmpWeight == 0) {
        it = newNhWtMap.erase(it);
      } else {
        uniqueNexthopWeights.emplace(ucmpWeight);
        ++it;
      }
    }
  }
  entry.isUcmpActive_ = uniqueNexthopWeights.size() > 1;
  return newNhWtMap;
}

std::pair<bool, bool> RibBase::computeChangePair(
    RibEntry& entry,
    const PathSelectionInput& input,
    bool topoInfoChanged,
    WeightedNexthopMap&& newNhWtMap) noexcept {
  bool multipathChanged = false;
  if ((input.oldMultipathWeightedNexthops == nullptr) ||
      (newNhWtMap != *input.oldMultipathWeightedNexthops)) {
    entry.weightedNexthops_ =
        std::make_shared<WeightedNexthopMap>(std::move(newNhWtMap));
    multipathChanged = true;
  }
  multipathChanged |= topoInfoChanged;

  if (!input.oldBestpath && !entry.bestpath_) {
    return std::make_pair(false, multipathChanged);
  }

  const bool bestpathChanged = (input.oldBestpath != entry.bestpath_) ||
      (entry.aggregateReceivedUcmpWeight_ !=
       input.oldAggregateReceivedUcmpWeight) ||
      (entry.aggregateLocalUcmpWeight_ != input.oldAggregateLocalUcmpWeight);

  return std::make_pair(bestpathChanged, multipathChanged);
}

std::pair<bool, bool> RibBase::selectBestPath(
    RibEntry& entry,
    const std::unique_ptr<RouteInfoSelector>& multipathSelector,
    const std::unique_ptr<RouteInfoSelector>& bestpathSelector,
    bool computeUcmp,
    uint32_t ucmpWidth,
    const std::optional<BgpUcmpQuantizer>& quantizer) noexcept {
  const auto input = snapshotAndResetForPathSelection(entry);
  auto routes = prePathSelectionFiltering(entry);

  if (routes.empty()) {
    entry.bestpath_ = nullptr;
    entry.multipaths_ = {};
    entry.installToFib_ = true;
    entry.weightedNexthops_ = nullptr;
    return std::make_pair(
        entry.bestpath_ != input.oldBestpath,
        entry.weightedNexthops_ != input.oldMultipathWeightedNexthops);
  }

  auto mp = multiPathSelection(entry, routes, multipathSelector);
  auto& selectedPaths = mp.selectedPaths;

  accumulateAggregateWeightsAndTopoInfo(
      entry, mp, input.oldNexthopAndTopoInfo, quantizer);

  bestPathSelection(entry, selectedPaths, bestpathSelector);

  auto newNhWtMap = buildAndNormalizeWeightedNexthops(
      entry, selectedPaths, computeUcmp, ucmpWidth, mp.lbwMultiplier);

  return computeChangePair(
      entry, input, mp.topoInfoChanged, std::move(newNhWtMap));
}

std::optional<BgpRouteType> RibBase::bestpathSource(
    const RouteInfo* bestpath) noexcept {
  /*
   * A null best path maps to nullopt (counted in no source bucket).
   * getBgpPathType classifies every real path into one of
   * LOCAL/EBGP/ConfedEBGP/ IBGP -- its residual (non-local, non-external,
   * non-confed-external) case is IBGP, the internal-peer class -- so it does
   * not return UNKNOWN today. The RibCounters `unknown` bucket therefore stays
   * 0 in practice; it exists only so an UNKNOWN winner would be isolated there
   * rather than mis-attributed.
   */
  if (bestpath == nullptr) {
    return std::nullopt;
  }
  return getBgpPathType(bestpath);
}

void RibBase::recordBestpathSourceDelta(
    RibCounters& ribCounters,
    const folly::CIDRNetwork& prefix,
    std::optional<BgpRouteType> oldSource,
    const RouteInfo* newBestpath) noexcept {
  ribCounters.onBestpathSourceChanged(
      prefix.first.isV4(), oldSource, bestpathSource(newBestpath));
}

void RibBase::recordInactivePathDelta(
    RibCounters& ribCounters,
    const folly::CIDRNetwork& prefix,
    uint32_t oldCount,
    const RibEntry& entry) noexcept {
  ribCounters.onInactivePathsDelta(
      prefix.first.isV4(),
      static_cast<int64_t>(entry.getInactivePathCnt()) -
          static_cast<int64_t>(oldCount));
}

std::pair<bool, bool> RibBase::runBestPathSelection(
    RibCounters& ribCounters,
    RibEntry& entry) noexcept {
  /*
   * Capture the winner's source class (a small value, not the owning
   * shared_ptr) before selection so the delta covers every bestpath write
   * inside selectBestPath (positive and all set-to-null paths) without
   * shared_ptr refcount traffic on the hot path. The old path may be freed by
   * selection, so we must not retain a pointer to it across the call.
   *
   * prePathSelectionFiltering(), called inside selectBestPath(), recomputes
   * and overwrites entry.inactivePathCount_. Preserve the previous cached
   * value here so recordInactivePathDelta() can compute newCount - oldCount
   * after selection returns.
   */
  const auto oldSource = bestpathSource(entry.getBestPathRaw());
  const auto oldInactivePathCount = entry.getInactivePathCnt();
  auto result = selectBestPath(
      entry,
      multipathSelector_,
      bestpathSelector_,
      globalConfig_.computeUcmpFromLbwComm,
      globalConfig_.ucmpWidth,
      std::optional<BgpUcmpQuantizer>(globalConfig_.ucmpQuantizer));

  recordBestpathSourceDelta(
      ribCounters, entry.getPrefix(), oldSource, entry.getBestPathRaw());
  recordInactivePathDelta(
      ribCounters, entry.getPrefix(), oldInactivePathCount, entry);
  return result;
}

void RibBase::prepareFibProgramming(VrfId vrfId, bool fullSync) noexcept {
  ScopedProfile profile("RibBase::prepareFibProgramming");
  auto* vrf = findVrf(vrfId);
  if (!vrf) {
    XLOGF(ERR, "Cancelling FIB programming for non-existent VRF {}", vrfId);
    return;
  }
  auto& ribEntries = vrf->ribEntries();
  BgpStats::incrDecisionProcessRunsCount();

  XLOGF(DBG1, "Trigger Fib programming with fullSync = {}", fullSync);
  // track if at least one rib entry got active ucmp
  bool isUcmpActive{false};

  RibOutWithdrawal withdrawal;
  RibOutWithdrawal withdrawalAddPath;
  withdrawal.entries.reserve(kRibChunkSize);
  withdrawalAddPath.addPathEntries.reserve(kRibChunkSize);
  /*
   * Full sync: clear current fibBatchList_ and populate with entire
   * ribEntries
   */
  auto& fibBatchList = vrf->fibBatchList();
  if (fullSync) {
    fibBatchList.clear();
    XLOGF(
        DBG4,
        "Start full sync best path selection for {} ribEntries",
        ribEntries.size());
  }
  /*
   * record rib entries that "needPathSelection", which need to go through
   * route attribute policy again
   */
  folly::F14FastSet<folly::CIDRNetwork> prefixesToOverwriteRouteAttributes;
  // record start time before we perform path selection
  auto ribPolicyProcessingStartTime = std::chrono::steady_clock::now();
  for (auto& [prefix, ribEntry] : ribEntries) {
    /*
     * Check if there was any update to the prefix; if not, continue. Note
     * that the change could be something that does not affect bestpath or
     * multipath nexthops.
     */
    if (!fullSync && !ribEntry.needPathSelection()) {
      continue;
    }

    prefixesToOverwriteRouteAttributes.insert(prefix);

    auto pathSelectionStartTime = std::chrono::steady_clock::now();
    auto [bestpathChanged, nexthopChanged] =
        runBestPathSelection(vrf->ribCounters(), ribEntry);
    if (ribEntry.consumeNexthopResolvabilityChangePending()) {
      RibStats::incrNhtRibResolvabilityChangePrefixReevaluation();
    }
    RibStats::STATS_ribPathSelectionTimeMs.addValue(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - pathSelectionStartTime)
            .count());

    if (ribEntry.isUcmpActive_) {
      isUcmpActive = true;
    }

    /*
     * NOTE: the RIB version is intentionally NOT bumped here at path-selection
     * time. It is stamped at queue-emission time instead -- on the withdrawal
     * path below and in handleFibProgrammedMessage() for announcements -- so a
     * prefix's ribVersion reflects the order its entries are pushed onto
     * ribOutQ_. Withdrawals are pushed before the FIB round-trip and
     * announcements after it; stamping at path-selection (F14 order) could give
     * a withdrawn prefix a higher version than an announced prefix that reaches
     * the queue later, breaking the reader-side invariant that a prefix
     * modified after a peer's detach point is strictly ahead of that peer's
     * detach version (the ordering AdjRibGroup::isEntryShared relies on).
     */

    // update fib for all entries if fullsync
    if (fullSync) {
      vrf->addToFibBatch(ribEntry);
    }

    /*
     * Incremental update:
     * If neither bestpath nor nexthops changed, there is nothing to be done
     * for Fib. However, there may still be some attribute change in one of
     * the ECMP paths that need to be advertised to the neighbor that has
     * expressed interest in AddPath. If so, add ribEntry to fibBatchList
     * and continue.
     */
    if (!bestpathChanged && !nexthopChanged && ribEntry.multipathChanged()) {
      vrf->addToFibBatch(ribEntry);
      continue;
    }

    // If bestpath or nexthops is changed, announce withdraw (and update fib)
    if (nexthopChanged || bestpathChanged) {
      const auto& bestpath = ribEntry.getBestPath();
      const auto& weightedNexthops = ribEntry.getMultipathWeightedNexthops();

      XLOGF(
          DBG3,
          "bestpathChanged: {}, nexthopChanged: {}, weightedNexthops: {}, bestpath: {}",
          bestpathChanged,
          nexthopChanged,
          weightedNexthops ? "not null" : "null",
          bestpath ? "not null" : "null");

      /*
       * If prefix is being withdrawn, advertise the withdrawal right away,
       * before programming fib
       * Since weightedNexthops could be non-null while bestpath be null, we
       * can't rely solely on weightedNexthops == null to determine if prefix
       * is to be withdrawn
       */
      if (!weightedNexthops || (bestpathChanged && !bestpath)) {
        XLOGF(
            DBG3,
            "Adding withdraw of {} to send to BGP peers",
            folly::IPAddress::networkToString(prefix));
        if (withdrawal.entries.size() == kRibChunkSize) {
          XLOGF(
              DBG1,
              "Sending {} withdrawals to all non add-path BGP peers",
              kRibChunkSize);

          ribOutQPushAndMayPauseBestPathAndFibProgramming(
              vrfId, std::move(withdrawal));
          withdrawal = RibOutWithdrawal();
          withdrawal.entries.reserve(kRibChunkSize);
        }
        /*
         * Stamp the RIB version at emission time for this withdrawn prefix.
         * currentRibVersion starts fresh (rule 3: different prefixes strictly
         * increase) and is re-bumped inside handleFullAddPathWithdrawal() at
         * each chunk (kRibChunkSize) flush, so a prefix whose add-path
         * withdrawals span more than one chunk strictly increases across them
         * (rule 2), while entries within a chunk share one version (rule 1).
         * The main withdrawal entry below carries the final (highest) version,
         * which is then persisted onto the entry.
         */
        uint64_t currentRibVersion = vrf->incrementRibVersion();
        handleFullAddPathWithdrawal(
            *vrf, ribEntry, withdrawalAddPath, currentRibVersion);
        withdrawal.entries.emplace_back(
            prefix, kDefaultPathID, std::nullopt, currentRibVersion);
        ribEntry.setRibVersion(currentRibVersion);
        ribEntry.commitBestpath();
        // wait till fib programming to commit multipathNexthops
      }

      // If route entry is not on the batch process list yet, add it.
      vrf->addToFibBatch(ribEntry);
    }

    /*
     * Path-selection policy or nexthop filtering can leave candidates in a
     * RibEntry while selecting neither a best path nor FIB nexthops. When the
     * final candidate is withdrawn, those selected outputs remain empty, so
     * bestpathChanged/nexthopChanged does not enqueue the now-pathless entry.
     * If it has prior FibOut state (including explicit NONE/removal), enqueue
     * an idempotent removal so FIB-ack processing can erase it.
     */
    if (ribEntry.getAllPathsCnt() == 0 && ribEntry.hasFibOutState()) {
      vrf->addToFibBatch(ribEntry);
    }
  }
  if (prefixesToOverwriteRouteAttributes.size() == ribEntries.size()) {
    auto pathSelectionTimeMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - ribPolicyProcessingStartTime);
    XLOGF(
        INFO,
        "Path Selection in a FullRibWalk for {} ribEntries took {} ms",
        ribEntries.size(),
        pathSelectionTimeMs.count());
    RibStats::STATS_ribFullSyncPathSelectionTimeMs.addValue(
        pathSelectionTimeMs.count());
  }

  withdrawal.addPathEntries = std::move(withdrawalAddPath.addPathEntries);
  // If there are withdrawals left, send them to peers
  if (withdrawal.entries.size() || withdrawal.addPathEntries.size()) {
    XLOGF(DBG1, "{}", formatRibOutWithdrawalLog(withdrawal));
    ribOutQPushAndMayPauseBestPathAndFibProgramming(
        vrfId, std::move(withdrawal));
  }

  // ods logging whether at least 1 rib entry actively uses ucmp
  BgpStats::setUcmpActive(isUcmpActive);

  /* Virtual hook: subclasses override to apply CTE route attribute overwrites.
     Timing and full-sync stats are handled inside the override. */
  // Route attribute policy applies to VRF 0 only.
  if (isVrfCTECapable(vrfId)) {
    overwriteRouteAttributes(*vrf, prefixesToOverwriteRouteAttributes);
  }

  /*
   * Generic post-path-selection hook, invoked once per pass after path
   * selection and route-attribute overwrite, before the fibBatchList_
   * early-exit. No-op default on RibBase; subclasses override to run
   * platform-specific end-of-pass work. RibBase neither knows nor performs
   * any such work itself.
   */
  onPrepareFibProgrammingComplete(fullSync);
  vrf->ribCounters().publishInactivePathCounters();

  // If no entry in fib update list and not full sync, skip fib programming
  if (fibBatchList.empty() && !fullSync) {
    return;
  }

  RibStats::addFibBatchListSize(fibBatchList.size());
  if (fullSync) {
    vrf->setFullSync(true);
  }
  fibProgrammingRequests_.push(*vrf);
}

void RibBase::flushAnnouncementChunk(
    RibVrf& vrf,
    RibOutAnnouncement& announcement,
    bool reserveAddPath,
    bool sendWithEoR) {
  if (sendWithEoR) {
    announcement.initialDump = true;
  }

  XLOGF(DBG1, "{}", formatRibOutAnnouncementLog(announcement));

  ribOutQPushAndMayPauseBestPathAndFibProgramming(
      vrf.vrfId(), std::move(announcement));
  announcement = RibOutAnnouncement();
  (reserveAddPath ? announcement.addPathEntries : announcement.entries)
      .reserve(kRibChunkSize);
}

void RibBase::flushWithdrawalChunk(
    RibVrf& vrf,
    RibOutWithdrawal& withdrawal,
    bool reserveAddPath,
    bool logAsAddPath) {
  XLOGF(DBG1, "{}", formatRibOutWithdrawalLog(withdrawal, logAsAddPath));

  ribOutQPushAndMayPauseBestPathAndFibProgramming(
      vrf.vrfId(), std::move(withdrawal));
  withdrawal = RibOutWithdrawal();
  (reserveAddPath ? withdrawal.addPathEntries : withdrawal.entries)
      .reserve(kRibChunkSize);
}

void RibBase::handleFullAddPathWithdrawal(
    RibVrf& vrf,
    RibEntry& ribEntry,
    RibOutWithdrawal& withdrawalAddPath,
    uint64_t& currentRibVersion) {
  /*
   * Withdraw every advertised path by the path ID it was announced with. The
   * nexthop is still filled for consumers that key paths by nexthop. The
   * advertised multipaths are then cleared, so paths kept in the entry are
   * announced again when a bestpath returns.
   */
  if (enableRibAllocatedPathId_) {
    for (const auto& [pathId, path] : ribEntry.getAdvertisedMultipaths()) {
      if (withdrawalAddPath.addPathEntries.size() == kRibChunkSize) {
        flushWithdrawalChunk(
            vrf,
            withdrawalAddPath,
            /*reserveAddPath=*/true,
            /*logAsAddPath=*/true);

        /*
         * rule 2: this prefix's remaining add-path withdrawals land in a new
         * chunk, so bump the version (see prepareFibProgramming for the
         * reasoning).
         */
        currentRibVersion = vrf.incrementRibVersion();
      }
      withdrawalAddPath.addPathEntries.emplace_back(
          ribEntry.getPrefix(),
          pathId,
          path->attrs->getNexthop(),
          currentRibVersion);
    }
    ribEntry.advertisedMultipaths_.clear();
    return;
  }

  if (ribEntry.getAdvertisedMultipathWeightedNexthops()) {
    for (const auto& advWeightedNhIter :
         *ribEntry.getAdvertisedMultipathWeightedNexthops()) {
      if (withdrawalAddPath.addPathEntries.size() == kRibChunkSize) {
        flushWithdrawalChunk(
            vrf,
            withdrawalAddPath,
            /*reserveAddPath=*/true,
            /*logAsAddPath=*/true);

        /*
         * rule 2: this prefix's remaining add-path withdrawals land in a new
         * chunk, so bump the version (see prepareFibProgramming for the
         * reasoning).
         */
        currentRibVersion = vrf.incrementRibVersion();
      }
      withdrawalAddPath.addPathEntries.emplace_back(
          ribEntry.getPrefix(),
          kPlaceholderPathID,
          advWeightedNhIter.first,
          currentRibVersion);
    }
  }
}

/**
 * @brief Get the backoff timeout for Fib programming, during churn.
 *
 * @return std::chrono::seconds - backoff timeout in seconds.
 */
std::chrono::seconds RibBase::getFibBackoffTimeout() const noexcept {
  return kFibBatchTimeoutChurn;
}

/*
 * handleFibProgrammedMessage() is called after Fib has been updated.  Iterate
 * over all programmed prefixes and send relevant announcements and
 * withdrawals.
 *
 * If a prefix was deleted from Fib, we would already have sent a withdrawal
 * message, so only need to erase the ribEntry if there were no further
 * changes in the interim.
 *
 * For prefixes not deleted, send MultiPath announcements and withdrawals
 */
void RibBase::handleFibProgrammedMessage(
    const Fib::FibProgrammedMessage& msg) noexcept {
  auto* vrf = findVrf(msg.vrfId);
  if (!vrf) {
    XLOGF(
        ERR,
        "Ignoring FibProgrammedMessage for non-existent VRF {}",
        msg.vrfId);
    return;
  }
  auto& ribEntries = vrf->ribEntries();

  // Initial dump to all established peers
  auto sendWithEoR = msg.isSync && !vrf->initialEorSent();
  if (sendWithEoR) {
    /*
     * Mark one-time flag
     * Because of this flag, sendWithEoR can only be true once at
     * initialization.
     */
    vrf->markInitialEorSent();

    /**
     * Send initial EOR notification to PeerMgr.
     * The RibInitialAnnouncementStart message is a signal to PeerMgr that the
     * initial dump will start and we will enqueue announcements.
     * This msg MUST be enqueued BEFORE
     * the first RibAnnouncement only if sendWithEoR is true.
     */
    ribOutQ_.push(RibOutMessage{msg.vrfId, RibInitialAnnouncementStart{}});

    // log BGP++ initialization event
    BgpStats::logInitializationEvent("Rib", BgpInitializationEvent::EOR_SENT);
  }

  // Update the time-stamp for lastest timestamp of programmed routes
  if (!msg.fibProgrammedPfxs.empty()) {
    lastProgrammedRoutesTimeStamp_ =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
  }

  RibOutAnnouncement announcement;
  RibOutAnnouncement announcementAddPath;
  announcement.entries.reserve(kRibChunkSize);
  announcementAddPath.addPathEntries.reserve(kRibChunkSize);

  /*
   * If prefix is deleted from Fib, we send RibOutWithdrawal message to our
   * peers in prepareFibProgramming(), before deleting from Fib. If prefix is
   * not deleted from Fib, there may be a case where one or more of the ECMP
   * paths were deleted, we need to send withdrawals of paths to neighbors.
   */
  RibOutWithdrawal withdrawalAddPath;
  withdrawalAddPath.addPathEntries.reserve(kRibChunkSize);

  for (const auto& [_, path] : msg.fibProgrammedPfxs) {
    // Send updates to all established peers
    auto iter = RibBase::FibProgrammedPrefixIterator(*vrf, path);

    for (auto elem = iter.cbegin(); elem != iter.cend(); elem = iter.next()) {
      const auto& prefix = elem->first;
      const auto& weightedNexthops = elem->second;

      auto kv = ribEntries.find(prefix);
      if (kv == ribEntries.cend()) {
        XLOGF(
            ERR,
            "Cannot find RibEntry for {}",
            folly::IPAddress::networkToString(prefix));
        continue;
      }
      auto& entry = kv->second;

      /* Handle prefix removed from Fib by deleteUnicastRoutes. */
      if (handleDeletedUnicastRoute(*vrf, entry, weightedNexthops)) {
        continue;
      }
      /* Handle prefix whose FIB programmed nexthops are outdated. */
      if (handleStaleUnicastRoute(entry, weightedNexthops)) {
        continue;
      }
      /* Handle prefix added or updated in Fib by addUnicastRoutes */
      handleAddUpdatedUnicastRoute(
          *vrf,
          entry,
          sendWithEoR,
          announcement,
          announcementAddPath,
          withdrawalAddPath);
    }
  }

  /*
   * TODO: Move chunking into a unified RibOut writer that owns the in-progress
   * announcement and withdrawal messages. The messages would then no longer
   * need to be passed through each loop iteration (into
   * handleAddUpdatedUnicastRoute) and flushed separately here at the end of
   * the loop.
   */
  // if there are further withdraw for add path entries, we send them out.
  if (withdrawalAddPath.addPathEntries.size()) {
    XLOGF(DBG1, "{}", formatRibOutWithdrawalLog(withdrawalAddPath));
    ribOutQPushAndMayPauseBestPathAndFibProgramming(
        msg.vrfId, std::move(withdrawalAddPath));
  }

  announcement.addPathEntries = std::move(announcementAddPath.addPathEntries);
  if (sendWithEoR || announcement.entries.size() ||
      announcement.addPathEntries.size()) {
    if (sendWithEoR) {
      announcement.initialDump = true;
      announcement.sendWithEoR = true;
    }
    XLOGF(DBG1, "{}", formatRibOutAnnouncementLog(announcement));

    ribOutQPushAndMayPauseBestPathAndFibProgramming(
        msg.vrfId, std::move(announcement));
  }

  // publish unicast routes size.
  // TODO_CTR_VRF(T298290036): Publish the unicast route count of every VRF.
  if (msg.vrfId == fboss::kDefaultVrf) {
    FibStats::publishTotalUCastRoutes(ribEntries.size());
  }

  // Check if FIB is flushed (only local routes remain)
  if (ribEntries.size() <= vrf->localRoutes().size()) {
    FibStats::incrFibFlushed();
  }
}

/*
 * For any withdraw routes, we must have already notified all peers. Erase
 * ribEntry if no update in the interim. Returns true if the prefix was deleted
 * from Fib.
 */
bool RibBase::handleDeletedUnicastRoute(
    RibVrf& vrf,
    RibEntry& entry,
    const std::shared_ptr<const WeightedNexthopMap>&
        weightedNexthops) noexcept {
  if (weightedNexthops != nullptr) {
    return false;
  }
  // copy: erasing the RibEntry below destroys entry.getPrefix()
  const auto prefix = entry.getPrefix();
  XLOGF(
      DBG3,
      "Done withdrawing {} from Fib. Skip notifying BGP peers",
      folly::IPAddress::networkToString(prefix));
  // If no new route is added, delete ribEntry for this prefix.
  if (entry.getAllPathsCnt() == 0) {
    if (!entry.isOnFibBatchList()) {
      XLOGF(
          DBG3,
          "All paths withdrawn for {}, deleting RibEntry.",
          folly::IPAddress::networkToString(prefix));
      vrf.remove(entry);
    } else {
      XLOGF(
          DBG1,
          "All paths withdrawn for {}, when on the FibBatchList. Not deleting RibEntry.",
          folly::IPAddress::networkToString(prefix));
    }
  }
  return true;
}

/*
 * If there has been some change to any of the ECMP paths, delay sending the
 * update. Returns true if the programmed nexthops are already stale.
 */
bool RibBase::handleStaleUnicastRoute(
    const RibEntry& entry,
    const std::shared_ptr<const WeightedNexthopMap>&
        weightedNexthops) noexcept {
  /*
   * If nexthops have been changed, do not advertise it to BGP peers now.
   * The new nexthops will trigger another Fib programming and then come
   * back here with the new nexthops. We will advertise the prefix to
   * BGP peers then.
   */
  const bool isStale = entry.getMultipathWeightedNexthops() != weightedNexthops;
  XLOGF_IF(
      DBG3,
      isStale,
      "Done updating {} to Fib. But nexthops will change in next fib "
      "programming. Skip notifying BGP peers",
      folly::IPAddress::networkToString(entry.getPrefix()));
  return isStale;
}

void RibBase::handleAddUpdatedUnicastRoute(
    RibVrf& vrf,
    RibEntry& entry,
    bool sendWithEoR,
    RibOutAnnouncement& announcement,
    RibOutAnnouncement& announcementAddPath,
    RibOutWithdrawal& withdrawalAddPath) noexcept {
  const auto& prefix = entry.getPrefix();
  /*
   * Stamp the RIB version at emission time. Each prefix starts at a fresh
   * version (rule 3: different prefixes within a message strictly
   * increase). currentRibVersion is re-bumped at each chunk (kRibChunkSize)
   * flush below, so a prefix whose add-path entries span more than one chunk
   * strictly increases across those chunks (rule 2),
   * while all entries emitted within a single chunk share one version
   * (rule 1). A prefix appears exactly once in the FIB-ack (one
   * updateUnicastRoute per prefix, best-path-attr key), so this per-prefix
   * initialization runs once and needs no deduplication. Called after the
   * deleted/stale checks so a prefix that is skipped (already withdrawn from
   * FIB, or whose nexthops will change next round) does not consume a version.
   *
   * The final (highest) version is persisted onto the entry after add-path
   * processing (see the entry.setRibVersion below); the emit sites read
   * currentRibVersion directly.
   */
  uint64_t currentRibVersion = vrf.incrementRibVersion();

  /*
   * If this is the first time we are calculating a bestpath then mark it
   * as new entry later in the RibOutAnnouncement for out-delay purpose.
   * Note that if a route was already in RIB and then withdrawn by all
   * advertising peers and then readvertised again by some(all) of peers
   * it is also treated as eligible for out-delay and as such marked with
   * newlyInstalledInRib flag.
   */
  bool newlyInstalledInLocalRib = false;
  if (entry.advertisedBestpath_ == nullptr) {
    entry.installTimeStamp_ = std::chrono::system_clock::now();
    newlyInstalledInLocalRib = true;
  }

  const auto prevAdvertisedMultipathSize =
      entry.getAdvertisedMultipaths().size();

  if (enableRibAllocatedPathId_) {
    handleAddUpdatedMultipathDelta(
        vrf,
        entry,
        sendWithEoR,
        newlyInstalledInLocalRib,
        currentRibVersion,
        announcementAddPath,
        withdrawalAddPath);
  } else {
    handleAddUpdatedMultipaths(
        vrf,
        entry,
        sendWithEoR,
        newlyInstalledInLocalRib,
        currentRibVersion,
        announcementAddPath,
        withdrawalAddPath);
  }

  /*
   * Persist the final (highest) version reached for this prefix, so
   * getRibVersion() and any later reader reflect the last chunk emitted --
   * including when the best path itself did not change and we
   * return early without emitting a best-path entry.
   */
  entry.setRibVersion(currentRibVersion);

  if (!entry.commitBestpath()) {
    if (!shouldReadvertiseBestpathOnMultipathSizeChange(entry) ||
        prevAdvertisedMultipathSize == entry.getAdvertisedMultipaths().size()) {
      // bestpath did not change, no need for new advertisement
      return;
    }
  }

  const auto& bestpath = entry.getAdvertisedBestPath();
  XLOGF(
      DBG3,
      "Done updating {} to Fib. Added to BGP peer announcement.",
      folly::IPAddress::networkToString(prefix));

  /*
   * Announce the messages if we reached chunk size. This ensures that
   * while Rib is processing remaining message, adjRib, FiberBgp etc will
   * start working on previous chunks.
   */
  if (announcement.entries.size() == kRibChunkSize) {
    flushAnnouncementChunk(
        vrf, announcement, /*reserveAddPath=*/false, sendWithEoR);
  }
  announcement.entries.emplace_back(
      prefix,
      kDefaultPathID,
      bestpath->peer,
      bestpath->attrs,
      switchId_,
      entry.getMultipaths().size(),
      entry.getAggregateReceivedUcmpWeight(),
      entry.getAggregateLocalUcmpWeight(),
      entry.getRibPolicyUcmpWeight(),
      newlyInstalledInLocalRib,
      entry.installTimeStamp_,
      currentRibVersion,
      entry.getIsPartialDrain());
}

void RibBase::handleAddUpdatedMultipaths(
    RibVrf& vrf,
    RibEntry& entry,
    bool sendWithEoR,
    bool newlyInstalledInLocalRib,
    uint64_t& currentRibVersion,
    RibOutAnnouncement& announcementAddPath,
    RibOutWithdrawal& withdrawalAddPath) noexcept {
  const auto oldAdvMultipathNHs =
      entry.getAdvertisedMultipathWeightedNexthops();
  entry.commitMultipathNexthops();
  const auto newAdvMultipathNHs =
      entry.getAdvertisedMultipathWeightedNexthops();

  if (!(entry.commitMultipaths() && entry.bestpath_)) {
    return;
  }

  /*
   * TODO: It looks here that we are re-advertising all paths whether or
   * not they changed.  While this is not incorrect, it will result in
   * sending more info than we need.  Something can be done here, or in
   * AdjRib to fix this.
   */
  const auto advMultPaths = entry.getAdvertisedMultipaths();
  for (const auto& [_, advMultPath] : advMultPaths) {
    if (announcementAddPath.addPathEntries.size() == kRibChunkSize) {
      flushAnnouncementChunk(
          vrf, announcementAddPath, /*reserveAddPath=*/true, sendWithEoR);

      /*
       * rule 2: this prefix's remaining add-path entries land in a new
       * chunk (a separate RibOut message), so bump the version. A peer
       * that detached after the flushed chunk must observe a strictly
       * higher version on the entries that follow, otherwise
       * isEntryShared would wrongly treat them as already-seen and the
       * peer would miss them.
       */
      currentRibVersion = vrf.incrementRibVersion();
    }

    XCHECK(advMultPath->pathIdToSend.has_value());
    announcementAddPath.addPathEntries.emplace_back(
        entry.getPrefix(),
        advMultPath->pathIdToSend.value(),
        advMultPath->peer,
        advMultPath->attrs,
        switchId_,
        entry.getMultipaths().size(),
        entry.getAggregateReceivedUcmpWeight(),
        entry.getAggregateLocalUcmpWeight(),
        entry.getRibPolicyUcmpWeight(),
        newlyInstalledInLocalRib,
        entry.installTimeStamp_,
        currentRibVersion,
        entry.getIsPartialDrain());
  }

  // if we are withdrawing certain paths, we need to notify AdjRib
  if (oldAdvMultipathNHs && newAdvMultipathNHs &&
      (*oldAdvMultipathNHs != *newAdvMultipathNHs)) {
    for (const auto oldNhIter : *oldAdvMultipathNHs) {
      if (newAdvMultipathNHs->find(oldNhIter.first) ==
          newAdvMultipathNHs->end()) {
        if (withdrawalAddPath.addPathEntries.size() == kRibChunkSize) {
          flushWithdrawalChunk(
              vrf,
              withdrawalAddPath,
              /*reserveAddPath=*/true,
              /*logAsAddPath=*/false);

          /*
           * rule 2: this prefix's remaining add-path withdrawals land in
           * a new chunk, so bump the version (see the add-path
           * announcement flush above for the reasoning).
           */
          currentRibVersion = vrf.incrementRibVersion();
        }
        withdrawalAddPath.addPathEntries.emplace_back(
            entry.getPrefix(),
            kPlaceholderPathID,
            oldNhIter.first,
            currentRibVersion);
      }
    }
  }
}

/*
 * Path ID based add-path processing, used when enableRibAllocatedPathId_ is
 * set. Announces only the paths that are new or changed since the last
 * advertisement, and withdraws the path IDs that are no longer advertised.
 */
void RibBase::handleAddUpdatedMultipathDelta(
    RibVrf& vrf,
    RibEntry& entry,
    bool sendWithEoR,
    bool newlyInstalledInLocalRib,
    uint64_t& currentRibVersion,
    RibOutAnnouncement& announcementAddPath,
    RibOutWithdrawal& withdrawalAddPath) noexcept {
  entry.commitMultipathNexthops();

  /*
   * Every add-path entry carries the per-prefix fields, so if any changed,
   * re-send every advertised path. commitBestpath() commits them afterwards.
   */
  const bool prefixFieldsChanged = entry.advertisedPrefixFieldsChanged();
  if (!entry.multipathChanged() && !prefixFieldsChanged) {
    return;
  }

  /*
   * Multipaths can exist without a bestpath (RibDC failed CPS criteria).
   * Nothing is announced and the multipaths are not committed, so they are
   * announced once a bestpath returns.
   */
  if (!entry.bestpath_) {
    return;
  }

  /*
   * commitMultipaths() below overwrites the advertised multipaths. Keep the
   * previous set to compute the delta.
   */
  auto oldAdvMultipaths = entry.getAdvertisedMultipaths();
  entry.commitMultipaths();

  /* Announce the updated/new multipaths. */
  for (const auto& [pathId, advMultPath] : entry.getAdvertisedMultipaths()) {
    bool unchanged = false;
    oldAdvMultipaths.eraseInto(pathId, [&](auto&& /*key*/, auto&& oldPath) {
      unchanged = oldPath == advMultPath;
    });
    /* Existing (old-advertised), unchanged multipaths are not announced.*/
    if (unchanged && !prefixFieldsChanged) {
      continue;
    }
    if (announcementAddPath.addPathEntries.size() == kRibChunkSize) {
      flushAnnouncementChunk(
          vrf, announcementAddPath, /*reserveAddPath=*/true, sendWithEoR);

      currentRibVersion = vrf.incrementRibVersion();
    }
    announcementAddPath.addPathEntries.emplace_back(
        entry.getPrefix(),
        pathId,
        advMultPath->peer,
        advMultPath->attrs,
        switchId_,
        entry.getMultipaths().size(),
        entry.getAggregateReceivedUcmpWeight(),
        entry.getAggregateLocalUcmpWeight(),
        entry.getRibPolicyUcmpWeight(),
        newlyInstalledInLocalRib,
        entry.installTimeStamp_,
        currentRibVersion,
        entry.getIsPartialDrain());
  }

  /* Withdraw the path IDs that weren't in advertised multipaths. */
  for (const auto& [pathId, oldPath] : oldAdvMultipaths) {
    if (withdrawalAddPath.addPathEntries.size() == kRibChunkSize) {
      flushWithdrawalChunk(
          vrf,
          withdrawalAddPath,
          /*reserveAddPath=*/true,
          /*logAsAddPath=*/false);

      currentRibVersion = vrf.incrementRibVersion();
    }
    withdrawalAddPath.addPathEntries.emplace_back(
        entry.getPrefix(),
        pathId,
        oldPath->attrs->getNexthop(),
        currentRibVersion);
  }
}

TFibNexthopDatabase RibBase::getFibNexthopDatabase(VrfId vrfId) {
  TFibNexthopDatabase database;
  database.enabled() = globalConfig_.enableFibOutTracking;
  if (!globalConfig_.enableFibOutTracking) {
    return database;
  }

  std::exception_ptr traversalException;
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() noexcept {
    const auto* vrf = findVrf(vrfId);
    if (!vrf) {
      return;
    }
    const auto& fibNexthopSets = vrf->fibNexthopSets();
    try {
      database.nexthop_sets()->reserve(fibNexthopSets.size());
      fibNexthopSets.forEach(
          [&](const FibNexthopSet& nexthops, size_t routeRefCount) {
            database.nexthop_sets()->push_back(toThriftFibNexthopSet(
                nexthops, static_cast<int64_t>(routeRefCount)));
          });
    } catch (...) {
      traversalException = std::current_exception();
    }
  });
  if (traversalException) {
    std::rethrow_exception(traversalException);
  }
  return database;
}

void RibBase::handleFibSyncReq(const Fib::FibSyncReq& /* unused */) noexcept {
  // The FIB agent lost every VRF's routes; full-sync each initialized VRF.
  for (const auto& [vrfId, ribVrf] : ribVrfs_) {
    /*
     * skip processing full-sync request sending from FIB
     * since initial RIB full-sync has NOT be triggered yet
     */
    if (!ribVrf->ribEoRReceived()) {
      continue;
    }
    if (pauseBestPathAndFibProgramming_) {
      fibSyncReqPending_ = true;
      XLOG(
          DBG2,
          "Setting FibSyncReqPending_ flag to trigger fullSync when resumed.");
      return;
    }
    prepareFibProgramming(vrfId, true /* fullSync */);
  }
}

std::vector<TRibEntry> RibBase::getRibEntries(TBgpAfi afi, VrfId vrfId) {
  std::vector<TRibEntry> tRibEntries;

  if (afi != TBgpAfi::AFI_IPV4 && afi != TBgpAfi::AFI_IPV6) {
    return tRibEntries;
  }

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    const auto* vrf = findVrf(vrfId);
    if (!vrf) {
      return;
    }
    auto expectIPv4 = afi == TBgpAfi::AFI_IPV4;
    for (const auto& entry : vrf->ribEntries()) {
      const auto& prefix = entry.first;
      auto isIPv4 = prefix.first.family() == AF_INET;
      if ((expectIPv4 && !isIPv4) || (!expectIPv4 && isIPv4)) {
        continue;
      }
      tRibEntries.emplace_back(createTRibEntry(entry));
    }
  });
  return tRibEntries;
}

TFibOutTable RibBase::getFibOutPrefix(
    const std::string& prefixString,
    VrfId vrfId) {
  folly::CIDRNetwork prefix;
  try {
    prefix =
        folly::IPAddress::createNetwork(prefixString, -1, true /* applyMask */);
  } catch (const std::exception& error) {
    throw std::invalid_argument(
        fmt::format(
            "Invalid FIB-out prefix '{}': {}", prefixString, error.what()));
  }

  TFibOutTable table;
  table.enabled() = globalConfig_.enableFibOutTracking;
  if (!globalConfig_.enableFibOutTracking) {
    return table;
  }

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    const auto* vrf = findVrf(vrfId);
    if (!vrf) {
      return;
    }
    const auto& ribEntries = vrf->ribEntries();
    const auto ribEntry = ribEntries.find(prefix);
    if (ribEntry == ribEntries.end()) {
      return;
    }
    TFibOutEntry entry;
    entry.prefix() = buildTPrefix(ribEntry->first);
    if (const auto* fibOut = vrf->findFibOut(ribEntry->second)) {
      auto thriftFibOut = toThriftFibOutRoute(*fibOut);
      thriftFibOut.nexthop_set_ref_count() =
          static_cast<int64_t>(fibOut->nexthops.use_count());
      entry.fib_out() = std::move(thriftFibOut);
    }
    table.entries()->push_back(std::move(entry));
  });
  return table;
}

TRibSummary RibBase::getRibSummary(TBgpAfi afi, VrfId vrfId) {
  TRibSummary summary;
  summary.afi() = afi;

  if (afi != TBgpAfi::AFI_IPV4 && afi != TBgpAfi::AFI_IPV6) {
    return summary;
  }

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    const auto* vrf = findVrf(vrfId);
    if (!vrf) {
      return;
    }
    const auto& ribCounters = vrf->ribCounters();
    const bool isV4 = afi == TBgpAfi::AFI_IPV4;
    summary.total_prefixes() = ribCounters.totalPrefixes(isV4);
    summary.total_paths() = ribCounters.totalPaths(isV4);
    summary.inactive_paths() = ribCounters.inactivePaths(isV4);
    const auto& counts = ribCounters.prefixLenCounts(isV4);
    for (size_t len = 0; len < counts.size(); ++len) {
      if (counts[len] != 0) {
        summary.prefix_length_counts()[static_cast<int16_t>(len)] = counts[len];
      }
    }
    summary.ebgp_prefixes() = ribCounters.ebgpPrefixes(isV4);
    summary.ibgp_prefixes() = ribCounters.ibgpPrefixes(isV4);
    summary.confed_ebgp_prefixes() = ribCounters.confedEbgpPrefixes(isV4);
    summary.local_prefixes() = ribCounters.localPrefixes(isV4);
    // RIB-wide (not per-AFI); identical in both responses.
    summary.unresolvable_nexthops_count() = ribCounters.unresolvableNexthops();
    summary.routes_with_unresolved_nexthops() =
        ribCounters.routesWithUnresolvedNexthops(isV4);
  });
  return summary;
}

uint64_t RibBase::getNumPrefixes(VrfId vrfId) {
  uint64_t count = 0;
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    if (const auto* vrf = findVrf(vrfId)) {
      count = vrf->ribCounters().totalPrefixes();
    }
  });
  return count;
}
std::vector<TRibEntry> RibBase::getRibEntriesForCommunities(
    TBgpAfi afi,
    const std::vector<nettools::bgplib::BgpAttrCommunityC>& communities,
    VrfId vrfId) {
  std::vector<TRibEntry> tRibEntries;

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    const auto* vrf = findVrf(vrfId);
    if (!vrf) {
      return;
    }
    for (const auto& entry : vrf->ribEntries()) {
      const auto& prefix = entry.first;
      if (((afi == TBgpAfi::AFI_IPV4) && (prefix.first.family() != AF_INET)) ||
          ((afi == TBgpAfi::AFI_IPV6) && (prefix.first.family() != AF_INET6))) {
        continue;
      }
      auto ret = createTRibEntryWithFilter(
          entry, [&communities](const RouteInfo& path) -> bool {
            const auto& comms = path.attrs->getCommunities();
            if (!comms.nullOrEmpty()) {
              for (const auto& comm : communities) {
                if (std::find(comms->cbegin(), comms->cend(), comm) !=
                    comms->cend()) {
                  return true;
                }
              }
            }
            return false;
          });
      if (!ret) {
        continue;
      }
      auto tRibEntry = *ret;
      /*
       * if all paths were pruned out by community filter above, then exclude
       * this entry from o/p list.
       */
      if (tRibEntry.paths()->size()) {
        tRibEntries.emplace_back(tRibEntry);
      }
    }
  });
  return tRibEntries;
}

std::vector<TRibEntry> RibBase::getRibEntryForPrefix(
    std::unique_ptr<std::string> prefix,
    VrfId vrfId) {
  std::vector<TRibEntry> tRibEntries;
  if (!prefix) {
    return tRibEntries;
  }

  folly::CIDRNetwork network;
  try {
    network = folly::IPAddress::createNetwork(*prefix);
  } catch (std::exception const&) {
    XLOGF(ERR, "Invalid prefix: {}", *prefix);
    return tRibEntries;
  }

  /* runImmediatelyOrRunInEventBaseThreadAndWait: runs inline if already
   * on Rib's evb (e.g., called via folly::via from co_runOnEvbWithTimeout),
   * otherwise dispatches and waits. Avoids deadlock in both contexts. */
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    const auto* vrf = findVrf(vrfId);
    if (!vrf) {
      return;
    }
    const auto& ribEntries = vrf->ribEntries();
    auto entry = ribEntries.find(network);
    if (entry != ribEntries.end()) {
      tRibEntries.emplace_back(createTRibEntry(*entry));
    }
  });

  return tRibEntries;
}

std::vector<TRibEntry> RibBase::getRibEntriesForSubprefixes(
    std::unique_ptr<std::string> prefix,
    VrfId vrfId) {
  std::vector<TRibEntry> tRibEntries;
  if (!prefix) {
    XLOG(ERR, "No prefix is provided");
    return tRibEntries;
  }

  folly::CIDRNetwork parentPrefix;
  try {
    parentPrefix = folly::IPAddress::createNetwork(*prefix);
  } catch (std::exception const&) {
    XLOGF(ERR, "Invalid prefix: {}", *prefix);
    return tRibEntries;
  }

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    const auto* vrf = findVrf(vrfId);
    if (!vrf) {
      return;
    }
    for (const auto& entry : vrf->ribEntries()) {
      const auto& subPrefix = entry.first;
      if (!isSubnet(subPrefix, parentPrefix)) {
        continue;
      }
      tRibEntries.emplace_back(createTRibEntry(entry));
    }
  });
  return tRibEntries;
}

void RibBase::injectLocalRoutes(
    const std::map<TIpPrefix, TBgpAttributes>& networks) {
  if (!networks.size()) {
    return;
  }

  for (auto iter = networks.begin(); iter != networks.end(); ++iter) {
    auto tIpPrefix = iter->first;
    auto communities = *iter->second.communities();

    auto prefix = std::make_pair(
        folly::IPAddress::fromBinary(
            folly::ByteRange(folly::StringPiece(*tIpPrefix.prefix_bin()))),
        *tIpPrefix.num_bits());
    auto prefixStr = folly::IPAddress::networkToString(prefix);
    XLOGF(INFO, "Inject LocalRoute: {}", prefixStr);
    std::vector<std::string> communitiesStr;
    communitiesStr.reserve(communities.size());
    for (const auto& comm : communities) {
      communitiesStr.push_back(
          fmt::format("{}:{}", *comm.asn(), *comm.value()));
    }
    thrift::BgpNetwork network;
    network.prefix() = prefixStr;
    network.communities() = communitiesStr;

    // setting optional fields for network
    if (iter->second.local_pref()) {
      network.local_pref() = *iter->second.local_pref();
    }

    if (iter->second.origin()) {
      network.origin() = *iter->second.origin();
    }

    if (iter->second.nexthop()) {
      const auto& nexthop_prefix = *iter->second.nexthop();
      auto nexthop = folly::IPAddress::fromBinary(
          folly::ByteRange(folly::StringPiece(*nexthop_prefix.prefix_bin())));
      network.nexthop() = nexthop.str();
    }

    if (iter->second.as_path()) {
      network.as_path() = *iter->second.as_path();
    }

    if (auto installToFib = iter->second.install_to_fib()) {
      network.install_to_fib() = *installToFib;
    }

    // TODO: support policy from CLI
    auto coro = co_withExecutor(
        &evb_,
        folly::coro::co_invoke(
            [this,
             prefix = std::move(prefix),
             network = std::move(network)]() -> folly::coro::Task<void> {
              auto localRouteOpt = createLocalRoute(prefix, network);
              if (!localRouteOpt.has_value()) {
                // could be rejected by policy
                co_return;
              }
              const auto& localRoute = localRouteOpt.value();
              PrefixPathIds pfxPathIds{{prefix, kDefaultPathID}};
              co_await ribInQ_.push(RibInAnnouncement(
                  fboss::kDefaultVrf,
                  kV4LocalPeerInfo,
                  pfxPathIds,
                  localRoute.attrs));
            }));
    folly::coro::blockingWait(std::move(coro));
  }
}

void RibBase::removeLocalRoutes(const std::set<TIpPrefix>& prefixes) {
  if (!prefixes.size()) {
    return;
  }

  for (auto tIpPrefix : prefixes) {
    auto prefix = std::make_pair(
        folly::IPAddress::fromBinary(
            folly::ByteRange(folly::StringPiece(*tIpPrefix.prefix_bin()))),
        *tIpPrefix.num_bits());
    auto prefixStr = folly::IPAddress::networkToString(prefix);
    XLOGF(INFO, "Remove LocalRoute: {}", prefixStr);
    // TODO: support install_to_fib from CLI
    auto coro = co_withExecutor(
        &evb_,
        folly::coro::co_invoke(
            [this, prefix = std::move(prefix)]() -> folly::coro::Task<void> {
              PrefixPathIds pfxPathIds{{prefix, kDefaultPathID}};
              co_await ribInQ_.push(RibInWithdrawal(
                  fboss::kDefaultVrf, kV4LocalPeerInfo, pfxPathIds));
            }));
    folly::coro::blockingWait(std::move(coro));
  }
}

std::shared_ptr<BgpPath> RibBase::getBgpPathFromPolicy(
    const std::string& policyName,
    const folly::CIDRNetwork& prefix,
    const std::shared_ptr<BgpPath>& preInAttrs) {
  if (!policyManager_ || !policyManager_->getPolicyFromName(policyName)) {
    throw BgpError(
        fmt::format(
            "Missing network policy ({}) for prefix ({})",
            policyName,
            folly::IPAddress::networkToString(prefix)));
  }
  if (!preInAttrs) {
    // There is no preIn, so we can't generate postIn
    return preInAttrs;
  }

  auto policyOut = policyManager_->applyPolicy(
      policyName, PolicyInMessage({prefix}, preInAttrs));
  if (policyOut.result.find(prefix) == policyOut.result.end()) {
    // Policy Filtered this prefix
    return nullptr;
  }
  return policyOut.result[prefix]->attrs;
}

std::optional<facebook::bgp::LocalRoute> RibBase::createLocalRoute(
    const folly::CIDRNetwork& prefix,
    const facebook::bgp::thrift::BgpNetwork& network) {
  facebook::nettools::bgplib::BgpPathC pathC;
  // initialize default attribute values, which could be overwritten by policy
  if (prefix.first.isV4()) {
    pathC.nexthop = network.nexthop().has_value()
        ? folly::IPAddress(network.nexthop().value())
        : facebook::bgp::kLocalRouteV4Nexthop;
  } else {
    pathC.nexthop = network.nexthop().has_value()
        ? folly::IPAddress(network.nexthop().value())
        : facebook::bgp::kLocalRouteV6Nexthop;
  }

  facebook::nettools::bgplib::BgpAttributesC mutableAttrs;

  if (network.origin().has_value()) {
    if (network.origin().value() <
            static_cast<int>(
                apache::thrift::TEnumTraits<
                    ::facebook::nettools::bgplib::BgpAttrOrigin>::min()) ||
        network.origin().value() >
            static_cast<int>(
                apache::thrift::TEnumTraits<
                    ::facebook::nettools::bgplib::BgpAttrOrigin>::max())) {
      XLOGF(ERR, "Invalid network origin value : {}", network.origin().value());
      return std::nullopt;
    }
    mutableAttrs.origin =
        static_cast<nettools::bgplib::BgpAttrOrigin>(network.origin().value());
  } else {
    mutableAttrs.origin =
        facebook::nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_IGP;
  }

  mutableAttrs.localPref = network.local_pref().has_value()
      ? network.local_pref().value()
      : facebook::bgp::kDefaultLocalPref;

  if (network.communities().has_value()) {
    mutableAttrs.communities =
        facebook::bgp::createBgpAttrCommunitiesC(network.communities().value());
  }

  if (network.as_path().has_value()) {
    mutableAttrs.asPath =
        facebook::bgp::createBgpAttrAsPathDedup(network.as_path().value());
  }

  pathC.attrs = std::move(mutableAttrs);

  auto attrs = std::make_shared<facebook::bgp::BgpPath>(
      static_cast<facebook::bgp::BgpPathFields>(pathC));

  /*
   * Set weight to 2^15 on local routes to ensure they win in path selection
   * when weight comparison is enabled
   */
  attrs->setWeight(1 << 15);

  if (auto policyName = network.policy_name()) {
    // run policy and set new attributes
    attrs = getBgpPathFromPolicy(*policyName, prefix, attrs);
    if (!attrs) {
      // policy rejected local route generation
      return std::nullopt;
    }
  }
  attrs->publish();

  return facebook::bgp::LocalRoute(network, 0, attrs);
}
bool RibBase::enableUnicastRouteLogging(const folly::CIDRNetwork& prefix) {
  if (prefix == kDefaultRouteV4 || prefix == kDefaultRouteV6) {
    return FLAGS_enable_default_route_logging;
  }
  return false; // default
}

TRibEntry RibBase::createTRibEntry(
    const std::pair<const folly::CIDRNetwork, facebook::bgp::RibEntry>& entry) {
  return *(
      createTRibEntryWithFilter(entry, [](const RouteInfo&) { return true; }));
}

std::optional<neteng::fboss::bgp::thrift::TRibEntry>
RibBase::createTRibEntryWithFilter(
    const std::pair<const folly::CIDRNetwork, facebook::bgp::RibEntry>& entry,
    const std::function<bool(const RouteInfo&)>& pathFilter) {
  TRibEntry tRibEntry;
  TBgpAfi afi =
      entry.first.first.isV4() ? TBgpAfi::AFI_IPV4 : TBgpAfi::AFI_IPV6;
  tRibEntry.prefix()->afi() = afi;
  tRibEntry.prefix()->num_bits() = entry.first.second;
  tRibEntry.prefix()->prefix_bin() =
      facebook::network::toBinaryAddress(entry.first.first)
          .addr()
          ->toStdString();

  const auto& ribEntry = entry.second;

  /*
   * Check if the route's weights are overridden by CTE (route attribute
   * policy). If so, set the corresponding active UCMP action to
   * active_cte_ucmp_action.
   */
  if (routeAttributePolicy_) {
    auto activeCteUcmpAction =
        routeAttributePolicy_->getActiveCteUcmpAction(ribEntry.getPrefix());
    if (activeCteUcmpAction) {
      tRibEntry.active_cte_ucmp_action() = std::move(*activeCteUcmpAction);
    }
  }

  const auto& bestpath = ribEntry.getBestPath();
  /*
   * Don't exit pre-maturely because we can have bestpath == null but
   * multipath not null in the case of bgp_native_path_selection_min_nexthop
   */
  if (bestpath) {
    auto bestNexthop = bestpath->attrs->getNexthop();
    tRibEntry.best_next_hop() = createTIpPrefix(bestNexthop);
  }
  /*
   * We group the paths into two groups:
   * (i) Selected Best/Multipath and (ii) default.
   */
  std::vector<TBgpPath> tDefaultPaths{};
  std::vector<TBgpPath> tBestPaths{};

  const auto& routeinfos = ribEntry.getAllPaths();
  const auto& multipath_routeinfos = ribEntry.getMultipaths();

  auto weightedNexthops = ribEntry.getMultipathWeightedNexthops();
  for (const auto& routeinfo : routeinfos) {
    if (!pathFilter(*routeinfo)) {
      continue;
    }
    auto tPath = createTBgpPath(*(routeinfo->attrs));
    tPath.router_id() = routeinfo->peer.routerId;
    tPath.peer_id() = createTIpPrefix(routeinfo->peer.addr);
    tPath.peer_description() = routeinfo->peer.description;
    if (weightedNexthops) {
      auto it = weightedNexthops->find(routeinfo->peer.addr);
      if (it != weightedNexthops->end()) {
        tPath.next_hop_weight() = it->second;
      }
    }
    /*
     * Reachability and cost presence are independent. Serialize igp_cost only
     * when supplied; local routes get the maximum value.
     */
    if (routeinfo->getIsRouteLocal() || routeinfo->hasIgpCost()) {
      tPath.igp_cost() = routeinfo->getIgpCostValue();
    }
    tPath.bestpath_filter_descr() = routeinfo->getBestPathFilterDescr();
    tPath.last_modified_time() = routeinfo->lastModifiedTime_;
    tPath.path_id() = routeinfo->receivedPathId;
    if (routeinfo->pathIdToSend.has_value()) {
      tPath.path_id_to_send() = routeinfo->pathIdToSend.value();
    }
    setInactiveFlag(tPath, *routeinfo);
    if (routeinfo->pathIdToSend.has_value() &&
        multipath_routeinfos.contains(routeinfo->pathIdToSend.value())) {
      if (bestpath && routeinfo == bestpath) {
        tPath.is_best_path() = true;
        /*
         * Surface the selected bestpath as a top-level field so FSDB
         * subscribers can fetch only the best path (cheap) without
         * subscribing to the full `paths` map. Reuses the just-built tPath
         * to avoid a second pass over the same attributes per prefix.
         */
        tRibEntry.best_path() = tPath;
      }
      tBestPaths.emplace_back(tPath);
    } else {
      tDefaultPaths.emplace_back(tPath);
    }
  }
  std::map<std::string, std::vector<TBgpPath>> pathGrps;
  if (tBestPaths.size()) {
    pathGrps.emplace(facebook::bgp::kBestPathGroup, tBestPaths);
    /*
     * best_group labels the selected group only when a best path exists;
     * left empty otherwise so it reads as a presence signal, not a constant.
     */
    tRibEntry.best_group() = facebook::bgp::kBestPathGroup;
  }
  if (tDefaultPaths.size()) {
    pathGrps.emplace(facebook::bgp::kDefaultPathGroup, tDefaultPaths);
  }
  tRibEntry.paths() = pathGrps;

  /*
   * Indicate if path selection is pending (IGP cost may have changed
   * but best-path hasn't been recalculated yet)
   */
  if (ribEntry.needPathSelection()) {
    tRibEntry.path_selection_pending() = true;
  }

  tRibEntry.rib_version() = ribEntry.getRibVersion();

  return tRibEntry;
}

// Get originated routes(local networks) for display
std::vector<TOriginatedRoute> RibBase::getOriginatedRoutes(VrfId vrfId) {
  std::vector<TOriginatedRoute> tRoutes;

  /* runImmediatelyOrRunInEventBaseThreadAndWait: runs inline if already
   * on Rib's evb (e.g., called via folly::via from co_runOnEvbWithTimeout),
   * otherwise dispatches and waits. Avoids deadlock in both contexts. */
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    const auto* vrf = findVrf(vrfId);
    if (!vrf) {
      return;
    }
    for (const auto& [prefix, localRoute] : vrf->localRoutes()) {
      TIpPrefix tPrefix;
      TOriginatedRoute tRoute;

      auto binAddr = toBinaryAddress(prefix.first);
      tPrefix.afi() =
          prefix.first.isV4() ? TBgpAfi::AFI_IPV4 : TBgpAfi::AFI_IPV6;
      tPrefix.num_bits() = prefix.second;
      tPrefix.prefix_bin() = binAddr.addr()->toStdString();

      tRoute.prefix() = tPrefix;
      tRoute.path() = createTBgpPath(*localRoute.attrs);

      /*
       * TODO: Communities are deprecated. We already populate attrs which has
       * communities.
       *       Remove communities support.
       */
      std::vector<TBgpCommunity> tComms;
      for (const auto& comm : localRoute.attrs->getCommunities().get()) {
        TBgpCommunity tComm;
        tComm.asn() = comm.asn;
        tComm.value() = comm.value;
        tComm.community() = ((int64_t)comm.asn << 16) + comm.value;
        tComms.emplace_back(std::move(tComm));
      }

      tRoute.communities() = tComms;
      tRoute.minimum_supporting_routes() =
          getMinSupportRoutes(localRoute.network);
      tRoute.install_to_fib() = getInstallToFib(localRoute.network);
      tRoute.supporting_route_count() = localRoute.supportPfxCnt;
      if (auto policyName = localRoute.network.policy_name()) {
        tRoute.policy_name() = *policyName;
      }
      if (localRoute.network.require_nexthop_resolution()) {
        tRoute.require_nexthop_resolution() =
            *localRoute.network.require_nexthop_resolution();
      }
      tRoutes.emplace_back(std::move(tRoute));
    }
  });
  return tRoutes;
}

rib_policy::TRibPolicy RibBase::getRibPolicy() {
  rib_policy::TRibPolicy result;

  /* runImmediatelyOrRunInEventBaseThreadAndWait: runs inline if already
   * on Rib's evb (e.g., called via folly::via from co_runOnEvbWithTimeout),
   * otherwise dispatches and waits. Avoids deadlock in both contexts. */
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    /*
     * RibBase fills the sub-policies it owns; platform subclasses can
     * extend by overriding getRibPolicy.
     */
    if (routeAttributePolicy_) {
      result.route_attribute_policy() = routeAttributePolicy_->toThrift();
    }
    if (routeFilterPolicy_) {
      result.route_filter_policy() = routeFilterPolicy_->toThrift();
    }
  });
  return result;
}

void RibBase::updateEntryStats(TEntryStats& stats, VrfId vrfId) noexcept {
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    const auto* vrf = findVrf(vrfId);
    if (!vrf) {
      return;
    }
    stats.total_ucast_routes() = vrf->ribEntries().size();
    stats.total_originated_routes() = vrf->localRoutes().size();

    /*
     * total_rib_paths is the add-path-correct sum of every prefix's path count.
     * RibCounters maintains it incrementally on the RIB evb (see onPathsDelta),
     * so read it directly instead of walking all ribEntries_ (O(total paths))
     * on every getEntryStats call.
     */
    stats.total_rib_paths() = vrf->ribCounters().totalPaths();
  });
}

void RibBase::clearRibPolicy() {
  // push clear message to policy queue
  enqueueRibPolicyMsg(RibPolicyClearMsg{});
}

/**
 * [Route Attribute Policy]
 */
neteng::fboss::bgp::thrift::TResult RibBase::setRouteAttributePolicy(
    std::unique_ptr<rib_policy::TRouteAttributePolicy> policy) {
  neteng::fboss::bgp::thrift::TResult result;
  try {
    RouteAttributePolicy raPolicy{*policy};
  } catch (const BgpError& ex) {
    auto errorMsg = folly::exceptionStr(ex);
    XLOGF(ERR, "{}", errorMsg);
    result.success() = false;
    result.err() = errorMsg;
    return result;
  }

  // push rib policy set message to policy queue
  enqueueRibPolicyMsg(RouteAttributePolicySetMsg{std::move(*policy)});
  result.success() = true;
  return result;
}

rib_policy::TRouteAttributePolicy RibBase::getRouteAttributePolicy() {
  rib_policy::TRouteAttributePolicy result;
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    if (routeAttributePolicy_ != nullptr) {
      result = routeAttributePolicy_->toThrift();
    }
  });
  return result;
}

void RibBase::clearRouteAttributePolicy() {
  // push clear message to policy queue
  enqueueRibPolicyMsg(RouteAttributePolicyClearMsg{});
}

int64_t RibBase::getRouteFilterPolicyVersion() const {
  return routeFilterPolicy_ ? routeFilterPolicy_->getVersion() : -1;
}

/**
 * [Route Filter Policy]
 */
void RibBase::setRouteFilterPolicy(
    std::unique_ptr<rib_policy::TRouteFilterPolicy> policy,
    bool forceUpdate) {
  // push rib policy set message to policy queue
  enqueueRibPolicyMsg(RouteFilterPolicySetMsg{std::move(*policy), forceUpdate});
}

rib_policy::TRouteFilterPolicy RibBase::getRouteFilterPolicy() {
  rib_policy::TRouteFilterPolicy result;
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    if (routeFilterPolicy_) {
      result = routeFilterPolicy_->toThrift();
    }
  });
  return result;
}

void RibBase::clearRouteFilterPolicy() {
  // push clear message to policy queue
  enqueueRibPolicyMsg(RouteFilterPolicyClearMsg{});
}

bool RibBase::replaceRouteFilterPolicy(
    std::unique_ptr<RouteFilterPolicy> newPolicy,
    bool isBootstrap,
    bool forceUpdate) {
  RibStats::STATS_rfPolicyRcvd.add(1);
  // hasUpdate is true when newPolicy is different from routeFilterPolicy_
  bool hasUpdate;

  if (routeFilterPolicy_) {
    /*
     * When routeFilterPolicy_ has cached one policy, hasUpdate if
     * 1. newPolicy == nullptr
     * 2. cached policy has delta with new one
     * forceUpdate bypasses the version check (used by FILE_MODE)
     */
    hasUpdate = (newPolicy == nullptr) ||
        ((*routeFilterPolicy_ != *newPolicy) &&
         (forceUpdate ||
          routeFilterPolicy_->getVersion() <= newPolicy->getVersion()));
    if (forceUpdate && newPolicy && *routeFilterPolicy_ != *newPolicy &&
        routeFilterPolicy_->getVersion() > newPolicy->getVersion()) {
      BgpStats::incrCrfForceUpdateBypass();
    }
  } else {
    /*
     * routeFilterPolicy_ does not cache anything, hasUpdate if newPolicy is
     * not nullptr
     */
    hasUpdate = (newPolicy != nullptr);
  }

  if (hasUpdate) {
    XLOG(DBG1, "[CRF] Updating RouteFilterPolicy.");

    routeFilterPolicy_ = std::move(newPolicy);

    // Only log and append to history for real policy updates, not bootstrap
    if (!isBootstrap) {
      // Save upon receipt of RouteFilterPolicy
      saveRibPolicyState();
      XLOGF(
          INFO,
          "[CRF] RouteFilterPolicy version: {}",
          getRouteFilterPolicyVersion());

      appendRibPolicyChangeHistory("CRF", getRouteFilterPolicyVersion());
    }

    RibStats::STATS_rfPolicyUpdate.add(1);

    /* Virtual hook: subclasses override to handle post route filter policy. */
    postRouteFilterPolicyReplaced();
  }
  return hasUpdate;
}

std::unique_ptr<RibPolicy> RibBase::readRibPolicyState() noexcept {
  auto tRibPolicyStore =
      readThriftArtifactFromFile<TRibPolicyStore>(FLAGS_rp_state_file);
  if (!tRibPolicyStore) {
    return nullptr;
  }

  if (*tRibPolicyStore->fileTermination() != kRibPolicyFileTermination) {
    XLOGF(
        ERR,
        "Invalid HA store file termination: {}",
        *tRibPolicyStore->fileTermination());
    return nullptr;
  }

  std::unique_ptr<RibPolicy> res;
  try {
    res = std::make_unique<RibPolicy>(*tRibPolicyStore->policy());
  } catch (const std::exception& ex) {
    XLOGF(
        ERR,
        "Could not convert thrift Rib Policy to c++: {}",
        folly::exceptionStr(ex));
    return nullptr;
  }

  XLOG(INFO, "Rib Policy file reading successful");

  return res;
}

void RibBase::saveRibPolicyState() noexcept {
  /*
   * Persists the sub-policies RibBase owns; platform subclasses can
   * extend by overriding saveRibPolicyState.
   */
  if (!routeAttributePolicy_ && !routeFilterPolicy_) {
    XLOG(INFO, "rib policy empty, remove previously saved rib policy if any");
    removeExistingRibPolicyStore();
    return;
  }
  XLOG(INFO, "rib policy file save start");

  // Fill in header and footer
  TRibPolicyStore tRibPolicyStore;
  tRibPolicyStore.storedTime() =
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  tRibPolicyStore.fileTermination() = kRibPolicyFileTermination;

  // add policy thrift format
  if (routeAttributePolicy_) {
    tRibPolicyStore.policy()->route_attribute_policy() =
        routeAttributePolicy_->toThrift();
  }
  if (routeFilterPolicy_) {
    tRibPolicyStore.policy()->route_filter_policy() =
        routeFilterPolicy_->toThrift();
  }

  XLOGF(
      INFO,
      "Saving rib policy: {}",
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(
          *tRibPolicyStore.policy()));

  // save to the disk
  saveTRibPolicyStore(tRibPolicyStore);
}

void RibBase::removeExistingRibPolicyStore(void) noexcept {
  try {
    if (boost::filesystem::exists(FLAGS_rp_state_file)) {
      boost::filesystem::remove(FLAGS_rp_state_file);
      XLOG(INFO, "Previous Rib Policy file removed");
    }
  } catch (const std::exception& ex) {
    XLOGF(
        ERR,
        "Could not remove Rib Policy file. Exception: {}",
        folly::exceptionStr(ex));
    return;
  }
}

void RibBase::saveTRibPolicyStore(
    const TRibPolicyStore& tRibPolicyStore) noexcept {
  // serialize rib policy store
  std::string ribPolicyStoreStr;
  try {
    ribPolicyStoreStr = folly::toPrettyJson(
        folly::parseJson(
            apache::thrift::SimpleJSONSerializer::serialize<std::string>(
                tRibPolicyStore)));
  } catch (const std::exception& ex) {
    XLOGF(
        ERR,
        "Could not serialize RibPolicyStore. Exception: {}",
        folly::exceptionStr(ex));
    return;
  }

  try {
    auto startTime = std::chrono::steady_clock::now();
    // Replace the content of the file (overwrite)
    folly::writeFileAtomic(FLAGS_rp_state_file, ribPolicyStoreStr);
    RibStats::STATS_saveRibPolicyStateToFileTimeMs.addValue(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime)
            .count());
  } catch (const std::exception& ex) {
    XLOGF(
        ERR,
        "Could not write rib policy to file. Exception: {}",
        folly::exceptionStr(ex));
    return;
  }

  // Helps in observing time it took to store
  XLOGF(INFO, "rib policy file saved in location: {}", FLAGS_rp_state_file);
}

void RibBase::appendRibPolicyChangeHistory(
    const std::string& policyType,
    int64_t version) noexcept {
  try {
    std::string existing;
    folly::readFile(FLAGS_rp_change_history_file.c_str(), existing);

    // Split into lines
    std::vector<std::string> lines;
    std::vector<folly::StringPiece> pieces;
    folly::split('\n', existing, pieces);
    for (auto& piece : pieces) {
      if (!piece.empty()) {
        lines.push_back(piece.str());
      }
    }

    // Create new entry with timestamp (using thread-safe ctime_r)
    std::time_t currentTime = std::time(nullptr);
    char timeBuf[26];
    ctime_r(&currentTime, timeBuf);
    std::string timeStr(timeBuf);
    if (!timeStr.empty() && timeStr.back() == '\n') {
      timeStr.pop_back();
    }
    lines.push_back(fmt::format("{} {} {}", timeStr, policyType, version));

    // Keep only last 50 lines
    constexpr size_t kMaxLines = 50;
    if (lines.size() > kMaxLines) {
      lines.erase(lines.begin(), lines.begin() + (lines.size() - kMaxLines));
    }

    // Write back
    auto combined = folly::join('\n', lines);
    folly::writeFileAtomic(FLAGS_rp_change_history_file, combined);
  } catch (const std::exception& ex) {
    XLOGF(
        ERR,
        "Could not write RIB policy change history: {}",
        folly::exceptionStr(ex));
  }
}

NexthopInfo* FOLLY_NULLABLE
RibBase::getNexthopInfo(const std::shared_ptr<const BgpPath>& attrs) {
  // Only an announcement carries a nexthop to track.
  if (!enableNexthopTracking_ || !attrs) {
    return nullptr;
  }
  XCHECK(nexthopCache_)
      << "nexthopCache_ must not be null when nexthop tracking is enabled";

  const auto& nexthop = attrs->getNexthop();
  /*
   * Skip the unspecified nexthop (::/0.0.0.0). Locally-originated and
   * aggregate routes (via kV4LocalPeerInfo) carry this next-hop-self
   * placeholder: it is never a resolvable FSDB FIB entry, is never subscribed
   * for tracking, and never gates selection (local routes are always
   * isResolvedForSelection()). Registering it would leave a permanently
   * unreachable NexthopInfo that never resolves -- which would stall the
   * initial-path-computation nexthop-resolution gate
   * (areAllRegisteredNexthopsResolved). Conditional local routes carry a
   * real, non-zero nexthop and remain tracked.
   */
  if (nexthop.isZero()) {
    return nullptr;
  }
  auto it = nexthopInfoMap_.find(nexthop);
  if (it != nexthopInfoMap_.end()) {
    XLOGF(
        DBG2,
        "Found existing NexthopInfo for nexthop {} (vrf={}) (reachable: {}, igpCost: {})",
        nexthop.str(),
        fboss::kDefaultVrf,
        it->second.isReachable(),
        it->second.getIgpCost().has_value()
            ? std::to_string(it->second.getIgpCost().value())
            : "unset");
    return &it->second;
  }

  /*
   * Get nexthop status from nexthopCache
   * nexthopCache will always return a NexthopStatus, creating a new one with
   * default values if needed
   */
  auto nextHopStatus = nexthopCache_->registerAndGetNexthopStatus(
      NexthopKey{.ip = nexthop, .nextHopVrfId = fboss::kDefaultVrf});
  const bool isReachable = nextHopStatus.isReachable();
  const auto igpCost = nextHopStatus.getIgpCost();
  const std::optional<bool> isConnected = nextHopStatus.isConnected();

  XLOGF(
      DBG2,
      "Retrieved nexthop status from nexthopCache for {} (vrf={}): reachable={}, igpCost={}, isConnected={}",
      nexthop.str(),
      fboss::kDefaultVrf,
      isReachable,
      igpCost.has_value() ? std::to_string(igpCost.value()) : "unset",
      isConnected.has_value() ? std::to_string(isConnected.value()) : "unset");

  /*
   * Create new NexthopInfo with the status. Use the cached status directly
   * rather than reconstructing from individual fields, so the
   * excludeNexthopWithoutCost policy carried on the NexthopStatus is
   * preserved (the FBOSS source hardcodes it false).
   */
  auto [newIt, inserted] =
      nexthopInfoMap_.emplace(nexthop, NexthopInfo(nextHopStatus));
  if (inserted) {
    RibStats::incrNexthopInfoCount();
    // TODO_NH_VRF: Count unresolvable nexthops in the counters of their VRF.
    if (!nextHopStatus.isResolvedForSelection()) {
      defaultVrf_.ribCounters().incrementUnresolvableNexthopCount();
    }
  }

  XLOGF(
      INFO,
      "Created new NexthopInfo for nexthop {} (vrf={}) (reachable: {}, igpCost: {}, isConnected: {})",
      nexthop.str(),
      fboss::kDefaultVrf,
      isReachable,
      igpCost.has_value() ? std::to_string(igpCost.value()) : "unset",
      isConnected.has_value() ? std::to_string(isConnected.value()) : "unset");

  /*
   * First time this nexthop is seen in RIB-IN: queue it for FSDB tracking
   * (de-duped via requestedNexthops_). Flushed per RIB-IN batch. The
   * unspecified nexthop (::/0.0.0.0) never reaches here -- it is skipped
   * above and never registered/tracked.
   */
  if (nexthopSubscribeRequester_) {
    /*
     * Only when RIB-IN-driven tracking is enabled (DC). On EBB the requester
     * is unset, so the checks below are skipped entirely.
     */
    if (requestedNexthops_.insert(nexthop).second) {
      pendingNexthopSubscriptions_.push_back(nexthop);
    }
  }

  return &newIt->second;
}

bool RibBase::unlinkRouteInfoAndCleanupNexthop(RouteInfo& routeInfo) {
  const auto nexthop = routeInfo.attrs->getNexthop();
  if (!routeInfo.unlinkFromNexthopInfo()) {
    return false;
  }
  checkAndDeleteNexthopInfo(nexthop);
  return true;
}

bool RibBase::checkAndDeleteNexthopInfo(const folly::IPAddress& nexthop) {
  XCHECK(nexthopCache_)
      << "nexthopCache_ must not be null when nexthop tracking is enabled";

  // Find the NexthopInfo in the map
  auto it = nexthopInfoMap_.find(nexthop);
  if (it == nexthopInfoMap_.end()) {
    XLOGF(
        DBG2,
        "NexthopInfo for {} (vrf={}) not found in nexthopInfoMap_",
        nexthop.str(),
        fboss::kDefaultVrf);
    return false;
  }

  const auto& nexthopInfo = it->second;

  // If there are RouteInfo objects associated with this nexthop, don't delete
  if (!nexthopInfo.isRouteInfoListEmpty()) {
    XLOGF(
        DBG3,
        "Not deleting NexthopInfo for {} (vrf={}): still has associated RouteInfo objects",
        nexthop.str(),
        fboss::kDefaultVrf);
    return false;
  }
  /*
   * A RibEntry in fibBatchList_ can still name this nexthop, and the FIB reads
   * its NexthopInfo when the FIB loop builds the request. The NexthopInfo stays
   * registered and in nexthopInfoMap_, with no RouteInfo. The FIB loop deletes
   * it after it takes the batch.
   *
   * TODO_NH_VRF: Check every VRF's fibBatchList_ once nexthops are tracked per
   * VRF.
   */
  if (!defaultVrf_.fibBatchList().empty()) {
    return false;
  }
  deleteNexthopInfo(it);
  return true;
}

RibBase::NexthopInfoMap::iterator RibBase::deleteNexthopInfo(
    NexthopInfoMap::iterator it) {
  const auto& nexthop = it->first;
  /*
   * Unregister and try to remove from nexthopCache.
   *
   * NOTE: nexthopCache only removes nexthopInfo if
   * the nexthop is unreachable.
   */
  XLOGF(
      DBG2,
      "Deleting nexthop status for {} (vrf={}) from nexthopCache",
      nexthop.str(),
      fboss::kDefaultVrf);
  nexthopCache_->unregisterAndRemoveNexthopStatus(
      NexthopKey{.ip = nexthop, .nextHopVrfId = fboss::kDefaultVrf});

  // TODO_NH_VRF: Count unresolvable nexthops in the counters of their VRF.
  if (!it->second.isResolvedForSelection()) {
    defaultVrf_.ribCounters().decrementUnresolvableNexthopCount();
  }

  // Delete from nexthopInfoMap_
  XLOGF(
      DBG2,
      "Deleting NexthopInfo for {} (vrf={}) from nexthopInfoMap_",
      nexthop.str(),
      fboss::kDefaultVrf);
  RibStats::decrNexthopInfoCount();
  return nexthopInfoMap_.erase(it);
}

void RibBase::deleteUnusedNexthopInfos() {
  /*
   * TODO_NH_VRF: Check every VRF's fibBatchList_ once nexthops are tracked per
   * VRF.
   */
  if (!defaultVrf_.fibBatchList().empty()) {
    return;
  }
  for (auto it = nexthopInfoMap_.begin(); it != nexthopInfoMap_.end();) {
    if (it->second.isRouteInfoListEmpty()) {
      it = deleteNexthopInfo(it);
    } else {
      ++it;
    }
  }
}

namespace {
/**
 * Convert an internal NexthopInfo cache entry to its thrift representation.
 * Shared by the single-nexthop and list-all query paths.
 */
TNexthopInfo toTNexthopInfo(
    const folly::IPAddress& nexthop,
    const NexthopInfo& nexthopInfo) {
  TNexthopInfo nexthopInfoThrift;
  nexthopInfoThrift.next_hop() = createTIpPrefix(nexthop);
  nexthopInfoThrift.is_reachable() = nexthopInfo.isReachable();
  if (nexthopInfo.getIgpCost().has_value()) {
    nexthopInfoThrift.igp_cost() = nexthopInfo.getIgpCost().value();
  }
  if (nexthopInfo.isConnected().has_value()) {
    nexthopInfoThrift.is_connected() = nexthopInfo.isConnected().value();
  }
  nexthopInfoThrift.is_resolved_for_selection() =
      nexthopInfo.isResolvedForSelection();
  nexthopInfoThrift.route_count() = nexthopInfo.getRouteInfoListSize();

  /*
   * Report change times as an age (seconds ago), computed here on the RIB
   * thread that owns the timestamps. Reachability age starts when the source
   * first reports reachable; cost age starts when a cost is received.
   */
  const auto now = std::chrono::steady_clock::now();
  if (const auto ts = nexthopInfo.getLastReachabilityChangeTs()) {
    nexthopInfoThrift.last_reachability_change_age_s() =
        std::chrono::duration_cast<std::chrono::seconds>(now - *ts).count();
  }
  if (const auto ts = nexthopInfo.getLastIgpCostChangeTs()) {
    nexthopInfoThrift.last_igp_cost_change_age_s() =
        std::chrono::duration_cast<std::chrono::seconds>(now - *ts).count();
  }
  return nexthopInfoThrift;
}
} // namespace

std::optional<TNexthopInfo> RibBase::getNexthopInfoForNexthop(
    const folly::IPAddress& nexthop) {
  std::optional<TNexthopInfo> result;

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    auto it = nexthopInfoMap_.find(nexthop);
    if (it == nexthopInfoMap_.end()) {
      result = std::nullopt;
      return;
    }
    result = toTNexthopInfo(nexthop, it->second);
  });

  return result;
}

std::vector<TNexthopInfo> RibBase::getNexthopInfos(
    const std::vector<folly::IPAddress>& nexthops) {
  std::vector<TNexthopInfo> result;

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    if (nexthops.empty()) {
      // No filter: return every entry in the nexthop cache.
      result.reserve(nexthopInfoMap_.size());
      for (const auto& [nexthop, nexthopInfo] : nexthopInfoMap_) {
        result.push_back(toTNexthopInfo(nexthop, nexthopInfo));
      }
      return;
    }

    for (const auto& nexthop : nexthops) {
      auto it = nexthopInfoMap_.find(nexthop);
      if (it != nexthopInfoMap_.end()) {
        result.push_back(toTNexthopInfo(nexthop, it->second));
      }
    }
  });

  return result;
}

} // namespace bgp
} // namespace facebook
