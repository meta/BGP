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

#include <algorithm>

#include <fstream>

#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <folly/FileUtil.h>
#include <folly/IPAddress.h>
#include <folly/Likely.h>
#include <folly/Overload.h>
#include <folly/ScopeGuard.h>
#include <folly/container/F14Set.h>
#include <folly/coro/CurrentExecutor.h>
#include <folly/coro/Sleep.h>
#include <folly/coro/WithCancellation.h>
#include <folly/logging/xlog.h>

#include "common/network/AddressUtil.h"
#include "fboss/agent/Constants.h"
#include "fboss/lib/AlertLogger.h"
#include "magic_enum/magic_enum.hpp"
#include "neteng/fboss/bgp/cpp/BgpProfiler.h"
#include "neteng/fboss/bgp/cpp/BgpServiceUtil.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/adjrib/RouteFilterLogger.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/config/ConfigManager.h"
#include "neteng/fboss/bgp/cpp/config/ConfigStructs.h"
#include "neteng/fboss/bgp/cpp/peer/PeerManagerBase.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyManager.h"
#include "neteng/fboss/bgp/cpp/rib/Utils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/if/gen-cpp2/BgpStructs_types.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

using namespace facebook::neteng::fboss::bgp_attr;
using namespace facebook::neteng::fboss::bgp::thrift;
using namespace facebook::nettools::bgplib;
using namespace facebook::bgp::BgpStats;
using std::chrono::milliseconds;
using std::chrono::seconds;

DEFINE_int32(
    validity_time_stateful_gr_s,
    180,
    "Validity time in seconds for saved GR state");
DEFINE_int32(
    peer_start_delay_ms,
    50,
    "Duration in milliseconds between start of two peers. T81178121");
DEFINE_int32(
    thrift_stream_publish_gap_ms,
    100,
    "Gap in milliseconds between thrift stream publisher loop");

/*
 * A client of a bounded stream subscriber can hold its stream open and can
 * stop to give thrift stream credit. Then the generator of that subscriber
 * waits at its co_yield. Thrift resumes such a generator only on new credit
 * or on a disconnect of the client, so the server alone cannot end the
 * stream. The subscriber would stay ESTABLISHED, would keep its AdjRib, would
 * hold a slot of streamSubscriberLimit, and would keep a change-list consumer
 * that never advances.
 *
 * This gflag sets the time after which the peer manager ends such a session.
 * The peer manager ends a session only when both of these are true for the
 * whole time:
 *   - the egress queue stays blocked, and
 *   - the client takes no message out of the queue.
 *
 * The second test is necessary. The stats task samples the blocked flag one
 * time in each thrift_stream_publish_gap_ms period. A client that reads
 * slowly can drain the queue and let the producer refill it between two
 * samples. Then every sample reads the queue as blocked, although the client
 * reads. The count of the consumed messages separates the two cases.
 *
 * Set the value to 0 to switch the reclamation off.
 */
DEFINE_int32(
    stream_subscriber_idle_timeout_ms,
    600000,
    "End the session of a bounded thrift stream subscriber when its egress "
    "queue stays blocked for this many milliseconds and the client takes no "
    "message in that time. 0 switches the reclamation off.");

// Connection retry parameters
DEFINE_int32(
    min_conn_retry_time_ms,
    500,
    "Minimum TCP connection retry time in milliseconds");
DEFINE_int32(
    max_conn_retry_time_ms,
    2000,
    "Maximum TCP connection retry time in milliseconds");
DEFINE_int32(
    conn_timeout_ms,
    500,
    "Timeout for TCP connection in milliseconds");
DEFINE_int32(
    min_session_retry_time_ms,
    1000,
    "Minimum BGP session retry time in milliseconds");
DEFINE_int32(
    max_session_retry_time_ms,
    60000,
    "Maximum BGP session retry time in milliseconds");
DEFINE_int32(
    max_session_dampen_time_ms,
    300000,
    "BGP sessions (regardless previously established or not) if retried "
    "to established during this window, backoff upto max_session_retry_time");
DEFINE_int32(
    counter_update_time_s,
    300,
    "Update counters every X seconds. Default is 5 minutes.");
DEFINE_string(
    gr_state_file,
    "/dev/shm/bgp_gr_state.txt",
    "File in which GR state is stored across bgp restarts");
DEFINE_string(
    stream_peer_tag,
    "BGP_MONITOR",
    "Peer tag for stream subscriber peer config");
DEFINE_string(
    safemode_file,
    "/dev/shm/safemode.txt",
    "Empty file to persist safe mode across BGP restarts");

namespace facebook {
namespace bgp {

using nettools::bgplib::DeDuplicatedBgpPath;

namespace {

int64_t nowEpochMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

int64_t nowSteadyMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

} // namespace

void StreamSubscriber::resetQueues() {
  /*
   * A subscriber uses the same queue sizes as a peer. The sizes come from the
   * shared constants in BgpStructs.h. A subscriber has no separate knob.
   */
  peerInputQ = std::make_shared<FiberBgpPeer::BoundedInputQueueT>(
      kMaxEgressQueueSize, kEgressQueueHighWatermark, kEgressQueueLowWatermark);
  peerOutputQ =
      std::make_shared<FiberBgpPeer::OutputQueueT>(kMaxIngressQueueSize);
  /*
   * A new object gives the new session its own state. A generator of an
   * earlier session can still hold the earlier object and can still write to
   * it. Those writes must not reach the new session.
   */
  sessionState = std::make_shared<StreamSessionState>();
  blockStartTimeMs.reset();
  noProgressStartTimeMs.reset();
  noProgressConsumedCount.reset();
}

PeerManagerBase::PeerManagerBase(
    std::shared_ptr<ConfigManager> configManager,
    const std::shared_ptr<PolicyManager> policyManager,
    MonitoredBackPressuredQueue<RibInMessage>& ribInQ,
    MonitoredMPMCQueue<RibOutMessage>& ribOutQ,
    std::optional<MonitoredMPMCQueue<NeighborWatcherMessage>>& nbrRouteChangeQ,
    bool requireNexthopResolution,
    std::chrono::milliseconds minConnRetryDur,
    std::chrono::milliseconds maxConnRetryDur,
    std::chrono::milliseconds connTimeout,
    std::chrono::milliseconds minSessionRetryDur,
    std::chrono::milliseconds maxSessionRetryDur,
    std::chrono::milliseconds maxSessionDampenDur)
    : BgpModuleBase(kModulePeerManager),
      eorWaitDuration_(
          std::chrono::seconds(
              *configManager->getConfig()->getConfig().eor_time_s())),
      defaultVrf_(createVrf(fboss::kDefaultVrf, /*publishStats=*/true)),
      // TODO: re-enable the quarantine VRF. See @quarantineVrf_.
      // quarantineVrf_(
      //     createVrf(fboss::kQuarantineVrfId, /*publishStats=*/false)),
      configManager_(configManager),
      policyManager_(policyManager),
      ribInQ_(ribInQ),
      ribOutQ_(ribOutQ),
      nbrRouteChangeQ_(nbrRouteChangeQ),
      minConnRetryDur_(minConnRetryDur),
      maxConnRetryDur_(maxConnRetryDur),
      connTimeout_(connTimeout),
      minSessionRetryDur_(minSessionRetryDur),
      maxSessionRetryDur_(maxSessionRetryDur),
      maxSessionDampenDur_(maxSessionDampenDur),
      requireNexthopResolution_(requireNexthopResolution),
      switchLimitConfig_(
          configManager_->getConfig()->getBgpSwitchLimitConfig()) {
  // Sanity check
  CHECK_GE(maxConnRetryDur_.count(), minConnRetryDur_.count());
  CHECK_GE(maxSessionRetryDur_.count(), minSessionRetryDur_.count());
  CHECK(configManager_->getConfig()) << "encountered nullptr of BGP Config";

  // initialize config knobs
  UpdateGroupConfig updateGroupConfig;
  if (auto globalConfig = configManager_->getConfig()->getBgpGlobalConfig()) {
    enableDynamicPolicyEvaluation_ =
        globalConfig->enableDynamicPolicyEvaluation;
    enableUpdateGroup_ = globalConfig->enableUpdateGroup;
    enableRibAllocatedPathId_ = globalConfig->enableRibAllocatedPathId;
    enableSerializeGroupPdu_ =
        globalConfig->updateGroupConfig.enableSerializeGroupPdu;
    updateGroupConfig = globalConfig->updateGroupConfig;
  }

  if (enableUpdateGroup_) {
    updateGroupManager_ = std::make_unique<UpdateGroupManager>(
        evb_, updateGroupConfig, policyManager_);
  }

  // receives from fromAdjRibQ_, so the direction is IN
  monitorQueue(
      kQueueNameFromAdjRib, fromAdjRibQ_, MonitorableQueueTrace::Direction::IN);
  if (nbrRouteChangeQ_) {
    // receives from nbrRouteChangeQ_, so the direction is IN
    monitorQueue(
        kQueueNameFromNeighborWatcher,
        *nbrRouteChangeQ_,
        MonitorableQueueTrace::Direction::IN);
  }

  /*
   * NOTE: eorTimer_ / initializedMaxWaitTimer_ (and, for BB,
   * ribComputationMaxWaitTimer_) are created in createAndScheduleTimers() on
   * the PeerManager EventBase thread when run() starts, not here.
   */

  // Get stream subscriber peering parameters
  auto p = getStreamPeeringParams();
  if (p.second) {
    streamPeerAddr_ = p.first;
    streamPeeringParams_ = std::move(p.second);
    XLOGF(
        INFO,
        "Stream subscribers will use peerAddr: {}",
        streamPeerAddr_.str());
  } else {
    XLOG(ERR, "Did not get stream peering params");
  }

  routeFilterLoggerFactory_ = createRouteFilterLoggerFactory();
  // if safemode file exists, then we are in safemode
  if (boost::filesystem::exists(FLAGS_safemode_file)) {
    XLOGF(
        INFO,
        "Safemode file {} exists, setting isSafeModeOn_ to true",
        FLAGS_safemode_file);
    *isSafeModeOn_ = true;
    BgpStats::setIsSafeModeOn(true);
  }
}

PeerManagerBase::~PeerManagerBase() {
  /**
   * Invoke destructor to cleanly stop all of the adjRib fibers and trees
   *
   *
   * 1. Clear adjRibOutGroup_ RadixTrees BEFORE destroying AdjRibs
   *    - These trees are shared across peers
   *    - With isDaemonShutdown_ set, AdjRibs won't access them
   *
   * 2. Destroy AdjRibs (they'll skip tree cleanup since isDaemonShutdown_=true)
   *
   * 3. Destroy adjRibOutGroups (trees already cleared, so fast)
   */

  // Step 1: Clear adjRibOutGroups_ RadixTrees before destruction
  XLOGF(
      INFO,
      "[Exit] Clearing adjRibOutGroups_ ({} groups)...",
      adjRibOutGroups_.size());
  for (auto& [_, group] : adjRibOutGroups_) {
    if (group) {
      group->PathTree_.clear();
      group->LiteTree_.clear();
    }
  }
  XLOG(INFO, "[Exit] adjRibOutGroups_ trees cleared");

  // Step 2: Destroy AdjRibs
  for (auto& [_, adjRib] : adjRibs_) {
    if (adjRib) {
      adjRib->resetChangeListConsumer();
      adjRib.reset();
    }
  }
  adjRibs_.clear();
  RibStats::setAdjRibCount(0);

  // Step 3: Destroy adjRibOutGroups (trees already cleared)
  adjRibOutGroups_.clear();
  BgpStats::setAdjRibOutGroupsCount(0);

  // terminate evb to deterministically shutdown PeerManagerBase evb
  evb_.terminateLoopSoon();

  for (auto& [_, vrf] : peerManagerVrfs_) {
    vrf->getChangeListTracker().reset();
  }
  XLOG(INFO, "[Exit] Successfully destructed PeerManagerBase object");
}

void PeerManagerBase::run() noexcept {
  XLOG(DBG1, "Start running PeerManagerBase...");
  CHECK(sessionMgr_) << "setSessionManager() must be called before run()";

  addPeersToSessionMgr();
  scheduleCoroTasks();

  /*
   * Platform EoR-timer policy seam, run once here on the PeerManager EventBase
   * thread just before the event loop starts. It both CREATES and schedules the
   * timers, so AsyncTimeouts are always made on this evb thread (not the
   * constructor). The base creates eorTimer_ + initializedMaxWaitTimer_ and
   * arms the standard EoR wait (eor_time_s from startup) used by DC and OSS;
   * platform subclasses override it to install a different startup policy (e.g.
   * PeerManagerBB creates + arms a boot-relative RIB-computation max-wait timer
   * and defers creating/arming eorTimer_ until the first session establishes).
   */
  createAndScheduleTimers();

  XLOG(INFO, "Start PeerManagerBase event-base loop");
  evb_.loop();
  XLOG(INFO, "[Exit] Successfully terminated PeerManagerBase event-base.");
}

void PeerManagerBase::scheduleCoroTasks() noexcept {
  asyncScope_.add(co_withExecutor(&evb_, processPeerEventLoop()));
  asyncScope_.add(co_withExecutor(&evb_, processAdjRibMsgLoop()));
  asyncScope_.add(co_withExecutor(&evb_, processRibOutMsgLoop()));
  if (nbrRouteChangeQ_) {
    asyncScope_.add(co_withExecutor(&evb_, processNeighborRouteChangeLoop()));
  }
  asyncScope_.add(co_withExecutor(&evb_, reportStreamSubscriberStatsRoutine()));
  asyncScope_.add(
      co_withExecutor(&evb_, startPeriodicPolicyCacheEvictionRoutine()));
  asyncScope_.add(
      co_withExecutor(&evb_, startPeriodicUpdatePeerCountersRoutine()));
  asyncScope_.add(co_withExecutor(&evb_, periodicEvictFromDeduplicatorLoop()));
}

void PeerManagerBase::createAndScheduleTimers() noexcept {
  const auto now = std::chrono::steady_clock::now();
  for (auto& [_, vrf] : peerManagerVrfs_) {
    vrf->createEorTimer();
    vrf->createInitializedMaxWaitTimer();
    vrf->setEorTimerArmedAt(now);
    scheduleTimer(vrf->getEorTimer(), "EoR timer", eorWaitDuration_);
  }
}

bool PeerManagerBase::scheduleTimer(
    std::unique_ptr<folly::AsyncTimeout>& timer,
    std::string_view name,
    std::chrono::seconds duration) noexcept {
  /*
   * Shared arm helper. No-op if `timer` has been reset — after
   * convergence-notify or stop(), or when reached without run() having created
   * it (e.g. direct unit-test calls). Returns true iff the timer was
   * (re-)armed.
   */
  if (!timer) {
    return false;
  }
  timer->scheduleTimeout(duration);

  XLOGF(DBG1, "Started {} for {}s.", name, duration.count());
  return true;
}

void PeerManagerBase::addPeersToSessionMgr() {
  /*
   * [GR State]
   *
   * Load Graceful-Restart(GR) state if any. The GR state file only covers the
   * default VRF.
   *
   * TODO_GR_VRF: Save and load the GR state of every VRF.
   */
  auto grLoadResult = readGrState();
  if (grLoadResult.loaded) {
    grStateLoaded_ = true;
    BgpStats::setStatefulGR(true);
  } else {
    BgpStats::setStatefulGR(false);
  }

  /*
   * Delay in milliseconds for starting peer after it is added. This increment
   * by `peer_start_delay` on every peer addition.
   */
  std::chrono::milliseconds startDelay{0};

  /*
   * [Static Peers]
   *
   * Static peers are BGP peers that have a configured local and remote address.
   * The actual BGP(TCP) session that gets established will have the same local
   * and remote addresses.
   */
  auto config = configManager_->getConfig();
  const auto& peerToConfigs = config->getPeerToConfig();
  for (const auto& [peerAddr, peerConfigPtr] : peerToConfigs) {
    auto params = config->getPeeringParamsForPeer(*peerConfigPtr);
    const auto startAfterDelay = startDelay;

    // Increment delay for subsequent peer addition
    startDelay += milliseconds(FLAGS_peer_start_delay_ms);

    // Add peer
    auto addPeerExpected = sessionMgr_->addPeer(
        peerAddr,
        params,
        ConnTimeParams(
            startAfterDelay,
            minConnRetryDur_,
            maxConnRetryDur_,
            connTimeout_,
            minSessionRetryDur_,
            maxSessionRetryDur_,
            maxSessionDampenDur_));

    if (addPeerExpected.hasError()) {
      XLOGF(
          ERR,
          "Error adding peer: {}, Error: {}",
          peerAddr.str(),
          magic_enum::enum_name(addPeerExpected.error()));
    }
    /*
     * Expect to receive EoR from all static peers, if GR capable.
     * Initialize to false for all static peers if no saved stateful GR state.
     * If valid saved GR state exists, wait for only saved (stateful) peers.
     *
     * TODO_PMTRIZED_VRF: Config peers are always in the default VRF for now
     * (see Config::getPeeringParamsHelper). Wait for EoR in the peer's VRF.
     */
    if (!grLoadResult.loaded) {
      XLOGF(
          INFO,
          "[Initialization] Will wait for EoR against {} from config.",
          peerAddr.str());

      defaultVrf_.getStaticPeerEoRReceived().emplace(
          peerAddr, std::make_pair(false, false));
    } else {
      for (const auto& savedBgpPeerId : *(grLoadResult.peers)) {
        if (savedBgpPeerId.peerAddr == peerAddr) {
          XLOGF(
              INFO,
              "[Initialization] Will wait for EoR against {} from stateful GR file.",
              peerAddr.str());

          defaultVrf_.getStaticPeerEoRReceived().emplace(
              peerAddr, std::make_pair(false, false));
          break;
        }
      }
    }
  }

  /*
   * [Dynamic Peers]
   *
   * Unlike static peers, dynamic peers do NOT have a specific local + remote
   * address. Instead the config specifies a subnet as peer address. The switch
   * will accept a TCP connection to peer from any address that belongs to that
   * subnet.
   *
   * NOTE: dynamic peers can have multiple peerings with same ip address with
   * different remote bgp peer ID's. So the stored key will be:
   *
   * `nettools::bgplib::BgpPeerId`
   *
   * instead of
   *
   * `folly::IPAddress` for static peers.
   */
  auto dynamicPeerToConfigs = config->getDynamicPeerToConfig();
  for (const auto& [peerPrefix, peerConfigPtr] : dynamicPeerToConfigs) {
    auto params = config->getPeeringParamsForDynamicPeer(*peerConfigPtr);
    auto peerAddExpected = sessionMgr_->addDynamicPeer(peerPrefix, params);

    if (peerAddExpected.hasError()) {
      // Output the error and the peer where the error occurs
      XLOGF(
          ERR,
          "Add peer to session manager failed for {}. Error code: {}",
          peerPrefix.first.str(),
          magic_enum::enum_name(peerAddExpected.error()));
    }
    if (!grLoadResult.loaded) {
      continue;
    }

    /*
     * Mark all previously Established peers belonging to the configured dynanic
     * peer subnet, to wait for EOR.
     *
     * TODO_PMTRIZED_VRF: Wait for EoR in the peer's VRF.
     */
    for (const auto& savedBgpPeerId : *(grLoadResult.peers)) {
      if (savedBgpPeerId.peerAddr.inSubnet(
              peerPrefix.first, peerPrefix.second)) {
        XLOGF(
            INFO,
            "[Initialization] Will wait for EoR against {} from stateful GR file.",
            savedBgpPeerId.peerAddr.str());

        defaultVrf_.getDynamicPeerEoRReceived().emplace(
            savedBgpPeerId, std::make_pair(false, false));
      }
    }
  }

  BgpStats::logInitializationEvent(
      "PeerManager", BgpInitializationEvent::PEER_INFO_LOADED);
}

folly::coro::Task<folly::Expected<folly::Unit, FiberBgpPeerManager::ErrorCode>>
PeerManagerBase::addPeers(
    const std::vector<std::shared_ptr<BgpPeerConfig>>& peerConfigs) {
  std::chrono::milliseconds startDelay{0};
  for (const auto& peerConfigPtr : peerConfigs) {
    const auto& peerConfig = *peerConfigPtr;
    auto params =
        configManager_->getConfig()->getPeeringParamsForPeer(peerConfig);
    auto result = co_await sessionMgr_->co_addPeer(
        peerConfig.peerAddr,
        params,
        ConnTimeParams(
            startDelay,
            minConnRetryDur_,
            maxConnRetryDur_,
            connTimeout_,
            minSessionRetryDur_,
            maxSessionRetryDur_,
            maxSessionDampenDur_));
    if (result.hasError()) {
      co_return folly::makeUnexpected(result.error());
    }
    startDelay += milliseconds(FLAGS_peer_start_delay_ms);
  }
  co_return folly::unit;
}

folly::coro::Task<folly::Expected<folly::Unit, FiberBgpPeerManager::ErrorCode>>
PeerManagerBase::delPeers(const std::vector<folly::IPAddress>& peerAddrs) {
  for (const auto& peerAddr : peerAddrs) {
    co_await folly::coro::co_safe_point;
    auto result =
        co_await sessionMgr_->co_dropPeer(peerAddr, /*peerDelete=*/true);
    if (result.hasError()) {
      if (result.error() ==
          FiberBgpPeerManager::ErrorCode::PEER_DOES_NOT_EXIST) {
        XLOGF(
            INFO, "delPeers: peer {} does not exist, skipping", peerAddr.str());
        continue;
      }
      co_return folly::makeUnexpected(result.error());
    }

    /*
     * Fallback: shutdownPeer skips peer->stop() when activeSessionInfo is
     * null (IDLE peers, GR helper mode), so no BgpSessionStop fires ->
     * sessionTerminated -> cleanupPeerState chain never runs. Drive cleanup
     * inline for any non-established peerId for this address.
     */
    auto addrIt = peerAddrToIds_.find(peerAddr);
    if (addrIt != peerAddrToIds_.end()) {
      std::vector<nettools::bgplib::BgpPeerId> peerIds(
          addrIt->second.begin(), addrIt->second.end());
      for (const auto& peerId : peerIds) {
        co_await folly::coro::co_safe_point;
        auto adjRib = findAdjRib(peerId);
        if (adjRib && adjRib->isStateEstablished()) {
          continue;
        }
        co_await cleanupPeerState(peerId, peerAddr);
      }
    }
  }
  co_return folly::unit;
}

void PeerManagerBase::stop() noexcept {
  auto task = [&]() -> folly::coro::Task<void> {
    XLOG(INFO, "Signaling all peer manager fibers to stop ...");

    /*
     * markDaemonShutdown() and saveGrState() are now called from Main.cpp
     * before sessionMgr->stop() to preserve GR state before sessions terminate.
     * shutdownWithGR() is called by SessionManager::stop() in Main.cpp.
     */

    // reset all of the timers
    for (auto& [_, vrf] : peerManagerVrfs_) {
      vrf->getEorTimer().reset();
      vrf->getInitializedMaxWaitTimer().reset();
    }

    /*
     * Stop the bounded stream subscribers before the AdjRibs that write to
     * them. This loop requests the cancellation and closes the queue. A
     * generator that waits in queue->pop() sees one of the two signals and
     * ends.
     *
     * This loop does not end a generator that waits at its co_yield. Thrift
     * resumes such a generator only on new stream credit or on a disconnect
     * of the client. Therefore PeerManagerBase must stay alive until the
     * threads of the thrift server stop. The main function joins those
     * threads before it destroys PeerManagerBase.
     */
    for (auto& [subscriberName, subscriber] : streamSubscribers_) {
      XLOGF(INFO, "Stopping stream subscriber {}", subscriberName);
      if (subscriber.streamCancelSource) {
        subscriber.streamCancelSource->requestCancellation();
      }
      if (subscriber.peerInputQ) {
        subscriber.peerInputQ->close();
      }
      subscriber.state = TBgpPeerState::IDLE;
    }

    for (const auto& [_, adjRib] : adjRibs_) {
      if (adjRib) {
        adjRib->deactivateChangeListConsumer();
        co_await adjRib->stop();
      }
    }

    /*
     * Unregister peers from update groups, then destroy any now-empty groups
     * in one batch after the loop.
     */
    if (enableUpdateGroup_ && updateGroupManager_) {
      folly::F14FastSet<std::shared_ptr<AdjRibOutGroup>> groups;
      for (const auto& [_, adjRib] : adjRibs_) {
        if (adjRib) {
          auto updateGroup = adjRib->getUpdateGroup();
          if (updateGroup) {
            updateGroup->unregisterPeer(adjRib);
            groups.insert(std::move(updateGroup));
          }
        }
      }
      co_await updateGroupManager_->maybeDestroyUpdateGroups(groups);
    }

    /*
     * When shutting down BGP instance, in case BGP has not yet finished
     * initialization sequence. Record the pending peer EoR to receive
     * from (ingressEoR) or send to (egressEoR) GR Helpers.
     */
    for (auto& [_, vrf] : peerManagerVrfs_) {
      if (!vrf->isRibInitPathComputationNotified() ||
          vrf->isEorTimerExpired()) {
        /*
         * Case 1: still waiting for ingress EoR from all GR Helpers
         * case 2: EOR_TIMER_EXPIRED. Log the ingress EoR it is still waiting
         * for
         */
        vrf->logEoRPeers(true);
      } else if (!vrf->isAllEgressEoRSent()) {
        // case 3: ALL_EOR_RECEIVED while not all egressEoR sent to
        vrf->logEoRPeers(false);
      }
    }
    co_return;
  };

  folly::coro::blockingWait(folly::coro::co_withExecutor(&evb_, task()));

  folly::coro::blockingWait(asyncScope_.cancelAndJoinAsync());
  XLOGF(INFO, "[Exit] All coro tasks finished.");
}

void PeerManagerBase::markDaemonShutdown() {
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([this] {
    XLOGF(INFO, "Marking BGP daemon shutdown for all AdjRibs...");
    daemonShutdown_ = true;

    for (const auto& [_, adjRib] : adjRibs_) {
      if (adjRib) {
        adjRib->setDaemonShutdown();
      }
    }
  });
}

folly::coro::Task<void> PeerManagerBase::processPeerEventLoop() noexcept {
  XLOG(INFO, "Starting peer event processing coro task...");

  auto& notifyQueue = sessionMgr_->getNotifyCoroQueue();
  while (true) {
    // when cancelAndJoinAsync is called, guaranteed to exit
    co_await folly::coro::co_safe_point;

    auto evt = co_await co_awaitTry(notifyQueue.pop());
    if (!evt.hasValue()) {
      XLOG(
          INFO, "[Exit] Coro task cancelled. Terminating processPeerEventLoop");
      break;
    }
    // Only process ObservableStateT (session up/down events)
    if (std::holds_alternative<FiberBgpPeer::ObservableStateT>(*evt)) {
      auto stateEvt = std::get<FiberBgpPeer::ObservableStateT>(std::move(*evt));
      if (stateEvt.state == BgpSessionState::ESTABLISHED) {
        co_await sessionEstablished(std::move(stateEvt));
      } else {
        co_await sessionTerminated(stateEvt);
      }
    }
    // ObservableMessageT is not expected via notifyQueue_
  }
}

folly::coro::Task<void> PeerManagerBase::processAdjRibEvent(
    AdjRib::ObservableMessageT&& evt) noexcept {
  const auto& peerId = evt.peerId;
  const auto vrfId = evt.vrfId;

  // We cannot set "static" here as the content varies according to peerId
  auto overload = folly::overload(
      [this, peerId, vrfId](
          AdjRib::EoR /*rcvEoRForAllAfi*/) -> folly::coro::Task<void> {
        // process ingress EoRs
        XLOGF(
            INFO,
            "{}Received EoR from {}",
            facebook::fboss::BGPAlert().str(),
            peerId.str());
        auto* vrf = findVrf(vrfId);
        if (!vrf) {
          XLOGF(
              ERR,
              "Ignoring EoR from peer {} due to non-existent VRF {}",
              peerId.str(),
              vrfId);
          co_return;
        }
        vrf->processPeerEoR(peerId);
        co_return;
      },
      [this, peerId, vrfId](
          AdjRib::EgressEoR /*rcvEoRFromRib*/) -> folly::coro::Task<void> {
        // process egress EoRs
        XLOGF(
            INFO,
            "{}Received EgressEoR from {}",
            facebook::fboss::BGPAlert().str(),
            peerId.str());
        auto* vrf = findVrf(vrfId);
        if (!vrf) {
          XLOGF(
              ERR,
              "Ignoring EgressEoR from peer {} due to non-existent VRF {}",
              peerId.str(),
              vrfId);
          co_return;
        }
        vrf->processEgressEoR(peerId);
        if (auto adjRib = findAdjRib(peerId)) {
          vrf->maybeSchedulePendingRouteRefresh(adjRib);
        }
        co_return;
      },
      [this,
       peerId](AdjRib::DetachedInitDumpCompleted) -> folly::coro::Task<void> {
        if (auto adjRib = findAdjRib(peerId)) {
          if (auto* vrf = findVrf(adjRib->getVrfId())) {
            vrf->maybeSchedulePendingRouteRefresh(adjRib);
          } else {
            XLOGF(
                ERR,
                "Skipping pending Route Refresh for peer {} due to non-existent VRF {}",
                peerId.str(),
                adjRib->getVrfId());
          }
        }
        co_return;
      },
      [this, peerId](AdjRib::Shutdown /*shutdown*/) -> folly::coro::Task<void> {
        XLOGF(
            INFO,
            "{}Received AdjRib shutdown due to received route limit reached",
            facebook::fboss::BGPAlert().str(),
            peerId.str());
        co_await sessionMgr_->co_shutdownPeer(peerId.peerAddr);
      },
      /*
       * process TriggerSafeMode generated internally from AdjRib when total
       * path scale or unique prefix limit is reached.
       */
      [this, &peerId](AdjRib::TriggerSafeMode /*triggerSafeMode*/)
          -> folly::coro::Task<void> {
        XLOGF(
            INFO,
            "{}Received trigger safe mode for {}",
            facebook::fboss::BGPAlert().str(),
            peerId.str());
        processTriggerSafeMode();
        co_return;
      },
      /* Route Refresh from a peer (RFC 2918): re-dump only the AFI the
       * request named (§3), not every family on the session. */
      [this, peerId](AdjRib::RouteRefreshReceived routeRefreshMsg)
          -> folly::coro::Task<void> {
        XLOGF_EVERY_MS(
            INFO,
            5000,
            "Processing Route Refresh for peer {} AFI={}, triggering RIB dump",
            peerId.str(),
            static_cast<int>(routeRefreshMsg.requestedAfi));
        auto peerIdAdjRib = adjRibs_.find(peerId);
        if (peerIdAdjRib != adjRibs_.cend() && peerIdAdjRib->second) {
          auto* vrf = findVrf(peerIdAdjRib->second->getVrfId());
          if (!vrf) {
            XLOGF(
                ERR,
                "Ignoring Route Refresh from peer {} due to non-existent VRF {}",
                peerId.str(),
                peerIdAdjRib->second->getVrfId());
            co_return;
          }
          if (enableUpdateGroup_ &&
              peerIdAdjRib->second->isUpdateGroupEnabled()) {
            vrf->scheduleRouteRefreshForUpdateGroupPeer(
                peerIdAdjRib->second, routeRefreshMsg.requestedAfi);
          } else {
            vrf->processRibDumpReq(
                peerIdAdjRib->second,
                RibDumpReq(
                    peerId,
                    peerIdAdjRib->second->sendAddPath(),
                    routeRefreshMsg.requestedAfi /* filterAfi */),
                true /* sendWithEoR */);
          }
        }
        co_return;
      });
  co_await std::visit(overload, evt.message);
}

folly::coro::Task<void> PeerManagerBase::reportStreamSubscriberStatsRoutine() {
  XLOG(DBG1, "Start stream subscriber stats coro task...");

  while (true) {
    // when cancelAndJoinAsync is called, loop will be broken to exit
    co_await folly::coro::co_safe_point;

    /*
     * The period sets how fast bgpd sees a block, so a block that lasts one
     * period stays visible.
     */
    co_await folly::coro::sleepReturnEarlyOnCancel(
        milliseconds(FLAGS_thrift_stream_publish_gap_ms));
    reportStreamSubscriberBackpressureStats();
  }

  XLOG(
      INFO, "[Exit] PeerManagerBase stream subscriber stats coroutine stopped");
  co_return;
}

SubscriberStreamTeardown::SubscriberStreamTeardown(
    PeerManagerBase* peerManager,
    const nettools::bgplib::BgpPeerId& peerId,
    std::string subscriberName,
    uint32_t publisherId)
    : peerManager_(peerManager),
      peerId_(peerId),
      subscriberName_(std::move(subscriberName)),
      publisherId_(publisherId) {}

SubscriberStreamTeardown::SubscriberStreamTeardown(
    SubscriberStreamTeardown&& other) noexcept
    : peerManager_(other.peerManager_),
      peerId_(other.peerId_),
      subscriberName_(std::move(other.subscriberName_)),
      publisherId_(other.publisherId_) {
  other.peerManager_ = nullptr;
}

SubscriberStreamTeardown::~SubscriberStreamTeardown() {
  if (peerManager_ == nullptr) {
    return;
  }
  /*
   * The destructor must not block the executor of thrift. Therefore it posts
   * the work to evb_ and does not wait for the result.
   *
   * This code must keep runInEventBaseThread. It must never use
   * runInEventBaseThreadAndWait. The destructor can run on evb_ itself when
   * subscribe() throws inside its second evb block, and the wait form
   * deadlocks in that case. The post form only enqueues the functor.
   */
  /*
   * The early return above tests peerManager_. Therefore the pointer is not
   * null here. A reference states that fact to the reader and to the static
   * analysis.
   */
  auto& peerManager = *peerManager_;
  peerManager.getEventBase().runInEventBaseThread(
      [&peerManager,
       peerId = peerId_,
       subscriberName = std::move(subscriberName_),
       publisherId = publisherId_]() {
        peerManager.cancelSubscriberStream(peerId, subscriberName, publisherId);
        XLOGF(
            INFO,
            "Channel for subscriber {} ({}) has been closed",
            subscriberName,
            peerId.str());
      });
}

folly::coro::AsyncGenerator<TBgpRouteDelta&&>
PeerManagerBase::subscriberStreamGenerator(
    std::shared_ptr<FiberBgpPeer::BoundedInputQueueT> queue,
    folly::CancellationToken cancelToken,
    std::shared_ptr<StreamSubscriber::StreamSessionState> sessionState,
    nettools::bgplib::BgpPeerId peerId,
    std::string subscriberName,
    SubscriberStreamTeardown teardown) {
  XLOGF(
      INFO,
      "Started bounded stream generator for subscriber {} ({})",
      subscriberName,
      peerId.str());

  /*
   * `teardown` ends the session of the subscriber. The coroutine frame owns
   * it, so its destructor runs when the generator ends. The generator ends
   * for four reasons: the client disconnects and thrift cancels the
   * generator, the server requests cancelToken, the queue closes, or thrift
   * destroys the frame before it resumes this body one time.
   */

  while (true) {
    co_await folly::coro::co_safe_point;
    /*
     * Two cancellation tokens can end the pop() below. The code must respect
     * both, so it merges them.
     *
     * ambientToken is the token of thrift. Thrift installs it around each
     * gen_.next() call and it fires when the client disconnects. Read it
     * again in each iteration. AsyncGenerator clears the coroutine context on
     * each resume, so a copy from an earlier iteration becomes stale.
     *
     * cancelToken is the token of the server. subscriber.streamCancelSource
     * owns it. resetSubscriberAdjRib() and the shutdown path request it.
     *
     * A merge is necessary because co_withCancellation keeps only the first
     * token. BasePromise::setCancellationToken ignores a later token. If this
     * code passed cancelToken alone, the semaphore wait inside pop() would
     * never see the disconnect of the client. Then a client that disconnects
     * while the queue is empty would leave this generator parked. The
     * subscriber would stay ESTABLISHED, would keep its AdjRib, and would
     * count against streamSubscriberLimit.
     */
    auto ambientToken = co_await folly::coro::co_current_cancellation_token;
    auto mergedToken =
        folly::cancellation_token_merge(ambientToken, cancelToken);
    auto maybeMsg = co_await folly::coro::co_awaitTry(
        folly::coro::co_withCancellation(mergedToken, queue->pop()));
    if (maybeMsg.hasException()) {
      XLOGF(
          INFO,
          "Stream generator for subscriber {} cancelled: {}",
          subscriberName,
          maybeMsg.exception().what());
      if (sessionState->evicted.load(std::memory_order_relaxed)) {
        co_yield folly::coro::co_error(
            TBgpServiceException(kStreamSubscriberEvictedMessage));
      }
      co_return;
    }
    if (!(*maybeMsg).has_value()) {
      XLOGF(
          WARN,
          "Received an empty message from AdjRib to subscriber {}",
          subscriberName);
      co_return;
    }

    /*
     * The pop above took a message out of the queue and thus made room for
     * the producer. Count it here, at the one place that consumes the queue.
     * reclaimIdleStreamSubscriber() reads this count on evb_ to tell a client
     * that reads slowly from a client that stopped to read.
     *
     * The count rises for every message that leaves the queue, also for a
     * message that the code below drops. A drop still frees queue space, so
     * it is progress for the producer.
     */
    sessionState->consumedCount.fetch_add(1, std::memory_order_relaxed);

    /*
     * Drop the message if the server tore the subscriber down while the
     * message was in flight.
     *
     * This generator runs on the executor of thrift. Therefore it must not
     * read streamSubscribers_, because evb_ owns that map. The cancellation
     * token is the correct signal between the two threads. The token is also
     * earlier than the state: resetSubscriberAdjRib() requests the
     * cancellation before it sets the state to IDLE.
     */
    if (cancelToken.isCancellationRequested()) {
      XLOGF(
          DBG2,
          "Subscriber {} torn down while message in flight; dropping",
          subscriberName);
      if (sessionState->evicted.load(std::memory_order_relaxed)) {
        co_yield folly::coro::co_error(
            TBgpServiceException(kStreamSubscriberEvictedMessage));
      }
      co_return;
    }

    TBgpRouteDelta delta;
    bool hasDelta = false;
    folly::variant_match(
        **maybeMsg, // dereference folly::Try<T> and std::optional
        [&](std::shared_ptr<const nettools::bgplib::BgpUpdate2> update) {
          delta.update2OrEor()->update2() = *update;
          hasDelta = true;
        },
        [&](const nettools::bgplib::UpdateDescriptor& /*not used*/) {
          /*
           * The I/O thread of a peer handles an UpdateDescriptor directly for
           * the zero-copy serialization path. A stream subscriber has no I/O
           * thread. Therefore this arm loses the route without a trace. No
           * code pushes an UpdateDescriptor into a subscriber queue today, so
           * this arm logs the loss and keeps the stream open.
           */
          XLOGF_EVERY_MS(
              ERR,
              1000,
              "Dropping UpdateDescriptor for stream subscriber {}: no "
              "zero-copy path exists for stream egress",
              subscriberName);
        },
        [&](const BgpEndOfRib& eor) {
          delta.update2OrEor()->eor() = eor;
          hasDelta = true;
        },
        [&](const BgpNotification& /*not used*/) {
          // Do nothing
        },
        [&](const BgpRouteRefresh& /*not used*/) {
          // TO BE IMPLEMENTED
        });

    if (!hasDelta) {
      continue;
    }
    delta.onDeviceTimeStampMs() = nowEpochMs();

    /*
     * Thrift resumes this generator from the yield only when the client gives
     * stream credit. Therefore a slow client keeps the generator suspended
     * here. Then nothing pops `queue`, the queue fills, and the AdjRib that
     * writes into it applies backpressure.
     *
     * This loop has no exit condition. An AsyncGenerator does not need one.
     * The generator ends when the consumer of the stream destroys it, or when
     * a cancellation ends the pop() above, or when the queue closes. The
     * three co_return statements above cover the last two cases. The guard
     * above runs in all three cases.
     */
    co_yield std::move(delta);
  }
}

void PeerManagerBase::reportStreamSubscriberBackpressureStats() noexcept {
  int64_t deepestQueue = 0;
  bool anyBoundedSubscriber = false;

  for (auto& [subscriberName, subscriber] : streamSubscribers_) {
    if (!subscriber.peerInputQ) {
      continue;
    }
    /*
     * The test below is for a bounded subscriber in any state. A daemon with
     * no bounded subscriber must report no depth at all. A daemon that has a
     * bounded subscriber must keep the depth current. If the last session of
     * that subscriber goes down, the depth must fall to 0 and must not hold
     * the last reading.
     */
    anyBoundedSubscriber = true;

    /*
     * Skip a subscriber that is not ESTABLISHED. streamSubscribers_ keeps the
     * entry of a subscriber after the teardown, in state IDLE and with a
     * closed queue, until the client disconnects. No reader pops that queue.
     * A sample of it would read as unblocked. Then bgpd would report an
     * unblock transition and a block duration for a recovery that did not
     * happen, and the depth gauge would stay high for an entry that only
     * waits for its destruction. resetQueues() clears blockStartTimeMs at the
     * next subscribe().
     */
    if (subscriber.state != TBgpPeerState::ESTABLISHED) {
      continue;
    }
    deepestQueue = std::max(
        deepestQueue, static_cast<int64_t>(subscriber.peerInputQ->size()));

    const bool blocked = subscriber.peerInputQ->isBlocked();
    if (blocked && !subscriber.blockStartTimeMs.has_value()) {
      /*
       * The start of the block uses the steady clock. The duration below is
       * unsigned. If the wall clock stepped back during the block, the
       * subtraction would underflow and would corrupt the duration stat. The
       * wall-clock reading goes only to the last-block-time gauge, which
       * reports a wall-clock time on purpose.
       */
      subscriber.blockStartTimeMs = static_cast<uint64_t>(nowSteadyMs());
      subscriber.noProgressStartTimeMs = subscriber.blockStartTimeMs;
      subscriber.noProgressConsumedCount =
          subscriber.sessionState->consumedCount.load(
              std::memory_order_relaxed);
      BgpStats::incStreamSubscriberBackpressuredEvents();
      BgpStats::setStreamSubscriberLastBlockTime(
          static_cast<uint64_t>(nowEpochMs()));
      XLOGF(
          INFO,
          "{}Stream subscriber {} egress queue blocked at depth {}",
          facebook::fboss::BGPAlert().str(),
          subscriberName,
          subscriber.peerInputQ->size());
    } else if (!blocked && subscriber.blockStartTimeMs.has_value()) {
      BgpStats::addStreamSubscriberBlockDuration(
          static_cast<uint64_t>(nowSteadyMs()) - *subscriber.blockStartTimeMs);
      subscriber.blockStartTimeMs.reset();
      subscriber.noProgressStartTimeMs.reset();
      subscriber.noProgressConsumedCount.reset();
      XLOGF(
          INFO,
          "Stream subscriber {} egress queue unblocked at depth {}",
          subscriberName,
          subscriber.peerInputQ->size());
    }

    reclaimIdleStreamSubscriber(subscriberName, subscriber);
  }

  if (anyBoundedSubscriber) {
    BgpStats::setStreamSubscriberQueueDepth(deepestQueue);
  }
}

void PeerManagerBase::reclaimIdleStreamSubscriber(
    const std::string& subscriberName,
    StreamSubscriber& subscriber) {
  if (FLAGS_stream_subscriber_idle_timeout_ms <= 0) {
    return;
  }
  if (!subscriber.blockStartTimeMs.has_value() ||
      !subscriber.noProgressStartTimeMs.has_value() ||
      !subscriber.noProgressConsumedCount.has_value()) {
    return;
  }

  const uint64_t nowMs = static_cast<uint64_t>(nowSteadyMs());
  const uint64_t noProgressMs = nowMs - *subscriber.noProgressStartTimeMs;
  if (noProgressMs <
      static_cast<uint64_t>(FLAGS_stream_subscriber_idle_timeout_ms)) {
    return;
  }

  /*
   * The client must have taken no message at all in that time.
   *
   * The blocked flag alone is not enough. The stats task samples the flag one
   * time in each thrift_stream_publish_gap_ms period. A client that reads
   * slowly takes the queue below the low watermark and the producer refills
   * it. If both steps happen between two samples, every sample reads the
   * queue as blocked, although the client reads. The count of the consumed
   * messages separates the two cases, because it rises for each message that
   * leaves the queue.
   */
  const uint64_t consumed =
      subscriber.sessionState->consumedCount.load(std::memory_order_relaxed);
  if (consumed != *subscriber.noProgressConsumedCount) {
    /*
     * The client makes progress. Start the next run of no progress here. The
     * block itself continues, so blockStartTimeMs keeps its value and the
     * block-duration stat still reports the whole block.
     */
    const uint64_t progressed = consumed - *subscriber.noProgressConsumedCount;
    subscriber.noProgressStartTimeMs = nowMs;
    subscriber.noProgressConsumedCount = consumed;
    XLOGF(
        INFO,
        "Stream subscriber {} egress queue stayed blocked for {} ms and the "
        "client took {} messages in that time, so the session continues",
        subscriberName,
        noProgressMs,
        progressed);
    return;
  }

  /*
   * The block ends here, so record its whole length. The unblock branch above
   * cannot record it, because the queue never unblocks in this case.
   */
  BgpStats::addStreamSubscriberBlockDuration(
      nowMs - *subscriber.blockStartTimeMs);
  BgpStats::incStreamSubscriberIdleTimeouts();

  XLOGF(
      ERR,
      "{}Stream subscriber {} egress queue stayed blocked at depth {} and the "
      "client took no message for {} ms. The limit is {} ms. Ending the "
      "session to free the AdjRib, the subscriber slot, and the change-list "
      "consumer.",
      facebook::fboss::BGPAlert().str(),
      subscriberName,
      subscriber.peerInputQ->size(),
      noProgressMs,
      FLAGS_stream_subscriber_idle_timeout_ms);

  subscriber.blockStartTimeMs.reset();
  subscriber.noProgressStartTimeMs.reset();
  subscriber.noProgressConsumedCount.reset();

  /*
   * Set the flag before the teardown below requests the cancellation. The
   * generator reads the flag only after the cancellation wakes it, so this
   * order makes the flag visible to it.
   *
   * The generator then ends the stream with an error and the client learns
   * about the eviction. A client that gives no credit at all does not wake
   * and does not learn. Only thrift can resume a generator that waits for
   * credit, so the server has no way to reach such a client. That client
   * finds out when it gives credit again or when its connection ends.
   */
  subscriber.sessionState->evicted.store(true, std::memory_order_relaxed);

  /*
   * This call sets the state to IDLE. The loop of the caller skips a
   * subscriber that is not ESTABLISHED, so the next sample skips this entry.
   * The call does not erase the entry of streamSubscribers_, so the iterator
   * of the caller stays valid.
   *
   * The generator of the session stays parked at its co_yield until the
   * client disconnects, because only thrift can resume a generator that waits
   * for credit. That generator writes nothing more: the AdjRib is terminated
   * and its queue is closed. When thrift destroys the frame of the generator,
   * the teardown object calls cancelSubscriberStream(), which returns early
   * for a state that is not ESTABLISHED.
   */
  resetSubscriberAdjRib(subscriber);
}

void PeerManagerBase::markRibInitialAnnouncementDone(
    PeerManagerVrf& vrf) noexcept {
  if (vrf.isRibInitialAnnouncementDone()) {
    return;
  }

  vrf.setRibInitialAnnouncementDone(true);

  if (enableUpdateGroup_) {
    if (updateGroupManager_) {
      /*
       * BGP initialization complete - trigger group-level initial dumps
       * for any update groups in UNINITIALIZED state
       */
      updateGroupManager_->triggerInitialDumpsForUninitializedGroups(
          vrf.vrfId());
    } else {
      XLOG(
          ERR,
          "Unexpected uninitialized UpdateGroupManager when update groups is enabled");
    }
  } else {
    /*
     * Non-update-group mode: handle all pending RibDumpReqs
     */
    vrf.scheduleHandleBufferedRibDumpReqs();
  }
}

folly::coro::Task<void> PeerManagerBase::processAdjRibMsgLoop() noexcept {
  XLOG(INFO, "Starting adjRib msg processing coro task...");

  while (true) {
    // when cancelAndJoinAsync is called, guaranteed to exit
    co_await folly::coro::co_safe_point;

    auto msg = co_await co_awaitTry(fromAdjRibQ_.pop());
    if (!msg.hasValue()) {
      XLOG(
          INFO, "[Exit] Coro task cancelled. Terminating processAdjRibMsgLoop");
      break;
    }
    ScopedProfile profile("PeerManagerBase::processAdjRibMsg");
    co_await processAdjRibEvent(std::move(*msg));
  }
}

folly::coro::Task<void> PeerManagerBase::processRibOutMsgLoop() noexcept {
  XLOG(INFO, "Starting ribOutMsg processing coro task...");
  auto msgsProcessed = 0;

  while (true) {
    // when cancelAndJoinAsync is called, guaranteed to exit
    co_await folly::coro::co_safe_point;

    /*
     * DO NOT REMOVE THIS SLEEP CALL!
     *
     * co_await will automatically yield the execution of coro tasks without
     * explicit sleep call. However, the current consumption rate from adjrib
     * can't match up with PeerManagerBase production rate. This speed mismatch
     * can cause severe queue build-up situation to accumulate memory.
     *
     * In case of changeList enabled, process more messages from the egress rib
     * queue before yielding
     */
    if (!defaultVrf_.isRibInitialAnnouncementDone() ||
        (msgsProcessed > announcmentsProcessBatch_)) {
      /**
       * Process more announcements once RIB is already initialized,
       * then yield to avoid queue build-up
       */
      co_await folly::coro::sleep(std::chrono::milliseconds(1));
      msgsProcessed = 0;
    }

    auto msg = co_await co_awaitTry(ribOutQ_.pop());
    if (!msg.hasValue()) {
      XLOG(
          INFO, "[Exit] Coro task cancelled. Terminating processRibOutMsgLoop");
      break;
    }
    ScopedProfile profile("PeerManagerBase::processRibOutMsg");
    msgsProcessed++;
    /*
     * Make an equality check. If queue size is going down then it must hit
     * the lower watermark. Thus making this singular eqaulity check should
     * be sufficient
     */
    if (ribOutQ_.size() == ribOutQLowWatermark_) {
      XLOGF(INFO, "Resume RIB as ribOutQ size : {}", ribOutQ_.size());
      co_await ribInQ_.push(
          ResumeBestPathAndFibProgramming(RibPauseResumeCause::BACKPRESSURE));
    }

    auto* vrf = findVrf(msg->vrfId);
    if (!vrf) {
      XLOGF(
          ERR, "Ignoring RibOutMessage due to non-existent VRF {}", msg->vrfId);
      continue;
    }

    folly::variant_match(
        msg->payload,
        [&](RibOutAnnouncement& announcement) {
          /*
           * Incremental announcements come to PeerManagerBase, which will be
           * pushed to the individual AdjRibs. The number of RibOutAnnouncement
           * updates may be large, and may go into multiple chunks with
           * kRibChunkSize each. The last of these chunks will be flagged with
           * sendWithEoR.
           */
          XLOG(DBG3, "Passing RibOutAnnouncement to all established peers.");

          // Handle announcement to keep Rib and ShadowRib entries in-sync.
          vrf->handleShadowRibEntryAnnouncement(announcement);

          /*
           * Prepare RibOut structure sending to multiple adjRibs
           */
          if (enableUpdateGroup_ == false) {
            distributeRibOutAnnouncementToAdjRibs(*vrf, announcement);
          }

          /**
           * Now that we have completed the initial announcement, mark
           * initial announcement done flag and handle any processing
           * if required with this state transition
           */
          if (announcement.sendWithEoR) {
            markRibInitialAnnouncementDone(*vrf);
          }
        },
        [vrf](RibOutWithdrawal& withdrawal) {
          XLOG(DBG3, "Passing RibOutWithdrawal to all established peers.");

          /*
           * Withdrawals go through changeList only.
           * Handle withdrawal to keep Rib and ShadowRib entries in-sync.
           */
          vrf->handleShadowRibEntryWithdrawal(withdrawal);
        },
        [vrf](const RibInitialAnnouncementStart&) {
          XLOG(
              INFO,
              "[Initialization] Received RibInitialAnnouncementStart message.");

          vrf->setRibInitialAnnouncementStarted(true);
          vrf->maybeMarkInitialized();
        });
  }
  co_return;
}

folly::coro::Task<void> PeerManagerBase::processRibDumpReqCoro(
    RibDumpReq ribDumpReq) {
  auto adjrib = adjRibs_.find(ribDumpReq.peerId);
  if (adjrib == adjRibs_.cend()) {
    XLOGF(
        ERR,
        "Skip sending announcement since adjRib {} does not exist.",
        ribDumpReq.peerId.str());
    co_return;
  }

  auto* vrf = findVrf(adjrib->second->getVrfId());
  if (!vrf) {
    XLOGF(
        ERR,
        "Ignoring RibDumpReq from peer {} due to non-existent VRF {}",
        ribDumpReq.peerId.str(),
        adjrib->second->getVrfId());
    co_return;
  }

  vrf->processRibDumpReq(adjrib->second, ribDumpReq, true /* sendWithEoR */);
  co_return;
}

folly::coro::Task<void> PeerManagerBase::handleNeighborEventMsg(
    const NeighborEventMsg& msg) noexcept {
  if (!msg.isUp) {
    XLOGF(
        INFO,
        "Neighbor [{}] disappeared. Bringing session down...",
        msg.nbrAddr.str());
    /* If a peer is currently undergoing graceful restart, for example
     * during an admin down to update BGP during push, we will have
     * marked all routes learned from that peer as "stale" and they will
     * still be used in forwarding.  Since we just received nbrDown for
     * that peer, we need to clean up these stale routes and send
     * "withdraw" to RIB so that RIB recomputes best paths. Calling
     * sessionMgr_->stopPeer() does not work in this case because BGP
     * session is already down and FiberBgpPeer has lost communication
     * with AdjRib.  Therefore, if peer is in GR state, we call
     * adjRib->cleanupGrState() to do this clean up without joining
     * the asyncScope (which would cause a double-join hang if stop()
     * is later called during daemon shutdown).
     * See T107593063 / S254616 for motivation.
     */
    for (auto [peerId, adjRib] : adjRibs_) {
      if (peerId.peerAddr == msg.nbrAddr) {
        if (adjRib && adjRib->isPeerGracefulRestarting()) {
          XLOGF(INFO, "Received {} DOWN while nbr in GR state", peerId.str());
          co_await adjRib->cleanupGrState(/*isDaemonShutdown=*/false);
        }
        /*
         * If BGP is in initialization and the NeighborWatcher thread
         * reports a neighbor as down, remove the neighbor from static
         * or dynamic EOR waiting lists. This optimization improves
         * convergence time.
         */
        if (auto* vrf = findVrf(msg.vrfId)) {
          if (!vrf->isInitialized()) {
            vrf->processNeighborRouteChangeDuringInitialization(peerId);
          }
        } else {
          XLOGF(
              ERR,
              "[Initialization] Skipping neighbor down for peer {} due to non-existent VRF {}",
              peerId.str(),
              msg.vrfId);
        }
      }
    }
    co_await sessionMgr_->co_stopPeer(msg.nbrAddr, false /*withGR*/);
  }
}

folly::coro::Task<void>
PeerManagerBase::handleNeighborReachabilityMsg() noexcept {
  XLOG(INFO, "Received NeighborReachabilityMsg. Stopping all peers.");
  BgpStats::incDsfFastTearDownCount();

  for (auto [peerId, adjRib] : adjRibs_) {
    auto peerConfig =
        configManager_->getConfig()->getConfigOfAPeer(peerId.peerAddr);
    /*
     * HACK: We have hardcoded the DSF roles in a set. This is a very
     * rough shortcut in order to satisfy DSF requirements. We
     * add this check here with the knowledge that this is a
     * tradeoff to unblock DSF.
     */
    if (!peerConfig || !peerConfig->peerTag ||
        !kDsfSwitchRoles.contains(*peerConfig->peerTag)) {
      // We will only stop peers that are in DSF zone.
      continue;
    }
    if (adjRib && adjRib->isPeerGracefulRestarting()) {
      XLOGF(INFO, "Stopping peer while peer in GR state: {}", peerId.str());
      co_await adjRib->cleanupGrState(/*isDaemonShutdown=*/false);
    } else {
      XLOGF(INFO, "Stopping peer: {}", peerId.str());
    }
    co_await sessionMgr_->co_stopPeer(peerId.peerAddr, false /*withGR*/);
  }
}

folly::coro::Task<void>
PeerManagerBase::processNeighborRouteChangeLoop() noexcept {
  XLOG(DBG1, "Starting neighbor event processing coro task...");
  while (true) {
    // when cancelAndJoinAsync is called, guaranteed to exit
    co_await folly::coro::co_safe_point;

    auto msg = co_await co_awaitTry(nbrRouteChangeQ_->pop());
    if (!msg.hasValue()) {
      XLOG(
          INFO,
          "[Exit] Coro task cancelled. Terminating processNeighborRouteChangeLoop.");
      break;
    }

    // using static as we only need to define it once
    static auto overload = folly::overload(
        [this](const NeighborEventMsg& neighborEventMsg)
            -> folly::coro::Task<void> {
          co_await handleNeighborEventMsg(neighborEventMsg);
        },
        [this](const NeighborReachabilityMsg&) -> folly::coro::Task<void> {
          co_await handleNeighborReachabilityMsg();
        });

    co_await std::visit(overload, *msg);
  }
  co_return;
}

/*
 * peerAddr state (established/terminated) has changed, update non graceful
 * counter for peer_tag of this peer
 */
void PeerManagerBase::updateNonGracefulCounters(
    const folly::IPAddress& peerAddr,
    bool isTerminated) noexcept {
  auto peerConfig = configManager_->getConfig()->getConfigOfAPeer(peerAddr);
  if (!peerConfig) {
    XLOGF(INFO, "Cannot find config for peer {}", peerAddr.str());
    return;
  }
  if (isTerminated) {
    BgpStats::decNonGrPeers(*(peerConfig->peerTag));
  } else {
    BgpStats::incNonGrPeers(*(peerConfig->peerTag));
  }
}

void PeerManagerBase::addLivePeerGroup(
    const std::string& peerGroupName) noexcept {
  if (++livePeerGroups_[peerGroupName] == 1) {
    BgpStats::setNumLivePeerGroups(livePeerGroups_.size());
  }
}

void PeerManagerBase::removeLivePeerGroup(
    const std::string& peerGroupName) noexcept {
  auto it = livePeerGroups_.find(peerGroupName);
  /*
   * A terminate can arrive for a session we never counted (the establish was
   * ignored because the peer flapped before we processed it), so an absent
   * entry is expected rather than an invariant break.
   */
  if (it == livePeerGroups_.end()) {
    return;
  }
  if (--it->second == 0) {
    livePeerGroups_.erase(it);
    BgpStats::setNumLivePeerGroups(livePeerGroups_.size());
  }
}

folly::coro::Task<void> PeerManagerBase::waitForSessionTerminateBaton(
    const BgpPeerId& peerId) noexcept {
  /*
   * Wait till adjRib sees the terminate notification. This ensures that
   * peerManager doesn't move adjRib to established when it's already in
   * established state.
   *
   * Even though co_await here suspends, this should not suspend long as, one
   * round robin to AdjRib task will ensure that NULL in the front is read
   * immediately and session is terminated from adjRib. Under stable conditions
   * number of such occurrences should be very minimal. If successive flaps are
   * from same peer, we will skip over those because of version number.
   */
  XLOGF(
      DBG1,
      "Peer Manager waiting for adjRib of [{}] to terminate",
      peerId.str());

  auto terminateBaton = sessionTerminateBatons_[peerId];
  auto startTime = std::chrono::steady_clock::now();

  /*
   * co_await *terminateBaton is safe from cancellation.
   *
   * folly::coro::Baton is explicitly NOT cancellation-aware (Baton.h):
   *
   * co_await *baton will:
   *  - Pass through immediately if already posted (latch semantics)
   *  - Suspend until post() is called — cancellation requests are ignored
   *  - Never throw OperationCancelled
   *
   * The latch semantics also prevent hangs during rapid session flaps with
   * version compression (S619541): if the baton was posted by a previous
   * session termination and not yet reset (due to a stale version causing
   * early return), subsequent co_await calls pass through immediately.
   *
   * Note: the old code used co_withCancellation(CancellationToken{}, ...)
   * to shield BatchSemaphore::co_wait() from outer cancellation. With
   * folly::coro::Baton, this shielding is unnecessary — the primitive
   * itself ignores cancellation by design.
   */
  co_await *terminateBaton;
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - startTime);
  XLOGF_IF(
      DBG1,
      duration.count() > 1,
      "Peer Manager blocked waiting for adjRib of [{}] to terminate for {}ms",
      peerId.str(),
      duration.count());
  co_return;
}

folly::coro::Task<void> PeerManagerBase::cleanupPeerState(
    const BgpPeerId& peerId,
    const folly::IPAddress& peerAddr) noexcept {
  XLOGF(
      INFO,
      "cleanupPeerState: starting cleanup for peer {} addr {}",
      peerId.str(),
      peerAddr.str());

  /*
   * Capture AdjRib pointer before baton wait. In the common case
   * sessionEstablished reuses the existing AdjRib for a given peerId, so
   * findAdjRib(peerId) == expectedAdjRib after the wait. Mismatch is rare
   * and requires concurrent delPeers for the same peer plus an interleaved
   * add: del_1 and del_2 both capture expected = adjRib_1 (overlapping so
   * both see the same pointer before either erases), del_2 resumes first
   * and erases, the add then runs sessionEstablished which sees an empty
   * slot and creates a fresh adjRib_2 via createAdjRib. When del_1 resumes
   * it finds adjRib_2 — skip the erase to avoid destroying it.
   */
  auto expectedAdjRib = findAdjRib(peerId);

  /*
   * 1. Wait for AdjRib message loops to exit (baton posted by
   * postTerminateBaton after both processPeerMessageLoop and
   * processRibMessageLoop signal the semaphore).
   * Only wait if an AdjRib exists (peer was ever established).
   */
  if (expectedAdjRib) {
    co_await waitForSessionTerminateBaton(peerId);
  }

  // 2. AdjRib erasure
  auto adjRib = findAdjRib(peerId);
  if (adjRib && adjRib != expectedAdjRib) {
    XLOGF(
        INFO,
        "cleanupPeerState: AdjRib for {} was replaced by a new session, "
        "skipping erase",
        peerId.str());
    co_return;
  } else if (adjRib) {
    /*
     * Always call stop() before erasing. It forces the deferred GR
     * withdrawal (when in GR helper mode) and drains in-flight pushes from
     * the GR timer callbacks.
     */
    co_await adjRib->stop();
    // Re-check identity after suspension.
    if (findAdjRib(peerId) != expectedAdjRib) {
      XLOGF(
          INFO,
          "cleanupPeerState: AdjRib for {} was replaced during stop(), "
          "skipping erase",
          peerId.str());
      co_return;
    }
    stopMonitoring(peerId.toOdsKey());
    PeerStats::clearPeerCounters(
        adjRib->getStats().getPeerIdOdsStr(), peerId.str());
    adjRib->resetChangeListConsumer();
    adjRibs_.erase(peerId);
    RibStats::decrAdjRibCount();
    XLOGF(INFO, "cleanupPeerState: erased AdjRib for {}", peerId.str());

    // 3. Erase per-peer states
    auto addrIt = peerAddrToIds_.find(peerAddr);
    if (addrIt != peerAddrToIds_.end()) {
      if (addrIt->second.erase(peerId) > 0) {
        BgpStats::decrPeerAddrToIdsCount();
      }
      if (addrIt->second.empty()) {
        peerAddrToIds_.erase(addrIt);
      }
    }
    sessionTerminateBatons_.erase(peerId);
    if (establishedGrPeers_.erase(peerId) > 0) {
      BgpStats::decrEstablishedGrPeersCount();
    }
    auto* vrf = findVrf(adjRib->getVrfId());
    if (!vrf) {
      XLOGF(
          ERR,
          "cleanupPeerState: skipping per-VRF cleanup for peer {} due to non-existent VRF {}",
          peerId.str(),
          adjRib->getVrfId());
    } else {
      vrf->getDynamicPeerEoRReceived().erase(peerId);
      vrf->getStaticPeerEoRReceived().erase(peerAddr);
      if (enableUpdateGroup_) {
        vrf->cancelRibDumpForAdjRib(
            adjRib, true /* discardRouteRefreshRequests */);
      } else if (vrf->getPendingRibDumpReqs().erase(peerId) > 0) {
        BgpStats::decrPendingRibDumpReqsCount(1);
      }
    }
  }
  XLOGF(
      INFO,
      "cleanupPeerState: completed cleanup for peer {} addr {}",
      peerId.str(),
      peerAddr.str());
  co_return;
}

folly::coro::Task<void> PeerManagerBase::sessionEstablished(
    FiberBgpPeer::ObservableStateT evt) noexcept {
  ScopedProfile profile("PeerManagerBase::sessionEstablished");
  const auto& peerId = evt.peerId;
  const auto& versionNumber = evt.versionNumber;
  const auto& sessionInfo = evt.sessionInfo;

  XLOGF(
      INFO,
      "Session ESTABLISHED for [{}] version {}",
      peerId.str(),
      versionNumber);

  const auto& peerAddr = peerId.peerAddr;
  auto adjRib = findAdjRib(peerId);
  XLOGF(
      INFO,
      "BGP_PEER_EVENT peer={} incarnation={} phase={} src={}",
      peerAddr.str(),
      adjRib ? adjRib->flapCounter() : 0,
      "SESSION_ESTABLISH_NOTIFIED",
      BGP_LOG_SRC());
  if (adjRib) {
    /* Wait for all coroutines spawned from previous session to finish. */
    co_await waitForSessionTerminateBaton(peerId);
    adjRib->logPeerEvent("SESSION_PREV_TEARDOWN_COMPLETE", BGP_LOG_SRC());
    co_await adjRib->ensureAsyncScopeInitialized();
  }

  auto* vrf = findVrf(evt.vrfId);
  if (!vrf) {
    XLOGF(
        ERR,
        "Ignoring session establishment for peer [{}] due to non-existent VRF {}",
        peerId.str(),
        evt.vrfId);
    co_return;
  }

  /*
   * count peers with which we negotiated graceful restart capability.
   * in the case of static peer without GR capability, treat EoR is received
   * immediately.
   */
  const auto& peerInfo = sessionInfo->peerInfo;
  CHECK(peerInfo.has_value());
  /*
   * if SET_LINK_BPS is set for UCMP, link bandwidth Bps must be a valid
   * value
   */
  if ((peerInfo->peeringParams.advertiseLinkBandwidth.has_value() &&
       *peerInfo->peeringParams.advertiseLinkBandwidth ==
           AdvertiseLinkBandwidth::SET_LINK_BPS) ||
      (peerInfo->peeringParams.receiveLinkBandwidth.has_value() &&
       *peerInfo->peeringParams.receiveLinkBandwidth ==
           ReceiveLinkBandwidth::SET_LINK_BPS)) {
    CHECK(peerInfo->peeringParams.linkBandwidthBps.has_value()) << fmt::format(
        "UCMP SET_LINK_BPS is specified for peer {}, but linkBandwidthBps does not have a value.",
        peerId.peerAddr.str());
  }

  // check whether add path capabilities are the same for each address family
  std::optional<nettools::bgplib::BgpAddPathSendRec> cachedCapa{std::nullopt};

  for (const auto& addPathCapa :
       *peerInfo.value().negotiatedCapabilities.addPathCapabilities()) {
    if (!cachedCapa.has_value()) {
      cachedCapa = *addPathCapa.sor();
    }
    if (cachedCapa.value() != *addPathCapa.sor()) {
      XLOG(
          ERR,
          "We don't support different add path capability for different "
          "address family over one peering");
      co_return;
    }
  }

  bool inInitialAnnouncement = !vrf->isRibInitialAnnouncementStarted();

  /* get bgp peer output queue, aka, adjRibInQueue */
  auto& oqueue = sessionInfo->outputQueue;
  CHECK(oqueue != nullptr);

  /* Get bgp bounded peer input queue, aka, adjRibOutQueue. */
  auto& iqueue = sessionInfo->boundedInputQueue;
  CHECK(iqueue != nullptr);

  { // start of the critical section, protected by versionLock
    auto versionLock = sessionInfo->currentVersion->grabScopedLock();

    /*
     * The act of putting RibDumpReq message on ribInQ_ above requires
     * acquiring a fiber-aware lock. If RIB thread is accessing this queue
     * at the same time, this current Fiber may have to suspend. Thus, we
     * need to re-check if FiberBgpPeerManager has flapped the session or
     * moved on to a new incarnation of that session while this fiber was
     * suspended. If either of those events happened, then we must ignore
     * the Establish event.
     */
    if (sessionInfo->currentVersion->getWithoutLock() != versionNumber) {
      XLOGF(
          INFO,
          "Ignoring session establishment notification for peer [{}] "
          "version {}: State no longer valid",
          peerAddr.str(),
          versionNumber);
      co_return;
    }

    // create AdjRib fiber for this peer
    if (!adjRib) {
      adjRib = createAdjRib(*vrf, peerId, peerInfo->peeringParams);
      /*
       * set route filter statement and golden prefix policy before this
       * peer starts
       */
      setRouteFilterStatement(adjRib);
      setGoldenPrefixPolicy(adjRib, true /* initializeAdjRib */);
      auto changeListConsumer = std::make_shared<AdjRibOutConsumer>(
          vrf->getChangeListTracker(),
          adjRib,
          "ChangeList Consumer",
          evb_,
          vrf->getAddPathConsumerBitmap(),
          vrf->getNonAddPathConsumerBitmap());
      adjRib->setChangeListConsumer(changeListConsumer);
    } else if (!adjRib->getChangeListConsumer()) {
      /*
       * Recreate change list consumer for reconnecting peers.
       * When a peer leaves an update group (session down), its
       * changeListConsumer is reset to nullptr. When the peer reconnects
       * (session up again), we need to recreate the consumer so it can
       * catch up independently before rejoining the group
       * (DETACHED_INIT_DUMP state).
       */
      auto changeListConsumer = std::make_shared<AdjRibOutConsumer>(
          vrf->getChangeListTracker(),
          adjRib,
          "ChangeList Consumer",
          evb_,
          vrf->getAddPathConsumerBitmap(),
          vrf->getNonAddPathConsumerBitmap());
      adjRib->setChangeListConsumer(changeListConsumer);
      XLOGF(
          INFO,
          "Recreated change list consumer for reconnecting peer {}",
          peerId.str());
    }

    adjRib->sessionEstablished(
        evt.remoteAs,
        std::optional<uint16_t>(peerInfo->remoteGrRestartTime),
        oqueue, /* aka adjRibInQueue */
        iqueue, /* aka, adjRibOutQueue */
        AfiIpv4Negotiated{*peerInfo->negotiatedCapabilities.mpExtV4Unicast()},
        AfiIpv6Negotiated{*peerInfo->negotiatedCapabilities.mpExtV6Unicast()},
        V4OverV6Nexthop{
            !peerInfo->negotiatedCapabilities.extNHEncodingCapabilities()
                 ->empty()},
        EnhancedRouteRefreshNegotiated{
            *peerInfo->negotiatedCapabilities.enhancedRouteRefresh()},
        RouteRefreshNegotiated{
            *peerInfo->negotiatedCapabilities.routeRefresh()},
        cachedCapa,
        *peerInfo->negotiatedCapabilities.as4byte(),
        !peerInfo->negotiatedCapabilities.extNHEncodingCapabilities()->empty(),
        *peerInfo->remoteCapabilities.mpExtExist());
    adjRib->logPeerEvent("SESSION_ADJRIB_ESTABLISHED", BGP_LOG_SRC());

    /*
     * Update group association.
     *
     * When the update_group feature is enabled, associate this peer with an
     * update group based on its "UpdateGroupKey". Members in the same group
     * will share generated update.
     */
    if (!enableUpdateGroup_) {
      /*
       * Only buffer RibDumpReq when update groups are disabled
       * With update groups enabled, decision is made after peer state is
       * determined
       */
      vrf->maybeBufferRibDumpReq(adjRib);
    } else {
      /*
       * Attention: adjRibOutGroup will be first created when adjRib is
       * created based on configuration. When updateGroup feature is
       * enabled, this will be overridden to a DIFFERENT group keyed by the
       * "UpdateGroupKey".
       */
      const auto& updateGroupKey = adjRib->getUpdateGroupKey();
      auto updateGroup = updateGroupManager_->findOrCreateGroup(
          updateGroupKey, vrf->getShadowRibView());

      /*
       * Wire the group's change list tracker and bitmaps once. The group's
       * change list consumer is created lazily after the initial RIB dump
       * (AdjRibOutGroup::registerGroupConsumer), not here.
       */
      if (!updateGroup->getChangeListTracker()) {
        XLOGF(
            INFO,
            "Setting change list tracker on update group [{}]",
            updateGroup->getGroupDescriptor());
        updateGroup->setChangeListTracker(
            vrf->getChangeListTracker(),
            vrf->getAddPathConsumerBitmap(),
            vrf->getNonAddPathConsumerBitmap());
      } else {
        XLOGF(
            DBG1,
            "Update group [{}] already has change list tracker",
            updateGroup->getGroupDescriptor());
      }

      adjRib->setUpdateGroup(updateGroup);

      XLOGF(
          DBG1,
          "Peer [{}] associated with update group [{}]",
          peerId.str(),
          updateGroup->getGroupDescriptor());

      // Register peer with the update group
      updateGroup->registerPeer(adjRib);

      // Handle BGP initialization sequence and group-level initial dump
      if (updateGroup->getState() == UpdateGroupState::UNINITIALIZED) {
        if (!vrf->isRibInitialAnnouncementDone()) {
          /*
           * Case 1: BGP initialization not complete
           * Defer initial dump until markRibInitialAnnouncementDone()
           * triggers all groups
           */
          XLOGF(
              INFO,
              "Peer [{}] in group [{}]: BGP initialization not complete, "
              "deferring initial dump",
              peerId.str(),
              updateGroup->getGroupDescriptor());
        } else {
          /*
           * Case 2: BGP initialization already complete
           * Trigger group-level initial dump immediately for this new group
           */
          XLOGF(
              INFO,
              "Peer [{}] in group [{}]: Triggering group-level initial dump",
              peerId.str(),
              updateGroup->getGroupDescriptor());
          updateGroup->scheduleInitialDump();
        }
      }

      // Check if peer needs independent initial dump (detached mode)
      auto peerState = adjRib->getPeerState();
      if (peerState == PeerUpdateState::DETACHED_INIT_DUMP) {
        // Peer must catch up independently before joining group
        XLOGF(
            INFO,
            "Peer [{}] in DETACHED_INIT_DUMP state, buffering independent RibDumpReq",
            peerId.str());
        vrf->maybeBufferRibDumpForDetachedPeer(adjRib);
      }
      // else: peer is in INIT state, group handles the initial dump
    }
  } // end of the critical section, protected by versionLock

  if (inInitialAnnouncement) {
    adjRib->setInInitialAnnouncement();
  }

  // Update statistics
  adjRib->logPeerEvent("SESSION_UPDATE_GROUP_JOINED", BGP_LOG_SRC());
  adjRib->markStateEstablished();
  runningSessions_ += 1;
  setRunningSessions(runningSessions_);

  /*
   * Virtual hook: subclasses override to (re-)arm the EoR wait timer on the
   * first established session.
   */
  onSessionEstablishedEorHook();

  addSessionStateChanges();
  if (isDynamicVipPeer(peerAddr, adjRib->getRemoteAs())) {
    runningVipSessions_ += 1;
    setRunningVipSessions(runningVipSessions_);
  }
  if (peerAddrToIds_[peerAddr].insert(peerId).second) {
    BgpStats::incrPeerAddrToIdsCount();
  }

  /*
   * Read the group off the AdjRib, not off peerInfo. A flapping peer keeps its
   * existing AdjRib (createAdjRib is skipped when one is already present), so
   * the event's peeringParams can carry a newer config than the AdjRib does.
   * The AdjRib's copy never changes for its lifetime, so sourcing both the
   * add and the remove from it keeps them symmetric.
   */
  const auto& peerGroupName = adjRib->getPeeringParams().peerGroupName;
  if (peerGroupName.has_value()) {
    addLivePeerGroup(*peerGroupName);
  }

  // Track peers which have negotiated graceful restart for stateful GR
  if (*peerInfo->negotiatedCapabilities.gracefulRestart()) {
    if (establishedGrPeers_.insert(peerId).second) {
      BgpStats::incrEstablishedGrPeersCount();
    }
  } else {
    updateNonGracefulCounters(peerAddr, false /* isTerminated */);
  }

  auto grSupported = *peerInfo->negotiatedCapabilities.gracefulRestart();
  auto isRestarting = *peerInfo->negotiatedCapabilities.isRestarting();
  if (!grSupported || isRestarting) {
    /*
     * Don't wait for EoR if peer does not support GR or is itself
     * restarting
     */
    if (!grSupported) {
      XLOGF(
          INFO,
          "{}Peer [{}] does not support GR capability, not waiting for EoR",
          facebook::fboss::BGPAlert().str(),
          peerAddr.str());
    }
    if (isRestarting) {
      XLOGF(
          INFO,
          "{}Peer [{}] is restarting, not waiting for EoR",
          facebook::fboss::BGPAlert().str(),
          peerAddr.str());
    }
    // pretend we got EoR from this peer
    vrf->processPeerEoR(peerId);
  }
  adjRib->startMessageProcessingLoop();
  co_return;
}

folly::coro::Task<void> PeerManagerBase::sessionTerminated(
    const FiberBgpPeer::ObservableStateT& evt) noexcept {
  ScopedProfile profile("PeerManagerBase::sessionTerminated");
  if (daemonShutdown_) {
    co_return;
  }

  const auto& peerId = evt.peerId;
  const auto& peerAddr = peerId.peerAddr;
  const auto& versionNumber = evt.versionNumber;

  const auto adjRib = findAdjRib(peerId);
  if ((!adjRib) || (!adjRib->isStateEstablished())) {
    /*
     * This can happen if there are rapid transitions between establish and
     * terminate, if we did not see establish by the time peer transitioned
     * to terminate, we would have ignored the establish notification, so,
     * for this terminate notification nothing needs to be done.
     */
    XLOGF(
        DBG2,
        "Ignoring session terminate for peer {} version {}: "
        "Previously session down.",
        peerId.str(),
        versionNumber);
    co_return;
  }

  const auto remoteAs = adjRib->getRemoteAs();
  const bool isVipPeer = isDynamicVipPeer(peerAddr, remoteAs);
  // TODO: Temporary; remove once GR VIP peers are cleaned up on GR expiry.
  const bool cleanupVipPeer =
      isVipPeer && adjRib->getRemoteGrRestartTime() == 0s;
  if (cleanupVipPeer) {
    sessionMgr_->scheduleDeleteTerminatedSession(
        peerId, versionNumber, kVipAsn);
  }

  XLOGF(
      INFO,
      "Session TERMINATED for [{}] version {}",
      peerId.str(),
      versionNumber);
  adjRib->logPeerEvent("SHUTDOWN_INITIATED", BGP_LOG_SRC());

  auto* vrf = findVrf(adjRib->getVrfId());
  if (!vrf) {
    XLOGF(
        ERR,
        "Skipping rib dump cancellation, pending RibDumpReq and EoR cleanup for peer {} due to non-existent VRF {}",
        peerId.str(),
        adjRib->getVrfId());
  }

  if (!enableUpdateGroup_) {
    if (vrf && vrf->getPendingRibDumpReqs().erase(peerId) > 0) {
      /*
       * Remove peer's request from the pending RibDumpReq collection if a
       * RibDumpReq was made during initialization. The update-group
       * equivalent is handled by cancelRibDumpForAdjRib below.
       */
      BgpStats::decrPendingRibDumpReqsCount(1);
    }
  } else {
    /*
     * Handle update group cleanup (Scenario E).
     *
     * Drop a buffered request and cancel any scheduled/in-flight rib dump for
     * this peer (a RibDumpReq may have been made during initialization). This
     * must happen before the maybeDestroyUpdateGroups co_await below: while
     * suspended there, handleBufferedRibDumpsForDetachedPeers could otherwise
     * pull this terminating peer off pendingRibDumpAdjRibs_ and service a
     * torn-down peer.
     */
    if (vrf) {
      vrf->cancelRibDumpForAdjRib(
          adjRib, true /* discardRouteRefreshRequests */);
    }

    auto updateGroup = adjRib->getUpdateGroup();
    if (updateGroup) {
      XLOGF(
          INFO,
          "Session terminated for peer {}, unregistering from update group",
          peerId.str());

      updateGroup->unregisterPeer(adjRib);

      // Maybe destroy group if no members remain
      co_await updateGroupManager_->maybeDestroyUpdateGroups({updateGroup});
    }
  }

  adjRib->markStateTerminated();
  adjRib->resetInInitialAnnouncement();
  adjRib->deactivateChangeListConsumer();
  adjRib->clearLastSeenRibVersion();

  /*
   * when a session goes down, remove it from the static and dynamic peer
   * EoR waiting collection. This will be a no-op if the key is not
   * populated.
   */
  if (vrf && !vrf->isInitialized()) {
    vrf->getStaticPeerEoRReceived().erase(peerAddr);
    vrf->getDynamicPeerEoRReceived().erase(peerId);

    XLOGF(
        INFO,
        "[Initialization] Session terminated. Exclude peer: {} from initialization",
        peerId.str());

    vrf->maybeMarkInitialized();
  }

  /*
   * Session terminated when queue get's null. This ensures no
   * OOB order issues
   */
  runningSessions_ -= 1;
  setRunningSessions(runningSessions_);
  addSessionStateChanges();
  if (isVipPeer) {
    runningVipSessions_ -= 1;
    setRunningVipSessions(runningVipSessions_);
  }

  if (evt.lastResetReason == ResetReason::HOLD_TIMER_EXPIRE) {
    PeerStats::incrTotalHoldTimerExpiry();
  }

  const auto& peerGroupName = adjRib->getPeeringParams().peerGroupName;
  if (peerGroupName.has_value()) {
    removeLivePeerGroup(*peerGroupName);
  }

  if (establishedGrPeers_.find(peerId) != establishedGrPeers_.end()) {
    establishedGrPeers_.erase(peerId);
    BgpStats::decrEstablishedGrPeersCount();
  } else {
    updateNonGracefulCounters(peerAddr, true /* isTerminated */);
  }
  adjRib->logPeerEvent("SHUTDOWN_COMPLETE", BGP_LOG_SRC());

  if (evt.peerDelete || cleanupVipPeer) {
    XLOGF(
        INFO,
        "sessionTerminated for {}: peerDelete={}, remoteAs={}, running "
        "cleanupPeerState",
        peerId.str(),
        evt.peerDelete,
        remoteAs);
    co_await cleanupPeerState(peerId, peerAddr);
  }
  co_return;
}

void PeerManagerBase::maybeFinishRibInitialPathComputation(
    VrfId vrfId) noexcept {
  // TODO_GR_VRF: Wait for every VRF once the GR state file covers them.
  if (vrfId != fboss::kDefaultVrf) {
    return;
  }

  /*
   * Remove the GR state file only now, once the initial RIB computation of
   * the default VRF has been triggered, and not when it is read at startup.
   *
   * Despite its name, the file carries no Graceful Restart state that BGP++
   * relies on: it does not track whether peers are still in GR helper mode or
   * still hold our routes. Its only use is at startup, to decide which peers
   * to wait for EoR from before computing the RIB: the peers that were
   * Established and GR capable at the last clean shutdown, instead of every
   * configured static peer (see addPeersToSessionMgr()).
   *
   * That wait list stays valid until the initial RIB computation. If bgpd
   * restarts before this point, e.g. back-to-back restarts a few seconds apart
   * or a crash during startup, the next start must find the same file.
   * Otherwise it falls back to waiting for EoR from every configured static
   * peer, including peers that cannot come up (drained or down), until
   * eor_time_s expires. After this point the list is no longer needed, and a
   * clean shutdown writes a new one in saveGrState().
   */
  std::remove(FLAGS_gr_state_file.c_str());

  /*
   * Once we've notified RIB to program FIB, we are not in restarting mode
   * anymore.  Let sessionMgr know about this so that it can set GR
   * Capability accordingly
   */
  sessionMgr_->setRestartingState(false);
}

void PeerManagerBase::processTriggerSafeMode() noexcept {
  XLOG(INFO, "Process TriggerSafeMode message");
  // write the safemode file
  try {
    XLOGF(DBG1, "Writing safemode file {}", FLAGS_safemode_file);
    folly::writeFileAtomic(FLAGS_safemode_file, "");
  } catch (const std::exception& ex) {
    XLOGF(
        ERR,
        "Could not create safemode file {}. Exception: {}",
        FLAGS_safemode_file,
        folly::exceptionStr(ex));
  }
  // Schedule a coro task to re-evaluate AdjRibs
  asyncScope_.add(co_withExecutor(
      &evb_, startAdjRibReEvaluationRoutine(RibPauseResumeCause::SAFE_MODE)));
}

/**
 * @brief Creates peering parameters for thrift stream subscribers.
 *
 * This function constructs a PeeringParams object with default values
 * suitable for stream-based BGP route subscribers. These subscribers
 * receive route updates via thrift streaming rather than actual BGP
 * protocol sessions.
 *
 * The peer address is set to localhost (::1)
 */
std::pair<folly::IPAddress, std::unique_ptr<PeeringParams>>
PeerManagerBase::getStreamPeeringParams() {
  // Use localhost as the peer address for stream subscribers
  auto peerAddr = folly::IPAddress("::1");
  auto peeringParams = std::make_unique<PeeringParams>(fboss::kDefaultVrf);

  peeringParams->peerAddr = peerAddr;

  /**
   * The following peering parameters are irrelevant for stream subscribers
   * since they don't establish actual BGP sessions
   *
   * peeringParams->peerPrefix, globalAs, localAs, remoteAs, localBgpId,
   * peerPort, nexthopV4, nexthopV6
   */

  /**
   * The following only parameters are explicitly initialized, since they
   * are the only relevant ones
   */
  peeringParams->isRrClient = RrClientConfigured{true};
  peeringParams->isAfiIpv4Configured = AfiIpv4Configured{true};
  peeringParams->isAfiIpv6Configured = AfiIpv6Configured{true};
  peeringParams->isShutdown = false;
  peeringParams->advertiseLinkBandwidth = AdvertiseLinkBandwidth::BEST_PATH;
  peeringParams->addPath = AddPath::SEND; // Enable ADD-PATH for multiple paths

  return std::pair<folly::IPAddress, std::unique_ptr<PeeringParams>>(
      peerAddr, std::move(peeringParams));
}

/**
 * @brief  subscriber state and stats management as result of reset,
 *         mark adjRib terminated
 *
 * @params subscriber  StreamSubscriber handle
 */
void PeerManagerBase::resetSubscriberAdjRib(StreamSubscriber& subscriber) {
  /*
   * End the stream generator of the bounded mode. The server calls this
   * function on a re-subscribe and on a subscriber teardown. Without this
   * request the generator would stay on the old queue and the old stream
   * would stay open.
   */
  if (subscriber.streamCancelSource) {
    subscriber.streamCancelSource->requestCancellation();
  }

  // Sent stop signal to corresponding AdjRib
  subscriber.peerOutputQ->forcePush(FiberBgpPeer::BgpSessionStop{false});

  auto adjRib = findAdjRib(subscriber.peerId);
  if (!adjRib) {
    XLOGF(ERR, "Could not find AdjRib for {}", subscriber.peerId.str());
    return;
  }

  XLOGF(INFO, "Terminating {}", subscriber.peerId.str());

  adjRib->markStateTerminated();
  subscriber.state = TBgpPeerState::IDLE;
  subscriber.numFlaps += 1;
  runningSessions_ -= 1;
  setRunningSessions(runningSessions_);
  addSessionStateChanges();
  /*
   * Make sure to do it after state is set to IDLE
   * Any outstanding messages for this AdjRibOut are ignored only if
   * state had transitioned to IDLE
   */
  adjRib->deactivateChangeListConsumer();

  return;
}

/**
 * @brief  subscriber state and stats management as result of setup,
 *         mark adjRib established
 *
 * @params subscriber  StreamSubscriber handle
 * @params adjRib      subscriber related adjRib
 */
void PeerManagerBase::setSubscriberAdjRib(
    StreamSubscriber& subscriber,
    std::shared_ptr<AdjRib>& adjRib) {
  auto addPathCapa = nettools::bgplib::BgpAddPathSendRec::SEND;
  /*
   * The AdjRib of the subscriber runs the same cycle as the AdjRib of a peer.
   * It writes into peerInputQ. When that queue reaches the high
   * watermark, waitForQueueSpace() cancels the change-list consume timer. The
   * timer starts again when the reader takes the queue below the low
   * watermark.
   */
  /*
   * The AdjRib of a subscriber is never registered with UpdateGroupManager.
   * It keeps the per-peer AdjRibOutGroup that createAdjRib() built with
   * enableUpdateGroup=false. The bounded path sends through
   * sendBgpUpdates(), which would run the group state machine against that
   * non-group. Disable the update group for this AdjRib. Then it uses the
   * per-peer packing path in reschedulePackingTimers().
   */
  adjRib->setEnableUpdateGroup(false);
  adjRib->sessionEstablished(
      std::nullopt, /* GR disabled */
      subscriber.peerOutputQ, /* aka adjRibInQueue */
      subscriber.peerInputQ, /* aka adjRibOutQueue */
      AfiIpv4Negotiated{true}, /* default argument */
      AfiIpv6Negotiated{true}, /* default argument */
      V4OverV6Nexthop{true}, /* isV4OverV6NexthopNegotiated */
      EnhancedRouteRefreshNegotiated{false}, /* Disabled until ERR support is
                                                added for stream subscriber*/
      RouteRefreshNegotiated{
          false}, /* Disabled until RR support is added for stream subscriber */
      addPathCapa);
  adjRib->markStateEstablished();
  runningSessions_ += 1;
  setRunningSessions(runningSessions_);
  addSessionStateChanges();
  adjRib->startMessageProcessingLoop();

  return;
}

/**
 * @brief  This is a lambda function executed when invoked as a
 *         result when subscriber channel is going away
 *
 * @params peerId  bgp formed local identifier for subscriber session
 * @params subscriberName  string name of a subscriber
 * @params publisherId     id of the session that calls this function. The
 *                         function does nothing when the subscriber runs a
 *                         different session.
 */
void PeerManagerBase::cancelSubscriberStream(
    const nettools::bgplib::BgpPeerId& peerId,
    const std::string& subscriberName,
    uint32_t publisherId) {
  auto adjRib = findAdjRib(peerId);
  if (!adjRib) {
    XLOGF(ERR, "Could not find AdjRib for {}", peerId.str());
    return;
  }
  if (!streamSubscribers_.contains(subscriberName)) {
    XLOGF(ERR, "Could not find subscriber info for {}", subscriberName);
    return;
  }
  auto& subscriber = streamSubscribers_.at(subscriberName);
  if (subscriber.state != TBgpPeerState::ESTABLISHED) {
    XLOGF(INFO, "subscriber {} is not in ESTABLISHED state", subscriberName);
    return;
  }

  /*
   * Only the session that runs now can end the subscriber. An id that differs
   * belongs to an earlier session, or to a stream whose subscribe() call took
   * an id and then failed before it started the session.
   */
  if (publisherId != subscriber.publisherId) {
    XLOGF(
        INFO,
        "subscriber {} runs session {}, so the teardown of session {} does nothing",
        subscriberName,
        subscriber.publisherId,
        publisherId);
    return;
  }

  resetSubscriberAdjRib(subscriber);
}

/**
 * Number of established subscriber sessions.
 *
 * @return Number of established subscriber sessions.
 */
uint32_t PeerManagerBase::numStreamSubscribers(void) {
  uint32_t establishedSessions = 0;
  for (const auto& [subscriberName, subscriber] : streamSubscribers_) {
    if (subscriber.state == TBgpPeerState::ESTABLISHED) {
      establishedSessions++;
    }
  }
  return establishedSessions;
}

/**
 * Checks if the number of established stream peers exceeds the stream
 * susbscriber limit.
 *
 * @return true if the number of established stream peers exceeds the stream
 * subscriber limit, false otherwise.
 */
bool PeerManagerBase::exceedsStreamSubscriberLimit() {
  auto globalConfig = configManager_->getConfig()->getBgpGlobalConfig();

  // If subscriber limit is not set, treat limit as infinite.
  if (!globalConfig->streamSubscriberLimit.has_value()) {
    return false;
  }
  return numStreamSubscribers() >= globalConfig->streamSubscriberLimit;
}

StreamSubscriber* FOLLY_NULLABLE
PeerManagerBase::getStreamSubscriber(const std::string& subscriberName) {
  auto it = streamSubscribers_.find(subscriberName);
  return it == streamSubscribers_.end() ? nullptr : &it->second;
}

apache::thrift::ServerStream<TBgpRouteDelta> PeerManagerBase::subscribe(
    const std::unique_ptr<std::string>& subscriberName_p) {
  std::unique_ptr<apache::thrift::ServerStream<TBgpRouteDelta>> resultStream{
      nullptr};
  bool failedAlready = false;
  std::string exceptionMsg("Undefined exception");
  auto subscriberName = *subscriberName_p;

  XLOGF(INFO, "Received stream subscribe request from {}", subscriberName);

  /*
   * evb_ owns streamSubscribers_. reportStreamSubscriberBackpressureStats()
   * iterates the map on evb_ one time in each thrift_stream_publish_gap_ms
   * period.
   * cancelSubscriberStream() also runs only on evb_.
   *
   * subscribe() runs on a thread of the thrift server. Therefore its lookups,
   * its emplace, and its increment of lastStreamPeerId_ must also run on
   * evb_. An emplace can rehash the map. A rehash during the iteration in
   * reportStreamSubscriberBackpressureStats() makes the iterator of that loop
   * invalid.
   *
   * No code erases an entry from streamSubscribers_, and F14NodeMap keeps a
   * reference to an element valid across a rehash. Therefore the pointer that
   * this function takes stays valid after the hop returns. The evb_ block
   * below writes through the same reference and depends on the same rule.
   */
  StreamSubscriber* subscriberPtr = nullptr;
  uint32_t publisherId = 0;

  evb_.runInEventBaseThreadAndWait([&]() {
    if (!streamPeeringParams_) {
      exceptionMsg =
          "Subscription failed: stream peering params not initialized";
      failedAlready = true;
      return;
    }
    /*
     * The limit guards new slots. A request that already has an ESTABLISHED
     * entry under this name never needs one: it either takes over its own
     * session (same address, see below) or is rejected as a duplicate. The
     * test mirrors numStreamSubscribers(), which counts ESTABLISHED entries,
     * so the exemption and the count cannot drift apart.
     *
     * Skipping the limit here also lets the duplicate case report the accurate
     * error. Checking the limit first would mask it with "Max stream
     * subscribers reached", pointing the operator at capacity when the real
     * problem is two hosts sharing a subscriber name.
     *
     * Without the exemption a network blackhole locks the subscriber out:
     * nothing tells the peer manager the client is gone, so the stale entry
     * stays ESTABLISHED and keeps counting until
     * reclaimIdleStreamSubscriber() fires, up to
     * stream_subscriber_idle_timeout_ms later.
     */
    const auto existingSubscriber = streamSubscribers_.find(subscriberName);
    const bool hasEstablishedEntry =
        existingSubscriber != streamSubscribers_.end() &&
        existingSubscriber->second.state == TBgpPeerState::ESTABLISHED;

    if (!hasEstablishedEntry && exceedsStreamSubscriberLimit()) {
      XLOGF(
          INFO,
          "Max stream subscribers reached: Rejecting connection from {}",
          subscriberName);
      exceptionMsg = "Subscription failed: Max stream subscribers reached";
      failedAlready = true;
      BgpStats::incStreamingSessionsRejected();
      return;
    }

    if (streamSubscribers_.contains(subscriberName)) {
      auto& existing = streamSubscribers_.at(subscriberName);
      if (existing.state != TBgpPeerState::IDLE) {
        XLOGF(
            INFO,
            "subscription already exists in non IDLE state for peer {}",
            existing.peerId.peerAddr.str());
        if (existing.peerId.peerAddr != streamPeerAddr_) {
          XLOGF(
              INFO,
              "duplicate subscription request for {}",
              streamPeerAddr_.str());
          exceptionMsg =
              "Subscription failed: duplicate subscription attempted";
          failedAlready = true;
          return;
        }
        /**
         * Already on evb_, so this runs inline. The baton in the code below
         * confirms only execution of BgpSessionStop message. It
         * does not confirm resetSubscriberAdjRib() is fully
         * executed, and hence this must complete before the hop returns
         */
        resetSubscriberAdjRib(existing);
        /*
         * The generator ends itself when it sees the cancellation that
         * resetSubscriberAdjRib() requested above.
         *
         * One case is slower: a generator that is suspended at its co_yield.
         * That happens when the client of the subscriber stops to give stream
         * credit. Thrift resumes such a generator only on new credit or on a
         * disconnect of the client, so the generator sees the cancellation
         * only at that time. The previous stream can thus outlive this
         * re-subscribe. The result stays correct. The session-id test in
         * cancelSubscriberStream() makes the late teardown harmless, and
         * F14NodeMap keeps the StreamSubscriber reference stable. The old
         * generator and its queue stay in memory until the client goes away,
         * and that queue is bounded.
         */
      }
    }

    if (!streamSubscribers_.contains(subscriberName)) {
      uint32_t remoteBgpId = ++lastStreamPeerId_;
      const BgpPeerId newPeerId{streamPeerAddr_, remoteBgpId};
      XLOGF(
          INFO,
          "Assign peerId {} to subscriber {}",
          newPeerId.str(),
          subscriberName);
      StreamSubscriber newSubscriber(newPeerId);
      streamSubscribers_.emplace(subscriberName, std::move(newSubscriber));
    }

    subscriberPtr = &streamSubscribers_.at(subscriberName);

    /*
     * Take the id of this session here, in the same evb_ hop that finds the
     * entry. evb_ owns streamSubscribers_, so the increment and the read make
     * one atomic step against any other call of subscribe().
     *
     * The allocation must not move out of this hop. subscribe() runs on a
     * thread of the thrift server and it leaves evb_ before the hop that
     * starts the session. An increment in that later hop and a read here are
     * two steps. Then two calls of subscribe() for one subscriber name can
     * read the same value and can build two generators that carry the same
     * id. The newer of the two streams could then never end its subscriber,
     * and the subscriber would stay ESTABLISHED, would keep its AdjRib, and
     * would hold a slot of streamSubscriberLimit for the life of the daemon.
     */
    publisherId = ++subscriberPtr->nextPublisherId;
  });

  /*
   * The hop sets subscriberPtr on every path that leaves failedAlready false.
   * Both are tested here so the dereference below has a branch in front of it.
   */
  if (failedAlready || subscriberPtr == nullptr) {
    XLOGF(ERR, "{}", exceptionMsg);
    throw TBgpServiceException(exceptionMsg);
  }

  auto& subscriber = *subscriberPtr;

  auto peerId = subscriber.peerId;

  /**
   * If there already is an existing adjRib that means 2 possibilities
   * 1) Either previous channel has already been closed by registered
   *    lambda call-back
   * 2) Or in this call, we found adjrib non-idle and forcefully
   *    completed channel in earlier blocking
   *
   * In both the cases, baton has been posted and thus for the case
   * of existing adjrib, we can wait here for the baton.
   *
   * The baton has latch semantics: it stays posted between post() and
   * reset(), so multiple waiters pass through without consuming the signal.
   * The baton is reset only when we commit to starting new message loops.
   *
   * NOTE: Waiting for baton inside runInEventBaseThreadAndWait() can
   *       freeze evb_ from running any other peer-manager events.
   *       This has occured before, and thus an important note for
   *       the future reader
   */
  std::shared_ptr<AdjRib> adjRib = nullptr;
  evb_.runInEventBaseThreadAndWait([&]() { adjRib = findAdjRib(peerId); });

  if (adjRib) {
    auto terminateBaton = sessionTerminateBatons_[peerId];
    XLOGF(INFO, "Pre-baton wait for {}", peerId.str());
    auto startTime = std::chrono::steady_clock::now();
    /*
     * Baton has latch semantics: if already posted, passes through
     * immediately.
     */
    folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
      co_await *terminateBaton;
      co_await adjRib->ensureAsyncScopeInitialized();
    }());
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime);
    XLOGF(INFO, "Post-baton wait for {}", peerId.str());
    XLOGF(
        INFO,
        "Peer Manager blocked waiting for baton for {}",
        duration.count());
  }

  /**
   * It is necessary to run this block of the code to the completion
   * before returning from this function call. And since it has to
   * be run in the context of peer-manager thread, use of evb_ and
   * blocking call.
   */
  evb_.runInEventBaseThreadAndWait([&]() {
    if (!adjRib) {
      auto peeringParams = *streamPeeringParams_;
      peeringParams.description = subscriberName;
      adjRib = createAdjRib(defaultVrf_, peerId, peeringParams);
      auto changeListConsumer = std::make_shared<AdjRibOutConsumer>(
          defaultVrf_.getChangeListTracker(),
          adjRib,
          "ChangeList Consumer",
          evb_,
          defaultVrf_.getAddPathConsumerBitmap(),
          defaultVrf_.getNonAddPathConsumerBitmap());
      adjRib->setChangeListConsumer(changeListConsumer);
    }

    if (!defaultVrf_.isRibInitialAnnouncementStarted()) {
      adjRib->setInInitialAnnouncement();
    } else {
      adjRib->resetInInitialAnnouncement();
    }

    /*
     * The AdjRib of the previous session closed these queues at its
     * teardown, and a closed MPMCWatermarkQueue drops a push without an
     * error. Therefore a re-subscribe must give the AdjRib new queues. The
     * terminate baton above already confirmed that the old session ended,
     * so it is safe to replace the queues here.
     */
    subscriber.resetQueues();
    subscriber.streamCancelSource =
        std::make_shared<folly::CancellationSource>();
    subscriber.state = TBgpPeerState::ESTABLISHED;
    subscriber.upSince = std::chrono::steady_clock::now();

    /*
     * This session starts now, so its id becomes the id of the subscriber.
     * This hop runs on evb_, and a later call of subscribe() replaces the
     * value from its own hop. Then cancelSubscriberStream() lets only the
     * newest session end the subscriber.
     */
    subscriber.publisherId = publisherId;

    if (!adjRib->inInitialAnnouncement()) {
      // Request RIB for full dump
      asyncScope_.add(co_withExecutor(
          &evb_,
          processRibDumpReqCoro(RibDumpReq(peerId, true /* sendAddPath */))));
    }

    setSubscriberAdjRib(subscriber, adjRib);

    /*
     * This code creates the generator but does not start it. Thrift
     * schedules the generator on its own server executor after this
     * function returns the stream to the client.
     */
    resultStream =
        std::make_unique<apache::thrift::ServerStream<TBgpRouteDelta>>(
            subscriberStreamGenerator(
                subscriber.peerInputQ,
                subscriber.streamCancelSource->getToken(),
                subscriber.sessionState,
                peerId,
                subscriberName,
                SubscriberStreamTeardown(
                    this, peerId, subscriberName, publisherId)));
    XLOGF(INFO, "Start publishing to peer {}", peerId.str());
  });

  if (!resultStream) {
    XLOGF(ERR, "{}", exceptionMsg);
    throw TBgpServiceException(exceptionMsg);
  }
  // Return the stream
  return std::move(*resultStream);
}

/**
 * @brief  Find AdjRibOutGroup given a group name
 *
 * @params groupName  Name of the group to be looked up
 *
 * @return std::shared_ptr<AdjRibOutGroup>
 */
std::shared_ptr<AdjRibOutGroup> PeerManagerBase::findAdjRibOutGroup(
    const std::string& groupName) noexcept {
  auto match = adjRibOutGroups_.find(groupName);
  if (match == adjRibOutGroups_.cend()) {
    return nullptr;
  }

  return match->second;
}

/**
 * @brief  Create AdjRibOutGroup given a group name
 *
 * @params groupName  Name of the group to be created
 *
 * @return std::shared_ptr<AdjRibOutGroup>
 */
std::shared_ptr<AdjRibOutGroup> PeerManagerBase::createAdjRibOutGroup(
    PeerManagerVrf& vrf,
    const std::string& groupName) noexcept {
  std::shared_ptr<AdjRibOutGroup> adjRibOutGroup;

  XLOGF(INFO, "Create Adjacency Out Group: {}", groupName);
  /*
   * Wire the shadow RIB view so peers on this group (the update-group-disabled
   * path) can read maxRibVersion after consuming to the end of the change
   * list, the same way UpdateGroupManager-created groups do.
   */
  adjRibOutGroup = std::make_shared<AdjRibOutGroup>(
      evb_,
      vrf.vrfId(),
      groupName,
      0 /* groupId */,
      false /* enableUpdateGroup */,
      UpdateGroupKey{.vrfId = vrf.vrfId()},
      vrf.getShadowRibView());
  adjRibOutGroups_.emplace(groupName, adjRibOutGroup);
  BgpStats::incrAdjRibOutGroupsCount();
  return adjRibOutGroup;
}

std::shared_ptr<AdjRib> PeerManagerBase::createAdjRib(
    PeerManagerVrf& vrf,
    const BgpPeerId& peerId,
    const PeeringParams& peeringParams) noexcept {
  std::shared_ptr<AdjRib> adjRib;
  auto sessionTerminateBaton = std::make_shared<folly::coro::Baton>();
  sessionTerminateBatons_.insert_or_assign(peerId, sessionTerminateBaton);

  const auto& peerAddr = peerId.peerAddr;

  auto peerConfig = configManager_->getConfig()->getConfigOfAPeer(peerAddr);

  auto outGroupName =
      PeerManagerVrf::buildAdjRibOutGroupName(peerId, peeringParams);
  auto adjRibOutGroup = findAdjRibOutGroup(outGroupName);
  if (!adjRibOutGroup) {
    adjRibOutGroup = createAdjRibOutGroup(vrf, outGroupName);
  }

  adjRib = std::make_shared<AdjRib>(
      peerId,
      peeringParams,
      evb_,
      ribInQ_, /* write to ribInQ */
      fromAdjRibQ_, /* write to fromAdjRibQ */
      sessionTerminateBaton,
      policyManager_,
      isSafeModeOn_,
      peerConfig ? peerConfig->ingressPolicyName : std::nullopt,
      peerConfig ? peerConfig->egressPolicyName : std::nullopt,
      adjRibOutGroup,
      peerConfig ? peerConfig->outDelay : std::nullopt,
      configManager_);

  monitorModule(peerId.toOdsKey(), *adjRib);

  if (adjRibs_.emplace(peerId, adjRib).second) {
    RibStats::incrAdjRibCount();
  }
  return adjRib;
}

PeerManagerVrf& PeerManagerBase::createVrf(VrfId vrfId, bool publishStats) {
  auto& vrf = peerManagerVrfs_[vrfId];
  if (!vrf) {
    vrf = std::make_unique<PeerManagerVrf>(
        vrfId, *this, asyncScope_, publishStats);
  }
  return *vrf;
}

PeerManagerVrf* FOLLY_NULLABLE PeerManagerBase::findVrf(VrfId vrfId) noexcept {
  auto match = peerManagerVrfs_.find(vrfId);
  if (match == peerManagerVrfs_.cend()) {
    return nullptr;
  }
  return match->second.get();
}

std::shared_ptr<AdjRib> PeerManagerBase::findAdjRib(
    const BgpPeerId& peerId) noexcept {
  auto match = adjRibs_.find(peerId);
  if (match == adjRibs_.cend()) {
    return nullptr;
  } else {
    return match->second;
  }
}

/**
 * @brief Determine if this is a dynamic peer.
 *
 * @param peerAddr The IP address of the peer.
 *
 * @return True if the peer is dynamic, false otherwise.
 */
bool PeerManagerBase::isPeerDynamic(const folly::IPAddress& peerAddr) {
  auto dynamicPeerToConfigs =
      configManager_->getConfig()->getDynamicPeerToConfig();

  for (const auto& [peerPrefix, peerConfigPtr] : dynamicPeerToConfigs) {
    if (peerAddr.inSubnet(peerPrefix.first, peerPrefix.second)) {
      return true;
    }
  }

  return false;
}

bool PeerManagerBase::isDynamicVipPeer(
    const folly::IPAddress& peerAddr,
    uint32_t remoteAs) {
  return remoteAs == kVipAsn && isPeerDynamic(peerAddr);
}

folly::coro::Task<void> PeerManagerBase::co_clearCounters(
    const std::vector<folly::IPAddress>& peers) {
  const bool clearAll = peers.empty();
  const folly::F14FastSet<folly::IPAddress> filter(peers.begin(), peers.end());
  co_await folly::coro::co_withExecutor(
      &getEventBase(), [this, clearAll, filter]() -> folly::coro::Task<void> {
        for (auto& [peerId, adjRib] : adjRibs_) {
          if (!clearAll && !filter.contains(peerId.peerAddr)) {
            continue;
          }
          adjRib->clearEgressMessageCounts();
          adjRib->clearIngressMessageCounts();
          PeerStats::clearPeerEgressMessageCounters(
              adjRib->getStats().getPeerIdOdsStr());
          PeerStats::clearPeerIngressMessageCounters(
              adjRib->getStats().getPeerIdOdsStr());
        }
        co_return;
      }());
}

folly::coro::Task<void> PeerManagerBase::co_updateRswSlbInRejectionCounter() {
  constexpr auto kRswSlbInPolicyName = "PROPAGATE_RSW_SLB_IN";
  static const re2::RE2 policyFilter{fmt::format(
      "^(Denied by {} term |"
      "Did not match any policy term in {}[.] Default denied$)",
      kRswSlbInPolicyName,
      kRswSlbInPolicyName)};
  RejectedNetworksQuery query;
  query.direction = TDirectionFilter::INGRESS;
  query.policyNameFilter = &policyFilter;
  query.maxEntries = 0;
  query.excludeStale = true;

  std::vector<BgpPeerId> peerIds;
  for (const auto& [peerId, adjRib] : adjRibs_) {
    if (adjRib->getIngressPolicyName() == kRswSlbInPolicyName) {
      peerIds.push_back(peerId);
    }
  }

  int64_t rejections = 0;
  for (const auto& peerId : peerIds) {
    co_await folly::coro::co_safe_point;
    co_await folly::coro::co_reschedule_on_current_executor;
    if (daemonShutdown_) {
      co_return;
    }
    /*
     * Re-resolve after yielding: the peer may have gone away or changed policy.
     */
    const auto it = adjRibs_.find(peerId);
    if (it == adjRibs_.end() ||
        it->second->getIngressPolicyName() != kRswSlbInPolicyName ||
        it->second->isPeerGracefulRestarting()) {
      continue;
    }
    RejectedNetworks rejected;
    it->second->getRejectedNetworks(rejected, query);
    for (const auto& [policyName, count] : rejected.pathCountByPolicy) {
      rejections += count;
    }
  }
  PeerStats::setNumRswSlbInRejections(rejections);
}

folly::coro::Task<void> PeerManagerBase::updatePeerCounters() {
  if (daemonShutdown_) {
    co_return;
  }
  auto sessions = co_await sessionMgr_->co_getAllEstablishedPeerDisplayInfo();
  co_await co_updateRswSlbInRejectionCounter();
  uint32_t establishedNoRoutePeers = 0;

  for (const auto& sessionInfo : sessions) {
    const auto& peerAddr = sessionInfo.peeringParams.peerAddr;
    BgpPeerId bgpPeerId{peerAddr, sessionInfo.remoteBgpId};
    auto it = adjRibs_.find(bgpPeerId);
    if (it != adjRibs_.end()) {
      const auto stats = it->second->getStats();
      /*
       * A peer is exchanging routes if it received prefixes (ingress) or is
       * being advertised prefixes (egress). getEffectivePostOutPrefixCount()
       * hides the update-group split: it returns the group-level advertised
       * count for an in-sync group member (whose per-peer count is cleared by
       * markPeerInSync) and the per-peer count otherwise, so this check is
       * correct whether or not update-group is enabled.
       */
      if (stats.getPreInPrefixCount() > 0 ||
          it->second->getEffectivePostOutPrefixCount() > 0) {
        continue;
      }
      /*
       * VIP injector sessions can be in established state without any
       * routes.
       */
      if (isPeerDynamic(peerAddr)) {
        continue;
      }
    }
    // Did not find any advertised prefixes for this peer
    XLOGF(
        WARN, "{} is established but no routes are exchanged", peerAddr.str());
    establishedNoRoutePeers++;
  }
  PeerStats::setTotalPeerWithNoRouteExchange(establishedNoRoutePeers);
}

void PeerManagerBase::setSessionManager(
    std::shared_ptr<SessionManager> sessionManager) {
  if (sessionManager != nullptr) {
    if (sessionMgr_) {
      stopMonitoring(kModuleSessionManager);
    }

    sessionMgr_ = std::move(sessionManager);

    /*
     * Start monitoring of FiberBgpPeerManager
     * TODO: move this to Main.cpp once FiberBgpPeerManager is running
     * separately
     */
    monitorModule(kModuleSessionManager, *sessionMgr_);
  }
}

void PeerManagerBase::updateEntryStats(
    TEntryStats& stats,
    VrfId vrfId) noexcept {
  stats.total_adj_ribs() = adjRibs_.size();
  auto* vrf = findVrf(vrfId);
  if (!vrf) {
    XLOGF(
        ERR,
        "Skipping the shadow RIB entry count in updateEntryStats due to non-existent VRF {}",
        vrfId);
    return;
  }
  stats.total_shadow_rib_entries() = vrf->getShadowRibEntries().size();
}

void PeerManagerBase::saveGrState() {
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([this] {
    std::ofstream grFile;

    if (!configManager_->getConfig()->getBgpGlobalConfig()->supportStatefulGr) {
      XLOG(INFO, "Stateful GR disabled");
      return;
    }

    /*
     * We will save only once. We need to save before sessions are brought down,
     * For SIGTERM (bgp updation) this method will be called only once.
     * For call to restartSessionsAndExit (agent updation) this method will be
     * called twice, once before sessions go down and once after SIGTERM is
     * raised. Ignoring the 2nd call.
     */
    if (grStateSaved_) {
      XLOG(INFO, "GR state file was already saved");
      return;
    }
    grStateSaved_ = true;
    /*
     * If we have not yet notified RIB about EOR and we are trying to
     * terminate, there is no need to store GR state information.
     *
     * TODO_GR_VRF: The file only covers the default VRF. Save the GR state of
     * every VRF.
     */
    if (!defaultVrf_.isRibInitPathComputationNotified()) {
      XLOG(
          INFO,
          "Daemon restarting before reaching stable state. Not saving GR state");
      return;
    }

    grFile.open(FLAGS_gr_state_file, std::ios::out | std::ios::trunc);
    if (!grFile.is_open()) {
      XLOG(ERR, "Could not open GR state file for writing");
      return;
    }

    const auto nowInSec =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    // Start with epoch time
    grFile << nowInSec << "\n";

    // All the established GR peers
    for (const auto& peerId : establishedGrPeers_) {
      grFile << peerId.peerAddr.str() << " " << peerId.remoteBgpId << "\n";
    }

    // Sign termination (signature) to indicate properly terminated.
    grFile << kGrStateFileTermination;
    grFile.close();
    XLOGF(INFO, "Saved GR state to {}", FLAGS_gr_state_file);
  });
}

GrLoadResult PeerManagerBase::readGrState() const noexcept {
  std::string line;
  std::vector<std::string> lines;
  std::ifstream grFile;

  if (!configManager_->getConfig()->getBgpGlobalConfig()->supportStatefulGr) {
    XLOG(INFO, "Stateful GR disabled");
    return GrLoadResult::NotLoaded();
  }

  grFile.open(FLAGS_gr_state_file);
  if (!grFile.is_open()) {
    XLOGF(
        INFO,
        "Could not open GR state file {} for reading",
        FLAGS_gr_state_file);
    return GrLoadResult::NotLoaded();
  }

  const auto nowInSec = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();

  while (getline(grFile, line)) {
    lines.emplace_back(line);
  }
  grFile.close();

  if (lines.size() < 2) {
    XLOGF(ERR, "Invalid lines size {}", lines.size());
    return GrLoadResult::NotLoaded();
  }

  if (lines.back() != kGrStateFileTermination) {
    XLOGF(ERR, "Invalid last line: {}", lines.back());
    return GrLoadResult::NotLoaded();
  }

  auto storeTimeInSec = std::stol(lines[0], nullptr, 10);
  if ((nowInSec < storeTimeInSec) ||
      ((nowInSec - storeTimeInSec) > FLAGS_validity_time_stateful_gr_s)) {
    XLOG(INFO, "Stale GR state file for reading");
    return GrLoadResult::NotLoaded();
  }

  auto establishedPeers = std::make_unique<std::unordered_set<BgpPeerId>>();

  // Remove header time stamp and tail signature
  lines.pop_back();
  lines.erase(lines.begin());

  if (lines.size() == 0) {
    XLOG(WARN, "GR state file contains no peers");
  }

  for (const auto& peerAddrStr : lines) {
    std::vector<std::string> peerElems;
    folly::split(' ', peerAddrStr, peerElems);

    if (peerElems.size() != 2) {
      // We can not trust this file
      XLOG(ERR, "GR state file has invalid peerId details");
      return GrLoadResult::NotLoaded();
    }

    if (!folly::IPAddress::validate(peerElems[0])) {
      // We can not trust this file
      XLOGF(ERR, "GR state file has invalid IP address {}", peerElems[0]);
      return GrLoadResult::NotLoaded();
    }

    try {
      establishedPeers->insert(BgpPeerId(
          folly::IPAddress(peerElems[0]),
          boost::lexical_cast<uint32_t>(peerElems[1])));
      XLOGF(
          INFO,
          "Added peer with addr {} and bgpId {} to list of established peers.",
          peerElems[0],
          peerElems[1]);
    } catch (const boost::bad_lexical_cast& e) {
      XLOGF(
          ERR,
          "GR state file has invalid remoteBgpId for peer addr {} and bgpId {} Error {}",
          peerElems[0],
          peerElems[1],
          e.what());
    }
  }

  XLOG(INFO, "GR state read successfully");
  return GrLoadResult{
      .loaded = true,
      .peers = std::move(establishedPeers),
  };
}

void PeerManagerBase::applyRouteFilterPolicy(
    std::unique_ptr<RouteFilterPolicy> policy,
    bool forceUpdate,
    bool applyGoldenPrefixPolicy) noexcept {
  if (!forceUpdate && routeFilterPolicy_ && policy) {
    if (routeFilterPolicy_->getVersion() > policy->getVersion()) {
      XLOGF(
          WARNING,
          "Route filter policy update ignored: version {} < {}",
          policy->getVersion(),
          routeFilterPolicy_->getVersion());
      return;
    }
    if (*routeFilterPolicy_ == *policy) {
      XLOG_EVERY_MS(INFO, 60000)
          << "Identical route filter policy update ignored";
      return;
    }
  }

  routeFilterPolicy_ = std::move(policy);

  // Mark affected adjRibs and count changes
  size_t ingressAffectedCount = 0;
  size_t egressAffectedCount = 0;

  for (auto& [peerId, adjRib] : adjRibs_) {
    auto [ingressChanged, egressChanged] = setRouteFilterStatement(adjRib);

    /**
     * Mark adjRib as pending policy update if new policy changed in the
     * ingress or egress.
     *
     * These flags will be cleared after policy re-evaluation:
     * - Ingress flag: cleared in processAdjRibReEvaluationTask() after
     *  calling adjRib->processAdjRibReEvaluation()
     * - Egress flag: cleared in
     *  processRibDumpReqForEgressPolicyUpdate() after a per-peer dump, or by
     *  the update-group policy re-evaluation for in-sync peers.
     *
     */

    adjRib->setPendingIngressPolicyUpdate(ingressChanged);
    adjRib->setPendingEgressPolicyUpdate(egressChanged);

    // Collect stats for number of adjRibs affected by the policy change
    if (adjRib->isPendingIngressPolicyUpdate()) {
      ingressAffectedCount++;
    }
    if (adjRib->isEgressPolicyUpdateRequired()) {
      egressAffectedCount++;
    }
    /*
     * clearIngressEgressRouteFiltersPolicy reuses this method to clear route
     * filters but must not touch the golden prefix policy, so it opts out.
     */
    if (applyGoldenPrefixPolicy) {
      setGoldenPrefixPolicy(adjRib);
    }
  }

  /*
   * Record stats for number of peers affected by the route filter policy
   * change
   */
  BgpStats::setIngressRouteFilterPolicyAffectedPeers(ingressAffectedCount);
  BgpStats::setEgressRouteFilterPolicyAffectedPeers(egressAffectedCount);

  /*
   * Only call processIngressAndEgressRouteFilterUpdate if any adjRib's
   * route filter policy updated
   */
  if (ingressAffectedCount == 0 && egressAffectedCount == 0) {
    XLOG(INFO, "Route filter policy update: no adjRibs affected");
    return;
  }

  XLOGF(
      INFO,
      "Route filter policy update for : {} ingress, {} egress adjRibs",
      ingressAffectedCount,
      egressAffectedCount);

  asyncScope_.add(co_withExecutor(
      &evb_,
      processIngressAndEgressRouteFilterUpdate(
          ingressAffectedCount, egressAffectedCount)));
}

void PeerManagerBase::setRouteFilterPolicy(
    std::unique_ptr<RouteFilterPolicy> policy,
    bool forceUpdate) noexcept {
  evb_.runInEventBaseThread(
      [policy = std::move(policy), forceUpdate, this]() mutable {
        applyRouteFilterPolicy(std::move(policy), forceUpdate);
      });
}

/*
 * Clears every peer's ingress and egress route-filter statements.
 *
 * Non-update-group case:
 *   - Clear each adjRib's statement directly, then issue a per-peer rib dump
 *     (processRibDumpReqCoro) for peers past initial announcement.
 *   - routeFilterPolicy_ is left intact.
 *
 * Update-group case:
 *   - Route through applyRouteFilterPolicy to re-evaluate ingress filters.
 *     Egress route filters are unsupported and require no group re-evaluation.
 *     applyGoldenPrefixPolicy=false so this only clears route filters.
 *
 * Difference:
 *   - Non-UG: any clear issues a rib dump.
 *   - Non-UG leaves the global routeFilterPolicy_ intact; UG nulls it, so the
 *     clear persists for peers that connect afterwards.
 */
void PeerManagerBase::clearIngressEgressRouteFiltersPolicy() noexcept {
  evb_.runInEventBaseThread([this]() mutable {
    if (enableUpdateGroup_) {
      applyRouteFilterPolicy(
          nullptr, /*forceUpdate=*/false, /*applyGoldenPrefixPolicy=*/false);
      return;
    }

    std::vector<BgpPeerId> affectedAdjRibs;
    affectedAdjRibs.reserve(adjRibs_.size());
    for (auto& [peerId, adjRib] : adjRibs_) {
      adjRib->setRouteFilterStatement(nullptr);
      affectedAdjRibs.emplace_back(peerId);
    }
    // send RibDumpReq for affectedAdjRibs
    for (const auto& peerId : affectedAdjRibs) {
      const auto& adjRib = adjRibs_[peerId];
      if (!adjRib->inInitialAnnouncement()) {
        asyncScope_.add(co_withExecutor(
            &evb_,
            processRibDumpReqCoro(RibDumpReq(peerId, adjRib->sendAddPath()))));
      }
    }
  });
}

void PeerManagerBase::clearGoldenPrefixesPolicy() noexcept {
  goldenPrefixesPolicyActive_ = false;
  evb_.runInEventBaseThread([this]() mutable {
    std::vector<BgpPeerId> affectedAdjRibs;
    for (auto& [peerId, adjRib] : adjRibs_) {
      adjRib->setGoldenPrefixPolicy(nullptr);
    }
    /*
     * If safe mode is on, because safe mode removes adjrib entries, there
     * is no need to send RibDumpReq for affectedAdjRibs. If safe mode is
     * off, Golden Prefixes policy isn't applied, also needn't send
     * RibDumpReq
     */
  });
}

void PeerManagerBase::updateIngressEgressPolicyNames(
    std::unique_ptr<PeerToPolicyMap> peerToPolicyNames) noexcept {
  if (enableUpdateGroup_) {
    asyncScope_.add(co_withExecutor(
        &evb_,
        updateIngressEgressPolicyNamesForUpdateGroups(
            std::move(peerToPolicyNames))));
    return;
  }

  evb_.runInEventBaseThread([peerToPolicyNames = std::move(peerToPolicyNames),
                             this]() mutable {
    // Query current config version when executing (not when posting)
    auto currentVersion = configManager_->getConfigVersion();

    /*
     * Skip stale updates - if version hasn't changed since last applied,
     * another update with newer config has already been processed
     */
    if (currentVersion <= lastAppliedPolicyVersion_) {
      XLOGF(
          INFO,
          "Skipping stale policy update: config version {} <= last applied {}",
          currentVersion,
          lastAppliedPolicyVersion_);
      return;
    }

    // Mark affected adjRibs and count changes
    size_t ingressAffectedCount = 0;
    size_t egressAffectedCount = 0;

    for (auto& [peerId, adjRib] : adjRibs_) {
      auto [ingressChanged, egressChanged] =
          updateIngressEgressPolicyNamesForAdjRib(adjRib, *peerToPolicyNames);

      /*
       * Set pending ingress and egress policy update flags if
       * ingressChanged or egressChanged
       */
      adjRib->setPendingIngressPolicyUpdate(ingressChanged);
      adjRib->setPendingEgressPolicyUpdate(egressChanged);

      // Collect stats for number of adjRibs affected by the policy change
      if (adjRib->isPendingIngressPolicyUpdate()) {
        ingressAffectedCount++;
      }
      if (adjRib->isEgressPolicyUpdateRequired()) {
        egressAffectedCount++;
      }
    }

    // Record this version as applied
    lastAppliedPolicyVersion_ = currentVersion;

    // Record stats for number of peers affected by the policy change
    BgpStats::setIngressRoutingPolicyAffectedPeers(ingressAffectedCount);
    BgpStats::setEgressRoutingPolicyAffectedPeers(egressAffectedCount);

    /*
     * Only call processIngressAndEgressRouteFilterUpdate if any adjRib's
     * route filter policy updated
     */
    if (ingressAffectedCount == 0 && egressAffectedCount == 0) {
      XLOG(INFO, "Routing policy update: no adjRibs affected");
      return;
    }

    XLOGF(
        INFO,
        "Routing policy update for : {} ingress, {} egress adjRibs",
        ingressAffectedCount,
        egressAffectedCount);

    asyncScope_.add(co_withExecutor(
        &evb_,
        processIngressAndEgressRouteFilterUpdate(
            ingressAffectedCount, egressAffectedCount)));
  });
}

folly::coro::Task<void>
PeerManagerBase::updateIngressEgressPolicyNamesForUpdateGroups(
    std::unique_ptr<PeerToPolicyMap> peerToPolicyNames) {
  // Query current config version when executing (not when posting)
  auto currentVersion = configManager_->getConfigVersion();

  /*
   * Skip stale updates - if version hasn't changed since last applied,
   * another update with newer config has already been processed
   */
  if (currentVersion <= lastAppliedPolicyVersion_) {
    XLOGF(
        INFO,
        "Skipping stale policy update: config version {} <= last applied {}",
        currentVersion,
        lastAppliedPolicyVersion_);
    co_return;
  }

  /*
   * Apply both halves in one pass, before anything suspends, so no peer is
   * ever left carrying one updated name and one stale one. Same loop and same
   * per-peer helper as the non-update-group path.
   */
  size_t ingressAffectedCount = 0;
  size_t egressAffectedCount = 0;
  for (auto& [_, adjRib] : adjRibs_) {
    auto [ingressChanged, egressChanged] =
        updateIngressEgressPolicyNamesForAdjRib(adjRib, *peerToPolicyNames);

    adjRib->setPendingIngressPolicyUpdate(ingressChanged);
    adjRib->setPendingEgressPolicyUpdate(egressChanged);

    if (adjRib->isPendingIngressPolicyUpdate()) {
      ++ingressAffectedCount;
    }
    if (adjRib->isEgressPolicyUpdateRequired()) {
      ++egressAffectedCount;
    }
  }
  BgpStats::setIngressRoutingPolicyAffectedPeers(ingressAffectedCount);
  BgpStats::setEgressRoutingPolicyAffectedPeers(egressAffectedCount);

  /*
   * Stamp once both halves are applied and before anything suspends: an update
   * arriving while the re-evaluations are in flight is gated as stale rather
   * than interleaving with them.
   */
  lastAppliedPolicyVersion_ = currentVersion;

  if (ingressAffectedCount == 0 && egressAffectedCount == 0) {
    XLOG(INFO, "Routing policy update: no adjRibs affected");
    co_return;
  }

  /*
   * Ingress is scheduled rather than awaited, as the non-update-group path
   * does. Awaiting it would order nothing: the pass only enqueues onto
   * ribInQ_, and the RIB recomputes and republishes on its own thread and
   * batching interval well after either ordering has returned.
   */
  if (ingressAffectedCount > 0) {
    XLOGF(
        INFO,
        "Routing policy update: {} ingress adjRibs",
        ingressAffectedCount);
    asyncScope_.add(co_withExecutor(
        &evb_,
        processIngressAndEgressRouteFilterUpdate(
            ingressAffectedCount, /*egressAffectedCount=*/0)));
  }

  if (egressAffectedCount == 0) {
    co_return;
  }

  XLOGF(INFO, "Routing policy update: {} egress adjRibs", egressAffectedCount);
  /*
   * Egress runs inline. Its effect -- unlike ingress -- is synchronous: it
   * rebuilds the affected peers' update group keys and reconciles group
   * membership and RIB-OUT directly, rather than issuing the per-peer
   * RibDumpReqs the non-update-group path uses, which would corrupt the
   * group's shared accounting. Nothing may run between the egress names
   * landing above and that rekey, so it is awaited here; scheduling the
   * ingress pass just above cannot intervene, since add() does not suspend.
   */
  co_await processUpdateGroupsEgressPolicyReevaluation();
}

std::tuple<bool, bool> PeerManagerBase::updateIngressEgressPolicyNamesForAdjRib(
    std::shared_ptr<AdjRib> adjRib,
    const PeerToPolicyMap& peerToPolicyNames) noexcept {
  const auto& peeringParams = adjRib->getPeeringParams();
  const auto& peerAddrStr = peeringParams.peerAddr.str();

  XLOGF(
      DBG1,
      "Processing policy update for peer {} (peerGroup: {})",
      peerAddrStr,
      peeringParams.peerGroupName.value_or("(none)"));

  auto policyIt = peerToPolicyNames.find(peerAddrStr);
  if (policyIt == peerToPolicyNames.end()) {
    XLOGF(DBG3, "No policy update found for peer {}, skipping", peerAddrStr);
    return {false, false};
  }

  return adjRib->updateIngressEgressPolicyNames(policyIt->second);
}

std::tuple<bool, bool> PeerManagerBase::setRouteFilterStatement(
    std::shared_ptr<AdjRib> adjRib) noexcept {
  if (!adjRib) {
    return std::make_tuple(false, false);
  }
  if (!routeFilterPolicy_) {
    return adjRib->setRouteFilterStatement(nullptr);
  }
  for (const auto& [stmtName, stmt] : routeFilterPolicy_->getStatements()) {
    bool isMatch = false;

    // Check if key_type is PEER_GROUP_NAME, match against peer group name
    if (routeFilterPolicy_->matchAgainstPeerGroupName()) {
      const auto& peeringParams = adjRib->getPeeringParams();
      if (peeringParams.peerGroupName.has_value() &&
          stmtName == *peeringParams.peerGroupName) {
        XLOGF(
            DBG2,
            "RouteFilterPolicy: Matched peer group {} for peer {}",
            stmtName,
            adjRib->getPeeringParams().peerAddr.str());
        isMatch = true;
      }
    } else {
      /*
       * If key_type is not present or is DEVICE_REGEX, use existing regex
       * match
       */
      re2::RE2 peerRegex(stmtName);
      if (re2::RE2::FullMatch(
              adjRib->getPeeringParams().description, peerRegex)) {
        XLOGF(
            DBG2,
            "RouteFilterPolicy: Matched peer regex {} for peer {}",
            stmtName,
            adjRib->getPeeringParams().description);
        isMatch = true;
      }
    }

    if (isMatch) {
      auto globalConfig = configManager_->getConfig()->getBgpGlobalConfig();
      if (routeFilterLoggerFactory_ && globalConfig->deviceName) {
        auto logger = routeFilterLoggerFactory_->create(
            *(globalConfig->deviceName),
            stmtName,
            adjRib->getPeeringParams().description);
        return adjRib->setRouteFilterStatement(stmt, std::move(logger));
      }
      return adjRib->setRouteFilterStatement(stmt);
    }
  }
  return adjRib->setRouteFilterStatement(nullptr);
}

bool PeerManagerBase::setGoldenPrefixPolicy(
    std::shared_ptr<AdjRib> adjRib,
    bool initializeAdjRib) noexcept {
  if (!adjRib) {
    return false;
  }
  /*
   * Don't update golden prefix policy if safe mode is on, unless this is a
   * newly initialized AdjRib that doesn't have a policy yet.
   */
  if (adjRib->isSafeModeOn() && !initializeAdjRib) {
    return false;
  }
  if (routeFilterPolicy_) {
    auto goldenPolicy = routeFilterPolicy_->getGoldenPrefixPolicy();
    goldenPrefixesPolicyActive_ = goldenPolicy && switchLimitConfig_ &&
        switchLimitConfig_->overload_protection_mode() ==
            thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY;
    return adjRib->setGoldenPrefixPolicy(std::move(goldenPolicy));
  }
  goldenPrefixesPolicyActive_ = false;
  return adjRib->setGoldenPrefixPolicy(nullptr);
}

bool PeerManagerBase::getIsInitialized() const {
  return defaultVrf_.isInitialized();
}

bool PeerManagerBase::getIsSafeModeOn() const {
  return *isSafeModeOn_;
}

bool PeerManagerBase::getIsGoldenPrefixPolicyActive() const {
  return goldenPrefixesPolicyActive_;
}

void PeerManagerBase::removeSafeModeFile() {
  try {
    if (boost::filesystem::exists(FLAGS_safemode_file)) {
      boost::filesystem::remove(FLAGS_safemode_file);
      XLOGF(INFO, "Previous safe mode file {} removed", FLAGS_safemode_file);
    }
  } catch (const std::exception& ex) {
    XLOGF(
        ERR,
        "Could not remove safe mode file {}. Exception: {}",
        FLAGS_safemode_file,
        folly::exceptionStr(ex));
  }
  return;
}

folly::coro::Task<void> PeerManagerBase::startAdjRibReEvaluationRoutine(
    RibPauseResumeCause cause) {
  ScopedProfile profile("PeerManagerBase::startAdjRibReEvaluationRoutine");
  XLOGF(
      INFO,
      "Starting AdjRib Re-evaluation for {}",
      magic_enum::enum_name(cause));

  /**
   * Instructs the RIB to pause best-path selection and FIB programming
   * during route re-evaluation operations. This optimization prevents
   * expensive repeated computations and reduces FIB churn.
   */
  co_await ribInQ_.push(PauseBestPathAndFibProgramming(cause));

  bool policyUpdate =
      (cause == RibPauseResumeCause::ROUTE_FILTER_POLICY_UPDATE ||
       cause == RibPauseResumeCause::ROUTING_POLICY_UPDATE);

  for (const auto& [peerId, adjRib] : adjRibs_) {
    if (!adjRib) {
      continue;
    }

    /*
     * For policy updates, skip adjRibs without pending
     * updates
     */
    if (policyUpdate && !adjRib->isPendingIngressPolicyUpdate()) {
      continue;
    }

    const auto adjRibStart = std::chrono::steady_clock::now();
    // Process each AdjRib re-evaluation with policy application
    co_await processAdjRibReEvaluationTask(adjRib, cause, policyUpdate);

    // Report per-peer-group processing time for CRF route filter updates
    if (cause == RibPauseResumeCause::ROUTE_FILTER_POLICY_UPDATE) {
      const auto& peerGroupName = adjRib->getPeeringParams().peerGroupName;
      BgpStats::addIngressRouteFilterPeerGroupProcessTimeMs(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - adjRibStart)
              .count(),
          peerGroupName.value_or(peerId.toOdsKey()));
    }
  }

  XLOGF(
      INFO,
      "Completed AdjRib Re-evaluation for {}",
      magic_enum::enum_name(cause));

  // Send a message to RIB to resume best-path selection and FIB programming
  co_await ribInQ_.push(ResumeBestPathAndFibProgramming(cause));
}

folly::coro::Task<void> PeerManagerBase::processAdjRibReEvaluationTask(
    std::shared_ptr<AdjRib> adjRib,
    RibPauseResumeCause cause,
    bool policyUpdate) {
  XLOGF(
      DBG2,
      "Processing AdjRib re-evaluation task for peer {}",
      adjRib->getPeeringParams().peerId);

  // Process AdjRib re-evaluation with policy application
  co_await adjRib->processAdjRibReEvaluation(cause);

  // Clear flag after processing for policy updates
  if (policyUpdate) {
    adjRib->clearPendingIngressPolicyUpdate();
  }

  co_return;
}

folly::coro::Task<void>
PeerManagerBase::startPeriodicPolicyCacheEvictionRoutine() {
  XLOG(INFO, "Starting Periodic Policy Cache Eviction");
  while (true) {
    // when cancelAndJoinAsync is called, loop will be broken to exit
    co_await folly::coro::co_safe_point;

    // sleep a constant time and yield to other coroutines
    co_await folly::coro::sleepReturnEarlyOnCancel(
        periodicPolicyCacheEvictionInterval_);

    // evict stale entries of policy cache
    auto policyCache = AdjRibPolicyCache::get();
    if (policyCache) {
      policyCache->evictFromPolicyCache();
    }
  }
}

folly::coro::Task<void>
PeerManagerBase::startPeriodicUpdatePeerCountersRoutine() {
  XLOG(DBG1, "Start Counter Update Loop");
  while (true) {
    // when cancelAndJoinAsync is called, loop will be broken to exit
    co_await folly::coro::co_safe_point;

    // sleep a constant time and yield to other coroutines
    co_await folly::coro::sleepReturnEarlyOnCancel(
        seconds(FLAGS_counter_update_time_s));

    /*
     * Skip cross-module call to SessionManager after daemon shutdown is
     * marked. SessionManager's evb_ may already be stopped, and
     * co_getAllEstablishedPeerDisplayInfo dispatches via co_withExecutor
     * to that evb_, which would hang forever.
     */
    if (daemonShutdown_) {
      break;
    }

    co_await updatePeerCounters();
  }
}

/**
 * @brief: Publish the current size of each deduplicated attributes
 * collection as ODS counters.
 */
void publishDeduplicatedAttributesSizes() {
  const auto stats = PeerManagerBase::getDeduplicatorStats();
  bgp::BgpStats::setDeduplicatedAttributesTotal(
      stats.bgp_attributes()->entry_count().value());
  bgp::BgpStats::setDeduplicatedAttributesAsPath(
      stats.as_path()->entry_count().value());
  bgp::BgpStats::setDeduplicatedAttributesCommunities(
      stats.communities()->entry_count().value());
  bgp::BgpStats::setDeduplicatedAttributesClusterList(
      stats.cluster_list()->entry_count().value());
  bgp::BgpStats::setDeduplicatedAttributesExtCommunities(
      stats.ext_communities()->entry_count().value());
  bgp::BgpStats::setDeduplicatedAttributesBgpPath(
      stats.bgp_path()->entry_count().value());
}

/**
 * @brief: Schedule periodic evict from deduplicators coroutine on
 * CancellableAsyncScope.
 */
folly::coro::Task<void>
PeerManagerBase::periodicEvictFromDeduplicatorLoop() noexcept {
  void (*evictFunctions[])(void) = {
      nettools::bgplib::DeDuplicatedBgpAttributesC::
          evictDeletedEntriesFromDeduplicator,
      nettools::bgplib::DeDuplicatedAsPath::evictDeletedEntriesFromDeduplicator,
      nettools::bgplib::DeDuplicatedCommunities::
          evictDeletedEntriesFromDeduplicator,
      nettools::bgplib::DeDuplicatedClusterList::
          evictDeletedEntriesFromDeduplicator,
      nettools::bgplib::DeDuplicatedExtCommunities::
          evictDeletedEntriesFromDeduplicator,
      DeDuplicatedBgpPath::evictDeletedEntriesFromDeduplicator};
  auto evictFunctionsLen = sizeof(evictFunctions) / sizeof(evictFunctions[0]);

  while (true) {
    for (int i = 0; i < evictFunctionsLen; ++i) {
      // when cancelAndJoinAsync is called, loop will be broken to exit
      co_await folly::coro::co_safe_point;

      // Clean up deduplicators every 30 seconds
      co_await folly::coro::sleepReturnEarlyOnCancel(30s);

      evictFunctions[i]();
      publishDeduplicatedAttributesSizes();
    }
  }
}

folly::F14NodeSet<std::shared_ptr<AdjRibOutGroup>>
PeerManagerBase::getPolicyReEvalPendingGroups() {
  folly::F14NodeSet<std::shared_ptr<AdjRibOutGroup>> affectedGroups;
  for (const auto& [peerId, adjRib] : adjRibs_) {
    if (adjRib->isEgressPolicyUpdateRequired() &&
        !adjRib->inInitialAnnouncement()) {
      auto group = adjRib->getUpdateGroup();
      if (group) {
        affectedGroups.insert(group);
      }
    }
  }
  return affectedGroups;
}

void PeerManagerBase::schedulePolicyReEvalForAdjRibs() {
  for (const auto& [peerId, adjRib] : adjRibs_) {
    if (adjRib->isEgressPolicyUpdateRequired() &&
        !adjRib->inInitialAnnouncement()) {
      XLOGF(
          INFO,
          "Sending RibDumpReq for egress affected adjrib {}",
          peerId.str());
      /*
       * Non-update-group path: schedule the per-peer egress dump on
       * asyncScope_. processRibDumpReqForEgressPolicyUpdate clears the pending
       * egress policy update flag once the dump completes.
       */
      asyncScope_.add(co_withExecutor(
          &evb_, processRibDumpReqForEgressPolicyUpdate(peerId, adjRib)));
    }
  }
}

folly::coro::Task<void>
PeerManagerBase::processUpdateGroupsEgressPolicyReevaluation() {
  auto cancelToken = co_await folly::coro::co_current_cancellation_token;
  if (cancelToken.isCancellationRequested()) {
    co_return;
  }

  /*
   * 1. Rebuild every peer's UpdateGroupKey and, in the same pass, bucket each
   * peer whose rebuilt key no longer matches its group's key by
   * target key and source group:
   *   targetKey -> {
   *     { sourceGroup1 -> [adjRibsToMoveFrom1] },
   *     { sourceGroup2 -> [adjRibsToMoveFrom2] },
   *     ...
   *   }
   * Nothing moves physically here.
   */
  folly::F14FastMap<
      UpdateGroupKey,
      folly::F14FastMap<
          std::shared_ptr<AdjRibOutGroup>,
          std::vector<std::shared_ptr<AdjRib>>,
          std::hash<std::shared_ptr<AdjRibOutGroup>>>,
      UpdateGroupKeyHash>
      newKeyToSourceGroups;
  folly::F14FastSet<std::shared_ptr<AdjRibOutGroup>> sourceGroups;
  uint32_t affectedAdjRibs = 0;
  for (const auto& [peerId, adjRib] : adjRibs_) {
    /*
     * Ignore peers whose session is down. A terminated-but-not-deleted peer
     * stays in adjRibs_ with a stale (non-null) update-group pointer, so
     * re-keying/splitting/moving it during egress-policy re-evaluation would be
     * incorrect. It re-joins a group with a freshly built key when its session
     * is re-established.
     */
    if (adjRib->getPeerState() == PeerUpdateState::DOWN) {
      continue;
    }
    adjRib->buildAndSetUpdateGroupKey();
    const auto& group = adjRib->getUpdateGroup();
    if (!group) {
      continue;
    }
    const auto& key = adjRib->getUpdateGroupKey();
    if (!(key == group->getGroupKey())) {
      newKeyToSourceGroups[key][group].push_back(adjRib);
      sourceGroups.insert(group);
      ++affectedAdjRibs;
    }
  }
  XLOGF(
      INFO,
      "Starting processUpdateGroupsEgressPolicyReevaluation for {} adjRibs from {} groups",
      affectedAdjRibs,
      sourceGroups.size());

  /*
   * 2. Reconcile each target key's new members. The subset of members
   * being moved from old group into new group MAY or MAY NOT require an egress
   * policy change if the name changed between old and new group.
   *
   * There are two scenarios for a targetKey.
   *   (1) A group already exists with that targetKey.
   *   (2) A group does NOT exist with that targetKey.
   *
   * In case (1), we can detach and move the peers from their sourceGroups,
   * and re-evaluate policy (RIB walk) for each individual peer if needed.
   *
   * In case (2), we now have a set of
   *     { sourceGroup1 -> [adjRibsToMoveFrom1] },
   *     { sourceGroup2 -> [adjRibsToMoveFrom2] },
   *     ...
   *
   * We should pick one of these groups as the base container,
   * and detach + move all other adjRibs into that container.
   *
   * There are two APIs provided from AdjRibGroup to extract peers
   * from itself to a new group container; splitToNewGroup and movePeers.
   * There is also an API that moves the entire group to a new key
   * without touching the peers, UpdateGroupManager::rekeyGroup.
   *
   * The selection for the base container is described below given the existing
   * APIs.
   *
   * We prefer the group with the highest number of in sync peers to preserve
   * the peers that are already moving in unison.
   *
   * If that group should extract all of its peers, then we can use rekeyGroup
   * to do O(1) membership change for all peers.
   * Otherwise, create a new group container and use splitToGroup to
   * copy the peer states of the oldGroup to that new container.
   *
   * Once we have created a baseGroup, then we reduce the rest of case (2) to
   * case (1).
   * Each target's surviving group is collected so its no-sync-peer recovery can
   * run once, after all moves across all targets are complete.
   */
  folly::F14FastSet<std::shared_ptr<AdjRibOutGroup>> allAffectedGroups;
  for (auto& [newKey, adjRibsToMoveFromSource] : newKeyToSourceGroups) {
    auto* vrf = findVrf(newKey.vrfId);
    if (!vrf) {
      XLOGF(
          ERR,
          "Skipping update group reconciliation for key {} due to non-existent VRF {}",
          UpdateGroupKey::toString(newKey),
          newKey.vrfId);
      continue;
    }
    auto targetGroup = updateGroupManager_->getGroup(newKey);
    std::shared_ptr<AdjRibOutGroup> baseGroup;
    bool baseGroupNeedsPolicyReEval = false;

    /* Case (2), a group did not exist for this key. */
    if (!targetGroup) {
      size_t mostInSync = 0;
      /* Pick the group with the most in sync peers to keep them in unison. */
      for (const auto& [sourceGroup, adjRibsToMove] : adjRibsToMoveFromSource) {
        const auto inSync = sourceGroup->getNumInSyncPeers();
        if (!baseGroup || inSync > mostInSync) {
          mostInSync = inSync;
          baseGroup = sourceGroup;
        }
      }
      /*
       * Use this group as the basis for the new container which will receive
       * all other moving adjRibs.
       */
      const auto& adjRibsFromBaseGroup = adjRibsToMoveFromSource.at(baseGroup);
      baseGroupNeedsPolicyReEval =
          baseGroup->getGroupKey().egressPolicyName != newKey.egressPolicyName;
      if (adjRibsFromBaseGroup.size() == baseGroup->getMemberCount()) {
        /*
         * If the entire group needs to move, we can directly rekey the group
         * with no copying.
         */
        updateGroupManager_->rekeyGroup(baseGroup, newKey);
        targetGroup = baseGroup;
      } else {
        /*
         * Only part of the group moves; copy the state of the group and
         * split the moving adjRibs into a newly created group container.
         */
        targetGroup = updateGroupManager_->findOrCreateGroup(
            newKey, vrf->getShadowRibView());
        baseGroup->splitToNewGroup(targetGroup, adjRibsFromBaseGroup);
      }

      allAffectedGroups.insert(baseGroup);
    }

    /*
     * If the egress policy changed, we need to update the RIB-OUT to
     * the new policy for this set of peers.
     *
     * Not all adjRibs being moved into the group might require egress policy
     * update, so we must evaluate the situation per set of
     * <oldGroup, adjRibsToMove>.
     *
     * An uninitialized target needs the walk whatever the policy did: it has
     * never dumped, so it holds no RIB-OUT, its peers sit in INIT, and it has
     * no change list consumer for the peers moving in to bound themselves
     * against. Walking here, before those peers are moved and activated
     * below, is what gives it all three.
     */
    if (baseGroupNeedsPolicyReEval ||
        targetGroup->getState() == UpdateGroupState::UNINITIALIZED) {
      processGroupEgressPolicyReEvaluation(targetGroup);
    }

    /* Case (1), and fall-through handling from case (2). */
    for (const auto& [sourceGroup, adjRibsToMove] : adjRibsToMoveFromSource) {
      if (sourceGroup == baseGroup || sourceGroup == targetGroup) {
        /*
         * Nothing needed if the adjRibs are in the target group already.
         * The baseGroup comparison is because we already moved the
         * adjRibs to the targetGroup, and do not need to do anything on the
         * baseGroup.
         */
        continue;
      }

      allAffectedGroups.insert(sourceGroup);

      /*
       * Detach the peers and move their materialized RIB-OUTs and states to the
       * targetGroup. movePeers is a pure move; it does not arm the detached
       * consume timer, so each moved peer would otherwise be stranded in
       * DETACHED_INIT_DUMP with no way to resume consuming or rejoin. Pass an
       * onPeerMoved hook that reactivates each peer once the batch is re-homed:
       *   - egress policy changed: full per-peer re-evaluation (walk the shadow
       *     RIB under the new policy, consume the CL to the end); this also
       *     activates detached mode processing.
       *   - egress policy unchanged: no re-walk needed (movePeers already
       *     carried over the materialized RIB-OUT), just re-activate detached
       *     mode processing so the peer resumes in the target group.
       *
       * A peer movePeers left DETACHED_BLOCKED is intentionally NOT activated:
       * its queue is full, so it must not send until it unblocks; its in-flight
       * push re-activates it via markPeerUnblocked once the queue drains.
       */
      sourceGroup->detachPeers(
          adjRibsToMove, AdjRibOutGroup::DetachReason::Policy);
      const bool peersNeedPolicyReEval =
          sourceGroup->getGroupKey().egressPolicyName !=
          newKey.egressPolicyName;
      sourceGroup->movePeers(
          adjRibsToMove,
          targetGroup,
          [vrf, peersNeedPolicyReEval](const std::shared_ptr<AdjRib>& adjRib) {
            /*
             * Policy changed: re-evaluate (walk the shadow RIB under the new
             * policy, consume the CL to the end). This only refreshes RIB-OUT;
             * it does not activate, so control falls through to the activation
             * below. Every moved peer lands in targetGroup, so they all share
             * its VRF.
             */
            if (peersNeedPolicyReEval) {
              vrf->processDetachedPeerEgressPolicyReEvaluation(adjRib);
            }
            /*
             * Activate every moved peer so it drains and rejoins the target
             * group, whether it was re-evaluated (policy changed) or carried
             * over (policy unchanged). A DETACHED_BLOCKED peer is excluded: its
             * queue is full, so it must not send until markPeerUnblocked
             * re-activates it once the in-flight push drains.
             */
            if (adjRib->getPeerState() != PeerUpdateState::DETACHED_BLOCKED) {
              adjRib->activateDetachedModeProcessing();
            }
            adjRib->clearPendingEgressPolicyUpdate();
          });
    }

    allAffectedGroups.insert(targetGroup);
  }

  /*
   * Merging or moving peers can leave a group with members but no in-sync peers
   * (e.g. detached peers merged into a group whose own sync peers are gone, or
   * a source group left with only detached members after its sync peers moved
   * out), or drain a source group of all its members. Handle every group that
   * participated in this re-evaluation once all moves are complete.
   *
   * allAffectedGroups is a superset of the source groups -- every source group
   * a peer moved out of is inserted above, alongside the merged/split targets,
   * and the targets always keep at least the moved-in peers -- so the emptied
   * groups to destroy can be collected in this same pass:
   *   - zero members: fully drained by the moves; collect it for destruction.
   *   - non-zero members: promote a peer to sync if needed;
   *     if at least one sync peer exists, restart group timers.
   */
  folly::F14FastSet<std::shared_ptr<AdjRibOutGroup>> emptiedOldGroups;
  for (const auto& group : allAffectedGroups) {
    if (group->getMemberCount() == 0) {
      emptiedOldGroups.insert(group);
      continue;
    }
    /* Try immediate promotion in group if no sync peers. */
    group->recoverIfNoSyncPeers();
    /*
     * Fall-through from promotion, if there are any sync peers, reschedule
     * group packing timer.
     */
    if (group->getNumInSyncPeers() > 0) {
      XLOGF(
          INFO,
          "Group {}: Resuming with {} sync peers after policy re-evaluation",
          group->getGroupDescriptor(),
          group->getNumInSyncPeers());
      group->scheduleChangeListConsumeTimer();
    } else {
      XLOGF(
          INFO,
          "Group {}: Paused for no-sync-peer recovery with {} detached peers after policy re-evaluation",
          group->getGroupDescriptor(),
          group->getDetachedPeers().size());
    }
  }
  /*
   * Steps 1 and 2 are done, so this run has handled every policy update seen so
   * far. Clear the flag before the co_await so an update arriving during the
   * maybeDestroyUpdateGroups drain schedules a fresh run instead of being
   * dropped, and dismiss the guard so it does not re-clear on exit and stomp
   * that run. maybeDestroyUpdateGroups only drains already-emptied groups, so
   * an overlapping run cannot interleave with any group mutation.
   */
  co_await updateGroupManager_->maybeDestroyUpdateGroups(emptiedOldGroups);

  XLOGF_IF(
      INFO,
      !emptiedOldGroups.empty(),
      "Cleaned up {} empty groups",
      emptiedOldGroups.size());

  co_return;
}

void PeerManagerBase::distributeRibOutAnnouncementToAdjRibs(
    const PeerManagerVrf& vrf,
    const RibOutAnnouncement& announcement) {
  /*
   * handleShadowRibEntryAnnouncement re-stamped these entries with the shadow
   * RIB's versions before this call, so what AdjRib records below is on the
   * same numbering as every other consumer.
   */
  for (const auto& [_, adjRib] : adjRibs_) {
    if (!adjRib) {
      /* adjrib is not ready. Skip sending. */
      continue;
    }
    if (vrf.isRibInitialAnnouncementDone() || adjRib->inInitialAnnouncement()) {
      /*
       * Case 1: `ribInitialAnnouncementDone_ = true`
       *
       * This implies that BGP has been initialized, we need to send
       * incremental updates to peers.
       *
       * Case 2: `ribInitialAnnouncementDone_ = false` and
       *         `adjRib->inInitialAnnouncement() = true`.
       *
       * This implies BGP has NOT yet initialized. We send updates to
       * all peers as the initial RibDumpReq.
       */
      if (!vrf.isRibInitialAnnouncementDone()) {
        /*
         * This applies ONLY to BGP not yet initialized case.
         *
         * Not using intermediate queue to adjRibOut, instead
         * calling adjRib's method of processing ribMsg directly.
         */
        adjRib->processRibMessage(announcement);
      }

      if (announcement.sendWithEoR && adjRib->inInitialAnnouncement()) {
        adjRib->resetInInitialAnnouncement();
        /*
         * changeList consumers are to be registered to the tracker
         * library only after initial dump with EoR flag is sent to
         * adjRibOut.
         *
         * consumers must be registered right away as soon EoR flag
         * is sent so as to not miss any further incremental changes
         * to routes
         */
        adjRib->activateChangeListConsumer();
      }
    }
  }
}

folly::coro::Task<void> PeerManagerBase::processRibDumpReqForEgressPolicyUpdate(
    const nettools::bgplib::BgpPeerId& peerId,
    std::shared_ptr<AdjRib> adjRib) {
  co_await processRibDumpReqCoro(RibDumpReq(peerId, adjRib->sendAddPath()));
  /*
   * The dump skips entries pending on the change list, so consume those with
   * every multipath under the new policy.
   */
  if (adjRib->getChangeListConsumer()) {
    adjRib->tryIterateChangesToEnd(
        adjRib->sendAddPath() /* processFullMultipaths */);
  }
  adjRib->clearPendingEgressPolicyUpdate(); // Clear flag after completion
}

void PeerManagerBase::processGroupEgressPolicyReEvaluation(
    std::shared_ptr<AdjRibOutGroup> group) {
  [[maybe_unused]] ScopedProfile profile(
      "PeerManagerBase::processGroupEgressPolicyReEvaluation");

  XLOGF(
      INFO,
      "Group {}: Starting group egress policy re-evaluation",
      group->getGroupDescriptor());

  // Step 1: Group-level re-evaluation (serves all IN_SYNC members)
  group->reEvaluateSyncPeersEgressPolicy();

  // Every member of an update group is in the group's VRF.
  auto* vrf = findVrf(group->getVrfId());
  if (!vrf) {
    XLOGF(
        ERR,
        "Group {}: Ignoring detached peer egress policy re-evaluation due to non-existent VRF {}",
        group->getGroupDescriptor(),
        group->getVrfId());
  }

  /*
   * Step 2: Detached peers are not served by the group walk above, so each
   * needs its own RIB walk. Cancel any rib dump already scheduled for the peer
   * and run the re-evaluation dump directly (inline) so it completes within
   * this event-loop turn -- a deferred walk leaves a window where the peer
   * could rejoin with stale (old-policy) entries and surface discrepancies
   * during the collapse. In-sync peers were served by the group walk; clear
   * each member's pending flag as we iterate.
   */
  for (const auto& [_, adjRib] : group->getBitToAdjRibs()) {
    if (adjRib->isDetachedPeer()) {
      XLOGF(
          INFO,
          "Group {}: Re-evaluating detached peer {}",
          group->getGroupDescriptor(),
          adjRib->getPeerName());
      if (vrf) {
        vrf->processDetachedPeerEgressPolicyReEvaluation(adjRib);
      }
      /*
       * Activate detached mode processing so the peer schedules coroutine to
       * sends its updates and eventually rejoins.
       * A DETACHED_BLOCKED peer is intentionally left alone: its queue is full,
       * so it must not send until it unblocks, at which point markPeerUnblocked
       * re-activates it.
       */
      if (adjRib->getPeerState() != PeerUpdateState::DETACHED_BLOCKED) {
        adjRib->activateDetachedModeProcessing();
      }
    }

    /* This flag needs to be cleared for all peers in the group. */
    adjRib->clearPendingEgressPolicyUpdate();
  }

  XLOGF(
      INFO,
      "Group {}: Group egress policy re-evaluation complete",
      group->getGroupDescriptor());
}

folly::coro::Task<void>
PeerManagerBase::processIngressAndEgressRouteFilterUpdate(
    size_t ingressAffectedCount,
    size_t egressAffectedCount) {
  // Test-only: pause routing-policy processing at coroutine entry.
  if (testOnlyPolicyUpdateProcessingEntered_ &&
      testOnlyPolicyUpdateProcessingRelease_) [[unlikely]] {
    auto entered = std::move(testOnlyPolicyUpdateProcessingEntered_);
    auto release = std::move(testOnlyPolicyUpdateProcessingRelease_);
    entered->post();
    co_await *release;
  }

  /*
   * Check if dynamic policy evaluation is enabled and there are adjRibs
   * with ingress policy changes
   */
  if (enableDynamicPolicyEvaluation_ && ingressAffectedCount > 0) {
    XLOGF(
        INFO,
        "Dynamic policy evaluation enabled - starting re-evaluation for {} ingress adjribs",
        ingressAffectedCount);

    const auto allPeersStart = std::chrono::steady_clock::now();
    // Process ingress re-evaluation for affected adjRibs
    co_await startAdjRibReEvaluationRoutine(
        RibPauseResumeCause::ROUTE_FILTER_POLICY_UPDATE);

    const auto allPeersReEvaluationTimeMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - allPeersStart)
            .count();
    BgpStats::setIngressPolicyAllPeersLastReEvaluationTimeMs(
        allPeersReEvaluationTimeMs);

    XLOGF(
        INFO,
        "Completed route filter policy re-evaluation for ingress adjribs in {} ms",
        allPeersReEvaluationTimeMs);
  }

  /*
   * Process egress-affected adjRibs (always needed if
   * egressAffectedCount > 0)
   */
  if (egressAffectedCount > 0) {
    XLOGF(
        INFO,
        "Handling egress policy update for {} egress affected adjribs",
        egressAffectedCount);

    XLOGF(
        INFO,
        "Sending RibDumpReq for {} egress affected adjribs",
        egressAffectedCount);
    schedulePolicyReEvalForAdjRibs();
  }

  co_return;
}

std::vector<nettools::bgplib::BgpPeerId>
PeerManagerBase::triggerRouteRefreshRequestsForPeers(
    std::vector<nettools::bgplib::BgpPeerId> peerIds,
    std::optional<nettools::bgplib::BgpUpdateAfi> afi) {
  // TODO_PMTRIZED_VRF: Check the initialization of each peer's VRF.
  if (!defaultVrf_.isInitialized()) {
    XLOG(ERR, "Refresh request can not be initiated. BGP is not initialized");
    return peerIds;
  }
  /*
   * Trigger route refresh request for all the peers and keep track of the
   * failed peerIds
   */
  std::vector<nettools::bgplib::BgpPeerId> failedPeerIds;
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    for (const auto& peerId : peerIds) {
      if (!triggerRouteRefreshRequestForPeer(peerId, afi)) {
        failedPeerIds.emplace_back(peerId);
      }
    }
  });
  return failedPeerIds;
}

bool PeerManagerBase::triggerRouteRefreshRequestForPeer(
    const BgpPeerId& peerId,
    std::optional<BgpUpdateAfi> afi) noexcept {
  auto peerIdAdjRib = adjRibs_.find(peerId);
  if (peerIdAdjRib == adjRibs_.cend()) {
    XLOGF_EVERY_MS(
        ERR,
        1000,
        "Error sending route refresh request to {}. AdjRib does not exist",
        peerId.str());
    return false;
  }
  auto adjRib = peerIdAdjRib->second;
  if ((!adjRib) || (!adjRib->isStateEstablished()) ||
      (!adjRib->isEnhancedRouteRefreshNegotiated() &&
       !adjRib->isRouteRefreshNegotiated())) {
    XLOGF_EVERY_MS(
        ERR,
        1000,
        "Error sending route refresh request to {}. "
        "Session is not established or neither Route Refresh nor "
        "Enhanced Route Refresh is negotiated",
        peerId.str());
    return false;
  }
  /*
   * TODO: Handle GR case and check if ERR is not already in progress. If
   * so, return false.
   */
  const auto triggerForAfi = [&](BgpUpdateAfi requestedAfi) {
    return adjRib->buildAndSendRouteRefresh(
        BgpRouteRefreshMessageSubtype::ROUTE_REFRESH_REQUEST, requestedAfi);
  };
  if (afi.has_value()) {
    return triggerForAfi(*afi);
  }

  bool matchedAfi = false;
  bool allQueued = true;
  for (const auto requestedAfi :
       {BgpUpdateAfi::AFI_IPv4, BgpUpdateAfi::AFI_IPv6}) {
    if (adjRib->isAfiNegotiated(requestedAfi)) {
      matchedAfi = true;
      allQueued = triggerForAfi(requestedAfi) && allQueued;
    }
  }
  return matchedAfi && allQueued;
}

std::vector<BgpPeerId> PeerManagerBase::getEstablishedPeerIdsForAddr(
    const folly::IPAddress& peerAddr) {
  std::vector<BgpPeerId> result;
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    auto it = peerAddrToIds_.find(peerAddr);
    if (it != peerAddrToIds_.end()) {
      for (const auto& peerId : it->second) {
        auto ribIt = adjRibs_.find(peerId);
        if (ribIt != adjRibs_.end() && ribIt->second &&
            ribIt->second->isStateEstablished()) {
          result.push_back(peerId);
        }
      }
    }
  });
  return result;
}

bool PeerManagerBase::processRouteRefreshForPeer(
    const BgpPeerId& peerId,
    std::optional<nettools::bgplib::BgpUpdateAfi> afi) noexcept {
  if (!defaultVrf_.isInitialized()) {
    XLOGF_EVERY_MS(
        ERR,
        1000,
        "Error processing route refresh for {}. BGP is not initialized",
        peerId.str());
    return false;
  }
  bool success = true;
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    auto ribIt = adjRibs_.find(peerId);
    if (ribIt == adjRibs_.end() || !ribIt->second) {
      XLOGF_EVERY_MS(
          ERR,
          1000,
          "Error processing route refresh for {}. AdjRib does not exist",
          peerId.str());
      success = false;
      return;
    }
    /*
     * Defense-in-depth: caller is expected to pre-filter via
     * getEstablishedPeerIdsForAddr(), but the session can transition out of
     * ESTABLISHED between that lookup and this call (separate evb_ runs).
     */
    if (!ribIt->second->isStateEstablished()) {
      XLOGF_EVERY_MS(
          ERR,
          1000,
          "Error processing route refresh for {}. Session is not established",
          peerId.str());
      success = false;
      return;
    }
    if (afi.has_value()) {
      success = ribIt->second->triggerOutboundRedump(*afi);
      return;
    }

    bool matchedAfi = false;
    bool allTriggered = true;
    for (const auto requestedAfi :
         {nettools::bgplib::BgpUpdateAfi::AFI_IPv4,
          nettools::bgplib::BgpUpdateAfi::AFI_IPv6}) {
      if (ribIt->second->isAfiNegotiated(requestedAfi)) {
        matchedAfi = true;
        allTriggered =
            ribIt->second->triggerOutboundRedump(requestedAfi) && allTriggered;
      }
    }
    success = matchedAfi && allTriggered;
  });
  return success;
}

} // namespace bgp
} // namespace facebook
