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

#include <fboss/agent/if/gen-cpp2/ctrl_clients.h>
#include <fmt/core.h>
#include <folly/coro/Sleep.h>
#include <folly/gen/Base.h>
#include <folly/logging/xlog.h>
#include <gflags/gflags.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <thrift/lib/cpp2/op/Get.h>

#include "neteng/fboss/bgp/cpp/BgpServiceUtil.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/ThriftClientUtils.h"
#include "neteng/fboss/bgp/cpp/rib/FibFboss.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

DEFINE_int32(
    agent_thrift_port,
    facebook::bgp::kFbossAgentPort,
    "Agent thrift port");

DEFINE_int32(
    agent_thrift_recv_timeout_ms,
    facebook::bgp::kFbossAgentRecvTimeout.count(),
    "Agent thrift receive timeout in ms");

namespace {
static const int kBgpClientId =
    static_cast<int>(facebook::fboss::ClientID::BGPD);
} // namespace

namespace facebook::bgp {

namespace {

/**
 * Retain topology metadata only for nexthops in the submitted FBOSS request.
 *
 * The source map can contain metadata for nexthops that platform normalization
 * removes. Filtering keeps retained FIB-out state aligned with the exact
 * request sent to the agent and avoids retaining unrelated topology entries.
 * Local routes do not submit topology metadata. The original shared map is
 * reused when every entry was submitted, which avoids an unnecessary copy.
 */
std::shared_ptr<const FibOutTopologyInfoMap> retainSubmittedTopologyInfo(
    const WeightedNexthopMap& submittedNexthops,
    bool isLocalRoute,
    std::shared_ptr<const FibOutTopologyInfoMap> topologyInfo) {
  if (isLocalRoute || !topologyInfo || topologyInfo->empty()) {
    return nullptr;
  }

  bool everyTopologyEntryWasSubmitted = true;
  for (const auto& [address, _] : *topologyInfo) {
    if (submittedNexthops.find(address) == submittedNexthops.end()) {
      everyTopologyEntryWasSubmitted = false;
      break;
    }
  }
  if (everyTopologyEntryWasSubmitted) {
    return topologyInfo;
  }

  auto retained = std::make_shared<FibOutTopologyInfoMap>();
  for (const auto& [address, info] : *topologyInfo) {
    if (submittedNexthops.find(address) != submittedNexthops.end()) {
      retained->emplace(address, info);
    }
  }
  return retained->empty() ? nullptr : std::move(retained);
}

} // namespace

FibFboss::FibFboss(
    folly::EventBase* evb,
    folly::coro::CancellableAsyncScope& asyncScope,
    FibMessageQueue& toRibQ)
    : evb_(evb), asyncScope_(asyncScope), toRibQ_(toRibQ) {
  asyncScope_.add(co_withExecutor(evb_, keepAliveRoutine()));

  programmingHistory_ = std::make_unique<ProgrammingHistory>();
  holdDownState_ = std::make_unique<HoldDownState>();
}

FibFboss::~FibFboss() {
  XCHECK_EQ(asyncScope_.remaining(), 0);
}

std::unique_ptr<FibFboss> FibFboss::createFibFboss(
    folly::EventBase* evb,
    folly::coro::CancellableAsyncScope& asyncScope,
    Fib::FibMessageQueue& toRibQ) {
  return std::unique_ptr<FibFboss>(new FibFboss(evb, asyncScope, toRibQ));
}

void FibFboss::stop() {
  evb_->checkIsInEventBaseThread();

  // reset platform agent connection
  disconnectAgent();

  XLOG(INFO, "[Exit] Successfully stopped FibFboss");
}

void FibFboss::connectAgent() {
  client_ = createThriftClient<apache::thrift::Client<fboss::FbossCtrl>>(
      *evb_,
      kLoopBackAddressV6,
      FLAGS_agent_thrift_port,
      kFbossAgentConnTimeout,
      kFbossAgentSendTimeout,
      std::chrono::milliseconds(FLAGS_agent_thrift_recv_timeout_ms));
  XLOG(INFO, "Connecting to wedge_agent ...");

  // now, create a batch
  CHECK(!batch_)
      << "Existing batch shall be null when a new connection is formed";
  batch_ = std::make_unique<Batch>();
}

void FibFboss::disconnectAgent() {
  XLOG(INFO, "Disconnecting wedge_agent ...");
  client_.reset();
  fullSynced_ = false;
  agentAliveSince_ = 0;
  /*
   * No need to have the batch_ anymore since we do not connect to
   * the agent anymore. And no need to create a new batch either.
   */
  batch_.reset();
}

std::optional<FibOutRoute> FibFboss::updateUnicastRoute(
    const folly::CIDRNetwork& prefix,
    std::shared_ptr<const BgpPath> attrsToBeAdvertised,
    std::shared_ptr<const WeightedNexthopMap> weightedNexthops,
    const bool isLocalRouteBest,
    const bool installToFib,
    const folly::F14NodeMap<folly::IPAddress, facebook::bgp::NexthopInfo>&
        nextHopInfoMap,
    const std::optional<uint32_t>& classId,
    std::shared_ptr<const NexthopTopoInfoMap> nexthopTopoInfoMap,
    const BgpRouteType routeType,
    bool enableFibOutTracking) {
  return updateUnicastRouteWithBackup(
      prefix,
      std::move(attrsToBeAdvertised),
      std::move(weightedNexthops),
      isLocalRouteBest,
      installToFib,
      nextHopInfoMap,
      classId,
      std::move(nexthopTopoInfoMap),
      routeType,
      std::nullopt,
      enableFibOutTracking);
}

std::optional<FibOutRoute> FibFboss::updateUnicastRouteWithBackup(
    const folly::CIDRNetwork& prefix,
    std::shared_ptr<const BgpPath> attrsToBeAdvertised,
    std::shared_ptr<const WeightedNexthopMap> weightedNexthops,
    const bool isLocalRouteBest,
    const bool installToFib,
    const folly::F14NodeMap<folly::IPAddress, facebook::bgp::NexthopInfo>&,
    const std::optional<uint32_t>& classId,
    std::shared_ptr<const NexthopTopoInfoMap> nexthopTopoInfoMap,
    const BgpRouteType /* routeType */,
    const std::optional<folly::IPAddress>& backupAddr,
    bool enableFibOutTracking) {
  // if not connected, nothing to do
  if (!client_) {
    return {};
  }

  if (!weightedNexthops || weightedNexthops->empty() || !installToFib) {
    XLOGF(
        DBG3,
        "Deleting unicast prefix {}",
        folly::IPAddress::networkToString(prefix));
    fboss::IpPrefix ipPrefix;
    ipPrefix.ip() = network::toBinaryAddress(prefix.first);
    ipPrefix.prefixLength() = prefix.second;
    batch_->toDelete.push_back(std::move(ipPrefix));
    batch_->waitForAck[attrsToBeAdvertised][prefix] =
        std::move(weightedNexthops);
    return enableFibOutTracking ? std::optional<FibOutRoute>{FibOutRoute{}}
                                : std::nullopt;
  }

  const bool backupAddrFamilyMatches =
      backupAddr && prefix.first.family() == backupAddr->family();
  std::vector<fboss::NextHopThrift> tNextHops;
  FibNexthopSet fibOutNextHops;
  if (enableFibOutTracking && !isLocalRouteBest) {
    fibOutNextHops.reserve(
        weightedNexthops->size() + (backupAddrFamilyMatches ? 1 : 0));
  }
  for (const auto& nhwt : *weightedNexthops) {
    const auto& nh = nhwt.first;
    fboss::NextHopThrift nht;
    nht.address() = network::toBinaryAddress(nh);
    nht.weight() = nhwt.second;
    nht.role() = fboss::NextHopRole::PRIMARY;
    if (nexthopTopoInfoMap) {
      auto topoInfoIt = nexthopTopoInfoMap->find(nh);
      if (topoInfoIt != nexthopTopoInfoMap->end()) {
        nht.topologyInfo() = createNetworkTopoInfo(topoInfoIt->second);
      }
    }
    tNextHops.emplace_back(std::move(nht));

    if (enableFibOutTracking && !isLocalRouteBest) {
      fibOutNextHops.push_back(
          FibOutNexthop{
              .address = nh,
              .weight = nhwt.second,
              .role = FibOutNexthopRole::PRIMARY,
          });
    }
  }

  if (backupAddr && !backupAddrFamilyMatches) {
    XLOGF_EVERY_MS(
        ERR,
        60000,
        "Ignoring backup address {} for prefix {} due to address-family mismatch",
        backupAddr->str(),
        folly::IPAddress::networkToString(prefix));
  }
  /*
   * When installToFib is true for a local route, send an empty nexthop list.
   * This is the representation the agent expects for a null route.
   */
  if (isLocalRouteBest) {
    tNextHops.clear();
    if (enableFibOutTracking) {
      fibOutNextHops.clear();
    }
    XLOGF(
        DBG1,
        "Local route programming with empty nexthop for prefix {}",
        folly::IPAddress::networkToString(prefix));
  } else if (backupAddrFamilyMatches) {
    fboss::NextHopThrift backupNht;
    backupNht.address() = network::toBinaryAddress(*backupAddr);
    backupNht.weight() = 0;
    backupNht.role() = fboss::NextHopRole::BACKUP;
    tNextHops.emplace_back(std::move(backupNht));

    if (enableFibOutTracking) {
      fibOutNextHops.push_back(
          FibOutNexthop{
              .address = *backupAddr,
              .weight = 0,
              .role = FibOutNexthopRole::BACKUP,
          });
    }
  }

  XLOGF(
      DBG3,
      "Adding/updating unicast prefix {} with {} nexthops{}",
      folly::IPAddress::networkToString(prefix),
      tNextHops.size(),
      (classId ? fmt::format(" classid {}", *classId) : ""));

  if (XLOG_IS_ON(DBG3)) {
    for (const auto& nht : tNextHops) {
      XLOGF(
          DBG3,
          "FIB update: prefix={}, nexthop={}, weight={}",
          folly::IPAddress::networkToString(prefix),
          network::toIPAddress(*nht.address()).str(),
          *nht.weight());
    }
  }

  fboss::UnicastRoute tRoute;
  tRoute.dest()->ip() = network::toBinaryAddress(prefix.first);
  tRoute.dest()->prefixLength() = prefix.second;
  tRoute.adminDistance() = fboss::AdminDistance::EBGP;
  tRoute.nextHops() = std::move(tNextHops);
  std::optional<int32_t> fibOutClassId;
  if (classId) {
    // no error checking here as config has done validation.
    const auto wireClassId = static_cast<fboss::cfg::AclLookupClass>(*classId);
    tRoute.classID() = wireClassId;
    fibOutClassId = static_cast<int32_t>(wireClassId);
  }
  batch_->toAdd.emplace_back(std::move(tRoute));
  auto fibOutTopologyInfo = enableFibOutTracking
      ? retainSubmittedTopologyInfo(
            *weightedNexthops, isLocalRouteBest, std::move(nexthopTopoInfoMap))
      : nullptr;
  batch_->waitForAck[attrsToBeAdvertised][prefix] = std::move(weightedNexthops);

  return enableFibOutTracking
      ? std::optional<FibOutRoute>{FibOutRoute{
            .nexthops = std::move(fibOutNextHops),
            .metadata = FibOutRouteMetadata::program(
                static_cast<int32_t>(fboss::AdminDistance::EBGP),
                fibOutClassId),
            .topologyInfo = std::move(fibOutTopologyInfo),
        }}
      : std::nullopt;
}

folly::coro::Task<void> FibFboss::program(bool isSync, VrfId vrfId) {
  std::unique_lock<folly::coro::Mutex> lock{
      co_await agentMutex_.co_scoped_lock()};

  if (!client_) {
    // Does not connect the agent yet
    co_return;
  }

  if (isSync && batch_->toDelete.size()) {
    XLOGF(DBG1, "Sync FIB with {} routes to delete.", batch_->toDelete.size());
  }

  /*
   * Now handle this batch. And prepare for the new batch. The calls after the
   * next two lines could be blocked to wait for FBOSS agent to ack.
   * During the wait time, a new batch could be formed.
   */
  auto process = std::move(batch_);
  batch_ = std::make_unique<Batch>();

  try {
    /*
     * the following call could be blocked
     * if in fullSync, call syncFib regardless of toAdd.size()
     */

    if (isSync) {
      XLOG(INFO, "Start syncFib...");
      const auto syncStart = std::chrono::steady_clock::now();
      co_await client_->co_syncFib(kBgpClientId, process->toAdd);
      const auto syncDurationMs =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - syncStart)
              .count();
      FibStats::addFibSyncTimeMs(syncDurationMs);

      XLOGF(
          INFO,
          "Synced FIB with {} routes in {}ms.",
          process->toAdd.size(),
          syncDurationMs);
      programmingHistory_->markProgrammingSuccess();
      // populate one-time flag to mark initial FIB synced
      if (!initialFibSynced_) {
        initialFibSynced_ = true;

        // log BGP++ initialization event
        BgpStats::logInitializationEvent(
            "FibFboss",
            neteng::fboss::bgp::thrift::BgpInitializationEvent::FIB_SYNCED);
      }
    } else {
      // delete first
      if (process->toDelete.size()) {
        XLOG(INFO, "Start deleteUnicastRoutes...");

        if (XLOG_IS_ON(DBG3)) {
          auto strVec = folly::gen::from(process->toDelete) |
              folly::gen::map([&](const auto& r) { return toString(r); }) |
              folly::gen::as<std::vector<std::string>>();
          XLOGF(DBG3, "Delete unicast routes: {}", folly::join(",", strVec));
        }

        co_await client_->co_deleteUnicastRoutes(
            kBgpClientId, process->toDelete);
        FibStats::addFibUcastUpdates();

        XLOGF(INFO, "Programmed HW with {} withdraw", process->toDelete.size());
        programmingHistory_->markProgrammingSuccess();
      }
      // add routes
      if (process->toAdd.size()) {
        XLOG(INFO, "Start addUnicastRoutes...");

        if (XLOG_IS_ON(DBG3)) {
          auto strVec = folly::gen::from(process->toAdd) |
              folly::gen::map([&](const auto& r) {
                          return toString(*r.dest());
                        }) |
              folly::gen::as<std::vector<std::string>>();
          XLOGF(DBG3, "Add unicast routes: {}", folly::join(",", strVec));
        }

        co_await client_->co_addUnicastRoutes(kBgpClientId, process->toAdd);
        FibStats::addFibUcastUpdates();

        XLOGF(INFO, "Programmed HW with {} updates.", process->toAdd.size());
        programmingHistory_->markProgrammingSuccess();
      }
    }
  } catch (std::exception const& ex) {
    XLOGF(
        ERR,
        "Failed to program {} withdraw and {} update to HW due to: {}",
        process->toDelete.size(),
        process->toAdd.size(),
        ex.what());

    FibStats::addAgentUpdateFailures();
    // Update failed RIB/FIB are now out of sync
    FibStats::setFibSyncStatus(false);
    // The agent is not programmable
    FibStats::setAgentProgrammable(false);
    programmingHistory_->markProgrammingFail();
    holdDownState_->setHoldDownState(
        programmingHistory_->getRecentFailureCount());

    disconnectAgent();
    co_return;
  }

  // Programming succeeded. The agent is programmable
  FibStats::setAgentProgrammable(true);

  // update fullSynced_ flag
  fullSynced_ |= isSync;
  FibStats::setFibSyncStatus(fullSynced_);

  // notify back to Rib that all prefixes have been installed in HW
  toRibQ_.push(FibProgrammedMessage(vrfId, process->waitForAck, isSync));

  co_return;
}

folly::coro::Task<void> FibFboss::keepAliveRoutine() {
  XLOG(INFO, "Starting fib keepalive coroutine");
  while (true) {
    // when cancelAndJoinAsync is called, guaranteed to exit
    co_await folly::coro::co_safe_point;

    co_await folly::coro::sleepReturnEarlyOnCancel(kFbossAgentKeepAliveTimeout);
    co_await keepAlive();
  }

  XLOG(INFO, "[Exit] Fib keepalive coroutine stopped");
  co_return;
}

folly::coro::Task<void> FibFboss::keepAlive() {
  std::unique_lock<folly::coro::Mutex> lock{
      co_await agentMutex_.co_scoped_lock()};

  if (!client_) {
    connectAgent();
  }

  // query agent status
  int64_t aliveSince{0};
  auto status = fboss::SwitchRunState::UNINITIALIZED;
  try {
    aliveSince = co_await client_->co_aliveSince();
    status = co_await client_->co_getSwitchRunState();
  } catch (std::exception const& ex) {
    XLOGF(ERR, "Failed to get agent stats: {}", ex.what());
    FibStats::addAgentStatusFailures();
    // Agent is dead, reset client_
    disconnectAgent();
    co_return;
  }

  // update agent status, send full sync request if needed
  if (agentAliveSince_ != aliveSince) {
    XLOGF(
        INFO,
        "Detect agent restart at unix time {}; new agent status: {}",
        aliveSince,
        apache::thrift::util::enumNameSafe(status));
    /*
     * (status >= CONFIGURED) is not checked since there is EXITING above
     * CONFIGURED state.
     */
    if (status == fboss::SwitchRunState::CONFIGURED) {
      if (holdDownState_->clearHoldDownState()) {
        XLOG(INFO, "Request full SyncFib.");
        agentAliveSince_ = aliveSince;
        toRibQ_.push(FibSyncReq{});
      } else {
        XLOG(
            INFO,
            "Hold down state is not cleared. postpone full sync request.");
      }
    }
  }
  co_return;
}

fboss::NetworkTopologyInformation FibFboss::createNetworkTopoInfo(
    const std::unordered_map<std::string, int64_t>& topoInfoMap) {
  using TopoInfo = fboss::NetworkTopologyInformation;
  TopoInfo tTopoInfo;
  /*
   * iterate over all fields of NetworkTopologyInformation
   * for a key (field name) found in topologyInfo (dictionary)
   * assign the value to the corresponding field
   */
  apache::thrift::op::for_each_field_id<TopoInfo>([&topoInfoMap,
                                                   &tTopoInfo]<class Id>(Id) {
    auto it =
        topoInfoMap.find(apache::thrift::op::get_name_v<TopoInfo, Id>.str());
    if (it != topoInfoMap.end()) {
      apache::thrift::op::get<Id>(tTopoInfo) = it->second;
    }
  });
  return tTopoInfo;
}

} // namespace facebook::bgp
