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

#include <stdexcept>
#include <unordered_set>

#include <fb303/FollyLoggingHandler.h>
#include <fb303/ServiceData.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <set>

#include <folly/ExceptionString.h>
#include <folly/FBString.h>
#include <folly/IPAddress.h>
#include <folly/ScopeGuard.h>
#include <folly/logging/xlog.h>

#include <thrift/lib/cpp/TApplicationException.h>

#include "common/network/AddressUtil.h"
#include "fboss/lib/LogThriftCall.h"
#include "magic_enum/magic_enum.hpp"
#include "neteng/fboss/bgp/cpp/BgpProfiler.h"
#include "neteng/fboss/bgp/cpp/BgpServiceBase.h"
#include "neteng/fboss/bgp/cpp/BgpServiceUtil.h"
#include "neteng/fboss/bgp/cpp/common/EvbUtils.h"
#include "neteng/fboss/bgp/cpp/peer/PeerManagerBase.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

using namespace facebook::neteng::fboss::bgp_attr;
using namespace facebook::neteng::fboss::bgp::thrift;

using std::string;

namespace {
static const std::string kExitNullPtrLogPrefix = "BgpServiceBaseExitOrNullPtr";
static const std::string kPeerGroupValidationLogPrefix =
    "BgpServicePeerGroupValidation";

/*
 * Sentinel returned by the scalar RIB-read handlers (co_getRibVersion /
 * co_getNumPrefixes) when the value cannot be produced -- session exiting, or
 * the RIB evb hop timed out / failed. Negative is unambiguous because the real
 * values are non-negative (0 = empty RIB), matching the negative-as-"no value"
 * convention of getTimeElapsedSinceLastFibUpdate().
 */
static constexpr int64_t kRibReadUnavailable = -1;

TBgpNetwork toBgpNetwork(const TIpPrefix& prefix, const TBgpPath& path) {
  TBgpNetwork network;
  network.prefix() = prefix;
  network.as_path() = path.as_path().value();
  if (apache::thrift::get_pointer(path.communities())) {
    network.communities() = *apache::thrift::get_pointer(path.communities());
  }
  if (apache::thrift::get_pointer(path.extCommunities())) {
    network.extCommunities() =
        *apache::thrift::get_pointer(path.extCommunities());
  }
  if (apache::thrift::get_pointer(path.local_pref())) {
    network.local_pref() = *apache::thrift::get_pointer(path.local_pref());
  }

  facebook::network::thrift::BinaryAddress addr;
  addr.addr() =
      folly::to<folly::fbstring>(path.next_hop().value().prefix_bin().value());
  auto nexthop = facebook::network::toIPAddress(addr).str();
  auto nexthopV4Str =
      (prefix.afi().value() == TBgpAfi::AFI_IPV4 ? nexthop : "");
  auto nexthopV6Str =
      (prefix.afi().value() == TBgpAfi::AFI_IPV6 ? nexthop : "");
  network.next_hop4() = nexthopV4Str;
  network.next_hop6() = nexthopV6Str;
  return network;
}
} // namespace

namespace facebook::bgp {

BgpServiceBase::BgpServiceBase(
    PeerManagerBase& peerMgr,
    std::shared_ptr<ConfigManager> configManager,
    RibBase& rib,
    Watchdog& watchdog,
    bool enable_thrift_protection)
    : peerMgr_(peerMgr),
      sessionMgr_(peerMgr.getSessionManager()),
      configManager_(std::move(configManager)),
      rib_(rib),
      watchdog_(watchdog),
      healthValidator_(
          std::make_unique<HealthValidator>(
              &peerMgr,
              &rib,
              &watchdog,
              /*nexthopHandler=*/nullptr,
              configManager_)),
      thriftProtectionEnabled_(enable_thrift_protection) {
  /*
   * Allow the fb303 setOption() call to update the command line flag
   * settings.  This allows us to change the log levels on the fly using
   * fbData->setOption().
   */
  facebook::fb303::registerFollyLoggingOptionHandlers();
};

/**
 * @brief  To protect BGP++ from potential CPU hogging by constant thrift
 *         calls, implement a mechanism to sleep current thrift request
 *         under certain conditions. And if need to suspend beyond certain
 *         time then reject that thrift call.
 *
 *         Note: BGP++ runs single threaded for each BGP++ critical functions
 *               like PeerMgr and Rib. Where-as thrift server runs multiple
 *               threads (BGP++ is configured for thrift server to run as many
 *               threads as no of CPU cores). Continuous requests from thrift
 *               server are not guaranteed to run on the same core as where
 *               PeerMgr and Rib threads are running and thus, it is not
 *               necessary that continuous requests of thrift calls may or may
 *               not block PeerMgr, Rib threads. However, to keep the
 *               solution simple, avoid thrift calls from keeping CPU contantly
 *               busy, add some idle time at certain interval just in case if
 *               they were using CPU cycles from the same core where PeerMgr
 *               and Rib like critical BGP++ threads are running
 *
 * @param  allowSuspend  certain calls may not be allowed to be suspended.
 *                       this parameter suggests to bypass such a logic,
 *                       but still do certain book-keeping
 *
 * @return false  if dampening decides to give up on this request and thus
 *                tells service call to be rejected
 *         true   service request can continue now with actual execution
 */
bool BgpServiceBase::continueExecution(bool allowSuspend) noexcept {
  if (!isThriftProtectionEnabled()) {
    incrRequestsInExecution();
    return true;
  }

  auto entrantTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

  auto now = entrantTime;
  bool suspend = true;
  while (allowSuspend && suspend) {
    /**
     * Break out the loop at one point with return status to not
     * continue with the API call
     */
    if (rejectRequest(now, entrantTime)) {
      XLOG(INFO, "Thrift API request rejected");
      fb303::ThreadCachedServiceData::get()->incrementCounter(
          BgpStats::kThriftReject, 1);
      return false;
    }

    if (canAbsorbRequestInCurrentWindow(now)) {
      suspend = false;
    } else {
      if (canStartNewRequestsWindow(now)) {
        suspend = false;
      }
    }

    if (suspend) {
      fb303::ThreadCachedServiceData::get()->incrementCounter(
          BgpStats::kThriftSuspend, 1);
      // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
    }
  }

  incrRequestsInExecution();
  return true;
}

// fboss config bgp (display running config)
void BgpServiceBase::getRunningConfig(string& configStr) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    return;
  }

  if (!continueExecution(true)) {
    return;
  }
  auto config = configManager_->getConfig();
  configStr = config->getRunningConfig();
  decrRequestsInExecution();
}

void BgpServiceBase::getRunningConfigStruct(thrift::BgpConfig& config) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (!continueExecution(true)) {
    return;
  }
  auto currentConfig = configManager_->getConfig();
  config = currentConfig->getConfig();
  decrRequestsInExecution();
}

// has bgp policy as a separate config artifact
bool BgpServiceBase::hasPolicySymlink() {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    return false;
  }

  auto sharedData = fb303::ThreadCachedServiceData::get();
  if (sharedData->hasCounter(BgpStats::kPolicySymlink)) {
    return (sharedData->getCounter(BgpStats::kPolicySymlink));
  }

  return false;
}

// fboss policy bgp (display policy config)
void BgpServiceBase::getPolicyConfig(string& configStr) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    return;
  }

  if (!continueExecution(true)) {
    return;
  }
  auto config = configManager_->getConfig();
  configStr = config->getPolicyConfig();
  decrRequestsInExecution();
}

// bgp session summary info
folly::coro::Task<std::unique_ptr<std::vector<TBgpSession>>>
BgpServiceBase::co_getBgpSessions() {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (!continueExecution(true)) {
    co_return std::make_unique<std::vector<TBgpSession>>();
  }

  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto allPeers = co_await sessionMgr_->co_getAllPeerDisplayInfos();

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, peers = std::move(allPeers)]() {
        return peerMgr_.getSessionInfos(peers);
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::vector<TBgpSession>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(ERR, "getBgpSessions timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(ERR, "getBgpSessions failed: {}", result.exception().what());
  }
  co_return std::make_unique<std::vector<TBgpSession>>();
}

// bgp stream session summary info
folly::coro::Task<std::unique_ptr<std::vector<TBgpStreamSession>>>
BgpServiceBase::co_getBgpStreamSessions() {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return std::make_unique<std::vector<TBgpStreamSession>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::vector<TBgpStreamSession>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this]() { return peerMgr_.getBgpStreamSummary(); },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::vector<TBgpStreamSession>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getBgpStreamSessions timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(ERR, "getBgpStreamSessions failed: {}", result.exception().what());
  }
  co_return std::make_unique<std::vector<TBgpStreamSession>>();
}

// bgp neighbor detailed info (coro)
folly::coro::Task<std::unique_ptr<std::vector<TBgpSession>>>
BgpServiceBase::co_getBgpNeighbors(
    std::unique_ptr<std::vector<string>> peerAddresses) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || peerAddresses == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty peers list";
    XLOGF(
        ERR,
        "[{}]: Failed to get bgp neighbors. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::vector<TBgpSession>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::vector<TBgpSession>>();
  }

  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  std::unordered_multimap<
      folly::IPAddress,
      std::shared_ptr<nettools::bgplib::BgpPeerDisplayInfo>>
      peerInfoMap;

  if (peerAddresses->empty()) {
    peerInfoMap = co_await sessionMgr_->co_getAllPeerDisplayInfos();
  } else {
    for (const auto& peerAddr : *peerAddresses) {
      if (peerAddr.empty() || !folly::IPAddress::validate(peerAddr)) {
        continue;
      }
      auto addr = folly::IPAddress(peerAddr);
      auto peerInfoVector = co_await sessionMgr_->co_getPeerDisplayInfo(addr);
      if (!peerInfoVector.has_value()) {
        continue;
      }
      auto peerInfo = peerInfoVector.value()[0];
      peerInfoMap.emplace(
          addr,
          std::make_shared<nettools::bgplib::BgpPeerDisplayInfo>(peerInfo));
    }
  }

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, peers = std::move(peerInfoMap)]() {
        return peerMgr_.getDetailSessionInfos(peers);
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::vector<TBgpSession>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(ERR, "getBgpNeighbors timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(ERR, "getBgpNeighbors failed: {}", result.exception().what());
  }
  co_return std::make_unique<std::vector<TBgpSession>>();
}

// hold timer info (coro)
folly::coro::Task<std::unique_ptr<std::vector<THoldTimerInfo>>>
BgpServiceBase::co_getHoldTimers(
    std::unique_ptr<std::vector<string>> peerAddresses) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || peerAddresses == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Null peer addresses";
    XLOGF(
        ERR,
        "[{}]: Failed to get hold timers. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::vector<THoldTimerInfo>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::vector<THoldTimerInfo>>();
  }

  // Single coro hop: fetch all peers, then filter on handler side
  auto allPeers = co_await sessionMgr_->co_getAllPeerDisplayInfos();

  std::vector<THoldTimerInfo> holdTimerInfos;
  if (peerAddresses->empty()) {
    // No filter: return all peers
    holdTimerInfos = peerMgr_.getHoldTimerInfos(allPeers);
  } else {
    // Filter to requested peers on the handler side
    std::unordered_set<folly::IPAddress> requestedAddrs;
    for (const auto& addrStr : *peerAddresses) {
      if (!addrStr.empty() && folly::IPAddress::validate(addrStr)) {
        requestedAddrs.insert(folly::IPAddress(addrStr));
      }
    }
    std::unordered_multimap<
        folly::IPAddress,
        std::shared_ptr<nettools::bgplib::BgpPeerDisplayInfo>>
        filtered;
    for (auto& [addr, info] : allPeers) {
      if (requestedAddrs.count(addr) > 0) {
        filtered.emplace(addr, std::move(info));
      }
    }
    holdTimerInfos = peerMgr_.getHoldTimerInfos(filtered);
  }

  decrRequestsInExecution();
  co_return std::make_unique<std::vector<THoldTimerInfo>>(
      std::move(holdTimerInfos));
}

// bgp neighbors from session (coro)
folly::coro::Task<std::unique_ptr<std::vector<TBgpSession>>>
BgpServiceBase::co_getBgpNeighborsFromSession(
    std::unique_ptr<string> peerId,
    std::unique_ptr<string> sessionBgpId) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || peerId == nullptr || sessionBgpId == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits"
        : peerId == nullptr      ? "Empty peerId"
                                 : "Empty sessionBgpId";
    XLOGF(
        ERR,
        "[{}]: Failed to get bgp neighbors from session. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::vector<TBgpSession>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::vector<TBgpSession>>();
  }

  if (!folly::IPAddress::validate(*peerId) ||
      !folly::IPAddress::validate(*sessionBgpId)) {
    decrRequestsInExecution();
    co_return std::make_unique<std::vector<TBgpSession>>();
  }

  nettools::bgplib::BgpPeerId bgpPeerId{
      folly::IPAddress(*peerId), folly::IPAddressV4(*sessionBgpId).toLongHBO()};

  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto peerInfoVector = co_await sessionMgr_->co_getPeerDisplayInfo(bgpPeerId);
  if (!peerInfoVector.has_value()) {
    co_return std::make_unique<std::vector<TBgpSession>>();
  }

  std::unordered_multimap<
      folly::IPAddress,
      std::shared_ptr<nettools::bgplib::BgpPeerDisplayInfo>>
      peerInfoMap;
  peerInfoMap.emplace(
      folly::IPAddress(*peerId),
      std::make_shared<nettools::bgplib::BgpPeerDisplayInfo>(
          peerInfoVector.value()[0]));

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, peers = std::move(peerInfoMap)]() {
        return peerMgr_.getDetailSessionInfos(peers);
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::vector<TBgpSession>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getBgpNeighborsFromSession timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(
        ERR,
        "getBgpNeighborsFromSession failed: {}",
        result.exception().what());
  }
  co_return std::make_unique<std::vector<TBgpSession>>();
}

// bgp local config
void BgpServiceBase::getBgpLocalConfig(TBgpLocalConfig& retConfig) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    return;
  }
  if (!continueExecution(true)) {
    return;
  }
  auto config = configManager_->getConfig();
  auto globalConfig = config->getBgpGlobalConfig();
  retConfig.my_router_id() = globalConfig->routerId.asV4().toLong();
  // TODO: deprecate i32 asn fields T113736668
  retConfig.local_as() = globalConfig->localAsn;
  retConfig.local_as_4_byte() = globalConfig->localAsn;
  if (globalConfig->localConfedAsn) {
    retConfig.local_confed_as() = *globalConfig->localConfedAsn;
    retConfig.local_confed_as_4_byte() = *globalConfig->localConfedAsn;
  }
  retConfig.program_ucmp_weights() = globalConfig->computeUcmpFromLbwComm;
  retConfig.ucmp_width() = globalConfig->ucmpWidth;
  retConfig.enable_update_group() = globalConfig->enableUpdateGroup;
  retConfig.enable_route_refresh() = globalConfig->enableRouteRefresh;
  decrRequestsInExecution();
}

// bgp prefilter-received
folly::coro::Task<std::unique_ptr<std::map<TIpPrefix, TBgpPath>>>
BgpServiceBase::co_getPrefilterReceivedNetworks(std::unique_ptr<string> peer) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || peer == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty peer";
    XLOGF(
        ERR,
        "[{}]: Failed to get prefilter received networks. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, p = std::make_unique<string>(*peer)]() {
        std::map<TIpPrefix, TBgpPath> prefixToPath;
        peerMgr_.getNetworks(
            prefixToPath, p, RouteFilterType::PRE_FILTER_RECEIVED);
        return prefixToPath;
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getPrefilterReceivedNetworks for peer {} timed out — PeerManagerBase evb unresponsive",
        *peer);
  } else {
    XLOGF(
        ERR,
        "getPrefilterReceivedNetworks for peer {} failed: {}",
        *peer,
        result.exception().what());
  }
  co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>();
}

// bgp prefilter-received with add path
folly::coro::Task<std::unique_ptr<std::map<TIpPrefix, std::vector<TBgpPath>>>>
BgpServiceBase::co_getPrefilterReceivedNetworks2(std::unique_ptr<string> peer) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || peer == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty peer";
    XLOGF(
        ERR,
        "[{}]: Failed to get prefilter received networks: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, p = std::make_unique<string>(*peer)]() {
        std::map<TIpPrefix, std::vector<TBgpPath>> prefixToPath;
        peerMgr_.getNetworks2(
            prefixToPath, p, RouteFilterType::PRE_FILTER_RECEIVED);
        return prefixToPath;
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getPrefilterReceivedNetworks2 for peer {} timed out — PeerManagerBase evb unresponsive",
        *peer);
  } else {
    XLOGF(
        ERR,
        "getPrefilterReceivedNetworks2 for peer {} failed: {}",
        *peer,
        result.exception().what());
  }
  co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
}

folly::coro::Task<std::unique_ptr<std::map<TIpPrefix, TBgpPath>>>
BgpServiceBase::co_getPrefilterReceivedNetworksFromSession(
    std::unique_ptr<string> peer,
    std::unique_ptr<string> sessionBgpId) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || peer == nullptr || sessionBgpId == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits"
        : peer == nullptr        ? "Empty peer"
                                 : "Empty sessionBgpId";
    XLOGF(
        ERR,
        "[{}]: Failed to get prefilter received networks from session. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this,
       p = std::make_unique<string>(*peer),
       s = std::make_unique<string>(*sessionBgpId)]() {
        std::map<TIpPrefix, TBgpPath> prefixToPath;
        peerMgr_.getNetworks(
            prefixToPath, p, s, RouteFilterType::PRE_FILTER_RECEIVED);
        return prefixToPath;
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getPrefilterReceivedNetworksFromSession for peer {} session {} timed out — PeerManagerBase evb unresponsive",
        *peer,
        *sessionBgpId);
  } else {
    XLOGF(
        ERR,
        "getPrefilterReceivedNetworksFromSession for peer {} session {} failed: {}",
        *peer,
        *sessionBgpId,
        result.exception().what());
  }
  co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>();
}

folly::coro::Task<std::unique_ptr<std::map<TIpPrefix, std::vector<TBgpPath>>>>
BgpServiceBase::co_getPrefilterReceivedNetworksFromSession2(
    std::unique_ptr<string> peer,
    std::unique_ptr<string> sessionBgpId) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || peer == nullptr || sessionBgpId == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits"
        : peer == nullptr        ? "Empty peer"
                                 : "Empty sessionBgpId";
    XLOGF(
        ERR,
        "[{}]: Failed to get prefilter received networks from session. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this,
       p = std::make_unique<string>(*peer),
       s = std::make_unique<string>(*sessionBgpId)]() {
        std::map<TIpPrefix, std::vector<TBgpPath>> prefixToPath;
        peerMgr_.getNetworks2(
            prefixToPath, p, s, RouteFilterType::PRE_FILTER_RECEIVED);
        return prefixToPath;
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getPrefilterReceivedNetworksFromSession2 for peer {} session {} timed out — PeerManagerBase evb unresponsive",
        *peer,
        *sessionBgpId);
  } else {
    XLOGF(
        ERR,
        "getPrefilterReceivedNetworksFromSession2 for peer {} session {} failed: {}",
        *peer,
        *sessionBgpId,
        result.exception().what());
  }
  co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
}

// bgp postfilter-received
folly::coro::Task<std::unique_ptr<std::map<TIpPrefix, TBgpPath>>>
BgpServiceBase::co_getPostfilterReceivedNetworks(std::unique_ptr<string> peer) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || peer == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty peer";
    XLOGF(
        ERR,
        "[{}]: Failed to get postfilter received networks. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, p = std::make_unique<string>(*peer)]() {
        std::map<TIpPrefix, TBgpPath> prefixToPath;
        peerMgr_.getNetworks(
            prefixToPath, p, RouteFilterType::POST_FILTER_RECEIVED);
        return prefixToPath;
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getPostfilterReceivedNetworks for peer {} timed out — PeerManagerBase evb unresponsive",
        *peer);
  } else {
    XLOGF(
        ERR,
        "getPostfilterReceivedNetworks for peer {} failed: {}",
        *peer,
        result.exception().what());
  }
  co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>();
}

// bgp postfilter-received with add path
folly::coro::Task<std::unique_ptr<std::map<TIpPrefix, std::vector<TBgpPath>>>>
BgpServiceBase::co_getPostfilterReceivedNetworks2(
    std::unique_ptr<string> peer) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || peer == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty peer";
    XLOGF(
        ERR,
        "[{}]: Failed to get postfilter received networks. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, p = std::make_unique<string>(*peer)]() {
        std::map<TIpPrefix, std::vector<TBgpPath>> prefixToPath;
        peerMgr_.getNetworks2(
            prefixToPath, p, RouteFilterType::POST_FILTER_RECEIVED);
        return prefixToPath;
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getPostfilterReceivedNetworks2 for peer {} timed out — PeerManagerBase evb unresponsive",
        *peer);
  } else {
    XLOGF(
        ERR,
        "getPostfilterReceivedNetworks2 for peer {} failed: {}",
        *peer,
        result.exception().what());
  }
  co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
}

folly::coro::Task<std::unique_ptr<std::map<TIpPrefix, TBgpPath>>>
BgpServiceBase::co_getPostfilterReceivedNetworksFromSession(
    std::unique_ptr<string> peer,
    std::unique_ptr<string> sessionBgpId) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || peer == nullptr || sessionBgpId == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits"
        : peer == nullptr        ? "Empty peer"
                                 : "Empty sessionBgpId";
    XLOGF(
        ERR,
        "[{}]: Failed to get postfilter received networks from session. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this,
       p = std::make_unique<string>(*peer),
       s = std::make_unique<string>(*sessionBgpId)]() {
        std::map<TIpPrefix, TBgpPath> prefixToPath;
        peerMgr_.getNetworks(
            prefixToPath, p, s, RouteFilterType::POST_FILTER_RECEIVED);
        return prefixToPath;
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getPostfilterReceivedNetworksFromSession for peer {} session {} timed out — PeerManagerBase evb unresponsive",
        *peer,
        *sessionBgpId);
  } else {
    XLOGF(
        ERR,
        "getPostfilterReceivedNetworksFromSession for peer {} session {} failed: {}",
        *peer,
        *sessionBgpId,
        result.exception().what());
  }
  co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>();
}

folly::coro::Task<std::unique_ptr<std::map<TIpPrefix, std::vector<TBgpPath>>>>
BgpServiceBase::co_getPostfilterReceivedNetworksFromSession2(
    std::unique_ptr<string> peer,
    std::unique_ptr<string> sessionBgpId) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || peer == nullptr || sessionBgpId == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits"
        : peer == nullptr        ? "Empty peer"
                                 : "Empty sessionBgpId";
    XLOGF(
        ERR,
        "[{}]: Failed to get postfilter received networks from session. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this,
       p = std::make_unique<string>(*peer),
       s = std::make_unique<string>(*sessionBgpId)]() {
        std::map<TIpPrefix, std::vector<TBgpPath>> prefixToPath;
        peerMgr_.getNetworks2(
            prefixToPath, p, s, RouteFilterType::POST_FILTER_RECEIVED);
        return prefixToPath;
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getPostfilterReceivedNetworksFromSession2 for peer {} session {} timed out — PeerManagerBase evb unresponsive",
        *peer,
        *sessionBgpId);
  } else {
    XLOGF(
        ERR,
        "getPostfilterReceivedNetworksFromSession2 for peer {} session {} failed: {}",
        *peer,
        *sessionBgpId,
        result.exception().what());
  }
  co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
}

// bgp prefilter-advertised
folly::coro::Task<std::unique_ptr<std::map<TIpPrefix, TBgpPath>>>
BgpServiceBase::co_getPrefilterAdvertisedNetworks(
    std::unique_ptr<string> peer) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || peer == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty peer";
    XLOGF(
        ERR,
        "[{}]: Failed to get prefilter advertised networks. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, p = std::make_unique<string>(*peer)]() {
        std::map<TIpPrefix, TBgpPath> prefixToPath;
        peerMgr_.getNetworks(
            prefixToPath, p, RouteFilterType::PRE_FILTER_ADVERTISED);
        return prefixToPath;
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getPrefilterAdvertisedNetworks timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(
        ERR,
        "getPrefilterAdvertisedNetworks failed: {}",
        result.exception().what());
  }
  co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>();
}

// bgp prefilter-advertised with add path
folly::coro::Task<std::unique_ptr<std::map<TIpPrefix, std::vector<TBgpPath>>>>
BgpServiceBase::co_getPrefilterAdvertisedNetworks2(
    std::unique_ptr<string> peer) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || peer == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty peer";
    XLOGF(
        ERR,
        "[{}]: Failed to get prefilter advertised networks. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, p = std::make_unique<string>(*peer)]() {
        std::map<TIpPrefix, std::vector<TBgpPath>> prefixToPath;
        peerMgr_.getNetworks2(
            prefixToPath, p, RouteFilterType::PRE_FILTER_ADVERTISED);
        return prefixToPath;
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getPrefilterAdvertisedNetworks2 timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(
        ERR,
        "getPrefilterAdvertisedNetworks2 failed: {}",
        result.exception().what());
  }
  co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
}

// bgp postfilter-advertised
folly::coro::Task<std::unique_ptr<std::map<TIpPrefix, TBgpPath>>>
BgpServiceBase::co_getPostfilterAdvertisedNetworks(
    std::unique_ptr<string> peer) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || peer == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty peer";
    XLOGF(
        ERR,
        "[{}]: Failed to get post filter advertised networks. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, p = std::make_unique<string>(*peer)]() {
        std::map<TIpPrefix, TBgpPath> prefixToPath;
        peerMgr_.getNetworks(
            prefixToPath, p, RouteFilterType::POST_FILTER_ADVERTISED);
        return prefixToPath;
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getPostfilterAdvertisedNetworks timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(
        ERR,
        "getPostfilterAdvertisedNetworks failed: {}",
        result.exception().what());
  }
  co_return std::make_unique<std::map<TIpPrefix, TBgpPath>>();
}

// bgp postfilter-advertised with add path
folly::coro::Task<std::unique_ptr<std::map<TIpPrefix, std::vector<TBgpPath>>>>
BgpServiceBase::co_getPostfilterAdvertisedNetworks2(
    std::unique_ptr<string> peer) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || peer == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty peer";
    XLOGF(
        ERR,
        "[{}]: Failed to get post filter advertised networks. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, p = std::make_unique<string>(*peer)]() {
        std::map<TIpPrefix, std::vector<TBgpPath>> prefixToPath;
        peerMgr_.getNetworks2(
            prefixToPath, p, RouteFilterType::POST_FILTER_ADVERTISED);
        return prefixToPath;
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getPostfilterAdvertisedNetworks2 timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(
        ERR,
        "getPostfilterAdvertisedNetworks2 failed: {}",
        result.exception().what());
  }
  co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
}

namespace {

/*
 * INGRESS and EGRESS are the two directions this endpoint can serve. BOTH is
 * not one of them -- rejection is resolved against a single Adj-RIB per walk
 * -- so it is refused here rather than answered for one side.
 */
bool isServableDirection(TDirectionFilter direction) {
  switch (direction) {
    case TDirectionFilter::INGRESS:
    case TDirectionFilter::EGRESS:
      return true;
    case TDirectionFilter::BOTH:
      break;
  }
  return false;
}

/*
 * Shared by both getRejectedNetworks* handlers so they apply identical
 * filtering, peer resolution and walk semantics. Returns the number of
 * neighbors the filters matched, or nullopt if the request was refused before
 * any peer was walked.
 *
 * Both event base hops -- peer resolution here and each per-peer walk in
 * co_walkRejectedNetworkPeers -- are individually wrapped in
 * co_runOnEvbWithTimeout, so callers do not add timeout protection of their
 * own: this coroutine itself does no evb work to protect.
 */
folly::coro::Task<std::optional<int32_t>> co_walkRejectedNetworks(
    PeerManagerBase& peerMgr,
    const TRejectedNetworksRequest& request,
    RejectedNetworksAccumulator& accumulator) {
  const auto direction = folly::copy(request.direction().value());
  if (!isServableDirection(direction)) {
    XLOGF(
        ERR,
        "getRejectedNetworks: unsupported direction {}",
        magic_enum::enum_name(direction));
    co_return std::nullopt;
  }

  /*
   * Everything the evb lambdas below touch is captured by value, with the
   * regexes behind shared_ptr because re2::RE2 is neither copyable nor
   * movable. co_runOnEvbWithTimeout abandons the *wait* when the timeout
   * fires but cannot cancel work already queued on the event base, so a
   * lambda may still run after this coroutine has returned and its frame has
   * been destroyed. Owning its inputs keeps that late run harmless instead of
   * a use-after-free -- and a wedged peer manager evb is exactly the case the
   * timeout exists for.
   */
  std::shared_ptr<re2::RE2> nameFilter;
  std::shared_ptr<re2::RE2> policyFilter;
  if (!compileOptionalRegex(
          *request.neighbor_name_regex(), nameFilter, "neighbor name") ||
      !compileOptionalRegex(
          *request.policy_name_regex(), policyFilter, "policy name")) {
    co_return std::nullopt;
  }

  auto peersResult = co_await co_runOnEvbWithTimeout(
      peerMgr.getEventBase(),
      [&peerMgr, nameFilter, peers = *request.peers()]() {
        return peerMgr.resolveRejectedNetworkPeers(nameFilter.get(), peers);
      },
      kPeerMgrThriftHandlerTimeout);

  if (peersResult.hasException()) {
    XLOGF(
        ERR,
        "getRejectedNetworks: failed to resolve peers: {}",
        peersResult.exception().what());
    co_return std::nullopt;
  }

  const auto& peerIds = peersResult.value();
  const RejectedNetworksWalk walk{
      .direction = direction,
      .policyFilter = std::move(policyFilter),
      .prefixFilter = {request.prefixes()->begin(), request.prefixes()->end()},
      .afi = request.afi().to_optional()};
  co_await co_walkRejectedNetworkPeers(
      peerMgr, peerIds, walk, accumulator, kPeerMgrThriftHandlerTimeout);

  co_return static_cast<int32_t>(peerIds.size());
}

} // namespace

folly::coro::Task<void> co_walkRejectedNetworkPeers(
    PeerManagerBase& peerMgr,
    const std::vector<nettools::bgplib::BgpPeerId>& peerIds,
    const RejectedNetworksWalk& walk,
    RejectedNetworksAccumulator& accumulator,
    std::chrono::milliseconds timeout) {
  /*
   * One evb hop per peer. Walking every peer inside a single hop would hold
   * the PeerManager event base for the whole scan and stall BGP on a device
   * with a large RIB and many peers.
   */
  for (size_t i = 0; i < peerIds.size(); ++i) {
    co_await folly::coro::co_safe_point;

    const auto& peerId = peerIds[i];
    auto result = co_await co_runOnEvbWithTimeout(
        peerMgr.getEventBase(),
        [&peerMgr, peerId, walk]() {
          const RejectedNetworksQuery query{
              .direction = walk.direction,
              .policyNameFilter = walk.policyFilter.get(),
              .prefixFilter = walk.prefixFilter,
              .afi = walk.afi,
              .maxEntries = kMaxTrackedRejectedGroups};
          return peerMgr.getRejectedNetworksForPeer(peerId, query);
        },
        timeout);

    if (result.hasException()) {
      if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
        /*
         * The timed-out walk stays queued on the event base; it cannot be
         * cancelled. Queuing the next peer's walk behind it would only pile
         * more work onto an event base that is already not keeping up.
         */
        const auto abandoned = peerIds.size() - i;
        XLOGF(
            ERR,
            "getRejectedNetworks: walk timed out for peer {}, abandoning it "
            "and the {} peer(s) not yet walked",
            peerId.peerAddr.str(),
            abandoned - 1);
        accumulator.addFailedPeers(static_cast<int32_t>(abandoned));
        co_return;
      }
      XLOGF(
          ERR,
          "getRejectedNetworks: walk failed for peer {}: {}",
          peerId.peerAddr.str(),
          result.exception().what());
      accumulator.addFailedPeers(1);
      continue;
    }

    const auto& rejected = result.value();
    accumulator.setCurrentPeer(peerId.peerAddr.str(), peerId.peerDescription);
    for (const auto& [policyName, count] : rejected.pathCountByPolicy) {
      accumulator.addRejectedPaths(policyName, count);
    }
    for (const auto& entry : rejected.entries) {
      accumulator.addRejectedPrefix(entry.prefix, entry.policyName);
    }
    if (rejected.truncated) {
      accumulator.markTruncated();
    }
  }
}

folly::coro::Task<std::unique_ptr<TRejectedNetworksSummaryResponse>>
BgpServiceBase::co_getRejectedNetworksSummary(
    std::unique_ptr<TRejectedNetworksRequest> request) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || request == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty request";
    XLOGF(
        ERR,
        "[{}]: Failed to get rejected networks summary. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<TRejectedNetworksSummaryResponse>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<TRejectedNetworksSummaryResponse>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  RejectedNetworksAccumulator accumulator{{.maxGroups = 0}};
  auto matched =
      co_await co_walkRejectedNetworks(peerMgr_, *request, accumulator);
  if (!matched.has_value()) {
    co_return std::make_unique<TRejectedNetworksSummaryResponse>();
  }

  co_return std::make_unique<TRejectedNetworksSummaryResponse>(
      accumulator.buildSummary(*matched));
}

folly::coro::Task<std::unique_ptr<TRejectedNetworksDetailsResponse>>
BgpServiceBase::co_getRejectedNetworksDetails(
    std::unique_ptr<TRejectedNetworksRequest> request) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || request == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty request";
    XLOGF(
        ERR,
        "[{}]: Failed to get rejected networks details. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<TRejectedNetworksDetailsResponse>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<TRejectedNetworksDetailsResponse>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  const auto maxGroups = folly::copy(request->max_groups().value());
  RejectedNetworksAccumulator accumulator{
      {.maxGroups = maxGroups > 0
           ? std::min(static_cast<size_t>(maxGroups), kMaxTrackedRejectedGroups)
           : kMaxTrackedRejectedGroups}};
  auto matched =
      co_await co_walkRejectedNetworks(peerMgr_, *request, accumulator);
  if (!matched.has_value()) {
    co_return std::make_unique<TRejectedNetworksDetailsResponse>();
  }

  co_return std::make_unique<TRejectedNetworksDetailsResponse>(
      accumulator.buildDetails(*matched));
}

// Get post-policy network information for stream subscribers
folly::coro::Task<std::unique_ptr<std::map<TIpPrefix, std::vector<TBgpPath>>>>
BgpServiceBase::co_getSubscriberNetworkInfo(
    int32_t peerID,
    std::unique_ptr<string> policy_type) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (exitInitiated_ || policy_type == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty policy_type";
    XLOGF(
        ERR,
        "[{}]: Failed to get subscriber network info. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
  }
  RouteFilterType type;
  if (*policy_type == "pre-policy") {
    type = RouteFilterType::PRE_FILTER_ADVERTISED;
  } else if (*policy_type == "post-policy") {
    type = RouteFilterType::POST_FILTER_ADVERTISED;
  } else {
    XLOGF(INFO, "Invalid policy_type string: {}", *policy_type);
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, peerID, type]() {
        std::map<TIpPrefix, std::vector<TBgpPath>> prefixToPath;
        peerMgr_.getSubscriberNetworks(prefixToPath, peerID, type);
        return prefixToPath;
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getSubscriberNetworkInfo timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(
        ERR, "getSubscriberNetworkInfo failed: {}", result.exception().what());
  }
  co_return std::make_unique<std::map<TIpPrefix, std::vector<TBgpPath>>>();
}

void BgpServiceBase::changeSessionStateHelper(
    const string& peer,
    std::function<void(folly::CIDRNetwork)> functionForDynamicPeer,
    std::function<void(folly::IPAddress)> functionForStaticPeer) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (exitInitiated_) {
    return;
  }
  // Invalid input
  if (peer.empty()) {
    return;
  }
  if (folly::IPAddress::validate(peer)) {
    // peer is an IP address
    auto peerAddr = folly::IPAddress(peer);
    functionForStaticPeer(peerAddr);
    return;
  }
  auto mayBeNetwork = folly::IPAddress::tryCreateNetwork(peer);
  if (mayBeNetwork.hasValue()) {
    // peer is a prefix
    auto prefix = mayBeNetwork.value();
    functionForDynamicPeer(prefix);
    return;
  }
  // invalid peer
  XLOGF(INFO, "Peer {} is not a valid prefix or IP address", peer);
}

/*
 * Timeout protects the thrift thread pool, not the operation itself.
 * On timeout the lambda may still execute when the evb unsticks — this is
 * by design, consistent with all other co_runOnEvbWithTimeout handlers.
 * All captures are by value to avoid use-after-free if the lambda outlives
 * the coroutine frame.
 */
folly::coro::Task<void> BgpServiceBase::co_shutdownSession(
    std::unique_ptr<string> peer) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (exitInitiated_ || peer == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty peer";
    XLOGF(
        ERR,
        "[{}]: Failed to shutdown session. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return;
  }
  auto peerStr = *peer;
  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, peerStr = std::move(peerStr)]() {
        changeSessionStateHelper(
            peerStr,
            [this](folly::CIDRNetwork p) { sessionMgr_->shutdownSession(p); },
            [this](folly::IPAddress a) { sessionMgr_->shutdownSession(a); });
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasException()) {
    if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
      XLOGF(ERR, "shutdownSession timed out — evb unresponsive");
    } else {
      XLOGF(ERR, "shutdownSession failed: {}", result.exception().what());
    }
  }
}

folly::coro::Task<void> BgpServiceBase::co_restartSession(
    std::unique_ptr<string> peer) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (exitInitiated_ || peer == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty peer";
    XLOGF(
        ERR,
        "[{}]: Failed to get restart session. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return;
  }
  auto peerStr = *peer;
  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, peerStr = std::move(peerStr)]() {
        changeSessionStateHelper(
            peerStr,
            [this](folly::CIDRNetwork p) { sessionMgr_->restartSession(p); },
            [this](folly::IPAddress a) { sessionMgr_->restartSession(a); });
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasException()) {
    if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
      XLOGF(ERR, "restartSession timed out — evb unresponsive");
    } else {
      XLOGF(ERR, "restartSession failed: {}", result.exception().what());
    }
  }
}

folly::coro::Task<void> BgpServiceBase::co_startSession(
    std::unique_ptr<string> peer) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (exitInitiated_ || peer == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty peer";
    XLOGF(
        ERR,
        "[{}]: Failed to start session. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return;
  }
  auto peerStr = *peer;
  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, peerStr = std::move(peerStr)]() {
        changeSessionStateHelper(
            peerStr,
            [this](folly::CIDRNetwork p) { sessionMgr_->startSession(p); },
            [this](folly::IPAddress a) { sessionMgr_->startSession(a); });
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasException()) {
    if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
      XLOGF(ERR, "startSession timed out — evb unresponsive");
    } else {
      XLOGF(ERR, "startSession failed: {}", result.exception().what());
    }
  }
}

folly::coro::Task<void> BgpServiceBase::co_clearBgpNeighbor(
    std::unique_ptr<string> peer,
    ClearBgpNeighborDirection direction,
    TBgpAfi afi) {
  // @lint-ignore NULLSAFECLANG nullable-argument LogThriftCall handles null.
  auto log = LOG_THRIFT_CALL(INFO);
  co_await co_clearBgpNeighborImpl(std::move(peer), direction, afi);
}

folly::coro::Task<void> BgpServiceBase::co_clearBgpNeighborImpl(
    std::unique_ptr<string> peer,
    ClearBgpNeighborDirection direction,
    TBgpAfi afi) {
  if (exitInitiated_ || peer == nullptr || peer->empty()) {
    const auto errStr = exitInitiated_ ? "Service is exiting" : "Empty peer";
    throw apache::thrift::TApplicationException(
        apache::thrift::TApplicationException::INTERNAL_ERROR,
        fmt::format("Failed to clear bgp neighbor: {}", errStr));
  }

  if (direction == ClearBgpNeighborDirection::HARD_RESET &&
      afi != TBgpAfi::AFI_ALL) {
    throw apache::thrift::TApplicationException(
        apache::thrift::TApplicationException::UNKNOWN,
        "AFI filter is not supported for hard reset");
  }

  std::optional<nettools::bgplib::BgpUpdateAfi> bgpAfi = std::nullopt;
  switch (afi) {
    case TBgpAfi::AFI_ALL:
      bgpAfi = std::nullopt;
      break;
    case TBgpAfi::AFI_IPV4:
      bgpAfi = nettools::bgplib::BgpUpdateAfi::AFI_IPv4;
      break;
    case TBgpAfi::AFI_IPV6:
      bgpAfi = nettools::bgplib::BgpUpdateAfi::AFI_IPv6;
      break;
    default:
      /*
       * Reject any wire value (including UNKNOWN or an int from a
       * newer/older thrift schema) that doesn't map to a defined AFI.
       * Falling through to nullopt would silently widen the operation to
       * AFI_ALL, which the caller didn't ask for.
       */
      throw apache::thrift::TApplicationException(
          apache::thrift::TApplicationException::INVALID_PROTOCOL,
          fmt::format(
              "Unrecognized AFI value {} for clear bgp neighbor on peer {}",
              static_cast<int>(afi),
              *peer));
  }

  if (!continueExecution(true)) {
    throw apache::thrift::TApplicationException(
        apache::thrift::TApplicationException::INTERNAL_ERROR,
        "clearBgpNeighbor: request rejected by thrift request dampening");
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  if (!folly::IPAddress::validate(*peer)) {
    throw apache::thrift::TApplicationException(
        apache::thrift::TApplicationException::UNKNOWN,
        fmt::format(
            "Peer {} is not a valid IP address. "
            "CIDR prefix is not supported for clear bgp neighbor",
            *peer));
  }
  const auto peerAddr = folly::IPAddress(*peer);

  switch (direction) {
    case ClearBgpNeighborDirection::HARD_RESET: {
      /*
       * Resolve on the PeerManager EVB before dispatching to the SessionManager
       * EVB. restartSession tolerates a peer disappearing between the two.
       */
      auto resolveResult = co_await co_runOnEvbWithTimeout(
          peerMgr_.getEventBase(),
          [this, peerAddr]() { resolveSingleEstablishedPeerId(peerAddr); },
          kPeerMgrThriftHandlerTimeout);
      if (resolveResult.hasException()) {
        co_yield folly::coro::co_error(std::move(resolveResult).exception());
      }

      auto result = co_await co_runOnEvbWithTimeout(
          sessionMgr_->getEventBase(),
          [this, peerAddr]() { sessionMgr_->restartSession(peerAddr); },
          kSessionMgrThriftHandlerTimeout);
      if (result.hasException()) {
        co_yield folly::coro::co_error(std::move(result).exception());
      }
      break;
    }
    case ClearBgpNeighborDirection::ROUTE_REFRESH_IN: {
      auto result = co_await co_runOnEvbWithTimeout(
          peerMgr_.getEventBase(),
          [this, peerAddr, bgpAfi]() {
            const auto peerId = resolveSingleEstablishedPeerId(peerAddr);
            if (!peerMgr_.triggerRouteRefreshRequestForPeer(peerId, bgpAfi)) {
              throw apache::thrift::TApplicationException(
                  apache::thrift::TApplicationException::INTERNAL_ERROR,
                  fmt::format(
                      "Failed to queue all requested Route Refresh messages "
                      "for peer {}; any messages already queued may still be "
                      "sent",
                      peerAddr.str()));
            }
          },
          kPeerMgrThriftHandlerTimeout);
      if (result.hasException()) {
        co_yield folly::coro::co_error(std::move(result).exception());
      }
      break;
    }
    case ClearBgpNeighborDirection::ROUTE_REFRESH_OUT: {
      auto result = co_await co_runOnEvbWithTimeout(
          peerMgr_.getEventBase(),
          [this, peerAddr, bgpAfi]() {
            const auto peerId = resolveSingleEstablishedPeerId(peerAddr);
            if (!peerMgr_.processRouteRefreshForPeer(peerId, bgpAfi)) {
              throw apache::thrift::TApplicationException(
                  apache::thrift::TApplicationException::INTERNAL_ERROR,
                  fmt::format(
                      "Process route refresh failed for peer {}",
                      peerAddr.str()));
            }
          },
          kPeerMgrThriftHandlerTimeout);
      if (result.hasException()) {
        co_yield folly::coro::co_error(std::move(result).exception());
      }
      break;
    }
    case ClearBgpNeighborDirection::UNKNOWN:
      throw apache::thrift::TApplicationException(
          apache::thrift::TApplicationException::UNKNOWN,
          fmt::format(
              "Unknown clear bgp neighbor direction for peer {}", *peer));
    default:
      /*
       * Defensive: thrift wire can deliver an enum int that doesn't match
       * any defined value (e.g., older client compiled against newer
       * schema). Reject explicitly so the caller doesn't see a silent
       * success.
       */
      throw apache::thrift::TApplicationException(
          apache::thrift::TApplicationException::INVALID_PROTOCOL,
          fmt::format(
              "Unrecognized clear bgp neighbor direction value {} for peer {}",
              static_cast<int>(direction),
              *peer));
  }
}

nettools::bgplib::BgpPeerId BgpServiceBase::resolveSingleEstablishedPeerId(
    const folly::IPAddress& peerAddr) {
  const auto peerIds = peerMgr_.getEstablishedPeerIdsForAddr(peerAddr);
  if (peerIds.empty()) {
    throw apache::thrift::TApplicationException(
        apache::thrift::TApplicationException::UNKNOWN,
        fmt::format(
            "No established peer found for address {}", peerAddr.str()));
  }
  if (peerIds.size() > 1) {
    /*
     * Multiple peerIds for one IP usually mean a session-flap-with-router-ID-
     * change or VIP multi-session is in progress. Refuse to act ambiguously.
     */
    throw apache::thrift::TApplicationException(
        apache::thrift::TApplicationException::INTERNAL_ERROR,
        fmt::format(
            "Multiple ({}) established peers found for address {} — ambiguous, refusing",
            peerIds.size(),
            peerAddr.str()));
  }
  return peerIds[0];
}

// used by fbossdeploy
folly::coro::Task<std::unique_ptr<std::map<TIpPrefix, TBgpNetwork>>>
BgpServiceBase::co_getAdvertisedNetworksFiltered(
    std::unique_ptr<string> peer,
    std::unique_ptr<std::vector<TIpPrefix>> prefixes) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || peer == nullptr || prefixes == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits"
        : peer == nullptr        ? "Empty peer"
                                 : "Empty prefixes";
    XLOGF(
        ERR,
        "[{}]: Failed to get advertised networks filtered. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::map<TIpPrefix, TBgpNetwork>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::map<TIpPrefix, TBgpNetwork>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, p = std::make_unique<string>(*peer)]() {
        std::map<TIpPrefix, TBgpPath> prefixToPath;
        peerMgr_.getNetworks(
            prefixToPath, p, RouteFilterType::POST_FILTER_ADVERTISED);
        return prefixToPath;
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    auto prefixToNetwork = std::make_unique<std::map<TIpPrefix, TBgpNetwork>>();
    for (const auto& prefix : *prefixes) {
      auto it = result.value().find(prefix);
      if (it != result.value().end()) {
        (*prefixToNetwork)[prefix] = toBgpNetwork(prefix, it->second);
      }
    }
    co_return std::move(prefixToNetwork);
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getAdvertisedNetworksFiltered timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(
        ERR,
        "getAdvertisedNetworksFiltered failed: {}",
        result.exception().what());
  }
  co_return std::make_unique<std::map<TIpPrefix, TBgpNetwork>>();
}

// used by fcr
folly::coro::Task<std::unique_ptr<std::vector<TBgpNetwork>>>
BgpServiceBase::co_getReceivedNetworks(std::unique_ptr<string> peer) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || peer == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty peer";
    XLOGF(
        ERR,
        "[{}]: Failed to get received networks. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::vector<TBgpNetwork>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::vector<TBgpNetwork>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, p = std::make_unique<string>(*peer)]() {
        std::map<TIpPrefix, TBgpPath> prefixToPath;
        peerMgr_.getNetworks(
            prefixToPath, p, RouteFilterType::PRE_FILTER_RECEIVED);
        return prefixToPath;
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    auto networks = std::make_unique<std::vector<TBgpNetwork>>();
    networks->reserve(result.value().size());
    for (const auto& kv : result.value()) {
      networks->emplace_back(toBgpNetwork(kv.first, kv.second));
    }
    co_return std::move(networks);
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getReceivedNetworks timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(ERR, "getReceivedNetworks failed: {}", result.exception().what());
  }
  co_return std::make_unique<std::vector<TBgpNetwork>>();
}

folly::coro::Task<std::unique_ptr<std::vector<TBgpNetwork>>>
BgpServiceBase::co_getAdvertisedNetworks(std::unique_ptr<string> peer) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || peer == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty peer";
    XLOGF(
        ERR,
        "[{}]: Failed to get advertised networks. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::vector<TBgpNetwork>>();
  }
  if (!continueExecution(true)) {
    co_return std::make_unique<std::vector<TBgpNetwork>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, p = std::make_unique<string>(*peer)]() {
        std::map<TIpPrefix, TBgpPath> prefixToPath;
        peerMgr_.getNetworks(
            prefixToPath, p, RouteFilterType::POST_FILTER_ADVERTISED);
        return prefixToPath;
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    auto networks = std::make_unique<std::vector<TBgpNetwork>>();
    networks->reserve(result.value().size());
    for (const auto& kv : result.value()) {
      networks->emplace_back(toBgpNetwork(kv.first, kv.second));
    }
    co_return std::move(networks);
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getAdvertisedNetworks timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(ERR, "getAdvertisedNetworks failed: {}", result.exception().what());
  }
  co_return std::make_unique<std::vector<TBgpNetwork>>();
}

folly::coro::Task<std::unique_ptr<TGetDeduplicatorStatsResponse>>
BgpServiceBase::co_getDeduplicatorStats(
    std::unique_ptr<TGetDeduplicatorStatsRequest> /* request */) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return std::make_unique<TGetDeduplicatorStatsResponse>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<TGetDeduplicatorStatsResponse>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  co_return std::make_unique<TGetDeduplicatorStatsResponse>(
      PeerManagerBase::getDeduplicatorStats());
}

folly::coro::Task<std::unique_ptr<TGetAdjRibStatsResponse>>
BgpServiceBase::co_getAdjRibStats(
    std::unique_ptr<TGetAdjRibStatsRequest> request) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return std::make_unique<TGetAdjRibStatsResponse>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<TGetAdjRibStatsResponse>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  const auto direction = request->direction().value();
  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, direction]() { return peerMgr_.getAdjRibStats(direction); },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<TGetAdjRibStatsResponse>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(ERR, "getAdjRibStats timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(ERR, "getAdjRibStats failed: {}", result.exception().what());
  }
  co_return std::make_unique<TGetAdjRibStatsResponse>();
}

/**
 * Deprecated wire-compatibility placeholder. Deduplicator statistics are
 * served by getDeduplicatorStats() without walking the Adj-RIB; the legacy
 * total, reference-count, and average fields are intentionally unavailable.
 * Keep this handler empty until the legacy Thrift method can be removed.
 */
folly::coro::Task<std::unique_ptr<TAttributeStats>>
BgpServiceBase::co_getAttributeStats() {
  co_return std::make_unique<TAttributeStats>();
}

folly::coro::Task<
    std::unique_ptr<facebook::neteng::routing::policy::thrift::TPolicyStats>>
BgpServiceBase::co_getPolicyStats() {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return std::make_unique<
        facebook::neteng::routing::policy::thrift::TPolicyStats>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<
        facebook::neteng::routing::policy::thrift::TPolicyStats>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this]() {
        facebook::neteng::routing::policy::thrift::TPolicyStats stats;
        peerMgr_.getPolicyStats(stats);
        return stats;
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<
        facebook::neteng::routing::policy::thrift::TPolicyStats>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(ERR, "getPolicyStats timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(ERR, "getPolicyStats failed: {}", result.exception().what());
  }
  co_return std::make_unique<
      facebook::neteng::routing::policy::thrift::TPolicyStats>();
}

void BgpServiceBase::setDebugLevel(TBgpDebugLevel level) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (exitInitiated_) {
    return;
  }

  folly::LogLevel loggerLevel;
  switch (level) {
    case TBgpDebugLevel::DEBUG0:
      loggerLevel = folly::LogLevel::DBG0;
      break;
    case TBgpDebugLevel::DEBUG1:
      loggerLevel = folly::LogLevel::DBG1;
      break;
    case TBgpDebugLevel::DEBUG2:
      loggerLevel = folly::LogLevel::DBG2;
      break;
    case TBgpDebugLevel::DEBUG3:
      loggerLevel = folly::LogLevel::DBG3;
      break;
    case TBgpDebugLevel::DEBUG4:
      loggerLevel = folly::LogLevel::DBG4;
      break;
    case TBgpDebugLevel::DEBUG5:
      loggerLevel = folly::LogLevel::DBG5;
      break;
    case TBgpDebugLevel::INFO:
      loggerLevel = folly::LogLevel::INFO;
      break;
    default:
      return;
  }
  auto rootCategory = folly::LoggerDB::get().getCategory("");
  folly::LoggerDB::get().setLevel(rootCategory, loggerLevel, true);
}

/*
 * [Logging]
 */
static constexpr auto kDefaultAsync = "default:async=true";

void BgpServiceBase::setLogLevel(std::unique_ptr<std::string> levelString) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (levelString->find(kDefaultAsync) != std::string::npos) {
    fb303::ThreadCachedServiceData::get()->setOption("logging", *levelString);
  } else {
    fb303::ThreadCachedServiceData::get()->setOption(
        "logging", *levelString + ";" + kDefaultAsync);
  }
}

/*
 * [Config]
 */

// config parsing + sanity check
void BgpServiceBase::validateConfig(
    TResult& ret,
    std::unique_ptr<std::string> file_name) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (exitInitiated_ || file_name == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty file name";
    XLOGF(
        ERR,
        "[{}]: Failed to verify config file. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    setTResult(ret, false, errStr);
    return;
  }

  if (!continueExecution(true)) {
    ret.err() = "Overload shedding";
    ret.success() = false;
    return;
  }

  try {
    // Create policy manager using new config file
    auto config = configManager_->getConfig()->createConfigFromFile(*file_name);
    auto policyManager = Config::createPolicyManager(config);
    ret.success() = true;
  } catch (const std::exception& ex) {
    /*
     * This is a giant catch all. Config parsing, policy parsing does lot of
     * sanity checks on config file and throw errors so that main() exits for
     * errors in config file. For dryRun we do not want to exit or crash
     * for any mistakes in config files. Hence this giant catch all.
     */
    XLOGF(
        ERR,
        "Verify config file {} failed. Error: {}",
        *file_name,
        folly::exceptionStr(ex));
    ret.success() = false;
    ret.err() = folly::exceptionStr(ex);
  }

  decrRequestsInExecution();
}

// config and policy parsing + sanity check
void BgpServiceBase::validateConfigAndPolicy(
    TResult& ret,
    std::unique_ptr<std::string> config_file_name,
    std::unique_ptr<std::string> policy_file_name) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (exitInitiated_ || config_file_name == nullptr ||
      policy_file_name == nullptr) {
    std::string errStr;
    if (config_file_name == nullptr) {
      errStr = exitInitiated_ ? "Session exits" : "Empty config file name";
    } else if (policy_file_name == nullptr) {
      errStr = exitInitiated_ ? "Session exits" : "Empty policy file name";
    }
    XLOGF(
        ERR,
        "[{}]: Failed to verify config and policy file. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    setTResult(ret, false, errStr);
    return;
  }

  if (!continueExecution(true)) {
    ret.err() = "Overload shedding";
    ret.success() = false;
    return;
  }

  try {
    // Create policy manager using new config file
    ret.success() = false;
    auto config =
        configManager_->getConfig()->createConfigFromFile(*config_file_name);
    if (config) {
      // Validates JSON syntax via folly::parseJson() first
      config->setPolicyConfigFromFile(*policy_file_name);
      auto policyManager = Config::createPolicyManager(config);
      if (policyManager) {
        ret.success() = true;
      }
    }
  } catch (const std::exception& ex) {
    /*
     * This is a giant catch all. Config parsing, policy parsing does lot of
     * sanity checks on config file and throw errors so that main() exits for
     * errors in config file. For dryRun we do not want to exit or crash
     * for any mistakes in config files. Hence this giant catch all.
     */
    XLOGF(
        ERR,
        "Verify config file {} and policy file {} failed. Error: {}",
        *config_file_name,
        *policy_file_name,
        folly::exceptionStr(ex));
    ret.success() = false;
    ret.err() = folly::exceptionStr(ex);
  }

  decrRequestsInExecution();
}

void BgpServiceBase::getDrainState(TBgpDrainState& ret) {
  auto log = LOG_THRIFT_CALL(DBG2);

  continueExecution(false);
  auto config = configManager_->getConfig();
  ret.drain_state() = *config->getConfig().drain_state();
  ret.drained_interfaces() = *config->getConfig().drained_interfaces();
  decrRequestsInExecution();
}

folly::coro::Task<std::unique_ptr<TEntryStats>>
BgpServiceBase::co_getEntryStats() {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return std::make_unique<TEntryStats>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<TEntryStats>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto stats = std::make_unique<TEntryStats>();
  peerMgr_.updateEntryStats(*stats);

  auto ribStats = co_await co_runOnEvbWithTimeout(
      rib_.getEventBase(),
      [this]() {
        TEntryStats s;
        rib_.updateEntryStats(s);
        return s;
      },
      kRibThriftHandlerTimeout);

  if (ribStats.hasValue()) {
    stats->total_ucast_routes() = *ribStats.value().total_ucast_routes();
    stats->total_originated_routes() =
        *ribStats.value().total_originated_routes();
    stats->total_rib_paths() = *ribStats.value().total_rib_paths();
  } else {
    if (ribStats.exception().is_compatible_with<folly::FutureTimeout>()) {
      XLOGF(ERR, "getEntryStats timed out — Rib evb unresponsive");
    } else {
      XLOGF(ERR, "getEntryStats failed: {}", ribStats.exception().what());
    }
  }

  augmentEntryStatsForPlatform(*stats);

  co_return std::move(stats);
}

folly::coro::Task<std::unique_ptr<THealthReport>>
BgpServiceBase::co_getHealthReport() {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return std::make_unique<THealthReport>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<THealthReport>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto report = std::make_unique<THealthReport>(
      co_await healthValidator_->generateReport());
  co_return std::move(report);
}

/*
 * [Initialization]
 */

bool BgpServiceBase::initializationConverged() {
  auto log = LOG_THRIFT_CALL(INFO);
  auto convergenceKey = fmt::format(
      kInitEventCounterFormat,
      apache::thrift::util::enumNameSafe(BgpInitializationEvent::INITIALIZED));
  return fb303::ThreadCachedServiceData::get()->hasCounter(convergenceKey);
}

void BgpServiceBase::getInitializationEvents(BgpInitializationMap& _return) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (!continueExecution(true)) {
    return;
  }

  auto sharedData = fb303::ThreadCachedServiceData::get();
  for (int eventInt = int(BgpInitializationEvent::INITIALIZING);
       eventInt <= int(BgpInitializationEvent::INITIALIZED);
       ++eventInt) {
    auto event = static_cast<BgpInitializationEvent>(eventInt);

    // The fb303 counter is set in function logInitializationEvent().
    auto counterKey = fmt::format(
        kInitEventCounterFormat, apache::thrift::util::enumNameSafe(event));
    if (sharedData->hasCounter(counterKey)) {
      _return[event] = sharedData->getCounter(counterKey);
    }
  }

  decrRequestsInExecution();
}

int64_t BgpServiceBase::getTimeElapsedSinceLastFibUpdate() {
  auto log = LOG_THRIFT_CALL(INFO);
  continueExecution(false);
  // ATTN: by default the API will return delta in the unit of SECOND
  auto lastFibUpdate = rib_.getLastProgrammedRoutesTimeStamp();
  if (lastFibUpdate < 0) {
    // return negative number reprensenting NO fib update yet
    decrRequestsInExecution();
    return lastFibUpdate;
  }

  auto now = std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();
  decrRequestsInExecution();
  return now - lastFibUpdate;
}

folly::coro::Task<int64_t> BgpServiceBase::co_getRibVersion() {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return kRibReadUnavailable;
  }

  if (!continueExecution(true)) {
    co_return kRibReadUnavailable;
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this]() { return peerMgr_.getDefaultVrf().getMaxRibVersion(); },
      kRibThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return static_cast<int64_t>(result.value());
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(ERR, "getRibVersion timed out — PeerManager evb unresponsive");
  } else {
    XLOGF(ERR, "getRibVersion failed: {}", result.exception().what());
  }
  co_return kRibReadUnavailable;
}

folly::coro::Task<int64_t> BgpServiceBase::co_getNumPrefixes() {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return kRibReadUnavailable;
  }

  if (!continueExecution(true)) {
    co_return kRibReadUnavailable;
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      rib_.getEventBase(),
      [this]() { return rib_.getNumPrefixes(); },
      kRibThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return static_cast<int64_t>(result.value());
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(ERR, "getNumPrefixes timed out — Rib evb unresponsive");
  } else {
    XLOGF(ERR, "getNumPrefixes failed: {}", result.exception().what());
  }
  co_return kRibReadUnavailable;
}

int64_t BgpServiceBase::getProcessUptimeSeconds() {
  auto log = LOG_THRIFT_CALL(INFO);
  return watchdog_.getUptimeSeconds();
}

/**
 * [Route Filter Policy]
 */
folly::coro::Task<std::unique_ptr<TResult>>
BgpServiceBase::co_setRouteFilterPolicy(
    std::unique_ptr<rib_policy::TRouteFilterPolicy> policy) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || policy == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty policy";
    XLOGF(
        ERR,
        "[{}]: Failed to set route filter policy: {}",
        kExitNullPtrLogPrefix,
        errStr);
    auto ret = std::make_unique<TResult>();
    ret->success() = false;
    ret->err() = errStr;
    co_return ret;
  }

  // Validate peer group configuration if key_type is PEER_GROUP_NAME
  neteng::fboss::bgp::thrift::TResult validationResult;
  auto config = configManager_->getConfig();
  validatePeerGroupConfigInPolicy(
      validationResult, *policy, config->getPeerGroups());
  if (!*validationResult.success()) {
    XLOGF(
        ERR,
        "[{}]: Failed to set route filter policy: {}",
        kPeerGroupValidationLogPrefix,
        *validationResult.err());
    auto ret = std::make_unique<TResult>();
    ret->success() = false;
    ret->err() = *validationResult.err();
    co_return ret;
  }

  validateUpdateGroupRouteFilterPolicy(validationResult, *policy, *config);
  if (!*validationResult.success()) {
    XLOGF(ERR, "{}", *validationResult.err());
    auto ret = std::make_unique<TResult>();
    ret->success() = false;
    ret->err() = *validationResult.err();
    co_return ret;
  }

  continueExecution(false);
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  try {
    peerMgr_.setRouteFilterPolicy(std::make_unique<RouteFilterPolicy>(*policy));
  } catch (const BgpError& ex) {
    auto errorMsg = folly::exceptionStr(ex);
    XLOGF(ERR, "{}", errorMsg);
    auto ret = std::make_unique<TResult>();
    ret->success() = false;
    ret->err() = std::string(errorMsg);
    co_return ret;
  }

  /*
   * No RIB evb hop: the coalescing MergeQueue is thread-safe, so enqueue on the
   * calling thread.
   */
  rib_.setRouteFilterPolicy(std::move(policy));

  auto ret = std::make_unique<TResult>();
  ret->success() = true;
  co_return ret;
}

folly::coro::Task<std::unique_ptr<rib_policy::TRouteFilterPolicy>>
BgpServiceBase::co_getRouteFilterPolicy() {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return std::make_unique<rib_policy::TRouteFilterPolicy>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<rib_policy::TRouteFilterPolicy>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      rib_.getEventBase(),
      [this]() { return rib_.getRouteFilterPolicy(); },
      kRibThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<rib_policy::TRouteFilterPolicy>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(ERR, "getRouteFilterPolicy timed out — Rib evb unresponsive");
  } else {
    XLOGF(ERR, "getRouteFilterPolicy failed: {}", result.exception().what());
  }
  co_return std::make_unique<rib_policy::TRouteFilterPolicy>();
}

folly::coro::Task<void> BgpServiceBase::co_clearRouteFilterPolicy() {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return;
  }
  continueExecution(false);
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  peerMgr_.setRouteFilterPolicy(nullptr);

  rib_.clearRouteFilterPolicy();
}

/*
 * [Rib]
 */

// fboss bgp originated-routes
folly::coro::Task<std::unique_ptr<std::vector<TOriginatedRoute>>>
BgpServiceBase::co_getOriginatedRoutes() {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return std::make_unique<std::vector<TOriginatedRoute>>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<std::vector<TOriginatedRoute>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      rib_.getEventBase(),
      [this]() { return rib_.getOriginatedRoutes(); },
      kRibThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::vector<TOriginatedRoute>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(ERR, "getOriginatedRoutes timed out — Rib evb unresponsive");
  } else {
    XLOGF(ERR, "getOriginatedRoutes failed: {}", result.exception().what());
  }
  co_return std::make_unique<std::vector<TOriginatedRoute>>();
}

// changeList
folly::coro::Task<std::unique_ptr<std::vector<TRibEntry>>>
BgpServiceBase::co_getChangeListEntries(TBgpAfi afi) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return std::make_unique<std::vector<TRibEntry>>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<std::vector<TRibEntry>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, afi]() { return peerMgr_.getChangeListEntries(afi); },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::vector<TRibEntry>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getChangeListEntries timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(ERR, "getChangeListEntries failed: {}", result.exception().what());
  }
  co_return std::make_unique<std::vector<TRibEntry>>();
}

folly::coro::Task<std::unique_ptr<std::vector<TRibEntry>>>
BgpServiceBase::co_getShadowRibEntries(TBgpAfi afi) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return std::make_unique<std::vector<TRibEntry>>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<std::vector<TRibEntry>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, afi]() { return peerMgr_.getShadowRibEntries(afi); },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::vector<TRibEntry>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getShadowRibEntries timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(ERR, "getShadowRibEntries failed: {}", result.exception().what());
  }
  co_return std::make_unique<std::vector<TRibEntry>>();
}

folly::coro::Task<std::unique_ptr<TCanonicalRibState>>
BgpServiceBase::co_getShadowRibEntriesCanonical(TBgpAfi afi) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return std::make_unique<TCanonicalRibState>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<TCanonicalRibState>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, afi]() { return peerMgr_.getShadowRibEntriesCanonical(afi); },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<TCanonicalRibState>(std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getShadowRibEntriesCanonical timed out, PeerManager evb unresponsive");
  } else {
    XLOGF(
        ERR,
        "getShadowRibEntriesCanonical failed: {}",
        result.exception().what());
  }
  co_return std::make_unique<TCanonicalRibState>();
}

folly::coro::Task<std::unique_ptr<TCanonicalRibState>>
BgpServiceBase::co_getChangeListEntriesCanonical(TBgpAfi afi) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return std::make_unique<TCanonicalRibState>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<TCanonicalRibState>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, afi]() { return peerMgr_.getChangeListEntriesCanonical(afi); },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<TCanonicalRibState>(std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getChangeListEntriesCanonical timed out, PeerManager evb unresponsive");
  } else {
    XLOGF(
        ERR,
        "getChangeListEntriesCanonical failed: {}",
        result.exception().what());
  }
  co_return std::make_unique<TCanonicalRibState>();
}

folly::coro::Task<std::unique_ptr<
    std::vector<facebook::neteng::fboss::bgp::thrift::TPeerEgressStats>>>
BgpServiceBase::co_getPeerEgressStats() {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return std::make_unique<std::vector<TPeerEgressStats>>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<std::vector<TPeerEgressStats>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto& sessionMgr = peerMgr_.getSessionManager();
  auto allPeerStats = co_await sessionMgr->co_getAllPeerDisplayInfos();

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, ps = std::move(allPeerStats)]() mutable {
        return peerMgr_.getPeerEgressStats(std::move(ps));
      },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::vector<TPeerEgressStats>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR, "getPeerEgressStats timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(ERR, "getPeerEgressStats failed: {}", result.exception().what());
  }
  co_return std::make_unique<std::vector<TPeerEgressStats>>();
}

folly::coro::Task<std::unique_ptr<TGetUpdateGroupSummariesResponse>>
BgpServiceBase::co_getUpdateGroupSummaries() {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return std::make_unique<TGetUpdateGroupSummariesResponse>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<TGetUpdateGroupSummariesResponse>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this]() { return peerMgr_.getUpdateGroupSummaries(); },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    auto response = std::make_unique<TGetUpdateGroupSummariesResponse>();
    response->update_groups() = std::move(result.value());
    response->enable_update_group() =
        configManager_->getConfig()->getBgpGlobalConfig()->enableUpdateGroup;
    co_return response;
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getUpdateGroupSummaries timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(ERR, "getUpdateGroupSummaries failed: {}", result.exception().what());
  }
  co_return std::make_unique<TGetUpdateGroupSummariesResponse>();
}

folly::coro::Task<std::unique_ptr<TGetUpdateGroupInfoResponse>>
BgpServiceBase::co_getUpdateGroupInfo(
    std::unique_ptr<TGetUpdateGroupInfoRequest> request) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return std::make_unique<TGetUpdateGroupInfoResponse>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<TGetUpdateGroupInfoResponse>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  if (!request || !request->group_id().has_value()) {
    XLOG(ERR, "getUpdateGroupInfo requires group_id");
    auto response = std::make_unique<TGetUpdateGroupInfoResponse>();
    response->enable_update_group() =
        configManager_->getConfig()->getBgpGlobalConfig()->enableUpdateGroup;
    co_return response;
  }
  const auto groupId = request->group_id().value();

  auto result = co_await co_runOnEvbWithTimeout(
      peerMgr_.getEventBase(),
      [this, groupId]() { return peerMgr_.getUpdateGroupInfo(groupId); },
      kPeerMgrThriftHandlerTimeout);

  if (result.hasValue()) {
    auto response = std::make_unique<TGetUpdateGroupInfoResponse>();
    response->update_groups() = std::move(result.value());
    response->enable_update_group() =
        configManager_->getConfig()->isUpdateGroupEnabled();
    co_return response;
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR, "getUpdateGroupInfo timed out — PeerManagerBase evb unresponsive");
  } else {
    XLOGF(ERR, "getUpdateGroupInfo failed: {}", result.exception().what());
  }
  co_return std::make_unique<TGetUpdateGroupInfoResponse>();
}

// bgp table
folly::coro::Task<std::unique_ptr<std::vector<TRibEntry>>>
BgpServiceBase::co_getRibEntries(TBgpAfi afi) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_return std::make_unique<std::vector<TRibEntry>>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<std::vector<TRibEntry>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      rib_.getEventBase(),
      [this, afi]() { return rib_.getRibEntries(afi); },
      kRibThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::vector<TRibEntry>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getRibEntries timed out — Rib evb unresponsive, afi={}",
        magic_enum::enum_name(afi));
  } else {
    XLOGF(
        ERR,
        "getRibEntries failed: {}, afi={}",
        result.exception().what(),
        magic_enum::enum_name(afi));
  }
  co_return std::make_unique<std::vector<TRibEntry>>();
}

folly::coro::Task<std::unique_ptr<TFibOutTable>>
BgpServiceBase::co_getFibOutPrefix(
    std::unique_ptr<TFibOutPrefixRequest> request) {
  // LogThriftCall accepts and handles a null request context.
  // @lint-ignore NULLSAFECLANG nullable-argument
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || request == nullptr) {
    const auto reason =
        exitInitiated_ ? "service is exiting" : "request is null";
    co_yield folly::coro::co_error(
        std::runtime_error(
            fmt::format("getFibOutPrefix unavailable: {}", reason)));
  }

  if (!continueExecution(true)) {
    co_yield folly::coro::co_error(
        std::runtime_error(
            "getFibOutPrefix unavailable: request admission failed"));
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  const auto& prefixString = *request->prefix();
  auto result = co_await co_runOnEvbWithTimeout(
      rib_.getEventBase(),
      [this, prefixString]() { return rib_.getFibOutPrefix(prefixString); },
      kRibThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<TFibOutTable>(std::move(result.value()));
  }

  const auto message =
      result.exception().is_compatible_with<folly::FutureTimeout>()
      ? fmt::format(
            "getFibOutPrefix timed out: RIB event base is unresponsive, "
            "prefix={}",
            prefixString)
      : fmt::format(
            "getFibOutPrefix failed: {}, prefix={}",
            result.exception().what(),
            prefixString);
  XLOGF(ERR, "{}", message);
  co_yield folly::coro::co_error(std::runtime_error(message));
}

folly::coro::Task<std::unique_ptr<TRibSummary>>
BgpServiceBase::co_getRibSummary(TBgpAfi afi) {
  auto log = LOG_THRIFT_CALL(DBG2);
  /*
   * Always echo back the requested afi so the client can attribute an
   * empty/error response to the right address family.
   */
  auto emptySummary = [afi]() {
    auto summary = std::make_unique<TRibSummary>();
    summary->afi() = afi;
    return summary;
  };

  if (exitInitiated_) {
    co_return emptySummary();
  }

  if (!continueExecution(true)) {
    co_return emptySummary();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      rib_.getEventBase(),
      [this, afi]() { return rib_.getRibSummary(afi); },
      kRibThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<TRibSummary>(std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getRibSummary timed out — Rib evb unresponsive, afi={}",
        magic_enum::enum_name(afi));
  } else {
    XLOGF(
        ERR,
        "getRibSummary failed: {}, afi={}",
        result.exception().what(),
        magic_enum::enum_name(afi));
  }
  co_return emptySummary();
}

folly::coro::Task<std::unique_ptr<TFibNexthopDatabase>>
BgpServiceBase::co_getFibNexthopDatabase() {
  // LogThriftCall accepts and handles a null request context.
  // @lint-ignore NULLSAFECLANG nullable-argument
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    co_yield folly::coro::co_error(
        std::runtime_error(
            "getFibNexthopDatabase unavailable: service is exiting"));
  }

  if (!continueExecution(true)) {
    co_yield folly::coro::co_error(
        std::runtime_error(
            "getFibNexthopDatabase unavailable: request admission failed"));
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  if (!rib_.isFibOutTrackingEnabled()) {
    auto database = std::make_unique<TFibNexthopDatabase>();
    database->enabled() = false;
    co_return database;
  }

  auto result = co_await co_runOnEvbWithTimeout(
      rib_.getEventBase(),
      [this]() { return rib_.getFibNexthopDatabase(); },
      kRibThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<TFibNexthopDatabase>(std::move(result.value()));
  }

  const auto message =
      result.exception().is_compatible_with<folly::FutureTimeout>()
      ? "getFibNexthopDatabase timed out: RIB event base is unresponsive"
      : fmt::format(
            "getFibNexthopDatabase failed: {}", result.exception().what());
  XLOGF(ERR, "{}", message);
  co_yield folly::coro::co_error(std::runtime_error(message));
}

folly::coro::Task<std::unique_ptr<std::vector<TRibEntry>>>
BgpServiceBase::co_getRibPrefix(std::unique_ptr<std::string> prefix) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || prefix == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty prefix";
    XLOGF(
        ERR,
        "[{}]: Failed to get rib prefix. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::vector<TRibEntry>>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<std::vector<TRibEntry>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      rib_.getEventBase(),
      [this, p = std::move(prefix)]() mutable {
        return rib_.getRibEntryForPrefix(std::move(p));
      },
      kRibThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::vector<TRibEntry>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(ERR, "getRibPrefix timed out — Rib evb unresponsive");
  } else {
    XLOGF(ERR, "getRibPrefix failed: {}", result.exception().what());
  }
  co_return std::make_unique<std::vector<TRibEntry>>();
}

folly::coro::Task<std::unique_ptr<std::vector<TRibEntry>>>
BgpServiceBase::co_getRibSubprefixes(std::unique_ptr<std::string> prefix) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || prefix == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty prefix";
    XLOGF(
        ERR,
        "[{}]: Failed to get rib subprefixes. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::vector<TRibEntry>>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<std::vector<TRibEntry>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  auto result = co_await co_runOnEvbWithTimeout(
      rib_.getEventBase(),
      [this, p = std::move(prefix)]() mutable {
        return rib_.getRibEntriesForSubprefixes(std::move(p));
      },
      kRibThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::vector<TRibEntry>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(ERR, "getRibSubprefixes timed out — Rib evb unresponsive");
  } else {
    XLOGF(ERR, "getRibSubprefixes failed: {}", result.exception().what());
  }
  co_return std::make_unique<std::vector<TRibEntry>>();
}

folly::coro::Task<std::unique_ptr<std::vector<TRibEntry>>>
BgpServiceBase::co_getRibEntriesForCommunity(
    TBgpAfi afi,
    std::unique_ptr<std::string> community) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || community == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty community";
    XLOGF(
        ERR,
        "[{}]: Failed to get rib entries for community. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::vector<TRibEntry>>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<std::vector<TRibEntry>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  std::optional<nettools::bgplib::BgpAttrCommunityC> comm =
      nettools::bgplib::BgpAttrCommunityC::createBgpAttrCommunity(*community);
  if (comm == std::nullopt) {
    throw std::invalid_argument("Invalid Community Value!");
  }

  auto result = co_await co_runOnEvbWithTimeout(
      rib_.getEventBase(),
      [this, afi, c = *comm]() {
        return rib_.getRibEntriesForCommunities(afi, {c});
      },
      kRibThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::vector<TRibEntry>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getRibEntriesForCommunity timed out — Rib evb unresponsive, afi={}",
        magic_enum::enum_name(afi));
  } else {
    XLOGF(
        ERR,
        "getRibEntriesForCommunity failed: {}, afi={}",
        result.exception().what(),
        magic_enum::enum_name(afi));
  }
  co_return std::make_unique<std::vector<TRibEntry>>();
}

folly::coro::Task<std::unique_ptr<std::vector<TRibEntry>>>
BgpServiceBase::co_getRibEntriesForCommunities(
    TBgpAfi afi,
    std::unique_ptr<std::vector<std::string>> communities) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || communities == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty communities list";
    XLOGF(
        ERR,
        "[{}]: Failed to get rib entries for communities. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<std::vector<TRibEntry>>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<std::vector<TRibEntry>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  std::vector<nettools::bgplib::BgpAttrCommunityC> comms;
  for (const auto& community : *communities) {
    std::optional<nettools::bgplib::BgpAttrCommunityC> comm =
        nettools::bgplib::BgpAttrCommunityC::createBgpAttrCommunity(community);
    if (comm == std::nullopt) {
      throw std::invalid_argument("Invalid Community Value!");
    }
    comms.emplace_back(*comm);
  }

  auto result = co_await co_runOnEvbWithTimeout(
      rib_.getEventBase(),
      [this, afi, c = std::move(comms)]() {
        return rib_.getRibEntriesForCommunities(afi, c);
      },
      kRibThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<std::vector<TRibEntry>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(
        ERR,
        "getRibEntriesForCommunities timed out — Rib evb unresponsive, afi={}",
        magic_enum::enum_name(afi));
  } else {
    XLOGF(
        ERR,
        "getRibEntriesForCommunities failed: {}, afi={}",
        result.exception().what(),
        magic_enum::enum_name(afi));
  }
  co_return std::make_unique<std::vector<TRibEntry>>();
}

void BgpServiceBase::getMonitoredQueueSizes(
    QueueSizeMapT& ret,
    std::unique_ptr<std::vector<std::string>> paths) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    return;
  }

  continueExecution(false);
  ret = watchdog_.getQueueSizes(paths);
  decrRequestsInExecution();
}

folly::coro::Task<std::unique_ptr<neteng::fboss::bgp::thrift::TNexthopInfo>>
BgpServiceBase::co_getNexthopInfoForNexthop(
    std::unique_ptr<std::string> nexthop) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || nexthop == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Empty nexthop";
    XLOGF(
        ERR,
        "[{}]: Failed to get nexthop info. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<neteng::fboss::bgp::thrift::TNexthopInfo>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<neteng::fboss::bgp::thrift::TNexthopInfo>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  folly::IPAddress nexthopAddr;
  try {
    nexthopAddr = folly::IPAddress(*nexthop);
  } catch (std::exception const&) {
    XLOGF(ERR, "Invalid nexthop: {}", *nexthop);
    co_return std::make_unique<neteng::fboss::bgp::thrift::TNexthopInfo>();
  }

  auto result = co_await co_runOnEvbWithTimeout(
      rib_.getEventBase(),
      [this, addr = nexthopAddr]() {
        return rib_.getNexthopInfoForNexthop(addr);
      },
      kRibThriftHandlerTimeout);

  if (result.hasValue() && result.value().has_value()) {
    co_return std::make_unique<neteng::fboss::bgp::thrift::TNexthopInfo>(
        std::move(result.value().value()));
  }

  if (!result.hasValue()) {
    if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
      XLOGF(ERR, "getNexthopInfoForNexthop timed out — Rib evb unresponsive");
    } else {
      XLOGF(
          ERR,
          "getNexthopInfoForNexthop failed: {}",
          result.exception().what());
    }
  }
  co_return std::make_unique<neteng::fboss::bgp::thrift::TNexthopInfo>();
}

folly::coro::Task<
    std::unique_ptr<std::vector<neteng::fboss::bgp::thrift::TNexthopInfo>>>
BgpServiceBase::co_getNexthopInfos(
    std::unique_ptr<std::vector<std::string>> nexthops) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_ || nexthops == nullptr) {
    auto errStr = exitInitiated_ ? "Session exits" : "Null nexthops";
    XLOGF(
        ERR,
        "[{}]: Failed to get nexthop infos. Error: {}",
        kExitNullPtrLogPrefix,
        errStr);
    co_return std::make_unique<
        std::vector<neteng::fboss::bgp::thrift::TNexthopInfo>>();
  }

  if (!continueExecution(true)) {
    co_return std::make_unique<
        std::vector<neteng::fboss::bgp::thrift::TNexthopInfo>>();
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  /*
   * Empty request = all cache entries. A non-empty request filters to the
   * valid IPs; invalid entries are dropped (and logged) rather than silently
   * widening the query back to "list all".
   */
  const bool filterRequested = !nexthops->empty();
  std::vector<folly::IPAddress> filter;
  std::vector<std::string> invalidNexthops;
  for (const auto& addrStr : *nexthops) {
    if (!addrStr.empty() && folly::IPAddress::validate(addrStr)) {
      filter.emplace_back(addrStr);
    } else {
      invalidNexthops.push_back(addrStr);
    }
  }

  if (!invalidNexthops.empty()) {
    XLOGF(
        WARN,
        "getNexthopInfos: ignoring {} invalid nexthop(s): {}",
        invalidNexthops.size(),
        fmt::join(invalidNexthops, ", "));
  }

  /*
   * The caller asked for specific IPs but none were valid: return empty rather
   * than falling through to the list-all path (an empty filter = every entry).
   */
  if (filterRequested && filter.empty()) {
    co_return std::make_unique<
        std::vector<neteng::fboss::bgp::thrift::TNexthopInfo>>();
  }

  auto result = co_await co_runOnEvbWithTimeout(
      rib_.getEventBase(),
      [this, filter = std::move(filter)]() {
        return rib_.getNexthopInfos(filter);
      },
      kRibThriftHandlerTimeout);

  if (result.hasValue()) {
    co_return std::make_unique<
        std::vector<neteng::fboss::bgp::thrift::TNexthopInfo>>(
        std::move(result.value()));
  }

  if (result.exception().is_compatible_with<folly::FutureTimeout>()) {
    XLOGF(ERR, "getNexthopInfos timed out — Rib evb unresponsive");
  } else {
    XLOGF(ERR, "getNexthopInfos failed: {}", result.exception().what());
  }
  co_return std::make_unique<
      std::vector<neteng::fboss::bgp::thrift::TNexthopInfo>>();
}

/**
 * Deprecated wire-compatibility placeholder. Filtered attribute statistics
 * required the same full Adj-RIB walk as getAttributeStats(), so this handler
 * intentionally returns an empty response without dispatching to PeerManager.
 */
folly::coro::Task<std::unique_ptr<TAttributeStats>>
BgpServiceBase::co_getAttributeStatsFiltered(
    std::unique_ptr<facebook::neteng::fboss::bgp::thrift::TAttributeStatsFilter>
    /*filter*/) {
  co_return std::make_unique<TAttributeStats>();
}

void BgpServiceBase::startProfiler(bool enable) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (exitInitiated_) {
    return;
  }

  if (!continueExecution(false)) {
    return;
  }
  BgpProfiler::getInstance()->setEnabled(enable);
  decrRequestsInExecution();
}

void BgpServiceBase::setProfilerFilter(std::unique_ptr<std::string> regex) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (exitInitiated_ || regex == nullptr) {
    return;
  }

  if (!continueExecution(false)) {
    return;
  }
  BgpProfiler::getInstance()->setFilterRegex(*regex);
  decrRequestsInExecution();
}

void BgpServiceBase::getProfilerStats(std::vector<TBgpProfilerStat>& _return) {
  auto log = LOG_THRIFT_CALL(DBG2);
  if (exitInitiated_) {
    return;
  }

  if (!continueExecution(true)) {
    return;
  }

  auto stats = BgpProfiler::getInstance()->getStats();
  _return.reserve(stats.size());
  for (const auto& s : stats) {
    TBgpProfilerStat thriftStat;
    thriftStat.name() = s.name;
    thriftStat.count() = s.count;
    thriftStat.p50_ms() = s.p50Ms;
    thriftStat.p90_ms() = s.p90Ms;
    thriftStat.p99_ms() = s.p99Ms;
    thriftStat.max_ms() = s.maxMs;
    thriftStat.total_ms() = s.totalMs;
    _return.push_back(std::move(thriftStat));
  }
  decrRequestsInExecution();
}

void BgpServiceBase::clearProfilerStats() {
  auto log = LOG_THRIFT_CALL(INFO);
  if (exitInitiated_) {
    return;
  }

  if (!continueExecution(false)) {
    return;
  }
  BgpProfiler::getInstance()->clearStats();
  decrRequestsInExecution();
}

namespace {
std::vector<folly::IPAddress> toPeerAddrs(
    const std::vector<std::string>& peers) {
  std::vector<folly::IPAddress> addrs;
  addrs.reserve(peers.size());
  for (const auto& peer : peers) {
    try {
      addrs.emplace_back(peer);
    } catch (const std::exception& e) {
      throw std::invalid_argument(
          fmt::format("Invalid peer IP address '{}': {}", peer, e.what()));
    }
  }
  return addrs;
}
} // namespace

folly::coro::Task<void> BgpServiceBase::co_clearCounters(
    std::unique_ptr<std::vector<std::string>> peers) {
  auto log = LOG_THRIFT_CALL(INFO);
  if (exitInitiated_) {
    co_return;
  }
  if (!continueExecution(true)) {
    co_return;
  }
  SCOPE_EXIT {
    decrRequestsInExecution();
  };

  const auto peerAddrs = toPeerAddrs(*peers);

  // Data plane: zero each peer's socket tx/rx counters on the I/O evb.
  co_await sessionMgr_->co_clearSocketCounters(peerAddrs);

  /*
   * Control plane: zero each AdjRib's + update-group's sent/recv message counts
   * and their fb303 keys (runs on the PeerManager evb).
   */
  co_await peerMgr_.co_clearCounters(peerAddrs);
}

} // namespace facebook::bgp
