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
    const RibBase& rib,
    const Fib::FibProgrammedPfxToNexthops& pfxNhs)
    : rib_(rib), pfxNhs_(pfxNhs) {}

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
    localRouteIter_ = rib_.localRoutes_.cbegin();
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
    while (localRouteIter_ != rib_.localRoutes_.cend()) {
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
    if (rib_.localRoutes_.find(nonLocalRouteIter_->first) ==
        rib_.localRoutes_.cend()) {
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
      enableRibAllocatedPathId_(globalConfig.enableRibAllocatedPathId) {
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
    localRoutes_.emplace(prefix, localRouteOpt.value());
    /*
     * store a set of local routes requiring conditional origination
     * to avoid full walk of localRoutes_ every time nexthop resolution changes
     */
    if (getRequireNhResolution(localRouteOpt->network)) {
      // if require_nexthop_resolution is set, nexthop must be non-empty
      assert(localRouteOpt->network.nexthop());
      auto nexthop = folly::IPAddress(*localRouteOpt->network.nexthop());
      conditionalLocalRoutes_[nexthop].push_back(prefix);
    }
  }
  ribCounters_.setOriginatedRoutes(localRoutes_.size());

  // initialize fibBatchTimer_ for batch processing of fib programming requests
  fibBatchTimer_ = folly::AsyncTimeout::make(
      evb_, [this]() noexcept { prepareFibProgramming(); });

  // bounds the deferred initial full-sync's wait for nexthop resolution
  nexthopResolutionTimer_ = folly::AsyncTimeout::make(
      evb_, [this]() noexcept { onNexthopResolutionTimeout(); });

  XLOGF(
      INFO,
      "Created Rib with fibBatchTime_= {}ms, computeUcmpFromLbwComm:{}",
      fibBatchTime_.count(),
      static_cast<bool>(globalConfig_.computeUcmpFromLbwComm));
}

RibBase::~RibBase() {
  /*
   * Cancel the nexthop-resolution timer so its callback (which captures `this`)
   * cannot fire after destruction. It is armed only while a deferred initial
   * full-sync is pending; without this, tearing down RibBase with that wait
   * outstanding could invoke onNexthopResolutionTimeout() on a destroyed
   * object (use-after-free).
   */
  if (nexthopResolutionTimer_) {
    nexthopResolutionTimer_->cancelTimeout();
  }
  // reset the batch list
  fibBatchList_.clear();
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
    fibBatchTimer_.reset();
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
  XLOGF(
      INFO, "[Exit] Clearing ribEntries_ ({} entries)...", ribEntries_.size());
  ribEntries_.clear();
  fibNexthopSets_.clear();
  ribCounters_.reset();
  XLOG(INFO, "[Exit] ribEntries_ cleared");

  /*
   * kNexthopInfoCount counter does not need to be reset here;
   * device bootup will initialize the ODS counter to 0
   */
  nexthopInfoMap_.clear();
  localRoutes_.clear();
}

/*
 * This is the fiber task method to trigger the actual FIB programming.
 *
 * This method will refer to the following:
 *
 *  - fibBatchList_: the shared state can be modified in:
 *    - fibProgrammingLoop();
 *    - prepareFibProgramming();
 *  - toFibMessageQ_: the notification queue to allow RIB to start the
 *                             programming cycle;
 *
 * There are several types of messages being processed within the queue:
 *  - TriggerFibProgMessage with fullSync=false: generated every fibBatchTime_;
 *  - TriggerFibProgMessage with fullSync=true: generated by either EOR or
 *    fullSync request from FIB;
 *  - Terminaition message: generated by RibBase::stop() to close fiber task;
 *
 * ATTN: Under normal cases, messages are coming in one by one and consumed very
 * quickly to NOT pile up. However, agent can run into bad state and the fib
 * programming fiber task can yield to others to make messages jammed up.
 *
 * Consider the following cases:
 *
 * Type I:
 *
 * --------------------------------------------------
 * FULL_SYNC | FULL_SYNC or NON-FULL_SYNC
 * --------------------------------------------------
 *
 * fibBatchList_ is a shared state which has already been updated. Will drain
 * the queue to mark a FULL_SYNC fib programming cycle;
 *
 * Type II:
 *
 * --------------------------------------------------
 * NON-FULL_SYNC | FULL_SYNC
 * --------------------------------------------------
 *
 * fibBatchList_ is a shared state which has already been updated. Will drain
 * the queue to mark a FULL_SYNC fib programming cycle;
 *
 * Type III:
 *
 * --------------------------------------------------
 * NON-FULL_SYNC | NON-FULL_SYNC
 * --------------------------------------------------
 *
 * fibBatchList_ is a shared state which has already been updated. Will drain
 * the queue to mark a NON-FULL_SYNC fib programming cycle;
 *
 * Type IV:
 *
 * --------------------------------------------------
 * FULL_SYNC or NON-FULL_SYNC | TERMINATION
 * --------------------------------------------------
 *
 * fibBatchList_ is updated but TERMINATION received. Ignore and close fiber;
 */
folly::coro::Task<void> RibBase::processFibProgrammingMsgLoop() noexcept {
  while (true) {
    // when cancelAndJoinAsync is called, guaranteed to exit
    co_await folly::coro::co_safe_point;

    bool isTerminated{false};
    bool fullSync{false};

    /*
     * ATTN: given the fact that fibBatchList_ is a shared state, there can be a
     * situation with: fibBatchList_ is the compressed result with multiple
     * pending-read messages inside toFibMessageQ_.
     *
     * Processing the shared fibBatchList_ along with multiple fullSyncs leads
     * to syncing 0 routes to the FIB until next full-sync is triggered. To dig
     * one step further, it is because batch_ in FIB will be reset after a full
     * programming cycle finishes.
     */
    do {
      auto msg = co_await co_awaitTry(toFibMessageQ_.pop());
      if (!msg.hasValue()) {
        isTerminated = true;
        break;
      }
      fullSync |= msg->fullSync;
    } while (!toFibMessageQ_.empty());

    // Guarantee TERMINATION signal will be detected even during queue draining
    if (isTerminated) {
      XLOG(
          INFO,
          "[Exit] Coro task cancelled. Terminating processFibProgrammingMsgLoop.");
      break;
    }

    if (!fib_->isConnected()) {
      XLOG(INFO, "Fib agent is not connected. Skipping fib batch programming.");
      continue;
    }

    if (!fib_->isFullSynced() && !fullSync) {
      XLOG(INFO, "Skipping add/delete route calls before syncFib.");
      continue;
    }

    // TODO: put the fibBatchList_ population into a separate function call
    auto toFibTotal = 0;
    auto iter = fibBatchList_.begin();
    auto endIter = fibBatchList_.end();

    {
      ScopedProfile profile("RibBase::fibProgramming_batch");
      while (iter != endIter) {
        auto& entry = *iter;
        const auto& prefix = entry.getPrefix();
        const auto& weightedNexthops = entry.getMultipathWeightedNexthops();
        const auto& bestPath = entry.getBestPath();
        const auto& nexthopTopoInfoMap = entry.getNexthopTopoInfoMap();
        auto routeType = BgpRouteType::UNKNOWN;
        bool isLocalRouteBest = false;
        if (bestPath) {
          const auto& bestPathNexthop = bestPath->attrs->getNexthop();
          isLocalRouteBest = (bestPathNexthop == kLocalRouteV4Nexthop) ||
              (bestPathNexthop == kLocalRouteV6Nexthop);

          routeType = getBgpPathType(bestPath.get());
        }
        const auto& installToFib = entry.getInstallToFib();
        ++iter;

        auto fibOutRoute = fib_->updateUnicastRouteWithBackup(
            prefix,
            (bestPath) ? bestPath->attrs : nullptr,
            weightedNexthops,
            isLocalRouteBest,
            installToFib,
            nexthopInfoMap_,
            std::nullopt, /* classId */
            nexthopTopoInfoMap,
            routeType,
            bestPath ? bestPath->attrs->getBackupAddr()
                     : std::optional<folly::IPAddress>{},
            globalConfig_.enableFibOutTracking);
        if (fibOutRoute) {
          replaceFibOut(
              entry,
              FibOutState{
                  .nexthops = fibNexthopSets_.getOrCreate(
                      std::move(fibOutRoute->nexthops)),
                  .metadata = fibOutRoute->metadata,
                  .topologyInfo = std::move(fibOutRoute->topologyInfo),
              });
        }
        /*
         * When enableFibOutTracking is false, the FIB update is still added to
         * the programming batch, but no retained payload is returned. Record
         * FibOut presence independently; explicit NONE/removal updates also
         * count.
         */
        entry.markFibOutStatePresent();
        toFibTotal++;

        // log per prefix
        XLOG_IF(
            DBG1,
            enableUnicastRouteLogging(prefix),
            logRouteWithNexthops(prefix, weightedNexthops));
      }
    } // ScopedProfile for fibProgramming_batch

    fibBatchList_.clear();
    deleteUnusedNexthopInfos();
    if (fullSync || toFibTotal) {
      XLOGF(DBG2, "Added {} prefixes to Fib batch", toFibTotal);
      co_await fib_->program(fullSync);
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
          processRibInAnnouncement(
              announcement.peer, announcement.attrs, announcement.pfxPathIds);
        },
        [this](const RibInWithdrawal& withdrawal) {
          processRibInWithdrawal(withdrawal.peer, withdrawal.pfxPathIds);
        },
        [this](const RibInInitialPathComputation& initialPathComputation) {
          processRibInInitialPathComputation(
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

  for (const auto& [prefix, localRoute] : localRoutes_) {
    /*
     * If min_supporting_route is non 0, or if require_nexthop_resolution is
     * set, we cannot simply originate the local route here. Instead, we need to
     * wait for the corresponding condition to be met and originate on-demand.
     */
    if (getMinSupportRoutes(localRoute.network) == 0 &&
        !getRequireNhResolution(localRoute.network)) {
      PrefixPathIds pfxPathIds{{prefix, kDefaultPathID}};
      co_await ribInQ_.push(
          RibInAnnouncement(kV4LocalPeerInfo, pfxPathIds, localRoute.attrs));
    }
  }

  XLOG(INFO, "Local route advertisement coro task finished");
  co_return;
}

void RibBase::processRibInAnnouncement(
    const TinyPeerInfo& peer,
    std::shared_ptr<const BgpPath> attrs,
    const PrefixPathIds& pfxPathIds) noexcept {
  XLOGF(
      DBG2,
      "Processing RibInAnnouncement from [{}] with {} prefixes",
      BgpPeerId{peer.addr, peer.routerId}.str(),
      pfxPathIds.size());

  for (const auto& pfxPathId : pfxPathIds) {
    /*
     * As per RFC-4724 we must send all our local routes along with
     * peer-received routes before we send our first EoR after graceful-restart.
     * Following code injects the min-supporting local summary prefixes before
     * other received prefixes so that FIB can be programmed and summary routes
     * can be advertised to peers before we send EoR. Process the aggregate
     * routes first
     */
    auto aggs = processSingleRibInUpdate(peer, attrs, pfxPathId);
    for (auto& [network, bgpAttrs] : aggs) {
      /*
       * aggs iterated here MUST be local originated routes, not received
       * routes. Hence they always use default path ID
       */
      PrefixPathId aggPfxPid{network, kDefaultPathID};
      processSingleRibInUpdate(kV4LocalPeerInfo, bgpAttrs, aggPfxPid);
    }
  }
  ribCounters_.publishTotalPathCounters();
  // Subscribe any nexthops newly learned while processing this batch.
  maybeFlushNexthopSubscriptions();
  if (ribEoRReceived_) {
    // Monitor prefix count to detect route churn
    incrementPrefixCountForRouteChurn(pfxPathIds.size());

    // Schedule incremental fib programming timer
    schedulePrepareFibProgrammingTimer();
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

void RibBase::replaceFibOut(RibEntry& entry, FibOutState state) {
  DCHECK(globalConfig_.enableFibOutTracking);
  CHECK(state.nexthops);
  entry.fibOutState_ = std::make_shared<const FibOutState>(std::move(state));
}

void RibBase::eraseFibOut(RibEntry& entry) {
  entry.fibOutState_.reset();
  entry.markFibOutStateAbsent();
}

const FibOutState* RibBase::findFibOut(const RibEntry& entry) {
  return entry.fibOutState_.get();
}

void RibBase::checkWithdrawalBeforeRouteProgrammed(
    folly::CIDRNetwork& prefix,
    RibEntry& entry) noexcept {
  /*
   * Skip the normal selection/FIB cycle only when the entry has no FibOut
   * state. A null best path does not prove that FIB has no state for the
   * prefix.
   */
  if (!entry.hasFibOutState() && entry.routeInfos_.empty()) {
    XLOGF(
        DBG1,
        "The case of new prefix {} announcement followed by withdrawal during Fib-programming timer.",
        folly::IPAddress::networkToString(prefix));
    ribCounters_.onPrefixRemoved(
        prefix.first.isV4(), prefix.second, entry.getInactivePathCnt());
    eraseFibOut(entry);
    ribEntries_.erase(prefix);
  }
}

void RibBase::processRibInWithdrawal(
    const TinyPeerInfo& peer,
    const PrefixPathIds& pfxPathIds) noexcept {
  XLOGF(
      DBG2,
      "Processing RibInWithdrawal from [{}] with {} prefixes",
      BgpPeerId{peer.addr, peer.routerId}.str(),
      pfxPathIds.size());

  for (const auto& pfxPathId : pfxPathIds) {
    /*
     * As per RFC-4724 we must send all our local routes along with
     * peer-received routes before we send our first EoR after graceful-restart.
     * Following code injects the min-supporting local summary prefixes before
     * other received prefixes so that FIB can be programmed and summary routes
     * can be advertised to peers before we send EoR. Process the aggregate
     * routes first
     */
    auto aggs = processSingleRibInUpdate(peer, nullptr, pfxPathId);

    for (auto& [network, bgpAttrs] : aggs) {
      /*
       * aggs iterated here MUST be local originated routes, not received
       * routes. Hence they always use default path ID
       */
      PrefixPathId aggPfxPid{network, kDefaultPathID};
      processSingleRibInUpdate(kV4LocalPeerInfo, bgpAttrs, aggPfxPid);
    }
  }
  ribCounters_.publishTotalPathCounters();
  // Aggregate/local routes injected above may introduce new nexthops.
  maybeFlushNexthopSubscriptions();
  if (ribEoRReceived_) {
    // Monitor prefix count to detect route churn
    incrementPrefixCountForRouteChurn(pfxPathIds.size());

    // Schedule incremental fib programming timer
    schedulePrepareFibProgrammingTimer();
  }
}

std::vector<std::pair<folly::CIDRNetwork, std::shared_ptr<const BgpPath>>>
RibBase::processSingleRibInUpdate(
    const TinyPeerInfo& peer,
    std::shared_ptr<const BgpPath> attrs,
    const PrefixPathId& pfxPid) noexcept {
  auto prefix = get<0>(pfxPid);
  auto receivedPathId = get<1>(pfxPid);

  /*
   * ** NOTE **
   * ribEntries_ are managed in two places asynchronously:
   *   1. here
   *   2. In handleFibProgrammedMessage(): erase prefix after withdrawal is
   *   programmed
   *
   * It's likely a ribEntry still exists with getAllPathsCnt() == 0.
   * This should be considered as if the prefix does not exist before.
   */

  // check whether prefix exists in ribEntries_
  auto kv = ribEntries_.find(prefix);
  if (kv == ribEntries_.cend()) {
    // withdraw route of a prefix that does not exist, nothing to do
    if (!attrs) {
      return {};
    }
    // add prefix to ribEntries_
    kv = ribEntries_.emplace(std::make_pair(prefix, RibEntry(prefix))).first;
    ribCounters_.onPrefixAdded(prefix.first.isV4(), prefix.second);
  }
  auto& entry = kv->second;
  /*
   * oldAllPathCnt is used in aggregate route logic to indentify
   * prefix announcement/withdrawal (to distinguish with ecmp change)
   */
  auto oldAllPathCnt = entry.getAllPathsCnt();

  if (!attrs && oldAllPathCnt == 0) {
    /*
     * A previous withdrawn has been processed, but has not been programmed yet.
     * this is equal to withdraw route of a prefix that does not exist.
     * nothing more to do here.
     */
    return {};
  }

  bool isAggRoute{false}, installToFib{(!peer.isRedistributePeer)};
  if (peer == kV4LocalPeerInfo) {
    const auto& lr = localRoutes_.find(prefix);
    if (lr != localRoutes_.cend()) {
      installToFib = getInstallToFib(lr->second.network);
      isAggRoute = (getMinSupportRoutes(lr->second.network) > 0);
    }
  }

  NexthopInfo* nexthopInfo = nullptr;
  // update new path to the entry.
  if (enableNexthopTracking_ && attrs) {
    // Find the nexthop and associated NexthopInfo
    XCHECK(nexthopCache_) << "nexthopCache_ is null, this is not expected";
    nexthopInfo = getNexthopInfo(*attrs);
  }

  entry.updatePath(
      peer,
      attrs,
      installToFib,
      receivedPathId,
      nexthopInfo ? std::make_optional(nexthopInfo) : std::nullopt,
      [this](RouteInfo& routeInfo) {
        return unlinkRouteInfoAndCleanupNexthop(routeInfo);
      });

  /*
   * Get new path counts now. entry will be gone out of scope if
   * removed under (!attr) condition. Be careful, do not access
   * entry reference after this call
   */
  auto newAllPathCnt = entry.getAllPathsCnt();

  /*
   * Single maintenance point for the per-AFI total path count: the change in
   * this prefix's path count is add-path-correct here, and aggregate routes
   * also funnel through this method.
   */
  ribCounters_.onPathsDelta(
      prefix.first.isV4(),
      static_cast<int64_t>(newAllPathCnt) -
          static_cast<int64_t>(oldAllPathCnt));

  if (!attrs) {
    checkWithdrawalBeforeRouteProgrammed(prefix, entry);
  }

  XLOGF(
      DBG3,
      "Finish updating RibEntry of prefix {} from peer [{}]",
      folly::IPAddress::networkToString(prefix),
      BgpPeerId{peer.addr, peer.routerId}.str());

  /*
   * If route is configured with minimum_supporting_routes > 0, do not
   * aggregate For all other cases run aggregate logic:
   * 1. local route with minimum_supporting_routes = 0
   * 2. local injected routes (peer == localPeer, but not in localRoutes_)
   * 3. peer learnt routes
   */

  // skip aggreagate route
  if (isAggRoute) {
    return {};
  }

  /*
   * determine if this is supporting route announcement/withdrawn
   * TODO: Is sameAsLocalRoute logic expected behavior in RFC?
   */
  bool sameAsLocalRoute = localRoutes_.find(prefix) != localRoutes_.cend();
  /*
   * supporting route withdrawn:
   * 1. prefix != agg (prefix withdrawn):
   *      oldAllPathCnt == 1 and newAllPathCnt == 0
   * 2. prefix == agg (path withdrawn):
   *      getAllPathsCnt() = oldPathCnt - 1
   *    e.g. agg = <1.0.0.0/24, nh=0.0.0.0, supportPfxCnt=1>
   *         supporting prefix = <1.0.0.0/24, nh=peer1>
   *    If peer1 withdrawn supporting prefix => agg.supportPfxCnt--
   */
  bool supportingRouteWithdrawn =
      (!sameAsLocalRoute && oldAllPathCnt == 1 && newAllPathCnt == 0) ||
      (sameAsLocalRoute && newAllPathCnt == oldAllPathCnt - 1);
  /*
   * supporting route announcement:
   * 1. prefix != agg (prefix announcement):
   *      oldAllPathCnt == 0 and newAllPathCnt == 1
   * 2. prefix == agg (path announcement):
   *      getAllPathsCnt() = oldPathCnt + 1
   *    e.g. agg = <1.0.0.0/24, nh=0.0.0.0, supportPfxCnt=0>
   *         supporting prefix = <1.0.0.0/24, nh=peer1>
   *    If peer1 announce supporting prefix, agg.supportPfxCnt++
   */
  bool supportingRouteAnnouncement =
      (!sameAsLocalRoute && oldAllPathCnt == 0 && newAllPathCnt == 1) ||
      (sameAsLocalRoute && newAllPathCnt == oldAllPathCnt + 1);

  if (supportingRouteWithdrawn || supportingRouteAnnouncement) {
    return aggregateRoute(prefix, supportingRouteAnnouncement);
  }

  return {};
}

void RibBase::processRibInInitialPathComputation(
    std::chrono::milliseconds nexthopResolutionTimeout) noexcept {
  XLOG(INFO, "Processing RibInInitialPathComputation");

  // avoid duplicate initial full-sync to program FIB
  if (ribEoRReceived_) {
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
    pendingInitialPathComputation_ = true;
    nexthopResolutionTimer_->scheduleTimeout(nexthopResolutionTimeout);
    XLOGF(
        INFO,
        "[Initialization] Deferring initial path computation up to {}ms: "
        "nexthop resolution / conditional routes not ready yet.",
        nexthopResolutionTimeout.count());
    return;
  }

  runInitialPathComputation();
}

void RibBase::runInitialPathComputation() noexcept {
  pendingInitialPathComputation_ = false;
  // stop the resolution timer so none is live when the full-sync runs
  nexthopResolutionTimer_->cancelTimeout();

  // fib batch process list must be empty right now
  CHECK(fibBatchList_.empty());

  /*
   * set one-time flag before prepareFibProgramming so that subclass
   * hooks (called during prepareFibProgramming) can check
   * ribEoRReceived_ as a precondition
   */
  ribEoRReceived_ = true;

  // best path selection + notify programming FIB
  prepareFibProgramming(true /* fullSync */);

  // log BGP++ initialization event
  BgpStats::logInitializationEvent("Rib", BgpInitializationEvent::RIB_COMPUTED);

  /*
   * NOTE: route-churn detection coro task will only be scheduled AFTER the
   * RIB_COMPUTED stage to make sure FIB_SYNC is not accidentally performed to
   * break BGP++ initialization sequence. Bestpath selection happens within
   * prepareFibProgramming() and can't be paused/stopped once called.
   */

  // Schedule periodic task to monitor route churn
  asyncScope_.add(co_withExecutor(&evb_, monitorRouteChurn()));
}

void RibBase::maybeRunPendingInitialPathComputation() noexcept {
  if (!pendingInitialPathComputation_) {
    /* Already past the initial path computation stage; nothing pending. */
    return;
  }
  if (!isInitialPathComputationReady()) {
    /* Not yet ready (nexthop resolution / conditional routes); skip for now. */
    return;
  }
  XLOG(
      INFO,
      "[Initialization] Nexthop resolution / conditional routes ready; running "
      "deferred initial path computation.");
  runInitialPathComputation();
}

void RibBase::onNexthopResolutionTimeout() noexcept {
  if (!pendingInitialPathComputation_) {
    return;
  }
  XLOG(
      ERR,
      "[Initialization] Timed out waiting for nexthop resolution; running "
      "initial path computation with unresolved nexthops.");
  runInitialPathComputation();
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
    if (fibBatchTimer_->isScheduled()) {
      fibBatchTimer_->cancelTimeout();
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

  for (auto& [_, ribEntry] : ribEntries_) {
    ribEntry.requirePathSelection();
  }

  if (ribEoRReceived_) {
    // Capture and reset the pending FibSyncReq flag
    bool fullSync = fibSyncReqPending_;
    fibSyncReqPending_ = false;

    XLOGF(
        DBG2,
        "ResumeBestPathAndFibProgramming is triggering best-path computation and FIB programming with fullSync={}",
        fullSync);

    // Best path selection + notify programming FIB (fullSync if pending)
    prepareFibProgramming(fullSync);

    schedulePrepareFibProgrammingTimer();
  }
}

void RibBase::processRibInNexthopUpdate(
    const RibInNexthopUpdate& nexthopUpdate) noexcept {
  if (!enableNexthopTracking_ || !nexthopCache_) {
    XLOG(
        WARNING,
        "Nexthop tracking is not enabled or nexthopCache is null, ignoring nexthop update");
    return;
  }
  bool needPathSelection = false;
  // Changed nexthop(s) recorded for the trigger log at the end of this method.
  std::vector<std::string> triggeringNexthops;

  // Process each NexthopStatus in the update
  for (const auto& queuedStatus : nexthopUpdate.nexthopStatuses) {
    const auto& nexthopIp = queuedStatus.getNexthop();
    XLOGF(DBG2, "Processing nexthop update for {}", nexthopIp.str());

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
            "Skipping stale nexthop update for {}: epoch {} is older than {}",
            nexthopIp.str(),
            queuedStatus.getEpoch(),
            it->second.getEpoch());
        RibStats::incrNhtRibStaleUpdateSkipped();
        continue;
      }
      /*
       * The update names a nexthop whose status changed. The NexthopInfo takes
       * the current status from NexthopCache, not the status in the update.
       */
      const auto cachedStatus = nexthopCache_->getNexthopStatus(nexthopIp);
      if (!cachedStatus.has_value()) {
        XLOGF(
            ERR,
            "Ignoring nexthop update for {}: RIB has a NexthopInfo but NexthopCache has no entry. The RIB keeps the old status.",
            nexthopIp.str());
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

      if (isResolvabilityChanged) {
        if (isResolved) {
          ribCounters_.decrementUnresolvableNexthopCount();
        } else {
          ribCounters_.incrementUnresolvableNexthopCount();
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
            "Updating nexthop {} status: reachable={} -> {}, igpCost={} -> {}, resolvedForSelection={} -> {}",
            nexthopIp.str(),
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
      if (!needsPathSelection || !ribEoRReceived_) {
        continue;
      }

      triggeringNexthops.push_back(nexthopIp.str());

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
          "Ignoring stale nexthop update for untracked nexthop {}",
          nexthopIp.str());
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
    schedulePrepareFibProgrammingTimer();
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

std::vector<std::pair<folly::CIDRNetwork, std::shared_ptr<const BgpPath>>>
RibBase::aggregateRoute(
    const folly::CIDRNetwork& prefix,
    bool supportingRouteAnnouncement) noexcept {
  std::vector<std::pair<folly::CIDRNetwork, std::shared_ptr<const BgpPath>>>
      aggRoutes;
  // local route aggregation
  for (auto& [localPrefix, localRoute] : localRoutes_) {
    if (!isSubnet(prefix, localPrefix)) {
      continue;
    }

    /*
     * no need to aggregate local route that do not need
     * minimum_supporting_routes assumption here is local routes does not
     * overlap with each other in configurations
     */
    auto minSupportRoutes = getMinSupportRoutes(localRoute.network);
    if (minSupportRoutes == 0) {
      continue;
    }

    // withdrawal of non-existing supporting routes, simply ignore
    if (!supportingRouteAnnouncement && localRoute.supportPfxCnt == 0) {
      continue;
    }

    // supporting prefix withdrawal
    if (!supportingRouteAnnouncement) {
      // decrease aggregate supporting route count
      --localRoute.supportPfxCnt;
      XLOGF(
          DBG1,
          "Supporting route withdrawal for {}: {}, counter decreased to {}.",
          folly::IPAddress::networkToString(localPrefix),
          folly::IPAddress::networkToString(prefix),
          localRoute.supportPfxCnt);

      /*
       * minimum_supporting_routes drop down to threshold,
       * withdrawn aggregated RouteInfo
       */
      if (localRoute.supportPfxCnt == minSupportRoutes - 1) {
        aggRoutes.emplace_back(localPrefix, nullptr);
      }
    } else { // supporting prefix announcement
      ++localRoute.supportPfxCnt;
      XLOGF(
          DBG1,
          "Supporting route announcement for {}: {}, counter increased to {}.",
          folly::IPAddress::networkToString(localPrefix),
          folly::IPAddress::networkToString(prefix),
          localRoute.supportPfxCnt);
      /*
       * prefix announcement causes count reaches minimum_supporting_routes,
       * advertise aggregated route
       */
      if (localRoute.supportPfxCnt == minSupportRoutes) {
        aggRoutes.emplace_back(localPrefix, localRoute.attrs);
      }
    }
  }
  return aggRoutes;
}

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
 * toFibMessageQ_ in order to trigger the actual procedure to program Fib.
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
    const folly::CIDRNetwork& prefix,
    std::optional<BgpRouteType> oldSource,
    const RouteInfo* newBestpath) noexcept {
  ribCounters_.onBestpathSourceChanged(
      prefix.first.isV4(), oldSource, bestpathSource(newBestpath));
}

void RibBase::recordInactivePathDelta(
    const folly::CIDRNetwork& prefix,
    uint32_t oldCount,
    const RibEntry& entry) noexcept {
  ribCounters_.onInactivePathsDelta(
      prefix.first.isV4(),
      static_cast<int64_t>(entry.getInactivePathCnt()) -
          static_cast<int64_t>(oldCount));
}

std::pair<bool, bool> RibBase::runBestPathSelection(RibEntry& entry) noexcept {
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
      entry.getPrefix(), oldSource, entry.getBestPathRaw());
  recordInactivePathDelta(entry.getPrefix(), oldInactivePathCount, entry);
  return result;
}

void RibBase::prepareFibProgramming(bool fullSync) noexcept {
  ScopedProfile profile("RibBase::prepareFibProgramming");
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
  if (fullSync) {
    fibBatchList_.clear();
    XLOGF(
        DBG4,
        "Start full sync best path selection for {} ribEntries",
        ribEntries_.size());
  }
  /*
   * record rib entries that "needPathSelection", which need to go through
   * route attribute policy again
   */
  folly::F14FastSet<folly::CIDRNetwork> prefixesToOverwriteRouteAttributes;
  // record start time before we perform path selection
  auto ribPolicyProcessingStartTime = std::chrono::steady_clock::now();
  for (auto& [prefix, ribEntry] : ribEntries_) {
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
    auto [bestpathChanged, nexthopChanged] = runBestPathSelection(ribEntry);
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
      fibBatchList_.push_back(ribEntry);
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
      if (!ribEntry.isOnFibBatchList()) {
        fibBatchList_.push_back(ribEntry);
      }
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
              std::move(withdrawal));
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
        uint64_t currentRibVersion = incrementRibVersion();
        handleFullAddPathWithdrawal(
            ribEntry, withdrawalAddPath, currentRibVersion);
        withdrawal.entries.emplace_back(
            prefix, kDefaultPathID, std::nullopt, currentRibVersion);
        ribEntry.setRibVersion(currentRibVersion);
        ribEntry.commitBestpath();
        // wait till fib programming to commit multipathNexthops
      }

      // If route entry is not on the batch process list yet, add it.
      if (!ribEntry.isOnFibBatchList()) {
        fibBatchList_.push_back(ribEntry);
      }
    }

    /*
     * Path-selection policy or nexthop filtering can leave candidates in a
     * RibEntry while selecting neither a best path nor FIB nexthops. When the
     * final candidate is withdrawn, those selected outputs remain empty, so
     * bestpathChanged/nexthopChanged does not enqueue the now-pathless entry.
     * If it has prior FibOut state (including explicit NONE/removal), enqueue
     * an idempotent removal so FIB-ack processing can erase it.
     */
    if (ribEntry.getAllPathsCnt() == 0 && ribEntry.hasFibOutState() &&
        !ribEntry.isOnFibBatchList()) {
      fibBatchList_.push_back(ribEntry);
    }
  }
  if (prefixesToOverwriteRouteAttributes.size() == ribEntries_.size()) {
    auto pathSelectionTimeMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - ribPolicyProcessingStartTime);
    XLOGF(
        INFO,
        "Path Selection in a FullRibWalk for {} ribEntries took {} ms",
        ribEntries_.size(),
        pathSelectionTimeMs.count());
    RibStats::STATS_ribFullSyncPathSelectionTimeMs.addValue(
        pathSelectionTimeMs.count());
  }

  withdrawal.addPathEntries = std::move(withdrawalAddPath.addPathEntries);
  // If there are withdrawals left, send them to peers
  if (withdrawal.entries.size() || withdrawal.addPathEntries.size()) {
    XLOGF(DBG1, "{}", formatRibOutWithdrawalLog(withdrawal));
    ribOutQPushAndMayPauseBestPathAndFibProgramming(std::move(withdrawal));
  }

  // ods logging whether at least 1 rib entry actively uses ucmp
  BgpStats::setUcmpActive(isUcmpActive);

  /* Virtual hook: subclasses override to apply CTE route attribute overwrites.
     Timing and full-sync stats are handled inside the override. */
  overwriteRouteAttributes(prefixesToOverwriteRouteAttributes);

  /*
   * Generic post-path-selection hook, invoked once per pass after path
   * selection and route-attribute overwrite, before the fibBatchList_
   * early-exit. No-op default on RibBase; subclasses override to run
   * platform-specific end-of-pass work. RibBase neither knows nor performs
   * any such work itself.
   */
  onPrepareFibProgrammingComplete(fullSync);
  ribCounters_.publishInactivePathCounters();

  // If no entry in fib update list and not full sync, skip fib programming
  if (fibBatchList_.empty() && !fullSync) {
    return;
  }

  RibStats::addFibBatchListSize(fibBatchList_.size());
  toFibMessageQ_.push(TriggerFibProgMessage(fullSync));
}

void RibBase::flushAnnouncementChunk(
    RibOutAnnouncement& announcement,
    bool reserveAddPath,
    bool sendWithEoR) {
  if (sendWithEoR) {
    announcement.initialDump = true;
  }

  XLOGF(DBG1, "{}", formatRibOutAnnouncementLog(announcement));

  ribOutQPushAndMayPauseBestPathAndFibProgramming(std::move(announcement));
  announcement = RibOutAnnouncement();
  (reserveAddPath ? announcement.addPathEntries : announcement.entries)
      .reserve(kRibChunkSize);
}

void RibBase::flushWithdrawalChunk(
    RibOutWithdrawal& withdrawal,
    bool reserveAddPath,
    bool logAsAddPath) {
  XLOGF(DBG1, "{}", formatRibOutWithdrawalLog(withdrawal, logAsAddPath));

  ribOutQPushAndMayPauseBestPathAndFibProgramming(std::move(withdrawal));
  withdrawal = RibOutWithdrawal();
  (reserveAddPath ? withdrawal.addPathEntries : withdrawal.entries)
      .reserve(kRibChunkSize);
}

void RibBase::handleFullAddPathWithdrawal(
    const RibEntry& ribEntry,
    RibOutWithdrawal& withdrawalAddPath,
    uint64_t& currentRibVersion) {
  if (ribEntry.getAdvertisedMultipathWeightedNexthops()) {
    for (const auto& advWeightedNhIter :
         *ribEntry.getAdvertisedMultipathWeightedNexthops()) {
      if (withdrawalAddPath.addPathEntries.size() == kRibChunkSize) {
        flushWithdrawalChunk(
            withdrawalAddPath, /*reserveAddPath=*/true, /*logAsAddPath=*/true);

        /*
         * rule 2: this prefix's remaining add-path withdrawals land in a new
         * chunk, so bump the version (see prepareFibProgramming for the
         * reasoning).
         */
        currentRibVersion = incrementRibVersion();
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

/**
 * Schedule the prepareFibProgramming timer.
 *
 * This function is used to schedule the prepareFibProgramming function to be
 * called after a certain amount of time. The time is usually 200ms by
 * default. If the fib churn is high, a longer timeout of
 * kFibBatchTimeoutChurn is used instead.
 *
 * @return None
 */
void RibBase::schedulePrepareFibProgrammingTimer() noexcept {
  if (pauseBestPathAndFibProgramming_ || fibBatchTimer_->isScheduled()) {
    /*
     * Best path selection and Fib programming is paused or timer has been
     * scheduled already
     */
    return;
  }

  // Schedule fib programming in a batch fashion
  fibBatchTimer_->scheduleTimeout(fibBatchTime_);
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
  // Initial dump to all established peers
  auto sendWithEoR = msg.isSync && !initialEorSent_;
  if (sendWithEoR) {
    /*
     * Mark one-time flag
     * Because of this flag, sendWithEoR can only be true once at
     * initialization.
     */
    initialEorSent_ = true;

    /**
     * Send initial EOR notification to PeerMgr.
     * The RibInitialAnnouncementStart message is a signal to PeerMgr that the
     * initial dump will start and we will enqueue announcements.
     * This msg MUST be enqueued BEFORE
     * the first RibAnnouncement only if sendWithEoR is true.
     */
    ribOutQ_.push(RibInitialAnnouncementStart{});

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
  RibOutWithdrawal withdrawal;
  withdrawal.addPathEntries.reserve(kRibChunkSize);

  for (const auto& [_, path] : msg.fibProgrammedPfxs) {
    // Send updates to all established peers
    auto iter = RibBase::FibProgrammedPrefixIterator(*this, path);

    for (auto elem = iter.cbegin(); elem != iter.cend(); elem = iter.next()) {
      const auto& prefix = elem->first;
      const auto& weightedNexthops = elem->second;

      auto kv = ribEntries_.find(prefix);
      if (kv == ribEntries_.cend()) {
        XLOGF(
            ERR,
            "Cannot find RibEntry for {}",
            folly::IPAddress::networkToString(prefix));
        continue;
      }
      auto& entry = kv->second;

      /*
       * For any withdraw routes, we must have already notified all peers.
       * Erase ribEntry if no update in the interim.
       */
      if (weightedNexthops == nullptr) {
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
            ribCounters_.onPrefixRemoved(
                prefix.first.isV4(), prefix.second, entry.getInactivePathCnt());
            eraseFibOut(entry);
            ribEntries_.erase(prefix);
          } else {
            XLOGF(
                DBG1,
                "All paths withdrawn for {}, when on the FibBatchList. Not deleting RibEntry.",
                folly::IPAddress::networkToString(prefix));
          }
        }
        continue;
      }

      /*
       * If there has been some change to any of the ECMP paths, delay sending
       * the update
       */
      if (entry.getMultipathWeightedNexthops() != weightedNexthops) {
        /*
         * nexthops have been changed. Do not advertise it to BGP peers now.
         * The new nexthops will trigger another Fib programming and then come
         * back here with the new nexthops. We will advertise the prefix to
         * BGP peers then.
         */
        XLOGF(
            DBG3,
            "Done updating {} to Fib. But nexthops will change in next fib "
            "programming. Skip notifying BGP peers",
            folly::IPAddress::networkToString(prefix));
        continue;
      }

      /*
       * Stamp the RIB version at emission time. Each prefix starts at a fresh
       * version (rule 3: different prefixes within a message strictly
       * increase). The local currentRibVersion is re-bumped at each chunk
       * (kRibChunkSize) flush below, so a prefix whose add-path entries span
       * more than one chunk strictly increases across those chunks (rule 2),
       * while all entries emitted within a single chunk share one version
       * (rule 1). A prefix appears exactly once in the FIB-ack (one
       * updateUnicastRoute per prefix, best-path-attr key), so this per-prefix
       * initialization runs once and needs no deduplication. Placed after the
       * two continue-guards above so a prefix that is skipped (already
       * withdrawn from FIB, or whose nexthops will change next round) does not
       * consume a version.
       *
       * The final (highest) version is persisted onto the entry after add-path
       * processing (see the entry.setRibVersion below); the emit sites read
       * currentRibVersion directly.
       */
      uint64_t currentRibVersion = incrementRibVersion();

      const auto oldAdvMultipathNHs =
          entry.getAdvertisedMultipathWeightedNexthops();
      entry.commitMultipathNexthops();
      const auto newAdvMultipathNHs =
          entry.getAdvertisedMultipathWeightedNexthops();

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
      if (entry.commitMultipaths() && entry.bestpath_) {
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
                announcementAddPath, /*reserveAddPath=*/true, sendWithEoR);

            /*
             * rule 2: this prefix's remaining add-path entries land in a new
             * chunk (a separate RibOut message), so bump the version. A peer
             * that detached after the flushed chunk must observe a strictly
             * higher version on the entries that follow, otherwise
             * isEntryShared would wrongly treat them as already-seen and the
             * peer would miss them.
             */
            currentRibVersion = incrementRibVersion();
          }

          auto nextHop = advMultPath->attrs->getNexthop();
          std::optional<uint32_t> nhWeight;
          // Check if nhWeightMap is a nullptr or not
          if (newAdvMultipathNHs && newAdvMultipathNHs->contains(nextHop)) {
            nhWeight = newAdvMultipathNHs->at(nextHop);
          }

          XCHECK(advMultPath->pathIdToSend.has_value());
          announcementAddPath.addPathEntries.emplace_back(
              prefix,
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
              if (withdrawal.addPathEntries.size() == kRibChunkSize) {
                flushWithdrawalChunk(
                    withdrawal,
                    /*reserveAddPath=*/true,
                    /*logAsAddPath=*/false);

                /*
                 * rule 2: this prefix's remaining add-path withdrawals land in
                 * a new chunk, so bump the version (see the add-path
                 * announcement flush above for the reasoning).
                 */
                currentRibVersion = incrementRibVersion();
              }
              withdrawal.addPathEntries.emplace_back(
                  prefix,
                  kPlaceholderPathID,
                  oldNhIter.first,
                  currentRibVersion);
            }
          }
        }
      }

      /*
       * Persist the final (highest) version reached for this prefix, so
       * getRibVersion() and any later reader reflect the last chunk emitted --
       * including when the best path itself did not change and we
       * early-continue below without emitting a best-path entry.
       */
      entry.setRibVersion(currentRibVersion);

      if (!entry.commitBestpath()) {
        if (!shouldReadvertiseBestpathOnMultipathSizeChange(entry) ||
            prevAdvertisedMultipathSize ==
                entry.getAdvertisedMultipaths().size()) {
          // bestpath did not change, no need for new advertisement
          continue;
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
            announcement, /*reserveAddPath=*/false, sendWithEoR);
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
  }

  // if there are further withdraw for add path entries, we send them out.
  if (withdrawal.addPathEntries.size()) {
    XLOGF(DBG1, "{}", formatRibOutWithdrawalLog(withdrawal));
    ribOutQPushAndMayPauseBestPathAndFibProgramming(std::move(withdrawal));
  }

  announcement.addPathEntries = std::move(announcementAddPath.addPathEntries);
  if (sendWithEoR || announcement.entries.size() ||
      announcement.addPathEntries.size()) {
    if (sendWithEoR) {
      announcement.initialDump = true;
      announcement.sendWithEoR = true;
    }
    XLOGF(DBG1, "{}", formatRibOutAnnouncementLog(announcement));

    ribOutQPushAndMayPauseBestPathAndFibProgramming(std::move(announcement));
  }

  // publish unicast routes size.
  FibStats::publishTotalUCastRoutes(ribEntries_.size());

  // Check if FIB is flushed (only local routes remain)
  if (ribEntries_.size() <= localRoutes_.size()) {
    FibStats::incrFibFlushed();
  }
}

TFibNexthopDatabase RibBase::getFibNexthopDatabase() {
  TFibNexthopDatabase database;
  database.enabled() = globalConfig_.enableFibOutTracking;
  if (!globalConfig_.enableFibOutTracking) {
    return database;
  }

  std::exception_ptr traversalException;
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() noexcept {
    try {
      database.nexthop_sets()->reserve(fibNexthopSets_.size());
      fibNexthopSets_.forEach(
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
  /*
   * skip processing full-sync request sending from FIB
   * since initial RIB full-sync has NOT be triggered yet
   */
  if (!ribEoRReceived_) {
    return;
  }
  if (pauseBestPathAndFibProgramming_) {
    fibSyncReqPending_ = true;
    XLOG(
        DBG2,
        "Setting FibSyncReqPending_ flag to trigger fullSync when resumed.");
    return;
  }
  prepareFibProgramming(true /* fullSync */);
}

std::vector<TRibEntry> RibBase::getRibEntries(TBgpAfi afi) {
  std::vector<TRibEntry> tRibEntries;

  if (afi != TBgpAfi::AFI_IPV4 && afi != TBgpAfi::AFI_IPV6) {
    return tRibEntries;
  }

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    auto expectIPv4 = afi == TBgpAfi::AFI_IPV4;
    for (const auto& entry : ribEntries_) {
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

TFibOutTable RibBase::getFibOutPrefix(const std::string& prefixString) {
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
    const auto ribEntry = ribEntries_.find(prefix);
    if (ribEntry == ribEntries_.end()) {
      return;
    }
    TFibOutEntry entry;
    entry.prefix() = buildTPrefix(ribEntry->first);
    if (const auto* fibOut = findFibOut(ribEntry->second)) {
      auto thriftFibOut = toThriftFibOutRoute(*fibOut);
      thriftFibOut.nexthop_set_ref_count() =
          static_cast<int64_t>(fibOut->nexthops.use_count());
      entry.fib_out() = std::move(thriftFibOut);
    }
    table.entries()->push_back(std::move(entry));
  });
  return table;
}

TRibSummary RibBase::getRibSummary(TBgpAfi afi) {
  TRibSummary summary;
  summary.afi() = afi;

  if (afi != TBgpAfi::AFI_IPV4 && afi != TBgpAfi::AFI_IPV6) {
    return summary;
  }

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    const bool isV4 = afi == TBgpAfi::AFI_IPV4;
    summary.total_prefixes() = ribCounters_.totalPrefixes(isV4);
    summary.total_paths() = ribCounters_.totalPaths(isV4);
    summary.inactive_paths() = ribCounters_.inactivePaths(isV4);
    const auto& counts = ribCounters_.prefixLenCounts(isV4);
    for (size_t len = 0; len < counts.size(); ++len) {
      if (counts[len] != 0) {
        summary.prefix_length_counts()[static_cast<int16_t>(len)] = counts[len];
      }
    }
    summary.ebgp_prefixes() = ribCounters_.ebgpPrefixes(isV4);
    summary.ibgp_prefixes() = ribCounters_.ibgpPrefixes(isV4);
    summary.confed_ebgp_prefixes() = ribCounters_.confedEbgpPrefixes(isV4);
    summary.local_prefixes() = ribCounters_.localPrefixes(isV4);
    // RIB-wide (not per-AFI); identical in both responses.
    summary.unresolvable_nexthops_count() = ribCounters_.unresolvableNexthops();
    summary.routes_with_unresolved_nexthops() =
        ribCounters_.routesWithUnresolvedNexthops(isV4);
  });
  return summary;
}

uint64_t RibBase::getNumPrefixes() {
  uint64_t count = 0;
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait(
      [&]() { count = ribCounters_.totalPrefixes(); });
  return count;
}
std::vector<TRibEntry> RibBase::getRibEntriesForCommunities(
    TBgpAfi afi,
    const std::vector<nettools::bgplib::BgpAttrCommunityC>& communities) {
  std::vector<TRibEntry> tRibEntries;

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    for (const auto& entry : ribEntries_) {
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
    std::unique_ptr<std::string> prefix) {
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
    auto entry = ribEntries_.find(network);
    if (entry != ribEntries_.end()) {
      tRibEntries.emplace_back(createTRibEntry(*entry));
    }
  });

  return tRibEntries;
}

std::vector<TRibEntry> RibBase::getRibEntriesForSubprefixes(
    std::unique_ptr<std::string> prefix) {
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
    for (const auto& entry : ribEntries_) {
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
                  kV4LocalPeerInfo, pfxPathIds, localRoute.attrs));
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
              co_await ribInQ_.push(
                  RibInWithdrawal(kV4LocalPeerInfo, pfxPathIds));
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
std::vector<TOriginatedRoute> RibBase::getOriginatedRoutes() {
  std::vector<TOriginatedRoute> tRoutes;

  /* runImmediatelyOrRunInEventBaseThreadAndWait: runs inline if already
   * on Rib's evb (e.g., called via folly::via from co_runOnEvbWithTimeout),
   * otherwise dispatches and waits. Avoids deadlock in both contexts. */
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    for (const auto& [prefix, localRoute] : localRoutes_) {
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

void RibBase::updateEntryStats(TEntryStats& stats) noexcept {
  stats.total_ucast_routes() = ribEntries_.size();
  stats.total_originated_routes() = localRoutes_.size();

  /*
   * total_rib_paths is the add-path-correct sum of every prefix's path count.
   * RibCounters maintains it incrementally on the RIB evb (see onPathsDelta),
   * so read it directly instead of walking all ribEntries_ (O(total paths)) on
   * every getEntryStats call.
   */
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait(
      [&]() { stats.total_rib_paths() = ribCounters_.totalPaths(); });
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

NexthopInfo* FOLLY_NULLABLE RibBase::getNexthopInfo(const BgpPath& attrs) {
  XCHECK(nexthopCache_)
      << "nexthopCache_ must not be null when nexthop tracking is enabled";

  const auto& nexthop = attrs.getNexthop();
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
        "Found existing NexthopInfo for nexthop {} (reachable: {}, igpCost: {})",
        nexthop.str(),
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
  auto nextHopStatus = nexthopCache_->registerAndGetNexthopStatus(nexthop);
  const bool isReachable = nextHopStatus.isReachable();
  const auto igpCost = nextHopStatus.getIgpCost();
  const std::optional<bool> isConnected = nextHopStatus.isConnected();

  XLOGF(
      DBG2,
      "Retrieved nexthop status from nexthopCache for {}: reachable={}, igpCost={}, isConnected={}",
      nexthop.str(),
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
    if (!nextHopStatus.isResolvedForSelection()) {
      ribCounters_.incrementUnresolvableNexthopCount();
    }
  }

  XLOGF(
      INFO,
      "Created new NexthopInfo for nexthop {} (reachable: {}, igpCost: {}, isConnected: {})",
      nexthop.str(),
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
        DBG2, "NexthopInfo for {} not found in nexthopInfoMap_", nexthop.str());
    return false;
  }

  const auto& nexthopInfo = it->second;

  // If there are RouteInfo objects associated with this nexthop, don't delete
  if (!nexthopInfo.isRouteInfoListEmpty()) {
    XLOGF(
        DBG3,
        "Not deleting NexthopInfo for {}: still has associated RouteInfo objects",
        nexthop.str());
    return false;
  }
  /*
   * A RibEntry in fibBatchList_ can still name this nexthop, and the FIB reads
   * its NexthopInfo when the FIB loop builds the request. The NexthopInfo stays
   * registered and in nexthopInfoMap_, with no RouteInfo. The FIB loop deletes
   * it after it takes the batch.
   */
  if (!fibBatchList_.empty()) {
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
      DBG2, "Deleting nexthop status for {} from nexthopCache", nexthop.str());
  nexthopCache_->unregisterAndRemoveNexthopStatus(nexthop);

  if (!it->second.isResolvedForSelection()) {
    ribCounters_.decrementUnresolvableNexthopCount();
  }

  // Delete from nexthopInfoMap_
  XLOGF(
      DBG2, "Deleting NexthopInfo for {} from nexthopInfoMap_", nexthop.str());
  RibStats::decrNexthopInfoCount();
  return nexthopInfoMap_.erase(it);
}

void RibBase::deleteUnusedNexthopInfos() {
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
