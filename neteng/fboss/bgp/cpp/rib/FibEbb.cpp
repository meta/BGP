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

#include "neteng/fboss/bgp/cpp/rib/FibEbb.h"

#include <optional>
#include <string>

#include <fmt/core.h>
#include <folly/coro/Sleep.h>
#include <folly/gen/Base.h>
#include <folly/logging/xlog.h>
#include <gflags/gflags.h>
#include <openr/if/gen-cpp2/Network_types.h>
#include <openr/if/gen-cpp2/Network_types_custom_protocol.h>
#include <thrift/lib/cpp2/protocol/DebugProtocol.h>

#include "neteng/fboss/bgp/cpp/BgpServiceUtil.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/ThriftClientUtils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

DEFINE_int32(
    agent_thrift_port,
    facebook::bgp::kDefaultFibAgentPort,
    "Agent thrift port");

DEFINE_int32(
    agent_thrift_recv_timeout_ms,
    facebook::bgp::kFbossAgentRecvTimeout.count(),
    "Agent thrift receive timeout in ms");

namespace facebook::bgp {

namespace {
static const int kBgpClientId = static_cast<int>(openr::thrift::FibClient::BGP);

openr::thrift::AdminDistance getAdminDistanceForRouteType(
    BgpRouteType routeType) {
  switch (routeType) {
    case BgpRouteType::LOCAL:
      return openr::thrift::AdminDistance::DIRECTLY_CONNECTED;
    case BgpRouteType::EBGP:
      return openr::thrift::AdminDistance::EBGP;
    case BgpRouteType::IBGP:
    case BgpRouteType::ConfedEBGP: // ConfedEBGP uses IBGP admin distance
      return openr::thrift::AdminDistance::IBGP;
    case BgpRouteType::UNKNOWN:
    default:
      return openr::thrift::AdminDistance::MAX_ADMIN_DISTANCE;
  }
}

class AgentNullPtrException : public std::runtime_error {
 public:
  AgentNullPtrException() : std::runtime_error{"Agent is Nullptr"} {}
};

} // namespace

FibEbb::FibEbb(
    folly::EventBase* evb,
    folly::coro::CancellableAsyncScope& asyncScope,
    FibMessageQueue& toRibQ,
    uint16_t agentPort,
    uint32_t agentRecvTimeout)
    : agentPort_(agentPort),
      agentRecvTimeout_(agentRecvTimeout),
      evb_(evb),
      asyncScope_(asyncScope),
      toRibQ_(toRibQ),
      programmingHistory_(std::make_unique<ProgrammingHistory>()),
      holdDownState_(std::make_unique<HoldDownState>()) {
  asyncScope_.add(co_withExecutor(evb_, keepAliveRoutine()));
}

FibEbb::~FibEbb() {
  XCHECK_EQ(asyncScope_.remaining(), 0);
}

std::unique_ptr<FibEbb> FibEbb::createFibEbb(
    folly::EventBase* evb,
    folly::coro::CancellableAsyncScope& asyncScope,
    Fib::FibMessageQueue& toRibQ,
    uint16_t agentPort,
    uint32_t agentRecvTimeout) {
  return std::unique_ptr<FibEbb>(
      new FibEbb(evb, asyncScope, toRibQ, agentPort, agentRecvTimeout));
}

void FibEbb::stop() {
  evb_->checkIsInEventBaseThread();

  // reset platform agent connection
  disconnectAgent();

  XLOG(INFO, "[Exit] Successfully stopped FibEbb");
}

void FibEbb::connectAgent() {
  client_ =
      createThriftClient<apache::thrift::Client<openr::thrift::FibService>>(
          *evb_,
          kLoopBackAddressV6,
          agentPort_,
          kFibEbbConnTimeout,
          kFibEbbSendTimeout,
          std::chrono::milliseconds(agentRecvTimeout_));
  XLOG(INFO, "Connecting to EBB Fib Agent ...");

  // now, create a batch
  CHECK(!batch_)
      << "Existing batch shall be null when a new connection is formed";
  batch_ = std::make_unique<Batch>();
}

void FibEbb::disconnectAgent() {
  XLOG(INFO, "Disconnecting EBB Fib Agent ...");

  client_.reset();
  fullSynced_ = false;
  agentAliveSince_ = 0;
  /*
   * No need to have the batch_ anymore since we do not connect to
   * the agent anymore. And no need to create a new batch either.
   */
  batch_.reset();
}

std::optional<FibOutRoute> FibEbb::updateUnicastRoute(
    const folly::CIDRNetwork& prefix,
    std::shared_ptr<const BgpPath> bestPathAttributes,
    std::shared_ptr<const WeightedNexthopMap> weightedNexthops,
    const bool /*isLocalRouteBest*/,
    const bool installToFib,
    const folly::F14NodeMap<folly::IPAddress, facebook::bgp::NexthopInfo>&
        nexthopInfoMap,
    const std::optional<uint32_t>& classId,
    std::shared_ptr<const NexthopTopoInfoMap> /*nexthopTopoInfoMap*/,
    const BgpRouteType routeType,
    bool enableFibOutTracking) {
  XLOG(DBG3, "Fib EBB Starting fib updateUnicastRoute function");

  if (!weightedNexthops || weightedNexthops->empty() || !installToFib) {
    XLOGF(
        DBG3,
        "Deleting unicast prefix {}",
        folly::IPAddress::networkToString(prefix));
    openr::thrift::IpPrefix ipPrefix;
    ipPrefix.prefixAddress() = openr::toBinaryAddress(prefix.first);
    ipPrefix.prefixLength() = prefix.second;
    batch_->toDelete.push_back(std::move(ipPrefix));
    batch_->waitForAck[bestPathAttributes][prefix] =
        std::move(weightedNexthops);
    return enableFibOutTracking ? std::optional<FibOutRoute>{FibOutRoute{}}
                                : std::nullopt;
  }

  std::vector<openr::thrift::NextHopThrift> thriftNextHops;
  FibNexthopSet fibOutNextHops;
  if (enableFibOutTracking) {
    fibOutNextHops.reserve(weightedNexthops->size());
  }
  for (const std::pair<const folly::IPAddress, uint32_t>& pair :
       *weightedNexthops) {
    const folly::IPAddress& nh = pair.first;
    const bool isLocalRoute =
        nh == kLocalRouteV4Nexthop || nh == kLocalRouteV6Nexthop;
    openr::thrift::NextHopThrift nht;

    nht.address() = openr::toBinaryAddress(nh);
    /*
     * EOS doesn't support weights on nexthops
     * Currently, we will send each unique next hop with weight = 1
     * until we support LBW weight in EBB.
     */
    nht.weight() = 1;

    auto it = nexthopInfoMap.find(nh);
    std::optional<bool> isConnected;
    const std::string* interfaceName{nullptr};
    // Set whether the next hop is directly connected for the next hop
    if (it != nexthopInfoMap.end()) {
      const facebook::bgp::NexthopInfo& nextHopInfo = it->second;
      isConnected = nextHopInfo.isConnected();

      if (isConnected.has_value()) {
        nht.isConnected() = isConnected.value();
      }

      /*
       * The fib agent can put a route into an EOS nexthop-group. Each entry
       * in a nexthop-group carries a nexthop address and an egress interface.
       * EOS does not find the interface for such an entry. Thus the entry
       * must carry the interface name. This applies to a global address and
       * to a link-local address.
       *
       * Set the interface name when the nexthop source knows it. Other
       * sources leave the field empty. Then the fib agent sends no interface
       * name.
       */
      const auto& ifName = nextHopInfo.getIfName();
      if (ifName) {
        interfaceName = &*ifName;
        nht.address()->ifName() = *interfaceName;
      }

      // Log prefix, nexthop address, and isConnected
      XLOGF(
          DBG3,
          "FibEbb - Prefix: {}, NextHop: {}, isConnected: {}, ifName: {}",
          folly::IPAddress::networkToString(prefix),
          nh.str(),
          isConnected.has_value() ? std::to_string(isConnected.value())
                                  : "unset",
          interfaceName ? interfaceName->c_str() : "unset");
    }

    if (isLocalRoute) {
      nht.address()->ifName() = "Null0";
      thriftNextHops = {nht};
      if (enableFibOutTracking) {
        fibOutNextHops = {FibOutNexthop{
            .address = nh,
            .interfaceName = "Null0",
            .weight = 1,
            .role = FibOutNexthopRole::PRIMARY,
            .connected = isConnected,
        }};
      }
      break;
    }

    thriftNextHops.emplace_back(std::move(nht));
    if (enableFibOutTracking) {
      fibOutNextHops.push_back(
          FibOutNexthop{
              .address = nh,
              .interfaceName = interfaceName
                  ? std::optional<std::string>{*interfaceName}
                  : std::nullopt,
              .weight = 1,
              .role = FibOutNexthopRole::PRIMARY,
              .connected = isConnected,
          });
    }
  }

  if (thriftNextHops.size() != weightedNexthops->size()) {
    XLOGF(
        WARNING,
        "Nexthops programmed are not same as the weighted next hops for the prefix {}",
        folly::IPAddress::networkToString(prefix));
  }

  XLOGF(
      DBG3,
      "Fib EBB Adding/updating unicast prefix {} with {} nexthops{}, next hop details: {}",
      folly::IPAddress::networkToString(prefix),
      thriftNextHops.size(),
      apache::thrift::debugString(thriftNextHops),
      (classId ? fmt::format(" classid {}", *classId) : ""));

  openr::thrift::UnicastRoute thriftRoute;
  thriftRoute.dest()->prefixAddress() = openr::toBinaryAddress(prefix.first);
  thriftRoute.dest()->prefixLength() = prefix.second;
  const auto adminDistance = getAdminDistanceForRouteType(routeType);
  thriftRoute.adminDistance() = adminDistance;
  thriftRoute.nextHops() = std::move(thriftNextHops);

  batch_->toAdd.emplace_back(std::move(thriftRoute));
  batch_->waitForAck[bestPathAttributes][prefix] = std::move(weightedNexthops);

  return enableFibOutTracking
      ? std::optional<FibOutRoute>{FibOutRoute{
            .nexthops = std::move(fibOutNextHops),
            /* The EBB route payload has no class-ID field. */
            .metadata = FibOutRouteMetadata::program(
                static_cast<int32_t>(adminDistance)),
        }}
      : std::nullopt;
}

folly::coro::Task<void> FibEbb::program(bool isSync, VrfId vrfId) {
  XLOG(DBG3, "Fib EBB Starting fib program coroutine");

  if (!client_) {
    // Does not connect the agent yet
    co_return;
  }

  if (isSync && !batch_->toDelete.empty()) {
    XLOGF(DBG1, "Sync FIB with {} routes to delete.", batch_->toDelete.size());
  }

  /*
   * Isolate the staged batch before the platform RPCs. The RIB programming
   * coroutine awaits this method, so it cannot stage the next adapter batch
   * until this call returns.
   */
  std::unique_ptr<FibEbb::Batch> process = std::move(batch_);
  batch_ = std::make_unique<Batch>();
  bool isFullSynced = fullSynced_;

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
            "FibEbb",
            neteng::fboss::bgp::thrift::BgpInitializationEvent::FIB_SYNCED);
      }
    } else {
      // delete first
      if (!process->toDelete.empty()) {
        XLOG(INFO, "Start deleteUnicastRoutes...");

        XLOG_IF(DBG3, !process->toDelete.empty()) << [&]() {
          std::vector<std::string> routeStrings;
          routeStrings.reserve(process->toDelete.size());
          for (const openr::thrift::IpPrefix& r : process->toDelete) {
            routeStrings.push_back(
                fmt::format(
                    "{}/{}",
                    openr::toString(*r.prefixAddress()),
                    *r.prefixLength()));
          }
          return fmt::format(
              "Delete unicast routes: {}", folly::join(",", routeStrings));
        }();

        co_await client_->co_deleteUnicastRoutes(
            kBgpClientId, process->toDelete);

        FibStats::addFibUcastUpdates();

        XLOGF(INFO, "Programmed HW with {} withdraw", process->toDelete.size());
        programmingHistory_->markProgrammingSuccess();
      }
      // add routes
      if (!process->toAdd.empty()) {
        XLOG(INFO, "Start addUnicastRoutes...");

        XLOG_IF(DBG3, process->toAdd.size() > 0) << [&]() {
          std::vector<std::string> routeStrings;
          routeStrings.reserve(process->toAdd.size());
          for (const openr::thrift::UnicastRoute& r : process->toAdd) {
            routeStrings.push_back(
                fmt::format(
                    "{}/{}",
                    openr::toString(*r.dest()->prefixAddress()),
                    *r.dest()->prefixLength()));
          }
          return fmt::format(
              "Add unicast routes: {}", folly::join(",", routeStrings));
        }();

        // client could be reset by another coroutine
        if (!client_) {
          throw AgentNullPtrException();
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
  fullSynced_ = isFullSynced | isSync;
  FibStats::setFibSyncStatus(fullSynced_);

  // notify back to Rib that all prefixes have been installed in HW
  toRibQ_.push(FibProgrammedMessage(vrfId, process->waitForAck, isSync));

  co_return;
}

folly::coro::Task<void> FibEbb::keepAliveRoutine() {
  XLOG(DBG3, "Fib EBB Starting fib keepalive coroutine");
  while (true) {
    // when cancelAndJoinAsync is called, guaranteed to exit
    co_await folly::coro::co_safe_point;

    co_await folly::coro::sleepReturnEarlyOnCancel(kFibEbbKeepAliveTimeout);
    co_await keepAlive();
  }

  XLOG(INFO, "[Exit] Fib keepalive coroutine stopped");
  co_return;
}

folly::coro::Task<void> FibEbb::keepAlive() {
  if (!client_) {
    connectAgent();
  }

  // query agent status
  int64_t aliveSince{0};
  try {
    aliveSince = co_await client_->co_aliveSince();
    if (!client_) {
      // aliveSince_ should not be be set if client_ is not there.
      throw AgentNullPtrException();
    }
  } catch (std::exception const& ex) {
    XLOGF(ERR, "Failed to get agent stats: {}", ex.what());
    FibStats::addAgentStatusFailures();
    // Agent is dead, reset client_
    disconnectAgent();
    co_return;
  }

  /*
   * update agent status, send full sync request only if agent is connected and
   * restarted.
   */
  if (agentAliveSince_ != aliveSince) {
    if (holdDownState_->clearHoldDownState()) {
      XLOG(INFO, "Request full SyncFib.");
      agentAliveSince_ = aliveSince;
      toRibQ_.push(FibSyncReq{});
    } else {
      XLOG(INFO, "Hold down state is not cleared. postpone full sync request.");
    }
  }
  co_return;
}
} // namespace facebook::bgp
