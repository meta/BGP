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

#include <folly/coro/BlockingWait.h>
#include <folly/logging/xlog.h>

#include <fb303/ThreadCachedServiceData.h>
#include <gflags/gflags.h>
#include <re2/re2.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include "fboss/agent/Constants.h"
#include "fboss/fsdb/client/FsdbSubscriber.h"
#include "fboss/lib/AlertLogger.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/FeatureFlags.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/FsdbFibWatcher.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopCache.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopStatus.h"
#include "neteng/fboss/bgp/cpp/peer/NeighborWatcher.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

using namespace std::chrono_literals;

DEFINE_int32(
    fsdb_config_timeout_s,
    5,
    "Time in seconds for which to wait for FSDB config");

DEFINE_bool(
    enable_fsdb_patch_subscriber,
    false,
    "Enable FSDB subscriptions using patches, default false");

namespace facebook::bgp {

void FsdbConfigWatcher::run() noexcept {
  try {
    const auto path = fsdbStateRootPath_.agent().config().sw();
    auto connCb = [](auto /* oldState */,
                     auto newState,
                     std::optional<bool> /*initialSyncHasData*/) {
      if (newState == fboss::fsdb::SubscriptionState::CONNECTED) {
        XLOG(
            INFO,
            "[FsdbConfigWatcher]: FSDB Connection Established! Ready for State Publishing");
      } else {
        XLOG(INFO, "[FsdbConfigWatcher]: FSDB Not Connected");
      }
    };
    if (fsdbSubMgr_) {
      fsdbSubMgr_->addPath(path);
      fsdbSubMgr_->subscribe(
          [me = shared_from_this()](const auto&& update) {
            facebook::fboss::cfg::SwitchConfig switchConfig =
                std::move(*update.data->toThrift().agent()->config()->sw());
            me->neighborWatcherEvb_->runInEventBaseThread(
                [switchConfig = std::move(switchConfig), me]() {
                  me->fsdbSwitchCfgCb(switchConfig);
                });
          },
          connCb);
    } else {
      CHECK(fsdbPubSubMgr_);
      fsdbPubSubMgr_->addStatePathSubscription(
          path.tokens(),
          connCb,
          [me = shared_from_this()](const auto&& operState) {
            auto switchConfig = apache::thrift::BinarySerializer::deserialize<
                typename decltype(path)::DataT>(*operState.contents());
            me->neighborWatcherEvb_->runInEventBaseThread(
                [switchConfig = std::move(switchConfig), me]() {
                  me->fsdbSwitchCfgCb(std::move(switchConfig));
                });
          },
          fboss::utils::ConnectionOptions("::1", fsdbPort_));
    }

    fsdbSubscribed_ = true;
    XLOG(INFO, "FSDB config subscribe succeeded");
  } catch (std::exception const& ex) {
    XLOGF(ERR, "FSDB config subscribe failed: {}", ex.what());
  }
}

void FsdbConfigWatcher::stopFsdbSubscription() noexcept {
  if (fsdbSubscribed_) {
    if (fsdbSubMgr_) {
      fsdbSubMgr_->stop();
    } else {
      CHECK(fsdbPubSubMgr_);
      const auto path = fsdbStateRootPath_.agent().config().sw();
      fsdbPubSubMgr_->removeStatePathSubscription(path.tokens());
    }
    fsdbSubscribed_ = false;
    XLOG(INFO, "FSDB config subscribe stopped");
  }
}

void FsdbConfigWatcher::stop() noexcept {
  stopFsdbSubscription();
}

void FsdbConfigWatcher::processSwitchCfgChanges(
    const fboss::cfg::SwitchConfig& switchConfig) {
  XLOG(INFO, "FSDB: received SwitchConfig");
  /*
   * Create a mapping of peer subnet to interface info (which includes
   * vlanId) based on interface config details
   */
  folly::F14FastMap<folly::CIDRNetwork, fboss::cfg::Interface>
      peerSubnetInterfaceMap;
  for (const auto& intf : *switchConfig.interfaces()) {
    XLOGF(DBG3, " intfID:{} vlanID:{}", *intf.intfID(), *intf.vlanID());
    for (const auto& addr : *intf.ipAddresses()) {
      auto maybePrefix = folly::IPAddress::tryCreateNetwork(addr);
      if (!maybePrefix.hasValue()) {
        XLOGF(ERR, "Invalid prefix string:{}", addr);
        continue;
      }
      auto prefix = maybePrefix.value();
      if (prefix.first.isLinkLocal()) {
        continue;
      }
      XLOGF(DBG3, "  prefix:{}", folly::IPAddress::networkToString(prefix));
      peerSubnetInterfaceMap.emplace(std::move(prefix), intf);
    }
  }

  /*
   * Create mapping of vlanId to port speed based on port config details.
   * Note that in the case of aggregated ports, multiple ports will share the
   * same vlanId, so "value" is another map of portId to speed
   */
  folly::F14FastMap<int32_t, folly::F14FastMap<int32_t, int64_t>>
      vlanPortSpeedMap;
  for (const auto& port : *switchConfig.ports()) {
    auto portId = *port.logicalID();
    auto vlanId = *port.ingressVlan();
    std::string portName;
    if (port.name() &&
        !re2::RE2::FullMatch(*port.name(), kPortNameRegex, &portName)) {
      continue;
    }
    auto speedMbps = static_cast<int>(*port.speed());
    XLOGF(
        DBG3,
        " logicalID:{} ingressVlan:{} speedMpbs:{}",
        portId,
        vlanId,
        speedMbps);
    if (vlanPortSpeedMap.find(vlanId) != vlanPortSpeedMap.end()) {
      vlanPortSpeedMap.at(vlanId).emplace(portId, speedMbps);
    } else {
      vlanPortSpeedMap.emplace(
          vlanId, folly::F14FastMap<int32_t, int64_t>{{portId, speedMbps}});
    }
  }

  /*
   * Use peerSubnetInterfaceMap and vlanPortSpeedMap to create a mapping of
   * port subnet to aggregated port speed (LBW) map
   */
  folly::F14NodeMap<folly::CIDRNetwork, int64_t> peerSubnetLbwMap;
  for (const auto& [peerSubnet, interface] : peerSubnetInterfaceMap) {
    auto vlanId = *interface.vlanID();
    auto it = vlanPortSpeedMap.find(vlanId);
    if (it == vlanPortSpeedMap.end()) {
      // some vlans/interfaces don't map to any ports, just skip them
      continue;
    }
    auto aggregateSpeed = 0;
    for (const auto& [_, speed] : it->second) {
      CHECK_GT(speed, 0);
      aggregateSpeed += speed;
    }
    peerSubnetLbwMap.emplace(peerSubnet, aggregateSpeed);
  }
  peerSubnetLbwMap_ =
      std::make_shared<folly::F14NodeMap<folly::CIDRNetwork, int64_t>>(
          peerSubnetLbwMap);
}

void FsdbConfigWatcher::fsdbSwitchCfgCb(
    const fboss::cfg::SwitchConfig& switchConfig) {
  if (switchConfig_ != nullptr) {
    XLOG(WARN, "FSDB: already processed config; ignore");
    return;
  }
  processSwitchCfgChanges(switchConfig);
  switchConfig_ = std::make_shared<fboss::cfg::SwitchConfig>(switchConfig);
  fsdbCfgBaton_->post();

  /*
   * Currently, upon BGP restart, we wait for wedge_agent to publish its
   * switch config and use it to build peerSubnet-LBW map, which we use in
   * UCMP-related functionality.  If wedge_agent switch config were to change
   * without wedge_agent / BGP restart, we currently do not have code in place
   * to modify the received / advertised routes to use the updated values.
   * Till those changes are in place, it is better to simply stop subscribing
   * for FSDB updates after receiving the first published values.
   */
  stopFsdbSubscription();
}

std::optional<folly::F14NodeMap<folly::CIDRNetwork, int64_t>>
FsdbConfigWatcher::getPeerSubnetLbwMap() noexcept {
  if (peerSubnetLbwMap_) {
    return *peerSubnetLbwMap_;
  } else {
    return std::nullopt;
  }
}

void FsdbNeighborWatcher::run() noexcept {
  try {
    const auto path = fsdbStateRootPath_.agent().switchState().interfaceMaps();
    auto connCb = [](fboss::fsdb::SubscriptionState /*oldState*/,
                     fboss::fsdb::SubscriptionState /*newState*/,
                     std::optional<bool> /*initialSyncHasData*/) {};
    if (fsdbSubMgr_) {
      fsdbSubMgr_->addPath(path);
      fsdbSubMgr_->subscribe([me = shared_from_this()](const auto&& update) {
        auto interfaceMap = std::make_shared<std::map<
            fboss::state::SwitchIdList,
            std::map<int32_t, fboss::state::InterfaceFields>>>(
            *update.data->toThrift().agent()->switchState()->interfaceMaps());
        me->neighborWatcherEvb_->runInEventBaseThread(
            [interfaceMap = std::move(interfaceMap), me]() {
              for (const auto& [key, content] : *interfaceMap) {
                me->fsdbInterfaceStateCb(content);
              }
              me->forwardResolvedNeighborIps(*interfaceMap);
            });
      });
    } else {
      CHECK(fsdbPubSubMgr_);
      fsdbPubSubMgr_->addStatePathSubscription(
          path.tokens(),
          connCb,
          [me = shared_from_this()](const auto&& operState) {
            auto interfaceMap = std::make_shared<std::map<
                fboss::state::SwitchIdList,
                std::map<int32_t, fboss::state::InterfaceFields>>>(
                apache::thrift::BinarySerializer::deserialize<
                    typename decltype(path)::DataT>(*operState.contents()));
            me->neighborWatcherEvb_->runInEventBaseThread(
                [interfaceMap = std::move(interfaceMap), me]() {
                  for (const auto& [key, content] : *interfaceMap) {
                    me->fsdbInterfaceStateCb(content);
                  }
                  me->forwardResolvedNeighborIps(*interfaceMap);
                });
          },
          fboss::utils::ConnectionOptions("::1", fsdbPort_));
    }
    XLOG(INFO, "FSDB interfaceMap state subscribe succeeded");
  } catch (std::exception const& ex) {
    XLOGF(ERR, "FSDB: addStatePathSubscription failed: {}", ex.what());
  }
}

void FsdbNeighborWatcher::handleCowUpdate(
    const fboss::fsdb::FsdbCowStateSubManager::SubUpdate& update) {
  auto interfaceMap = std::make_shared<std::map<
      fboss::state::SwitchIdList,
      std::map<int32_t, fboss::state::InterfaceFields>>>(
      *update.data->toThrift().agent()->switchState()->interfaceMaps());
  for (const auto& [key, content] : *interfaceMap) {
    fsdbInterfaceStateCb(content);
  }
  forwardResolvedNeighborIps(*interfaceMap);
}

bool FsdbNeighborWatcher::isNeighborResolved(
    const fboss::state::NeighborEntryFields& nbrFields) {
  return *nbrFields.portId()->portId() != 0 &&
      *nbrFields.state() == fboss::state::NeighborState::Reachable;
}

void FsdbNeighborWatcher::reportPortIdStateMismatch(
    const fboss::state::NeighborEntryFields& nbrFields) {
  const bool portIdSet = *nbrFields.portId()->portId() != 0;
  const bool reachable =
      *nbrFields.state() == fboss::state::NeighborState::Reachable;
  if (portIdSet == reachable) {
    return;
  }
  facebook::fb303::ThreadCachedServiceData::get()->incrementCounter(
      facebook::bgp::BgpStats::kNeighborPortIdStateMismatch, 1);
  XLOGF_EVERY_MS(
      WARNING,
      10000,
      "{}inconsistent neighbor signal for {}: portId={}, state={}; "
      "treating as unresolved",
      facebook::fboss::BGPAlert().str(),
      *nbrFields.ipaddress(),
      *nbrFields.portId()->portId(),
      static_cast<int>(*nbrFields.state()));
}

void FsdbNeighborWatcher::collectResolvedIpsFromTable(
    const std::map<std::string, fboss::state::NeighborEntryFields>& nbrTable,
    folly::F14FastSet<NexthopKey, NexthopKey::Hash>& resolvedNexthops) {
  for (const auto& [ipStr, nbrFields] : nbrTable) {
    reportPortIdStateMismatch(nbrFields);
    if (!isNeighborResolved(nbrFields)) {
      continue;
    }
    auto maybeIp = folly::IPAddress::tryFromString(ipStr);
    if (!maybeIp.hasError() && !maybeIp.value().isLinkLocal()) {
      resolvedNexthops.insert(
          NexthopKey{
              .ip = maybeIp.value(), .nextHopVrfId = fboss::kDefaultVrf});
    }
  }
}

void FsdbNeighborWatcher::forwardResolvedNeighborIps(
    const std::map<
        fboss::state::SwitchIdList,
        std::map<int32_t, fboss::state::InterfaceFields>>& interfaceMap) {
  if (!resolvedNeighborCb_) {
    return;
  }
  folly::F14FastSet<NexthopKey, NexthopKey::Hash> resolvedNexthops;
  for (const auto& [switchIdList, innerMap] : interfaceMap) {
    for (const auto& [intfId, intfFields] : innerMap) {
      collectResolvedIpsFromTable(*intfFields.arpTable(), resolvedNexthops);
      collectResolvedIpsFromTable(*intfFields.ndpTable(), resolvedNexthops);
    }
  }
  resolvedNeighborCb_(std::move(resolvedNexthops));
}

void FsdbNeighborWatcher::stop() noexcept {
  if (fsdbSubMgr_) {
    fsdbSubMgr_->stop();
  } else {
    CHECK(fsdbPubSubMgr_);
    const auto path = fsdbStateRootPath_.agent().switchState().interfaceMaps();
    fsdbPubSubMgr_->removeStatePathSubscription(path.tokens());
  }
}

namespace {
bool isLinkLocalAddress(const folly::IPAddress& ipAddress) {
  if (ipAddress.isLinkLocal()) {
    XLOGF(DBG3, "Ignore link local {}", ipAddress.str());
    return true;
  }
  return false;
}

/*
 * Helper function to find addresses that are resolved in sourceEntry but
 * either missing or non-resolvable in targetEntry
 */
void findMissingOrNonResolvableAddr(
    const std::map<std::string, fboss::state::NeighborEntryFields>& sourceEntry,
    const std::map<std::string, fboss::state::NeighborEntryFields>& targetEntry,
    std::vector<folly::IPAddress>& resultAddrs) {
  for (const auto& [ipaddr, sourceNbrFields] : sourceEntry) {
    const auto ipAddress = folly::IPAddress(ipaddr);
    if (isLinkLocalAddress(ipAddress)) {
      continue;
    }
    if (FsdbNeighborWatcher::isNeighborResolved(sourceNbrFields)) {
      if (!targetEntry.contains(ipaddr)) {
        XLOGF(DBG3, "ipaddr:{} missing from target state", ipaddr);
        resultAddrs.push_back(ipAddress);
      } else if (!FsdbNeighborWatcher::isNeighborResolved(
                     targetEntry.at(ipaddr))) {
        XLOGF(DBG3, "ipaddr:{} is non-resolvable in target", ipaddr);
        resultAddrs.push_back(ipAddress);
      }
    }
  }
}
} // namespace

void FsdbNeighborWatcher::getNbrEntryChanges(
    const std::map<std::string, fboss::state::NeighborEntryFields>& oldNbrEntry,
    const std::map<std::string, fboss::state::NeighborEntryFields>& newNbrEntry,
    std::vector<folly::IPAddress>& deletedAddrs,
    std::vector<folly::IPAddress>& addedAddrs) {
  /*
   * Find addresses that were resolved in old state but are now missing or
   * non-resolvable in new state (deleted addresses)
   */
  findMissingOrNonResolvableAddr(oldNbrEntry, newNbrEntry, deletedAddrs);
  /*
   * Find addresses that are resolved in new state but were missing or
   * non-resolvable in old state (added addresses)
   */
  findMissingOrNonResolvableAddr(newNbrEntry, oldNbrEntry, addedAddrs);
}

/*
 * TODO: Instead of comparing old vs new interfaceMap to look for changes, keep
 * a list of ip-addresses of interest to us and notify PeerManagerBase if any of
 * them went down.  This is more resilient to FSDB going down/up
 */
folly::coro::Task<void> FsdbNeighborWatcher::processInterfaceMapChanges(
    std::map<int32_t, fboss::state::InterfaceFields> newInterfaceMap) {
  std::vector<folly::IPAddress> deletedAddrs;
  std::vector<folly::IPAddress> addedAddrs;

  /*
   * Iterate OLD interfaceMap to find added and deleted addresses
   */
  if (interfaceMap_ != nullptr) {
    for (const auto& [interfaceId, oldInterfaceFields] : *interfaceMap_) {
      co_await folly::coro::co_safe_point;
      if (!newInterfaceMap.contains(interfaceId)) {
        XLOGF(ERR, "new state does not have interface {}", interfaceId);
        /*
         * if new map doesn't have this interfaceId, then all old entries are
         * potentially deleted
         */
        getNbrEntryChanges(
            *oldInterfaceFields.arpTable(), {}, deletedAddrs, addedAddrs);
        getNbrEntryChanges(
            *oldInterfaceFields.ndpTable(), {}, deletedAddrs, addedAddrs);
      } else {
        const auto& newInterfaceFields = newInterfaceMap.at(interfaceId);
        getNbrEntryChanges(
            *oldInterfaceFields.arpTable(),
            *newInterfaceFields.arpTable(),
            deletedAddrs,
            addedAddrs);
        getNbrEntryChanges(
            *oldInterfaceFields.ndpTable(),
            *newInterfaceFields.ndpTable(),
            deletedAddrs,
            addedAddrs);
      }
    }
    /*
     * deleted entries are relative to the old map, so we can handle it here
     * peer manager only cares about deleted entries
     */
    for (const auto& ipAddress : deletedAddrs) {
      XLOGF(
          INFO,
          "FSDB: neighbor disappeared: {}: enqueue to PeerManagerBase",
          ipAddress.str());
      neighborEventQ_.push(
          NeighborEventMsg(fboss::kDefaultVrf, ipAddress, false));
    }
  }

  /*
   * Iterate NEW interfaceMap to find added and deleted addresses
   *
   * Attention: interfaces existing in both old and new maps have already been
   * iterated in the previous loop.
   */
  for (const auto& [interfaceId, newInterfaceFields] : newInterfaceMap) {
    co_await folly::coro::co_safe_point;
    // we should only be updating addedAddrs in this loop
    if (!interfaceMap_ || !interfaceMap_->contains(interfaceId)) {
      getNbrEntryChanges(
          {}, *newInterfaceFields.arpTable(), deletedAddrs, addedAddrs);
      getNbrEntryChanges(
          {}, *newInterfaceFields.ndpTable(), deletedAddrs, addedAddrs);
    }
    if (XLOG_IS_ON(DBG3)) {
      logNeighborTable(newInterfaceFields, *newInterfaceFields.arpTable());
      logNeighborTable(newInterfaceFields, *newInterfaceFields.ndpTable());
    }
  }

  /*
   * Update interfaceMap_ cache BEFORE the co_await push suspension point.
   * co_await ribInQ_.push() can suspend under backpressure, allowing a
   * second coroutine (from the next FSDB callback) to start. That second
   * coroutine reads interfaceMap_ to compute its diff. By updating here —
   * after the diff but before any suspension — the next coroutine always
   * diffs against fresh state, producing correct deltas even under
   * backpressure.
   */
  interfaceMap_ =
      std::make_unique<std::map<int32_t, fboss::state::InterfaceFields>>(
          std::move(newInterfaceMap));

  // Notify Rib for nexthop updates if any
  if (addedAddrs.size() || deletedAddrs.size()) {
    XLOGF(
        INFO,
        "FSDB: nexthop resolution update: {} deleted, {} added",
        deletedAddrs.size(),
        addedAddrs.size());
    try {
      NexthopResolutionUpdate nexthopResolutionUpdate(addedAddrs, deletedAddrs);
      co_await ribInQ_.push(std::move(nexthopResolutionUpdate));
    } catch (const std::exception& ex) {
      XLOGF(ERR, "Exception pushing to ribInQ_: {}", ex.what());
    }
  }
}

void FsdbNeighborWatcher::logNeighborTable(
    const fboss::state::InterfaceFields& interfaceFields,
    const std::map<std::string, fboss::state::NeighborEntryFields>& nbrTable) {
  for (const auto& [_, nbrFields] : nbrTable) {
    XLOGF(
        DBG3,
        "interfaceId:{} name:{} ip_addr:{} mac:{} port:{} state:{}",
        *interfaceFields.interfaceId(),
        *interfaceFields.name(),
        *nbrFields.ipaddress(),
        *nbrFields.mac(),
        *nbrFields.portId()->portId(),
        static_cast<int>(*nbrFields.state()));
  }
}

/*
 * Callback upon FSDB state change.  As per our registration request, FSDB
 * sends entire new state of interfaceMap if something changed
 */
void FsdbNeighborWatcher::fsdbInterfaceStateCb(
    const std::map<int32_t, fboss::state::InterfaceFields>& newInterfaceMap) {
  XLOG_IF(
      INFO,
      interfaceMap_ == nullptr,
      "FSDB: first time receiving interfaceMap state update");

  XLOGF(
      INFO,
      "FSDB: current interfaceMap size: {}, new interfaceMap size: {}",
      interfaceMap_ ? interfaceMap_->size() : 0,
      newInterfaceMap.size());

  asyncScope_.add(co_withExecutor(
      neighborWatcherEvb_, processInterfaceMapChanges(newInterfaceMap)));
}

/*
 * FsdbSwitchReachabilityWatcher
 */
void FsdbSwitchReachabilityWatcher::processSwitchReachability() {
  neighborEventQ_.push(NeighborReachabilityMsg{});
}

void FsdbSwitchReachabilityWatcher::run() noexcept {
  try {
    const auto path = fsdbStateRootPath_.agent().dsfSwitchReachability();
    auto connCb = [](fboss::fsdb::SubscriptionState /*oldState*/,
                     fboss::fsdb::SubscriptionState /*newState*/,
                     std::optional<bool> /*initialSyncHasData*/) {};
    auto checkReachability = [](const auto& dsfSwitchReachability) -> bool {
      /*
       * Check if dsfSwitchReachability table is populated. Return
       * early if not.
       */
      if (dsfSwitchReachability.empty()) {
        return false;
      }
      /*
       * The agent will only contain the entry for its own switchId
       * in the dsfSwitchReachability table.
       */
      auto entry = dsfSwitchReachability.begin();
      auto switchId = entry->first;

      // Grab the switchIdToGroupPort from the switchReachability info.
      auto switchIdToGroupPort = *entry->second.switchIdToFabricPortGroupMap();

      // Check reachability of current switch.
      if (!switchIdToGroupPort.contains(switchId) ||
          switchIdToGroupPort[switchId] == kNoPortGroup) {
        return true;
      }
      return false;
    };
    if (fsdbSubMgr_) {
      fsdbSubMgr_->addPath(path);
      fsdbSubMgr_->subscribe(
          [me = shared_from_this(), &checkReachability](auto update) {
            auto dsfSwitchReachability =
                *update.data->toThrift().agent()->dsfSwitchReachability();
            if (checkReachability(dsfSwitchReachability)) {
              me->neighborWatcherEvb_->runInEventBaseThread(
                  [me]() { me->processSwitchReachability(); });
            }
          });
    } else {
      CHECK(fsdbPubSubMgr_);
      fsdbPubSubMgr_->addStatePathSubscription(
          path.tokens(),
          connCb,
          [me = shared_from_this(),
           &checkReachability](const auto&& operState) {
            auto dsfSwitchReachability =
                apache::thrift::BinarySerializer::deserialize<
                    typename decltype(path)::DataT>(*operState.contents());
            if (checkReachability(dsfSwitchReachability)) {
              me->neighborWatcherEvb_->runInEventBaseThread(
                  [me]() { me->processSwitchReachability(); });
            }
          },
          fboss::utils::ConnectionOptions("::1", fsdbPort_));
    }
    XLOG(INFO, "FSDB dsfSwitchReachability subscribe succeeded");
  } catch (std::exception const& ex) {
    XLOGF(ERR, "FSDB: addStatePathSubscription failed: {}", ex.what());
  }
}

void FsdbSwitchReachabilityWatcher::stop() noexcept {
  if (fsdbSubMgr_) {
    fsdbSubMgr_->stop();
  } else {
    CHECK(fsdbPubSubMgr_);
    const auto path = fsdbStateRootPath_.agent().config().sw();
    fsdbPubSubMgr_->removeStatePathSubscription(path.tokens());
  }
}

/*
 * NeighborWatcher
 */

NeighborWatcher::NeighborWatcher(
    MonitoredMPMCQueue<NeighborWatcherMessage>& neighborEventQ,
    nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ,
    const bool enableDsfFastTearDown,
    std::shared_ptr<fboss::fsdb::FsdbCowStateSubManager> sharedFsdbSubMgr,
    const int32_t fsdbPort)
    : BgpModuleBase(kModuleNeighborWatcher),
      sharedFsdbSubMgr_(std::move(sharedFsdbSubMgr)) {
  XLOG(INFO, "Initializing fsdb neighborWatcher");

  fsdbNbrWatcher_ = std::make_shared<FsdbNeighborWatcher>(
      neighborEventQ, ribInQ, &evb_, asyncScope_, fsdbPort);

  fsdbCfgBaton_ = std::make_shared<folly::fibers::Baton>();
  fsdbConfigWatcher_ =
      std::make_shared<FsdbConfigWatcher>(&evb_, fsdbCfgBaton_, fsdbPort);

  if (enableDsfFastTearDown) {
    fsdbReachabilityWatcher_ = std::make_shared<FsdbSwitchReachabilityWatcher>(
        neighborEventQ, &evb_, fsdbPort);
  }
}

void NeighborWatcher::learnConnectedNeighbors(
    std::shared_ptr<NexthopCache> nexthopCache,
    nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ) {
  fsdbNbrWatcher_->setResolvedNeighborCallback(
      [nexthopCache, &ribInQ](
          const folly::F14FastSet<NexthopKey, NexthopKey::Hash>&
              resolvedNexthops) {
        std::vector<NexthopStatus> updates;
        updates.reserve(resolvedNexthops.size());
        for (const auto& nexthop : resolvedNexthops) {
          XLOGF(
              INFO,
              "[NeighborWatcher] Resolved directly connected nexthop: {}, "
              "registering with igpCost=1, isConnected=true",
              nexthop.ip.str());
          updates.emplace_back(nexthop, true, 1, true);
        }
        if (!updates.empty()) {
          auto statuses = nexthopCache->addOrUpdateNextHopStatus(updates);
          if (!statuses.empty()) {
            ribInQ.fiberPush(RibInNexthopUpdate(std::move(statuses)));
          }
        }
      });
}

void NeighborWatcher::startFibWatcher(
    std::shared_ptr<NexthopCache> nexthopCache,
    nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ,
    const std::vector<NexthopKey>& nexthops) {
  if (!sharedFsdbSubMgr_) {
    XLOG(ERR, "startFibWatcher called but shared sub manager is not available");
    return;
  }

  /*
   * When next_hop_tracking_use_openr_igp_cost is enabled, the IGP cost is
   * derived solely from the Open/R client's nexthops (no intersection with the
   * resolved fwd nexthops); otherwise it is derived from the resolved fwd
   * nexthops only.
   */
  const std::optional<fboss::ClientID> igpCostClientId =
      FeatureFlags::getBgpBestpathFeatures().nextHopTrackingUseOpenrIgpCost
      ? std::optional<fboss::ClientID>{fboss::ClientID::OPENR}
      : std::nullopt;

  fsdbFibWatcher_ = std::make_shared<FsdbFibWatcher>(
      nexthopCache, ribInQ, &evb_, sharedFsdbSubMgr_, igpCostClientId);

  fsdbFibWatcher_->registerNexthops(nexthops);
}

void NeighborWatcher::subscribeLocked() {
  if (fsdbFibWatcher_) {
    sharedFsdbSubMgr_->subscribe(
        [this, nbrWatcher = fsdbNbrWatcher_, fibWatcher = fsdbFibWatcher_](
            auto update) {
          evb_.runInEventBaseThread([this,
                                     nbrWatcher,
                                     fibWatcher,
                                     update = std::move(update)]() mutable {
            nbrWatcher->handleCowUpdate(update);
            asyncScope_.add(co_withExecutor(
                &evb_, fibWatcher->co_processFibUpdate(std::move(update))));
          });
        },
        [this, fibWatcher = fsdbFibWatcher_](
            fboss::fsdb::SubscriptionState /* oldState */,
            fboss::fsdb::SubscriptionState newState,
            std::optional<bool> /* initialSyncHasData */) {
          if (fboss::fsdb::isConnected(newState)) {
            XLOG(INFO, "[SharedFsdbSub] FSDB connected (neighbor + FIB)");
            evb_.runInEventBaseThread([this, fibWatcher]() {
              asyncScope_.add(
                  co_withExecutor(&evb_, fibWatcher->co_markNeedsReconcile()));
            });
          } else if (fboss::fsdb::isDisconnected(newState)) {
            XLOG(WARNING, "[SharedFsdbSub] FSDB disconnected");
          }
        });
  } else {
    sharedFsdbSubMgr_->subscribe(
        [nbrWatcher = fsdbNbrWatcher_, evb = &evb_](auto update) {
          evb->runInEventBaseThread(
              [nbrWatcher, update = std::move(update)]() mutable {
                nbrWatcher->handleCowUpdate(update);
              });
        },
        [](fboss::fsdb::SubscriptionState /* oldState */,
           fboss::fsdb::SubscriptionState newState,
           std::optional<bool> /* initialSyncHasData */) {
          if (fboss::fsdb::isConnected(newState)) {
            XLOG(INFO, "[SharedFsdbSub] FSDB connected (neighbor-only)");
          } else if (fboss::fsdb::isDisconnected(newState)) {
            XLOG(WARNING, "[SharedFsdbSub] FSDB disconnected");
          }
        });
  }

  fsdbSubscribed_ = true;
  XLOG(INFO, "[NeighborWatcher] Shared FSDB subscription started");
}

void NeighborWatcher::subscribe() {
  if (!sharedFsdbSubMgr_) {
    return;
  }

  evb_.runInEventBaseThread([this]() { subscribeLocked(); });
}

void NeighborWatcher::requestNexthopSubscribe(
    std::vector<NexthopKey> nexthops) {
  if (!sharedFsdbSubMgr_ || !fsdbFibWatcher_) {
    return;
  }

  evb_.runInEventBaseThread([this, nexthops = std::move(nexthops)]() mutable {
    auto newNexthops = fsdbFibWatcher_->filterNewNexthops(nexthops);
    if (newNexthops.empty()) {
      // All already tracked — nothing to do.
      return;
    }
    /*
     * Adding paths requires the subscription to have been established. In
     * production subscribe() runs at startup before any RIB-IN nexthops are
     * learned, so this guard is defensive: if we are not yet subscribed, skip.
     * The nexthops stay untracked (not added to subscribedNexthops_) and are
     * surfaced again by filterNewNexthops on the next RIB-IN request.
     */
    if (!fsdbSubscribed_) {
      XLOGF(
          WARNING,
          "[NeighborWatcher] Skipping add of {} RIB-learned nexthop(s):"
          " shared FSDB subscription not yet active",
          newNexthops.size());
      return;
    }
    /*
     * Prefer live add-path when the connected FSDB server supports it: the new
     * paths are appended to the running subscription with no teardown. Older
     * servers lack the API, so fall back to the traditional
     * stop() -> addPath() -> subscribe(): stop() nulls the subscriber (the
     * accumulated path set survives), addNexthopPaths stages the new FIB paths
     * pre-subscribe, and subscribe() re-subscribes the full set. The
     * re-subscribe re-syncs the whole shared subscription (interfaceMaps + all
     * FIB routes); both watchers dedup, so no route churn results.
     */
    const bool liveAdd = sharedFsdbSubMgr_->supportsLiveAddPath();
    XLOGF(
        INFO,
        "[NeighborWatcher] Adding {} RIB-learned nexthop(s) to the shared FSDB"
        " subscription (liveAdd={})",
        newNexthops.size(),
        liveAdd);
    /*
     * Guard every path so no exception can escape this EventBase callback -- an
     * escaping exception would crash the evb thread and leave the shared
     * subscription (also used by FsdbNeighborWatcher) torn down until process
     * restart.
     */
    if (liveAdd) {
      /*
       * Live add-path: append the new paths to the already-running
       * subscription; no teardown, nothing to re-establish.
       */
      try {
        fsdbFibWatcher_->addNexthopPaths(newNexthops, /*liveAdd=*/true);
      } catch (const std::exception& ex) {
        XLOGF(
            ERR,
            "[NeighborWatcher] Failed to live-add RIB-learned nexthop(s): {}",
            ex.what());
      }
      return;
    }
    /*
     * Fallback (older server): stop() -> stage paths -> re-subscribe.
     *
     * fsdbSubscribed_ is deliberately NOT cleared here. It records that
     * startup has run, not that the subscription is healthy right now, and
     * clearing it would make a failed re-subscribe unrecoverable: the guard
     * above would skip every later request instead of retrying. Left true, a
     * failed subscribeLocked() leaves the sub manager with no subscriber, so
     * supportsLiveAddPath() reads false and the next request carrying a new
     * nexthop lands back on this path and re-attempts the subscribe.
     *
     * stop() and the staging share a try block on purpose: addPath()
     * CHECK-fails ("Cannot add paths after subscribed") while a subscriber is
     * still live, so paths must only be staged if the teardown succeeded.
     * subscribeLocked() gets its OWN block so it ALWAYS runs -- the
     * accumulated path set survives stop(), so re-subscribing is right even
     * when staging failed, and skipping it would leave the shared subscription
     * (also used by FsdbNeighborWatcher) torn down until process restart.
     */
    try {
      sharedFsdbSubMgr_->stop();
      fsdbFibWatcher_->addNexthopPaths(newNexthops, /*liveAdd=*/false);
    } catch (const std::exception& ex) {
      XLOGF(
          ERR,
          "[NeighborWatcher] Failed to stage RIB-learned nexthop(s) on the"
          " fallback path: {}",
          ex.what());
    }
    try {
      subscribeLocked();
    } catch (const std::exception& ex) {
      XLOGF(
          ERR,
          "[NeighborWatcher] Failed to re-subscribe the shared FSDB"
          " subscription on the fallback path: {}",
          ex.what());
    }
  });
}

void NeighborWatcher::run() noexcept {
  SCOPE_EXIT {
    isRunning_ = false;
  };
  isRunning_ = true;

  if (sharedFsdbSubMgr_) {
    /**
     * Add interfaceMaps path to the shared sub manager. Subscribe is
     * deferred until subscribe() is called from Main.cpp, ensuring all
     * paths (interfaceMaps + optional FIB routes) are registered first.
     **/
    thriftpath::RootThriftPath<fboss::fsdb::FsdbOperStateRoot> rootPath;
    sharedFsdbSubMgr_->addPath(rootPath.agent().switchState().interfaceMaps());
    XLOG(
        INFO,
        "[NeighborWatcher] Added interfaceMaps path to shared FSDB sub"
        " manager (subscribe deferred)");
  } else {
    /**
     * TODO: with deprecation of NeighborWatcher, remove this else
     * clause
     */
    fsdbNbrWatcher_->run();
  }
  fsdbConfigWatcher_->run();
  if (fsdbReachabilityWatcher_) {
    XLOG(INFO, "fsdbReachabilityWatcher enabled.");
    fsdbReachabilityWatcher_->run();
  }

  XLOG(INFO, "Start NeighborWatcher event-base loop");
  evb_.loopForever();
  XLOG(INFO, "[Exit] Successfully terminated NeighborWatcher event-base");
}

void NeighborWatcher::stop() noexcept {
  if (isRunning_.exchange(false)) {
    /*
     * Stop all event sources first to prevent new callbacks from posting
     * coroutines to asyncScope_ after it is joined. Without this ordering,
     * a callback arriving after cancelAndJoinAsync() would call
     * asyncScope_.add() on a joined scope and crash.
     */
    evb_.runInEventBaseThreadAndWait([&]() {
      if (sharedFsdbSubMgr_) {
        sharedFsdbSubMgr_->stop();
      } else {
        fsdbNbrWatcher_->stop();
      }
      fsdbConfigWatcher_->stop();
      if (fsdbReachabilityWatcher_) {
        fsdbReachabilityWatcher_->stop();
      }
      if (fsdbFibWatcher_) {
        fsdbFibWatcher_->stop();
      }
    });

    XLOG(INFO, "[Exit] Cancel and stop all coroutines");
    folly::coro::blockingWait(asyncScope_.cancelAndJoinAsync());

    evb_.terminateLoopSoon();
  }
  XLOG(INFO, "[Exit] All tasks finished");
}

std::optional<folly::F14NodeMap<folly::CIDRNetwork, int64_t>>
NeighborWatcher::getFsdbPeerSubnetLbwMap() noexcept {
  // Wait for FSDB to publish config
  XLOG(INFO, "getFsdbPeerSubnetLbwMap: waiting for baton");
  if (fsdbCfgBaton_->try_wait_for(
          std::chrono::seconds(FLAGS_fsdb_config_timeout_s))) {
    XLOG(INFO, "getFsdbPeerSubnetLbwMap: got baton");
  } else {
    XLOG(ERR, "getFsdbPeerSubnetLbwMap: baton wait timed out");
    fsdbCfgBaton_->reset();
    return std::nullopt;
  }
  std::optional<folly::F14NodeMap<folly::CIDRNetwork, int64_t>>
      peerSubnetLbwMap = std::nullopt;
  evb_.runInEventBaseThreadAndWait(
      [&] { peerSubnetLbwMap = fsdbConfigWatcher_->getPeerSubnetLbwMap(); });
  return peerSubnetLbwMap;
}

folly::coro::Task<std::optional<folly::F14NodeMap<folly::CIDRNetwork, int64_t>>>
NeighborWatcher::co_getPeerSubnetLbwMap() noexcept {
  if (!isRunning_) {
    XLOG(INFO, "nbrWatcher is not running");
    co_return std::nullopt;
  }

  std::optional<folly::F14NodeMap<folly::CIDRNetwork, int64_t>>
      fsdbPeerSubnetLbwMap = std::nullopt;
  fsdbPeerSubnetLbwMap = getFsdbPeerSubnetLbwMap();
  if (fsdbPeerSubnetLbwMap) {
    XLOGF(
        INFO,
        "Constructed peerSubnetLbwMap with size {} via FSDB",
        fsdbPeerSubnetLbwMap->size());
    for (const auto& [peerSubnet, mbps] : *fsdbPeerSubnetLbwMap) {
      XLOGF(
          INFO,
          "PeerSubnet {}, LinkBandwidth {}Mbps",
          folly::IPAddress::networkToString(peerSubnet),
          mbps);
    }
  } else {
    XLOG(INFO, "Did not get FSDB peerSubnetLbwMap");
  }

  co_return fsdbPeerSubnetLbwMap;
}

} // namespace facebook::bgp
