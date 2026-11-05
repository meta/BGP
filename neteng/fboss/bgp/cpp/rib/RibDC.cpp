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

#include <folly/coro/BoundedQueue.h>
#include <folly/coro/CurrentExecutor.h>
#include <folly/logging/xlog.h>

#include "common/network/AddressUtil.h"
#include "neteng/fboss/bgp/cpp/BgpServiceUtil.h"
#include "neteng/fboss/bgp/cpp/common/BgpError.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/FeatureFlags.h"
#include "neteng/fboss/bgp/cpp/fsdb/CanonicalRibUpdateQueue.h"
#include "neteng/fboss/bgp/cpp/fsdb/FsdbSyncer.h"
#include "neteng/fboss/bgp/cpp/peer/NeighborWatcher.h"
#include "neteng/fboss/bgp/cpp/rib/FibDev.h"
#include "neteng/fboss/bgp/cpp/rib/FibFboss.h"
#include "neteng/fboss/bgp/cpp/rib/RibDC.h"
#include "neteng/fboss/bgp/cpp/rib/RibFileUtils.h"
#include "neteng/fboss/bgp/cpp/rib/RibPolicy.h"
#include "neteng/fboss/bgp/cpp/rib/RibPolicyLogger.h"
#include "neteng/fboss/bgp/cpp/rib/Utils.h"
#include "neteng/fboss/bgp/cpp/rib/canonical/CanonicalRibBuilder.h"
#include "neteng/fboss/bgp/cpp/stats/StatsDC.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

DEFINE_bool(
    publish_multipaths_to_fsdb,
    false,
    "When publishing the canonical RIB to FSDB, export the full ECMP multipath "
    "set per prefix instead of just the selected best path. Entries without "
    "a usable best path include all candidates regardless of this flag");

DEFINE_string(
    crf_policy_file,
    "",
    "Path to the CRF Policy artifact file for file-based CRF delivery");

DEFINE_string(
    cps_policy_file,
    "",
    "Path to the CPS Policy artifact file for file-based CPS delivery");

using namespace facebook::neteng::fboss::bgp_attr;
using namespace facebook::neteng::fboss::bgp::thrift;

namespace facebook::bgp {

/* Cross-thread, capacity-one notification for authoritative snapshot work. */
class CanonicalRibSnapshotRequests {
 public:
  void notify() noexcept {
    requests_.try_enqueue(folly::unit);
  }

  folly::coro::Task<void> wait() {
    co_await requests_.dequeue();
  }

 private:
  folly::coro::BoundedQueue<folly::Unit, true, true> requests_{1};
};

std::vector<CanonicalPathInput> RibDC::buildCanonicalPathInputs(
    const facebook::bgp::RibEntry& ribEntry,
    bool failedCpsNativeCriteria,
    folly::FunctionRef<bool(const RouteInfo&)> includePath,
    RouteInfo* singlePath) {
  auto* bestpath = ribEntry.bestpath_.get();
  const auto& multipathRouteinfos = ribEntry.getMultipaths();
  auto weightedNexthops = ribEntry.getMultipathWeightedNexthops();

  const bool nexthopTrackingEnabled =
      FeatureFlags::getBgpBestpathFeatures().enableNextHopTracking;

  std::vector<CanonicalPathInput> inputs;
  inputs.reserve(singlePath ? 1 : ribEntry.getAllPathsCnt());
  const auto appendPath = [&](RouteInfo& routeinfo) {
    if (!includePath(routeinfo)) {
      return;
    }
    CanonicalPathInput input;
    input.path = routeinfo.attrs;
    input.peerAddr = routeinfo.peer.addr;
    input.peerRouterId = routeinfo.peer.routerId;
    input.peerDescription = routeinfo.peer.description;
    input.pathId = routeinfo.receivedPathId;
    if (weightedNexthops) {
      auto it = weightedNexthops->find(routeinfo.peer.addr);
      if (it != weightedNexthops->end()) {
        input.nextHopWeight = it->second;
      }
    }
    /* Per-instance operational fields, mirror createTRibEntryWithFilter */
    /*
     * Reachability and cost presence are independent. Serialize igpCost only
     * when supplied; local routes get the maximum value.
     */
    if (routeinfo.getIsRouteLocal() || routeinfo.hasIgpCost()) {
      input.igpCost = routeinfo.getIgpCostValue();
    }
    input.bestPathFilterDescr = routeinfo.getBestPathFilterDescr();
    input.lastModifiedTime = routeinfo.lastModifiedTime_;
    if (routeinfo.pathIdToSend.has_value()) {
      input.pathIdToSend = routeinfo.pathIdToSend.value();
    }
    input.isInactive =
        nexthopTrackingEnabled && !routeinfo.isResolvedForSelection();
    const bool inMultipath = routeinfo.pathIdToSend.has_value() &&
        multipathRouteinfos.contains(routeinfo.pathIdToSend.value());
    /*
     * A community-scoped getter may omit the best path while retaining other
     * multipath members. Keep those paths in kBestPathGroup even though none
     * carries is_best_path.
     */
    if (inMultipath && !failedCpsNativeCriteria) {
      if (bestpath && &routeinfo == bestpath) {
        input.isBestPath = true;
      }
      input.group = facebook::bgp::kBestPathGroup;
    } else {
      input.group = facebook::bgp::kDefaultPathGroup;
    }
    inputs.push_back(std::move(input));
  };

  if (singlePath) {
    appendPath(*singlePath);
    return inputs;
  }
  for (const auto& peerRouteInfos : ribEntry.routeInfos_) {
    for (const auto& pathRouteInfo : peerRouteInfos.second) {
      appendPath(*pathRouteInfo.second);
    }
  }
  return inputs;
}

std::optional<CanonicalRibEntryInput> RibDC::buildCanonicalRibEntryInput(
    const RibEntry& ribEntry) {
  auto* bestPath = ribEntry.bestpath_.get();
  const bool failedCpsNativeCriteria =
      canonicalFailedCpsNativeCriteria(ribEntry);
  const bool includeAllCandidates =
      bestPath == nullptr || failedCpsNativeCriteria;
  CanonicalRibEntryInput entryInput;
  if (!FLAGS_publish_multipaths_to_fsdb && !includeAllCandidates) {
    entryInput.paths = buildCanonicalPathInputs(
        ribEntry,
        failedCpsNativeCriteria,
        [](const RouteInfo&) { return true; },
        bestPath);
    entryInput.includePaths = false;
  } else {
    const auto& multipaths = ribEntry.getMultipaths();
    entryInput.paths = buildCanonicalPathInputs(
        ribEntry,
        failedCpsNativeCriteria,
        [includeAllCandidates, &multipaths](const RouteInfo& routeInfo) {
          return includeAllCandidates ||
              (routeInfo.pathIdToSend.has_value() &&
               multipaths.contains(routeInfo.pathIdToSend.value()));
        });
    entryInput.includePaths = true;
  }
  if (entryInput.paths.empty()) {
    return std::nullopt;
  }
  entryInput.fields = buildCanonicalEntryFields(ribEntry);
  return entryInput;
}

CanonicalEntryFields RibDC::buildCanonicalEntryFields(
    const RibEntry& ribEntry) {
  CanonicalEntryFields entryFields;
  if (ribEntry.needPathSelection()) {
    entryFields.pathSelectionPending = true;
  }
  entryFields.activeCpsCriteria = getCanonicalActiveCpsCriteria(ribEntry);
  if (routeAttributePolicy_) {
    auto activeCteUcmpAction =
        routeAttributePolicy_->getActiveCteUcmpAction(ribEntry.getPrefix());
    if (activeCteUcmpAction) {
      entryFields.activeCteUcmpAction = std::move(*activeCteUcmpAction);
    }
  }
  return entryFields;
}

bool RibDC::addCanonicalGetterEntry(
    CanonicalRibBuilder& builder,
    const folly::CIDRNetwork& prefix,
    const facebook::bgp::RibEntry& ribEntry,
    folly::FunctionRef<bool(const RouteInfo&)> includePath) {
  auto inputs = buildCanonicalPathInputs(
      ribEntry, canonicalFailedCpsNativeCriteria(ribEntry), includePath);

  /* Nothing to export if no paths passed the filter */
  if (inputs.empty()) {
    return false;
  }

  builder.addEntry(
      prefix,
      ribEntry.getRibVersion(),
      inputs,
      buildCanonicalEntryFields(ribEntry));
  return true;
}

/*
 * Full-RIB canonical getter. Routes every entry through
 * addCanonicalGetterEntry, applying the same CPS native-criteria demotion as
 * the legacy getRibEntries / RibDC::createTRibEntryWithFilter path.
 */
neteng::fboss::bgp::thrift::TCanonicalRibState RibDC::getRibEntriesCanonical(
    TBgpAfi afi) {
  if (afi != TBgpAfi::AFI_IPV4 && afi != TBgpAfi::AFI_IPV6) {
    return neteng::fboss::bgp::thrift::TCanonicalRibState{};
  }

  CanonicalRibBuilder builder;

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    auto expectIPv4 = afi == TBgpAfi::AFI_IPV4;
    for (const auto& [prefix, ribEntry] : ribEntries_) {
      auto isIPv4 = prefix.first.family() == AF_INET;
      if (expectIPv4 != isIPv4) {
        continue;
      }
      addCanonicalGetterEntry(
          builder, prefix, ribEntry, [](const RouteInfo&) { return true; });
    }
  });
  return builder.build();
}

neteng::fboss::bgp::thrift::TCanonicalRibState RibDC::getRibPrefixCanonical(
    std::unique_ptr<std::string> prefix) {
  CanonicalRibBuilder builder;
  if (!prefix) {
    return builder.build();
  }

  folly::CIDRNetwork network;
  try {
    network = folly::IPAddress::createNetwork(*prefix);
  } catch (std::exception const&) {
    XLOGF(ERR, "Invalid prefix: {}", *prefix);
    return builder.build();
  }

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    auto entry = ribEntries_.find(network);
    if (entry != ribEntries_.end()) {
      addCanonicalGetterEntry(
          builder, entry->first, entry->second, [](const RouteInfo&) {
            return true;
          });
    }
  });

  return builder.build();
}

neteng::fboss::bgp::thrift::TCanonicalRibState
RibDC::getRibEntriesForCommunitiesCanonical(
    TBgpAfi afi,
    const std::vector<nettools::bgplib::BgpAttrCommunityC>& communities) {
  CanonicalRibBuilder builder;

  if (communities.empty()) {
    return builder.build();
  }

  if (afi != TBgpAfi::AFI_IPV4 && afi != TBgpAfi::AFI_IPV6) {
    return builder.build();
  }

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    for (const auto& [prefix, ribEntry] : ribEntries_) {
      if (((afi == TBgpAfi::AFI_IPV4) && (prefix.first.family() != AF_INET)) ||
          ((afi == TBgpAfi::AFI_IPV6) && (prefix.first.family() != AF_INET6))) {
        continue;
      }
      addCanonicalGetterEntry(
          builder,
          prefix,
          ribEntry,
          [&communities](const RouteInfo& path) -> bool {
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
    }
  });
  return builder.build();
}

neteng::fboss::bgp::thrift::TCanonicalRibState
RibDC::getRibEntriesForCommunityCanonical(
    TBgpAfi afi,
    std::unique_ptr<std::string> community) {
  CanonicalRibBuilder builder;
  if (!community) {
    XLOG(ERR, "No community is provided");
    return builder.build();
  }

  std::optional<nettools::bgplib::BgpAttrCommunityC> comm =
      nettools::bgplib::BgpAttrCommunityC::createBgpAttrCommunity(*community);
  if (comm == std::nullopt) {
    /*
     * Parity with the legacy community getter: throw on an unparseable
     * community (the co_* handler propagates it to the client).
     */
    throw std::invalid_argument("Invalid Community Value!");
  }
  std::vector<nettools::bgplib::BgpAttrCommunityC> communities{*comm};

  return getRibEntriesForCommunitiesCanonical(afi, communities);
}

neteng::fboss::bgp::thrift::TCanonicalRibState
RibDC::getRibSubprefixesCanonical(std::unique_ptr<std::string> prefix) {
  CanonicalRibBuilder builder;
  if (!prefix) {
    XLOG(ERR, "No prefix is provided");
    return builder.build();
  }

  folly::CIDRNetwork parentPrefix;
  try {
    parentPrefix = folly::IPAddress::createNetwork(*prefix);
  } catch (std::exception const&) {
    XLOGF(ERR, "Invalid prefix: {}", *prefix);
    return builder.build();
  }

  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    for (const auto& [subPrefix, ribEntry] : ribEntries_) {
      /*
       * isSubnet matches the parent prefix itself too, mirroring the legacy
       * getRibEntriesForSubprefixes (the parent is included in the result).
       */
      if (!isSubnet(subPrefix, parentPrefix)) {
        continue;
      }
      addCanonicalGetterEntry(
          builder, subPrefix, ribEntry, [](const RouteInfo&) { return true; });
    }
  });
  return builder.build();
}

namespace {
/*
 * Format a policy's most-recent (soonest) expiration timestamp for logging.
 * A policy with no statements has no expiration to report, so render it as
 * "N/A"; otherwise report the epoch-seconds value.
 */
std::string formatMostRecentExpirationTimeS(
    const RouteAttributePolicy& policy) {
  return policy.getStatements().empty()
      ? "N/A"
      : std::to_string(policy.getMostRecentExpirationTime());
}
} // namespace

RibDC::CrfResolution RibDC::resolveCrfPolicy(
    std::unique_ptr<RibPolicy> cachedPolicy,
    const std::optional<rib_policy::CrfPolicyArtifact>& artifact) {
  if (!artifact.has_value()) {
    return {std::move(cachedPolicy), false};
  }

  bool crfFileMode = !*artifact->dryrun();

  if (crfFileMode && cachedPolicy) {
    auto tRibPolicy = cachedPolicy->toThrift();
    tRibPolicy.route_filter_policy() = *artifact->policy();
    cachedPolicy = std::make_unique<RibPolicy>(tRibPolicy);
    XLOGF(
        INFO,
        "CRF FILE_MODE: replaced cached CRF with artifact policy (version={})",
        *artifact->policy()->version());
  } else if (crfFileMode) {
    rib_policy::TRibPolicy tRibPolicy;
    tRibPolicy.route_filter_policy() = *artifact->policy();
    cachedPolicy = std::make_unique<RibPolicy>(tRibPolicy);
    XLOGF(
        INFO,
        "CRF FILE_MODE: no cached policy, creating new with artifact (version={})",
        *artifact->policy()->version());
  }

  return {std::move(cachedPolicy), crfFileMode};
}

RibDC::CpsResolution RibDC::resolveCpsPolicy(
    std::unique_ptr<RibPolicy> cachedPolicy,
    const std::optional<rib_policy::CpsPolicyArtifact>& artifact) {
  if (!artifact.has_value()) {
    return {std::move(cachedPolicy), false};
  }

  bool cpsFileMode = !*artifact->dryrun();
  if (!cpsFileMode) {
    // dryrun: keep the cached policy untouched and stay in THRIFT_MODE.
    return {std::move(cachedPolicy), false};
  }

  if (cachedPolicy) {
    auto tRibPolicy = cachedPolicy->toThrift();
    tRibPolicy.path_selection_policy() = *artifact->policy();
    cachedPolicy = std::make_unique<RibPolicy>(tRibPolicy);
    XLOGF(
        INFO,
        "CPS FILE_MODE: replaced cached CPS with artifact policy (version={})",
        *artifact->policy()->version());
  } else {
    rib_policy::TRibPolicy tRibPolicy;
    tRibPolicy.path_selection_policy() = *artifact->policy();
    cachedPolicy = std::make_unique<RibPolicy>(tRibPolicy);
    XLOGF(
        INFO,
        "CPS FILE_MODE: no cached policy, creating new with artifact (version={})",
        *artifact->policy()->version());
  }

  return {std::move(cachedPolicy), cpsFileMode};
}

RibDC::RibDC(
    const std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>&
        localRoutes,
    const BgpGlobalConfig& globalConfig,
    const std::optional<bgp_policy::BgpPolicies>& policyConfig,
    nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ,
    MonitoredMPMCQueue<RibOutMessage>& ribOutQ,
    const std::string& platform,
    FsdbSyncer* fsdbSyncer,
    std::shared_ptr<NexthopCache> nexthopCache,
    uint16_t fibAgentPort,
    uint32_t fibAgentRecvTimeout,
    std::shared_ptr<NeighborWatcher> neighborWatcher)
    : RibBase(
          localRoutes,
          globalConfig,
          policyConfig,
          ribInQ,
          ribOutQ,
          platform,
          nexthopCache,
          fibAgentPort,
          fibAgentRecvTimeout) {
  fsdbSyncer_ = fsdbSyncer;

  /*
   * Wire RIB-IN-learned nexthops to the FIB watcher for FSDB tracking. Done in
   * the constructor (before the RIB thread starts) so no learned nexthop is
   * missed. nexthopCache_ is non-null exactly when nexthop tracking is enabled;
   * DC-only — EBB constructs RibBB and has no NeighborWatcher.
   */
  if (nexthopCache_ && neighborWatcher) {
    setNexthopSubscribeRequester(
        [neighborWatcher](std::vector<folly::IPAddress> nexthops) {
          neighborWatcher->requestNexthopSubscribe(std::move(nexthops));
        });
  }

  routeAttributePolicyTimer_ =
      folly::AsyncTimeout::make(evb_, [this]() noexcept {
        if (!routeAttributePolicy_) {
          return;
        }
        auto mostRecentExpTime =
            routeAttributePolicy_->getMostRecentExpirationTime();
        auto now = std::chrono::seconds(std::time(nullptr)).count();
        if (mostRecentExpTime < now) {
          enqueueRibPolicyMsg(RouteAttributePolicyTimerMsg{});
        }
        scheduleRouteAttributePolicyTimer();
      });

  if (globalConfig_.deviceName) {
    ribPolicyLogger_ = createRibPolicyLogger(*globalConfig_.deviceName);
  }

  /*
   * Read previous RibPolicy from disk to restore policy and trigger fib
   * programming. This must happen in the subclass constructor since
   * replaceRibPolicy() is pure virtual in RibBase.
   */
  auto crfRead = readThriftArtifactFromFile<rib_policy::CrfPolicyArtifact>(
      FLAGS_crf_policy_file);
  /*
   * Instrument the startup CRF artifact read so a restart-time read is
   * observable. The on-demand Thrift path (setCrfPolicyFromFile) increments
   * these counters, but the bootstrap path historically did not, leaving no
   * fb303/log signal for "did bgpd pick up the artifact on restart?".
   *
   * Count kError (file present but unreadable/corrupt) as a failure. kAbsent
   * (no path configured or file not present) is the expected state on devices
   * not yet onboarded to file-mode; counting it would swamp the failure counter
   * and make the success/failure ratio meaningless, so it is not counted.
   */
  std::optional<rib_policy::CrfPolicyArtifact> tCrfArtifact;
  if (crfRead.hasValue()) {
    BgpStatsDC::incrCrfArtifactReadSuccess();
    tCrfArtifact = std::move(crfRead.value());
  } else if (crfRead.error() == ArtifactReadError::kError) {
    BgpStatsDC::incrCrfArtifactReadFailure();
  }

  /*
   * Compose the CRF and CPS file-mode resolutions over a single cached
   * RibPolicy so a device can run either or both sub-policies in file mode:
   * resolve the cached policy against the CRF artifact first, then feed that
   * result as the "cached" policy into the CPS resolution. Each resolveX
   * step only swaps its own sub-policy (route_filter_policy /
   * path_selection_policy) and leaves the others untouched, so neither
   * overwrites the other. The single merged RibPolicy is then installed once
   * via replaceRibPolicy.
   */
  auto [crfRibPolicy, crfFileMode] =
      resolveCrfPolicy(readRibPolicyState(), tCrfArtifact);
  XLOGF(
      INFO,
      "CRF startup read: artifact={}, dryrun={}, mode={}",
      tCrfArtifact.has_value() ? "present" : "absent",
      tCrfArtifact.has_value() ? (*tCrfArtifact->dryrun() ? "true" : "false")
                               : "n/a",
      crfFileMode ? "FILE_MODE" : "THRIFT_MODE");

  auto cpsRead = readThriftArtifactFromFile<rib_policy::CpsPolicyArtifact>(
      FLAGS_cps_policy_file);
  /*
   * Instrument the startup CPS artifact read so a restart-time read is
   * observable, mirroring the CRF bootstrap read above. Count kError (file
   * present but unreadable/corrupt) as a failure. kAbsent (no path configured
   * or file not present) is the expected state on devices not yet onboarded to
   * file-mode and is not counted, so the success/failure ratio stays
   * meaningful.
   */
  std::optional<rib_policy::CpsPolicyArtifact> tCpsArtifact;
  if (cpsRead.hasValue()) {
    BgpStatsDC::incrCpsArtifactReadSuccess();
    tCpsArtifact = std::move(cpsRead.value());
  } else if (cpsRead.error() == ArtifactReadError::kError) {
    BgpStatsDC::incrCpsArtifactReadFailure();
  }
  auto [ribPolicy, cpsFileMode] =
      resolveCpsPolicy(std::move(crfRibPolicy), tCpsArtifact);
  XLOGF(
      INFO,
      "CPS startup read: artifact={}, dryrun={}, mode={}",
      tCpsArtifact.has_value() ? "present" : "absent",
      tCpsArtifact.has_value() ? (*tCpsArtifact->dryrun() ? "true" : "false")
                               : "n/a",
      cpsFileMode ? "FILE_MODE" : "THRIFT_MODE");

  /*
   * The bootstrap install flows through replacePathSelectionPolicy (via
   * replaceRibPolicy above), which increments bgpd.cps.policy_applied.success
   * on the real apply — so no separate bootstrap counter is emitted here.
   */
  replaceRibPolicy(std::move(ribPolicy), /*isBootstrap=*/true);
  setCrfFileModeEnabled(crfFileMode);
  setCpsFileModeEnabled(cpsFileMode);
}

void RibDC::configureCanonicalRibExport(FsdbSyncer& fsdbSyncer) {
  auto snapshotRequests = std::make_shared<CanonicalRibSnapshotRequests>();
  canonicalRibUpdateQueue_ = fsdbSyncer.registerCanonicalRibSource(
      [snapshotRequests]() { snapshotRequests->notify(); });
  if (!canonicalRibUpdateQueue_) {
    return;
  }
  getRibAsyncScope().add(
      folly::coro::co_withExecutor(
          &evb_,
          consumeCanonicalRibSnapshotRequests(std::move(snapshotRequests))));
}

bool RibDC::isCrfFileModeEnabled() const {
  return crfFileModeEnabled_;
}

void RibDC::setFileModeEnabled(
    std::atomic<bool>& flag,
    bool fileModeActive,
    std::string_view policyName) {
  if (flag.exchange(fileModeActive) != fileModeActive) {
    XLOGF(
        INFO,
        "{} Policy mode changed: {}",
        policyName,
        fileModeActive ? "FILE_MODE" : "THRIFT_MODE");
  }
}

void RibDC::setCrfFileModeEnabled(bool fileModeActive) {
  setFileModeEnabled(crfFileModeEnabled_, fileModeActive, "CRF");
  /*
   * Keep the fb303 gauge in sync with the real mode so monitoring can
   * distinguish FILE_MODE (1) from THRIFT_MODE (0). Set unconditionally (not
   * only on transition) so the gauge is also correct after the initial
   * bootstrap call. Without this the gauge stays at its init value (0) and can
   * never reflect FILE_MODE.
   */
  BgpStatsDC::setCrfFileModeEnabled(fileModeActive);
}

bool RibDC::isCpsFileModeEnabled() const {
  return cpsFileModeEnabled_;
}

void RibDC::setCpsFileModeEnabled(bool fileModeActive) {
  setFileModeEnabled(cpsFileModeEnabled_, fileModeActive, "CPS");
  /*
   * Keep the fb303 gauge in sync with the real mode so monitoring can
   * distinguish FILE_MODE (1) from THRIFT_MODE (0). Set unconditionally (not
   * only on transition) so the gauge is also correct after the initial
   * bootstrap call. Without this the gauge stays at its init value (0) and can
   * never reflect FILE_MODE.
   */
  BgpStatsDC::setCpsFileModeEnabled(fileModeActive);
}

void RibDC::run() noexcept {
  if (fsdbSyncer_) {
    configureCanonicalRibExport(*fsdbSyncer_);
  }
  RibBase::run();
}

folly::coro::Task<void> RibDC::consumeCanonicalRibSnapshotRequests(
    std::shared_ptr<CanonicalRibSnapshotRequests> requests) noexcept {
  while (true) {
    auto request = co_await folly::coro::co_awaitTry(requests->wait());
    if (request.hasException()) {
      if (!request.hasException<folly::OperationCancelled>()) {
        XLOGF(
            ERR,
            "Canonical RIB snapshot request consumer failed: {}",
            request.exception().what());
      }
      co_return;
    }
    const auto& cancellationToken =
        co_await folly::coro::co_current_cancellation_token;
    if (cancellationToken.isCancellationRequested()) {
      co_return;
    }
    canonicalSnapshotPending_ = true;
    if (pauseBestPathAndFibProgramming_ || fibBatchTimer_->isScheduled()) {
      continue;
    }
    enqueueCanonicalRibFullSnapshot("fsdb_connect");
  }
}

void RibDC::enqueueCanonicalRibFullSnapshot(std::string_view reason) {
  if (!canonicalRibUpdateQueue_) {
    return;
  }
  canonicalSnapshotPending_ = false;
  const auto start = std::chrono::steady_clock::now();
  XLOGF(
      INFO,
      "[CanonicalRib] Starting full RIB walk: reason={}, ribEntries={}",
      reason,
      ribEntries_.size());
  RibStatsDC::STATS_canonicalRibExportReconnectRebuildStart.add(1);
  CanonicalRibFullSnapshotInput snapshot;
  snapshot.entries.reserve(ribEntries_.size());
  for (const auto& [prefix, entry] : ribEntries_) {
    auto entryInput = buildCanonicalRibEntryInput(entry);
    if (!entryInput) {
      continue;
    }
    snapshot.entries.push_back(
        CanonicalRibPrefixUpdate{
            .prefix = prefix,
            .entry = std::move(*entryInput),
        });
  }
  const auto snapshotPrefixCount = snapshot.entries.size();
  canonicalRibUpdateQueue_->pushFullSnapshot(std::move(snapshot));
  const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count();
  XLOGF(
      INFO,
      "[CanonicalRib] Full RIB snapshot enqueued: reason={}, prefixes={}, durationMs={}",
      reason,
      snapshotPrefixCount,
      durationMs);
}

void RibDC::createFib() {
  if (platform_ == kDevPlatform) {
    XLOG(DBG1, "Creating Fib Dev with no actual route programming");
    fib_ = FibDev::createFibDev(fromFibMessageQ_);
  } else {
    XLOG(DBG1, "Creating Fib for FBOSS Agent");
    fib_ = FibFboss::createFibFboss(&evb_, asyncScope_, fromFibMessageQ_);
  }
}

void RibDC::processNexthopResolutionUpdate(
    const NexthopResolutionUpdate& nexthopResolutionUpdate) noexcept {
  /*
   * Process conditional-route advertisements/withdrawals first (if any), THEN
   * re-evaluate the deferred initial path computation. This ordering guarantees
   * that conditional routes are in ribEntries_ before the initial syncFib runs,
   * preventing it from wiping GR-retained conditional routes in FibAgent on BGP
   * daemon restart.
   */
  if (!conditionalLocalRoutes_.empty()) {
    processConditionalRoutesForNexthops(
        nexthopResolutionUpdate.resolved,
        [this](const folly::CIDRNetwork& prefix, PrefixPathIds&& pfxPathIds) {
          XLOGF(
              INFO,
              "Announcing conditional route {}",
              folly::IPAddress::networkToString(prefix));
          processRibInAnnouncement(
              kV4LocalPeerInfo, localRoutes_.at(prefix).attrs, pfxPathIds);
        });
    processConditionalRoutesForNexthops(
        nexthopResolutionUpdate.unresolved,
        [this](const folly::CIDRNetwork& prefix, PrefixPathIds&& pfxPathIds) {
          XLOGF(
              INFO,
              "Withdrawing conditional route {}",
              folly::IPAddress::networkToString(prefix));
          processRibInWithdrawal(kV4LocalPeerInfo, pfxPathIds);
        });
  }

  /*
   * The first NexthopResolutionUpdate has now been processed: conditional
   * routes (if any) are advertised into ribEntries_, so it is safe for the
   * initial full-sync to run without wiping them. This gates
   * isInitialPathComputationReady() for the conditional-routes case,
   * independent of nexthop tracking.
   */
  firstNexthopResolutionProcessed_ = true;

  /*
   * This update may have completed the initial-path-computation preconditions
   * (nexthop resolution and/or conditional-route advertisement); run the
   * deferred computation if it was waiting on this.
   */
  maybeRunPendingInitialPathComputation();
}

bool RibDC::enqueuePartialDrainState() {
  if (!fsdbSyncer_) {
    return false;
  }
  if (!FLAGS_publish_partial_drain_state_to_fsdb) {
    return false;
  }
  /*
   * Build the snapshot here (device summary + per-prefix drained set) rather
   * than in RibBase::prepareFibProgramming, keeping all drain-domain logic on
   * the DC side. getPartialDrainState() scans ribEntries_ for drained prefixes
   * — O(n_ribEntries) — but this path is gated on a pending publish, so the
   * scan runs only on drain-change passes, not every FIB programming pass.
   */
  fsdbSyncer_->setPartialDrainState(std::make_optional(getPartialDrainState()));
  return true;
}

/*
 * Instead of updating RibPolicy, only replace it. Each time the RibPolicy is
 * replaced, we also need to update the pointers atRibEntry.
 */
void RibDC::replaceRibPolicy(
    std::unique_ptr<RibPolicy> newRibPolicy,
    bool isBootstrap) {
  std::unique_ptr<RouteAttributePolicy> newRouteAttributePolicy = nullptr;
  bool hasUpdateRA = false;
  std::unique_ptr<PathSelectionPolicy> newPathSelectionPolicy = nullptr;
  bool hasUpdatePS = false;
  std::unique_ptr<RouteFilterPolicy> newRouteFilterPolicy = nullptr;
  bool hasUpdateRF = false;

  if (newRibPolicy) {
    newRouteAttributePolicy = newRibPolicy->hasRouteAttributePolicy()
        ? folly::copy_to_unique_ptr(
              std::move(*newRibPolicy->getRouteAttributePolicy()))
        : nullptr;

    newPathSelectionPolicy = newRibPolicy->hasPathSelectionPolicy()
        ? folly::copy_to_unique_ptr(
              std::move(*newRibPolicy->getPathSelectionPolicy()))
        : nullptr;

    newRouteFilterPolicy = newRibPolicy->hasRouteFilterPolicy()
        ? folly::copy_to_unique_ptr(
              std::move(*newRibPolicy->getRouteFilterPolicy()))
        : nullptr;
  }
  hasUpdateRA = replaceRouteAttributePolicy(std::move(newRouteAttributePolicy));
  hasUpdatePS = replacePathSelectionPolicy(
      std::move(newPathSelectionPolicy), isBootstrap);
  hasUpdateRF =
      replaceRouteFilterPolicy(std::move(newRouteFilterPolicy), isBootstrap);

  if (isBootstrap) {
    XLOG(DBG1, "restored RibPolicy from cache");
  } else if (hasUpdateRA || hasUpdatePS || hasUpdateRF) {
    XLOGF(
        DBG1,
        "Replace RibPolicy with a new one. "
        "hasUpdateRA = {}, hasUpdatePS = {}, hasUpdateRF = {}",
        hasUpdateRA,
        hasUpdatePS,
        hasUpdateRF);
  }
}

void RibDC::cleanupPlatform() noexcept {
  /*
   * Destroy the DC-specific timer on the evb thread. RibBase::stop() has
   * already joined all coroutines (so none can call
   * scheduleRouteAttributePolicyTimer() and dereference the timer) and has not
   * yet terminated the evb loop (so this dispatch can still run).
   */
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait(
      [&]() { routeAttributePolicyTimer_.reset(); });
}

void RibDC::postRouteFilterPolicyReplaced() {
  /*
   * This hook runs only after a real policy update. Count applies here rather
   * than at enqueue time so coalesced and identical refreshes are excluded;
   * the null check also excludes policy clears.
   */
  if (routeFilterPolicy_) {
    BgpStatsDC::incrCrfPolicyAppliedSuccess();
  }

  if (fsdbSyncer_) {
    fsdbSyncer_->setRouteFilterPolicy(
        routeFilterPolicy_ ? std::optional(routeFilterPolicy_->toThrift())
                           : std::nullopt);
  }

  if (ribPolicyLogger_) {
    int64_t psPolicyVersion = getPathSelectionPolicyVersion();
    int64_t rfPolicyVersion = getRouteFilterPolicyVersion();
    ribPolicyLogger_->log(psPolicyVersion, rfPolicyVersion);
  }
}

void RibDC::handleRouteAttributePolicySetMsg(
    const RouteAttributePolicySetMsg& msg) noexcept {
  replaceRouteAttributePolicy(
      std::make_unique<RouteAttributePolicy>(msg.policy));
}

void RibDC::handleRouteAttributePolicyClearMsg() noexcept {
  replaceRouteAttributePolicy(nullptr);
}

void RibDC::handleRouteAttributePolicyTimerMsg() noexcept {
  XLOG(
      INFO,
      "Received RouteAttributePolicyTimerMsg, triggering FIB programming...");
  for (auto& [_, ribEntry] : ribEntries_) {
    ribEntry.requirePathSelection();
  }
  schedulePrepareFibProgrammingTimer();
}

void RibDC::handlePathSelectionPolicySetMsg(
    const PathSelectionPolicySetMsg& msg) noexcept {
  replacePathSelectionPolicy(
      std::make_unique<PathSelectionPolicy>(msg.policy),
      /*isBootstrap=*/false,
      msg.forceUpdate);
}

void RibDC::handlePathSelectionPolicyClearMsg() noexcept {
  replacePathSelectionPolicy(nullptr);
}

/* DC processRibPolicyMsgLoop: handles all policy types including CTE and CPS.
 */
folly::coro::Task<void> RibDC::processRibPolicyMsgLoop() noexcept {
  while (true) {
    co_await folly::coro::co_safe_point;

    auto msg = co_await co_awaitTry(ribPolicyMsgQ_.pop());
    if (!msg.hasValue()) {
      XLOG(
          INFO,
          "[Exit] Coro task cancelled. Terminating processRibPolicyMsgLoop");
      break;
    }

    folly::variant_match(
        *msg,
        [this](const RibPolicyClearMsg& /* req */) {
          handleRibPolicyClearMsg();
        },
        [this](const RouteAttributePolicySetMsg& req) {
          handleRouteAttributePolicySetMsg(req);
        },
        [this](const RouteAttributePolicyClearMsg& /* req */) {
          handleRouteAttributePolicyClearMsg();
        },
        [this](const RouteAttributePolicyTimerMsg& /* req */) {
          handleRouteAttributePolicyTimerMsg();
        },
        [this](const PathSelectionPolicySetMsg& req) {
          handlePathSelectionPolicySetMsg(req);
        },
        [this](const PathSelectionPolicyClearMsg& /* req */) {
          handlePathSelectionPolicyClearMsg();
        },
        [this](const RouteFilterPolicySetMsg& req) {
          handleRouteFilterPolicySetMsg(req);
        },
        [this](const RouteFilterPolicyClearMsg& /* req */) {
          handleRouteFilterPolicyClearMsg();
        });

    /*
     * Yield after each message so no input queue can dominate the RIB thread.
     */
    co_await folly::coro::co_reschedule_on_current_executor;
  }
}

void RibDC::scheduleRouteAttributePolicyTimer() noexcept {
  if (!routeAttributePolicy_) {
    return;
  }

  auto mostRecentActiveExpTime =
      routeAttributePolicy_->getMostRecentActiveExpirationTime();
  if (mostRecentActiveExpTime < INT_MAX) {
    auto now = std::chrono::seconds(std::time(nullptr)).count();
    auto countdown = std::chrono::seconds(mostRecentActiveExpTime - now + 1);
    if (routeAttributePolicyTimer_) {
      routeAttributePolicyTimer_->cancelTimeout();
    }
    routeAttributePolicyTimer_->scheduleTimeout(countdown);
  }
}

RibDC::CacheMigrationResult RibDC::migrateRouteAttributePolicyCache(
    RouteAttributePolicy& oldPolicy,
    RouteAttributePolicy& newPolicy) {
  CacheMigrationResult result;
  const auto& oldStmts = oldPolicy.getStatements();
  const auto& newStmts = newPolicy.getStatements();

  /*
   * Single pass over statements to identify:
   * 1. hasUpdate (any difference in statements)
   * 2. needsReEvaluation (content changes or expiration)
   * 3. statementsWithNewContent and statementsRemoved for cache migration
   */
  folly::F14FastSet<std::string> statementsWithNewContent;
  folly::F14FastSet<std::string> statementsWithNewMatcher;
  folly::F14FastSet<std::string> statementsRemoved;
  bool hasNewStatements = false;
  bool hasReactivatedStatement = false;

  // Check old statements against new (find changed/removed)
  for (const auto& [name, oldStmt] : oldStmts) {
    auto it = newStmts.find(name);
    if (it == newStmts.end()) {
      // Statement removed
      statementsRemoved.insert(name);
      result.hasUpdate = true;
      result.needsReEvaluation = true;
    } else {
      auto reEvalResult = oldStmt.needsReEvaluation(it->second);
      result.hasUpdate |= reEvalResult.changed;
      if (reEvalResult.needsReEval) {
        statementsWithNewContent.insert(name);
        result.needsReEvaluation = true;
        if (reEvalResult.matcherChanged) {
          statementsWithNewMatcher.insert(name);
        }
      }
      /*
       * A statement that was expired (inactive) in the old policy but is active
       * in the new one can now match prefixes that were cached as negative (no
       * match) while it was inactive. Those negative entries must be
       * re-evaluated, exactly like a newly added statement would require.
       */
      if (!oldStmt.isActive() && it->second.isActive()) {
        hasReactivatedStatement = true;
      }
    }
  }

  // Check for new statements added
  if (newStmts.size() + statementsRemoved.size() > oldStmts.size()) {
    hasNewStatements = true;
    result.hasUpdate = true;
    result.needsReEvaluation = true;
  }

  XLOGF(
      INFO,
      "[CTE] Cache migration: classification, oldStatements={}, newStatements={}, statementsRemoved={}, statementsWithNewContent={}, statementsWithNewMatcher={}, hasNewStatements={}, hasReactivatedStatement={}, hasUpdate={}, needsReEvaluation={}",
      oldStmts.size(),
      newStmts.size(),
      statementsRemoved.size(),
      statementsWithNewContent.size(),
      statementsWithNewMatcher.size(),
      hasNewStatements,
      hasReactivatedStatement,
      result.hasUpdate,
      result.needsReEvaluation);

  // If no update at all, old policy keeps its cache and remains active
  if (!result.hasUpdate) {
    XLOGF(
        INFO, "[CTE] Cache migration: policies identical, no migration needed");
    RibStatsDC::STATS_raPolicyCacheMigrationIdentical.add(1);
    return result;
  }

  // If hasUpdate but no re-evaluation needed (expiration-only change)
  if (!result.needsReEvaluation) {
    newPolicy.moveCache(oldPolicy);
    newPolicy.moveIndices(oldPolicy);
    XLOGF(
        INFO,
        "[CTE] Cache migration: only expiration changed, full cache move");
    RibStatsDC::STATS_raPolicyCacheMigrationExpirationOnly.add(1);
    return result;
  }

  // Selective migration: copy unaffected entries, collect affected prefixes
  size_t preserved = 0;

  for (const auto& [prefix, matchResult] : oldPolicy.getCache()) {
    if (matchResult.has_value()) {
      // Positive cache entry - check if matched statement changed/removed
      const auto& stmtName = matchResult->getStatementName();

      if (statementsWithNewContent.contains(stmtName)) {
        result.affectedPrefixes.push_back(prefix);
        if (statementsWithNewMatcher.contains(stmtName)) {
          /*
           * Matcher changed - invalidate cache entry so re-evaluation goes
           * through the cache-miss path which re-checks the matcher.
           */
        } else {
          /*
           * Action-only change - prefix->statement mapping is still valid.
           * Preserve cache entry; re-evaluation will apply the new action.
           */
          newPolicy.setCacheEntry(prefix, matchResult);
          ++preserved;
        }
      } else if (statementsRemoved.contains(stmtName)) {
        /*
         * Statement no longer exists - invalidate cache entry.
         * Prefix may either match a newly added statement (if any), or be
         * purged of its attribute overwrites from the deleted statement.
         */
        result.affectedPrefixes.push_back(prefix);
      } else {
        // Cache entry still valid - copy it
        newPolicy.setCacheEntry(prefix, matchResult);
        ++preserved;
      }
    } else {
      // Negative cache entry (prefix matched no statement)
      if (hasNewStatements || hasReactivatedStatement) {
        /*
         * A new or newly-reactivated statement might match this prefix - need
         * to re-evaluate. Invalidate cache and mark as affected.
         */
        result.affectedPrefixes.push_back(prefix);
      } else {
        // No new/reactivated statements - negative cache is still valid
        newPolicy.setCacheEntry(prefix, std::nullopt);
        ++preserved;
      }
    }
  }

  XLOGF(
      INFO,
      "[CTE] Cache migration: preserved={}, needsReEvaluation={}",
      preserved,
      result.affectedPrefixes.size());

  RibStatsDC::STATS_raPolicyCacheMigrationSelective.add(1);
  RibStatsDC::STATS_raPolicyCachePreserved.add(preserved);
  RibStatsDC::STATS_raPolicyCacheInvalidated.add(
      result.affectedPrefixes.size());

  return result;
}

/* We only replace instead of updating route attribute policy.
   Each time the route attribute policy is replaced, when there is delta and
   not in read-only mode, trigger fib programming. */
bool RibDC::replaceRouteAttributePolicy(
    std::unique_ptr<RouteAttributePolicy> newPolicy) {
  RibStatsDC::STATS_raPolicyRcvd.add(1);

  bool hasUpdate = false;
  bool needsReEvaluation = false;
  std::vector<folly::CIDRNetwork> prefixesNeedingReEvaluation;

  XLOGF(
      INFO,
      "[CTE] replaceRouteAttributePolicy: hasExistingPolicy={}, newPolicyPresent={}, newNumStatements={}, newMostRecentExpirationTimeS={}",
      routeAttributePolicy_ != nullptr,
      newPolicy != nullptr,
      newPolicy ? newPolicy->getStatements().size() : 0,
      newPolicy ? formatMostRecentExpirationTimeS(*newPolicy) : "N/A");

  if (routeAttributePolicy_) {
    if (!newPolicy) {
      // Policy cleared - need full re-evaluation
      XLOGF(
          INFO,
          "[CTE] replaceRouteAttributePolicy: existing policy cleared (newPolicy null) -> full re-evaluation");
      hasUpdate = true;
      needsReEvaluation = true;
    } else {
      // Both old and new policy exist - single-pass comparison and migration
      auto migrationStart = std::chrono::steady_clock::now();
      auto migrationResult =
          migrateRouteAttributePolicyCache(*routeAttributePolicy_, *newPolicy);
      RibStatsDC::STATS_raPolicyCacheMigrationTimeMs.addValue(
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - migrationStart)
              .count());
      hasUpdate = migrationResult.hasUpdate;
      needsReEvaluation = migrationResult.needsReEvaluation;
      prefixesNeedingReEvaluation = std::move(migrationResult.affectedPrefixes);
    }
  } else {
    // No existing policy - hasUpdate if newPolicy is not nullptr
    hasUpdate = (newPolicy != nullptr);
    needsReEvaluation = hasUpdate;
    XLOGF(
        INFO,
        "[CTE] replaceRouteAttributePolicy: no existing policy, hasUpdate={}, needsReEvaluation={}",
        hasUpdate,
        needsReEvaluation);
  }

  XLOGF(
      INFO,
      "[CTE] replaceRouteAttributePolicy decision: hasUpdate={}, needsReEvaluation={}, affectedPrefixes={}, ribEoRReceived={}",
      hasUpdate,
      needsReEvaluation,
      prefixesNeedingReEvaluation.size(),
      ribEoRReceived_);

  /* We should replace routeAttributePolicy_ not only when its content changes
     but also when its expiration time changes. */
  if (hasUpdate) {
    XLOG(DBG1, "[CTE] Updating RouteAttributePolicy.");

    routeAttributePolicy_ = std::move(newPolicy);

    // Save upon receipt of RouteAttributePolicy
    saveRibPolicyState();

    if (fsdbSyncer_) {
      fsdbSyncer_->setRouteAttributePolicy(
          routeAttributePolicy_
              ? std::optional(routeAttributePolicy_->toThrift())
              : std::nullopt);
    }
    scheduleRouteAttributePolicyTimer();
    RibStatsDC::STATS_raPolicyUpdate.add(1);
  }

  /*
   * Only trigger a prepareFibProgramming with the following conditions
   * fulfilled:
   *  1. needsReEvaluation: True => policy update beyond refreshing;
   *  2. ribEoRReceived: True => already fulfilled the initial FULL_SYNC;
   *
   * Note: in case ribEoRReceived: False, the FULL_SYNC will be automatically
   * triggered after receiving the start signal inside
   * `processRibInInitialPathComputation`.
   */
  if (needsReEvaluation && ribEoRReceived_) {
    XLOG(INFO)
        << "[CTE] needsReEvaluation and ribEoRReceived, triggering path selection";

    /*
     * Selective re-evaluation: instead of iterating ALL ribEntries_, only
     * process collected prefixes. This is O(affected prefixes) instead of
     * O(all ribEntries_).
     */
    if (!prefixesNeedingReEvaluation.empty()) {
      // Selective re-evaluation: only affected prefixes
      for (const auto& prefix : prefixesNeedingReEvaluation) {
        auto ribIt = ribEntries_.find(prefix);
        if (ribIt != ribEntries_.end()) {
          ribIt->second.requirePathSelection();
        }
      }
      XLOGF(
          INFO,
          "[CTE] Selective re-evaluation: {} prefixes",
          prefixesNeedingReEvaluation.size());
      RibStatsDC::STATS_raPolicyReEvalPrefixes.add(
          prefixesNeedingReEvaluation.size());
    } else {
      /*
       * Fallback: full re-evaluation when affectedPrefixes is empty.
       * This can happen when a statement is removed/changed but no cached
       * prefix matched it (e.g., cache not fully populated, BGP just
       * restarted, or policy was cleared).
       */
      for (auto& [_, ribEntry] : ribEntries_) {
        ribEntry.requirePathSelection();
      }
      XLOGF(
          INFO,
          "[CTE] Full re-evaluation fallback: {} prefixes, "
          "affectedPrefixes empty, cache may not have been fully populated",
          ribEntries_.size());
      RibStatsDC::STATS_raPolicyReEvalPrefixes.add(ribEntries_.size());
    }

    schedulePrepareFibProgrammingTimer();
  } else if (needsReEvaluation && !ribEoRReceived_) {
    XLOGF(
        INFO,
        "[CTE] replaceRouteAttributePolicy: needsReEvaluation but ribEoR not yet received; deferring path selection to initial FULL_SYNC");
  }

  return hasUpdate;
}

namespace {
/*
 * Decide whether newPolicy should replace the currently cached path selection
 * policy. Extracted from replacePathSelectionPolicy for readability.
 *   - current == nullptr : update iff a new policy is provided
 *   - newPolicy == nullptr : always update (clearing the policy)
 *   - identical content : no update
 *   - forceUpdate : update, bypassing the version check (used by CPS FILE_MODE)
 *   - otherwise : update iff newPolicy version >= current version
 */
bool hasPathSelectionPolicyChange(
    PathSelectionPolicy* current,
    PathSelectionPolicy* newPolicy,
    bool forceUpdate) {
  if (current == nullptr) {
    return newPolicy != nullptr;
  }
  if (newPolicy == nullptr) {
    return true;
  }
  if (*current == *newPolicy) {
    return false;
  }
  if (forceUpdate) {
    return true;
  }
  return current->getVersion() <= newPolicy->getVersion();
}
} // namespace

/* We only replace instead of updating path selection policy.
   Each time the path selection policy is replaced, we also need to save
   path selection policy to disk. After that, when there is delta and not
   in read-only mode, trigger fib programming. */
bool RibDC::replacePathSelectionPolicy(
    std::unique_ptr<PathSelectionPolicy> newPolicy,
    bool isBootstrap,
    bool forceUpdate) {
  RibStatsDC::STATS_psPolicyRcvd.add(1);

  bool hasUpdate = hasPathSelectionPolicyChange(
      pathSelectionPolicy_.get(), newPolicy.get(), forceUpdate);

  /*
   * Count the case where forceUpdate actually bypassed the version gate: a
   * content change whose version regressed, which would NOT have updated
   * without forceUpdate. Kept out of hasPathSelectionPolicyChange() so that
   * helper stays a pure predicate.
   */
  if (forceUpdate && pathSelectionPolicy_ && newPolicy &&
      *pathSelectionPolicy_ != *newPolicy &&
      pathSelectionPolicy_->getVersion() > newPolicy->getVersion()) {
    BgpStatsDC::incrCpsForceUpdateBypass();
  }

  if (hasUpdate) {
    XLOG(DBG1, "[CPS] Updating PathSelectionPolicy.");

    pathSelectionPolicy_ = std::move(newPolicy);

    /*
     * Count a real CPS policy application here — where hasUpdate is known —
     * rather than at enqueue time in the Thrift handler: the coalescing queue
     * can drop superseded refreshes and an identical policy yields no update,
     * so an enqueue is not an apply. Excludes clears (null policy). Covers
     * bootstrap, the file-mode RPC, and Thrift-mode sets alike.
     */
    if (pathSelectionPolicy_) {
      BgpStatsDC::incrCpsPolicyAppliedSuccess();
    }

    // Only log and append to history for real policy updates, not bootstrap
    if (!isBootstrap) {
      // Save upon receipt of PathSelectionPolicy
      saveRibPolicyState();
      XLOGF(
          INFO,
          "[CPS] PathSelectionPolicy version: {}",
          getPathSelectionPolicyVersion());

      appendRibPolicyChangeHistory("CPS", getPathSelectionPolicyVersion());
    }

    if (fsdbSyncer_) {
      fsdbSyncer_->setPathSelectionPolicy(
          pathSelectionPolicy_ ? std::optional(pathSelectionPolicy_->toThrift())
                               : std::nullopt);
    }
    RibStatsDC::STATS_psPolicyUpdate.add(1);

    if (ribPolicyLogger_) {
      int64_t psPolicyVersion = getPathSelectionPolicyVersion();
      int64_t rfPolicyVersion = getRouteFilterPolicyVersion();
      ribPolicyLogger_->log(psPolicyVersion, rfPolicyVersion);
    }
  }

  /*
   * Only trigger a FULL_SYNC with the following conditions fulfilled:
   *  1. hasUpdate: True => policy update;
   *  2. ribEoRReceived: True => already fulfilled the initial FULL_SYNC;
   *
   * Note: in case ribEoRReceived: False, the FULL_SYNC will be automatically
   * triggered after receiving the start signal inside
   * `processRibInInitialPathComputation`.
   */
  if (hasUpdate && ribEoRReceived_) {
    XLOG(INFO, "[CPS] hasUpdate and ribEoRReceived, triggering FULL_SYNC");
    /*
     * recompute all the paths, similar to fullSync but will send out the
     * update announcement to the peers
     */
    for (auto& [prefix, ribEntry] : ribEntries_) {
      ribEntry.requirePathSelection();
    }
    schedulePrepareFibProgrammingTimer();
  }

  return hasUpdate;
}

/*
 * Apply RouteAttributePolicy to overwrite route attributes of computed RIB
 * entries.
 */
void RibDC::overwriteRouteAttributes(
    const folly::F14FastSet<folly::CIDRNetwork>& prefixes) {
  auto overwriteStartTime = std::chrono::steady_clock::now();

  // trigger rib policy calculation
  RouteAttributePolicy::RibChange ribChange;

  /*
   * Apply rib policy lbw to rib entries:
   *  - If no rib policy statement matched a ribEntry (including ribPolicy not
   *    available or matching statement is expired), we reset the rib policy
   *    ucmp weight (if available);
   *  - Otherwise, we let rib policy overwrite the route attributes
   */
  for (const auto& prefix : prefixes) {
    auto ribIt = ribEntries_.find(prefix);
    XCHECK(ribIt != ribEntries_.end());
    auto& ribEntry = ribIt->second;
    auto startTime = std::chrono::steady_clock::now();

    bool matched = false;
    if (routeAttributePolicy_) {
      matched =
          routeAttributePolicy_->overwriteRouteAttributes(ribEntry, ribChange);
    }
    if (!matched) {
      // if rib has non-empty lbw, reset it to 0 and add rib to update list
      if (ribEntry.getRibPolicyUcmpWeight().has_value()) {
        ribEntry.setRibPolicyUcmpWeight(0);
        ribChange.updatedRoutes.emplace(prefix);
      }
    }

    RibStatsDC::STATS_ribRouteAttributeOverwriteTimeMs.addValue(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime)
            .count());
  }

  // update fib batch list
  for (const auto& prefix : ribChange.updatedRoutes) {
    auto ribIt = ribEntries_.find(prefix);
    XCHECK(ribIt != ribEntries_.end());
    auto& ribEntry = ribIt->second;
    if (!ribEntry.isOnFibBatchList()) {
      fibBatchList_.push_back(ribEntry);
    }
  }

  if (prefixes.size() == ribEntries_.size()) {
    auto routeAttributeOverwriteTimeMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - overwriteStartTime);

    XLOGF(
        INFO,
        "Route attribute overwrite in a FullRibWalk for {} ribEntries took {} ms",
        ribEntries_.size(),
        routeAttributeOverwriteTimeMs.count());
    RibStatsDC::STATS_ribFullSyncRouteAttributeOverwriteTimeMs.addValue(
        routeAttributeOverwriteTimeMs.count());
  }

  enqueueCanonicalRibUpdatesAfterCompute(prefixes);
}

std::pair<bool, bool> RibDC::runBestPathSelection(RibEntry& entry) noexcept {
  /*
   * Capture before selectBestPath() — it is the sole producer of
   * isPartialDrain_ and may flip it on the entry. Recording the transition
   * here (rather than in RibBase::prepareFibProgramming) keeps all
   * partial-drain bookkeeping inside RibDC so RibBase stays platform-agnostic.
   */
  const bool oldIsPartialDrain = entry.getIsPartialDrain();
  /*
   * Capture the winner's source class (a small value, not the owning
   * shared_ptr) before selection so the delta covers every bestpath write
   * inside selectBestPath (DC has additional set-to-null paths) without
   * shared_ptr refcount traffic on the hot path.
   *
   * prePathSelectionFiltering(), called inside selectBestPath(), recomputes
   * and overwrites entry.inactivePathCount_. Preserve the previous cached
   * value here so recordInactivePathDelta() can compute newCount - oldCount
   * after selection returns.
   */
  const auto oldSource = bestpathSource(entry.getBestPathRaw());
  const auto oldInactivePathCount = entry.getInactivePathCnt();

  auto result = RibDC::selectBestPath(
      entry,
      multipathSelector_,
      bestpathSelector_,
      globalConfig_.computeUcmpFromLbwComm,
      globalConfig_.ucmpWidth,
      std::optional<BgpUcmpQuantizer>(globalConfig_.ucmpQuantizer),
      pathSelectionPolicy_);
  recordBestpathSourceDelta(
      entry.getPrefix(), oldSource, entry.getBestPathRaw());
  recordInactivePathDelta(entry.getPrefix(), oldInactivePathCount, entry);

  /*
   * Defer the partial-drain summary until this path-selection pass completes.
   */
  if (recordPartialDrainTransition(
          oldIsPartialDrain, entry.getIsPartialDrain())) {
    partialDrainEnqueuePending_ = true;
  }

  return result;
}

bool RibDC::shouldReadvertiseBestpathOnMultipathSizeChange(
    const RibEntry& entry) const noexcept {
  /*
   * In both UCMP and GAR, a multipath size change will most likely require
   * best-path re-advertisement because capacity or topology metadata has
   * changed. UCMP already relies on aggregate link bandwidth as an indicator,
   * while multipath size is a more deterministic and reliable signal for both.
   */
  const auto* bestpath = entry.getBestPathRaw();
  return bestpath && bestpath->attrs->hasNonTransitiveLbwExtCommunity();
}

void RibDC::onPrepareFibProgrammingComplete(bool fullSync) noexcept {
  /*
   * The initial pending value publishes the state computed by the first
   * path-selection pass instead of leaving the subtree absent. It is false and
   * empty only when that pass finds no partially drained prefixes. Queue it
   * before start() so the computed state is present in the first /bgp snapshot.
   */
  if (partialDrainEnqueuePending_ && enqueuePartialDrainState()) {
    partialDrainEnqueuePending_ = false;
  }

  if (fullSync && fsdbSyncer_ && !fsdbSyncerStarted_) {
    /*
     * start() blocks this RIB EventBase turn. A connection may notify the
     * snapshot consumer concurrently, but that consumer cannot run on this
     * EventBase until the flag below is set and this callback returns.
     */
    XLOG(INFO, "Starting FsdbSyncer after initial full RIB computation");
    fsdbSyncer_->start();
    fsdbSyncerStarted_ = true;
  }
}

void RibDC::enqueueCanonicalRibUpdatesAfterCompute(
    const folly::F14FastSet<folly::CIDRNetwork>& processedPrefixes) noexcept {
  if (!canonicalRibUpdateQueue_ || !fsdbSyncerStarted_) {
    return;
  }

  if (canonicalSnapshotPending_) {
    enqueueCanonicalRibFullSnapshot("fsdb_connect");
    return;
  }

  for (const auto& prefix : processedPrefixes) {
    CanonicalRibPrefixUpdate update;
    update.prefix = prefix;
    auto entryIt = ribEntries_.find(prefix);
    if (entryIt != ribEntries_.end()) {
      auto entryInput = buildCanonicalRibEntryInput(entryIt->second);
      if (entryInput) {
        update.entry = std::move(*entryInput);
      }
    }
    canonicalRibUpdateQueue_->pushPrefixUpdate(std::move(update));
  }
}

/*
 * DC-only path selection orchestrator. Same 7-phase pipeline as the
 * RibBase native orchestrator, except the multipath phase consults
 * the Centralized Path Selection (CPS) policy when one is passed in.
 */
std::pair<bool, bool> RibDC::selectBestPath(
    RibEntry& entry,
    const std::unique_ptr<RouteInfoSelector>& multipathSelector,
    const std::unique_ptr<RouteInfoSelector>& bestpathSelector,
    bool computeUcmp,
    uint32_t ucmpWidth,
    const std::optional<BgpUcmpQuantizer>& quantizer,
    const std::unique_ptr<PathSelectionPolicy>& pathSelectionPolicy) noexcept {
  const auto input = snapshotAndResetForPathSelection(entry);
  /*
   * Partial-drain state is DC-only — RibBase's snapshot/orchestrator is
   * intentionally CPS-free. Capture and reset it here so selectBestPath() is
   * the authoritative producer of isPartialDrain_: it is recomputed below from
   * the policy outcome, and leaving the previous value would persist drain
   * state across passes when the outcome no longer warrants it.
   */
  const auto oldIsPartialDrain = entry.isPartialDrain_;
  entry.isPartialDrain_ = false;
  entry.mnhThreshold_ = 0;
  entry.aggLbwBpsThreshold_ = 0;
  auto routes = prePathSelectionFiltering(entry);

  if (routes.empty()) {
    entry.bestpath_ = nullptr;
    entry.multipaths_ = {};
    entry.installToFib_ = true;
    entry.weightedNexthops_ = nullptr;
    return std::make_pair(
        (entry.bestpath_ != input.oldBestpath) ||
            (entry.isPartialDrain_ != oldIsPartialDrain),
        entry.weightedNexthops_ != input.oldMultipathWeightedNexthops);
  }

  /*
   * CPS-aware multipath selection. When a policy is installed it may
   * override the multipath set or reject the prefix entirely. An empty
   * selectedPaths after CPS is a legitimate rejection signal — we
   * clear bestpath/multipaths and early-return like the no-routes
   * fast-path above.
   */
  MultiPathSelectionResult mp;
  bool failedCpsNativeCriteria = false;
  if (pathSelectionPolicy) {
    mp.selectedPaths = pathSelectionPolicy->overrideMultipathSelection(
        entry, routes, multipathSelector);
    if (mp.selectedPaths.empty()) {
      entry.bestpath_ = nullptr;
      entry.multipaths_ = {};
      entry.installToFib_ = true;
      entry.weightedNexthops_ = nullptr;
      return std::make_pair(
          (entry.bestpath_ != input.oldBestpath) ||
              (entry.isPartialDrain_ != oldIsPartialDrain),
          entry.weightedNexthops_ != input.oldMultipathWeightedNexthops);
    }
    for (const auto& path : mp.selectedPaths) {
      entry.getOrAssignSendPathId(path);
    }
    const auto& policyResult =
        pathSelectionPolicy->getPathSelectionPolicyResult(entry.getPrefix());
    if (policyResult && policyResult->isCapacityThresholdViolation()) {
      if (policyResult->drainOnMinCapacityThresholdViolation) {
        /*
         * MNH/LBW capacity threshold violated but
         * drain_on_min_nexthop_violation is set: retain the bestpath and keep
         * FIB warm instead of withdrawing the prefix. The drain community is
         * attached downstream in AdjRibOut (MNH 3/N).
         */
        entry.isPartialDrain_ = true;
        entry.mnhThreshold_ = policyResult->mnhThreshold;
        entry.aggLbwBpsThreshold_ = policyResult->aggLbwBpsThreshold;
        XLOGF(
            DBG2,
            "Partial drain engaged for prefix {}",
            folly::IPAddress::networkToString(entry.getPrefix()));
      } else {
        entry.bestpath_ = nullptr;
        entry.installToFib_ = true;
        failedCpsNativeCriteria = true;
      }
    }
  } else {
    mp = multiPathSelection(entry, routes, multipathSelector);
  }

  accumulateAggregateWeightsAndTopoInfo(
      entry, mp, input.oldNexthopAndTopoInfo, quantizer);

  if (!failedCpsNativeCriteria) {
    bestPathSelection(entry, mp.selectedPaths, bestpathSelector);
  }

  auto newNhWtMap = buildAndNormalizeWeightedNexthops(
      entry, mp.selectedPaths, computeUcmp, ucmpWidth, mp.lbwMultiplier);

  auto changePair = computeChangePair(
      entry, input, mp.topoInfoChanged, std::move(newNhWtMap));
  /*
   * Fold the drain transition into bestpathChanged so the RibOut announcement
   * machinery re-advertises on drain rollout/rollback even when the bestpath
   * pointer itself is unchanged.
   */
  changePair.first =
      changePair.first || (entry.isPartialDrain_ != oldIsPartialDrain);
  return changePair;
}

/*
 * DC-only CPS thrift surfaces. Moved verbatim from RibBase as part of
 * the DC-vs-BB split so the BB binary's RibBase translation unit has
 * zero references to PathSelectionPolicy.
 */
neteng::fboss::bgp::thrift::TResult RibDC::setPathSelectionPolicy(
    std::unique_ptr<rib_policy::TPathSelectionPolicy> policy,
    bool forceUpdate) {
  neteng::fboss::bgp::thrift::TResult result;
  try {
    PathSelectionPolicy psPolicy{*policy};
  } catch (const BgpError& ex) {
    auto errorMsg = folly::exceptionStr(ex);
    XLOGF(ERR, "{}", errorMsg);
    result.success() = false;
    result.err() = errorMsg;
    return result;
  }

  // push rib policy set message to policy queue
  enqueueRibPolicyMsg(
      PathSelectionPolicySetMsg{std::move(*policy), forceUpdate});
  result.success() = true;
  return result;
}

rib_policy::TPathSelectionPolicy RibDC::getPathSelectionPolicy() {
  rib_policy::TPathSelectionPolicy result;
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    if (pathSelectionPolicy_ != nullptr) {
      result = pathSelectionPolicy_->toThrift();
    }
  });
  return result;
}

int64_t RibDC::getPathSelectionPolicyVersion() const {
  return pathSelectionPolicy_ ? pathSelectionPolicy_->getVersion() : -1;
}

void RibDC::clearPathSelectionPolicy() {
  // push clear message to policy queue
  enqueueRibPolicyMsg(PathSelectionPolicyClearMsg{});
}

std::vector<rib_policy::TPathSelector> RibDC::getActivePathSelectionCriteria(
    std::unique_ptr<std::vector<std::string>> prefixes) {
  std::vector<rib_policy::TPathSelector> result;
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    if (pathSelectionPolicy_) {
      result = pathSelectionPolicy_->getActivePathSelectionCriteria(*prefixes);
    }
  });
  return result;
}

/*
 * DC-only override. Calls the base implementation (which fills the
 * non-CPS sub-policies) and then layers in path_selection_policy on
 * its own evb hop.
 */
rib_policy::TRibPolicy RibDC::getRibPolicy() {
  auto result = RibBase::getRibPolicy();
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait([&]() {
    if (pathSelectionPolicy_) {
      result.path_selection_policy() = pathSelectionPolicy_->toThrift();
    }
  });
  return result;
}

bool RibDC::computeFailedCpsNativeCriteria(
    const facebook::bgp::RibEntry& ribEntry) const {
  if (!pathSelectionPolicy_) {
    return false;
  }
  auto psPolicyRes =
      pathSelectionPolicy_->getPathSelectionPolicyResult(ribEntry.getPrefix());
  return psPolicyRes && psPolicyRes->isFailedCpsNativeCriteria();
}

std::optional<::facebook::bgp::rib_policy::TPathSelector>
RibDC::getCanonicalActiveCpsCriteria(
    const facebook::bgp::RibEntry& ribEntry) const {
  if (!pathSelectionPolicy_) {
    return std::nullopt;
  }
  std::vector<std::string> prefixVector = {
      folly::IPAddress::networkToString(ribEntry.getPrefix())};
  auto activeCriteriaVector =
      pathSelectionPolicy_->getActivePathSelectionCriteria(prefixVector);
  /*
   * A single-prefix query yields exactly one criteria; any other count means
   * the policy has no unambiguous answer for this prefix, so report none
   * rather than aborting this RPC-serving path.
   */
  if (activeCriteriaVector.size() != 1) {
    return std::nullopt;
  }
  const auto& activeCriteria = activeCriteriaVector.at(0);
  if (activeCriteria != rib_policy::TPathSelector()) {
    return activeCriteria;
  }
  return std::nullopt;
}

bool RibDC::canonicalFailedCpsNativeCriteria(
    const facebook::bgp::RibEntry& ribEntry) const {
  return computeFailedCpsNativeCriteria(ribEntry);
}

/*
 * DC-only override. Duplicates the base body (rather than calling it)
 * because the CPS-rejection bit gates the per-path loop's tBestPaths-
 * vs-tDefaultPaths branching — there's no way to layer that in after
 * the loop runs.
 */
std::optional<neteng::fboss::bgp::thrift::TRibEntry>
RibDC::createTRibEntryWithFilter(
    const std::pair<const folly::CIDRNetwork, facebook::bgp::RibEntry>& entry,
    const std::function<bool(const RouteInfo&)>& pathFilter) {
  TRibEntry tRibEntry;
  tRibEntry.prefix() = buildTPrefix(entry.first);

  const auto& ribEntry = entry.second;

  /*
   * CPS-specific: populate active_cps_criteria.
   */
  if (pathSelectionPolicy_) {
    std::vector<std::string> prefixVector = {
        folly::IPAddress::networkToString(ribEntry.getPrefix())};
    auto activeCriteriaVector =
        pathSelectionPolicy_->getActivePathSelectionCriteria(prefixVector);
    XCHECK_EQ(activeCriteriaVector.size(), 1);
    const auto& activeCriteria = activeCriteriaVector.at(0);
    if (activeCriteria != rib_policy::TPathSelector()) {
      tRibEntry.active_cps_criteria() = activeCriteria;
    }
  }
  /*
   * If failedCpsNativeCriteria is true, no path is "best path", unless
   * drain_on_min_nexthop_violation is true, in which case we retain the best
   * path and advertise it with the drain community attached.
   */
  const bool failedCpsNativeCriteria = computeFailedCpsNativeCriteria(ribEntry);

  // CTE — same as RibBase's version.
  if (routeAttributePolicy_) {
    auto activeCteUcmpAction =
        routeAttributePolicy_->getActiveCteUcmpAction(ribEntry.getPrefix());
    if (activeCteUcmpAction) {
      tRibEntry.active_cte_ucmp_action() = std::move(*activeCteUcmpAction);
    }
  }

  const auto& bestpath = ribEntry.getBestPath();
  if (bestpath) {
    auto bestNexthop = bestpath->attrs->getNexthop();
    tRibEntry.best_next_hop() = createTIpPrefix(bestNexthop);
  }

  std::vector<TBgpPath> tDefaultPaths{};
  std::vector<TBgpPath> tBestPaths{};
  const auto& routeinfos = ribEntry.getAllPaths();
  const auto& multipath_routeinfos = ribEntry.getMultipaths();
  auto weightedNexthops = ribEntry.getMultipathWeightedNexthops();
  for (const auto& routeinfo : routeinfos) {
    if (!pathFilter(*routeinfo)) {
      continue;
    }
    auto tPath = toTBgpPath(routeinfo, weightedNexthops);
    if (routeinfo->pathIdToSend.has_value() &&
        multipath_routeinfos.contains(routeinfo->pathIdToSend.value()) &&
        !failedCpsNativeCriteria) {
      if (bestpath && routeinfo == bestpath) {
        tPath.is_best_path() = true;
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

  if (ribEntry.needPathSelection()) {
    tRibEntry.path_selection_pending() = true;
  }
  tRibEntry.rib_version() = ribEntry.getRibVersion();

  return tRibEntry;
}

/*
 * DC-only override. Same shape as the base saveRibPolicyState but
 * also serializes path_selection_policy when present. We can't call
 * the base + amend because saveTRibPolicyStore writes the file in
 * one shot, so we re-implement the small body with CPS included.
 */
void RibDC::saveRibPolicyState() noexcept {
  if (!pathSelectionPolicy_ && !routeAttributePolicy_ && !routeFilterPolicy_) {
    XLOG(INFO, "rib policy empty, remove previously saved rib policy if any");
    removeExistingRibPolicyStore();
    return;
  }
  XLOG(INFO, "rib policy file save start");

  TRibPolicyStore tRibPolicyStore;
  tRibPolicyStore.storedTime() =
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  tRibPolicyStore.fileTermination() = kRibPolicyFileTermination;

  if (pathSelectionPolicy_) {
    tRibPolicyStore.policy()->path_selection_policy() =
        pathSelectionPolicy_->toThrift();
  }
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

  saveTRibPolicyStore(tRibPolicyStore);
}

bool RibDC::isDevicePartiallyDrained() const {
  return drainedPrefixCount_ > 0;
}

TPartialDrainStatus RibDC::getPartialDrainStatus() const {
  TPartialDrainStatus status;
  status.is_partially_drained() = isDevicePartiallyDrained();
  status.num_affected_prefixes() = static_cast<int32_t>(
      std::min(drainedPrefixCount_, static_cast<int64_t>(INT32_MAX)));
  status.partial_drain_transition_count() = partialDrainTransitionCount_;
  return status;
}

/*
 * Current aggregate link bandwidth (bps) across a prefix's multipaths.
 * Mirrors the aggregation in PathSelector::overrideMultipathSelection
 * (RibPolicy.cpp): sum of each path's non-transitive LBW (bytes/sec) * 8 —
 * the same value that was compared against the configured threshold when the
 * drain engaged. Computed on demand here (no per-RibEntry storage, mirroring
 * the on-demand path_count) since the publish path is gated on a drain
 * transition rather than run every FIB pass.
 */
static int64_t currentAggLbwBps(const RibEntry& ribEntry) {
  int64_t aggLbwBps = 0;
  for (const auto& [_, routeInfo] : ribEntry.getMultipaths()) {
    const auto lbwBytesPerSecond = routeInfo->attrs->getNonTransitiveLbw();
    if (lbwBytesPerSecond) {
      aggLbwBps += static_cast<int64_t>(lbwBytesPerSecond->second * 8);
    }
  }
  return aggLbwBps;
}

TPartiallyDrainedPrefix RibDC::buildPartialDrainPrefixEntry(
    const folly::CIDRNetwork& prefix,
    const RibEntry& ribEntry) const {
  TPartiallyDrainedPrefix entry;
  entry.prefix() = createTIpPrefix(prefix);
  const auto pathCount = static_cast<int32_t>(ribEntry.getMultipaths().size());
  /*
   * min_capacity (the violated threshold) and current_capacity (the current
   * observed value) both use the TCapacity union, on the same criterion arm as
   * the drain trigger. These are the canonical fields. The deprecated fields
   * (path_count, mnh_threshold, min_capacity_threshold) are left unset — they
   * have no readers (the publish and consume sides land together in this
   * stack) and are retained in the schema only for compatibility.
   */
  TCapacity current, threshold;
  if (ribEntry.getMnhThreshold() != 0) {
    threshold.next_hop_count() = ribEntry.getMnhThreshold();
    // For the current value, the next_hop_count arm holds the live count.
    current.next_hop_count() = pathCount;
  } else if (ribEntry.getAggLbwBpsThreshold() != 0) {
    threshold.agg_lbw_bps() = ribEntry.getAggLbwBpsThreshold();
    current.agg_lbw_bps() = currentAggLbwBps(ribEntry);
  }
  entry.min_capacity() = std::move(threshold);
  entry.current_capacity() = std::move(current);
  return entry;
}

std::vector<TPartiallyDrainedPrefix> RibDC::getPartiallyDrainedPrefixes()
    const {
  std::vector<TPartiallyDrainedPrefix> result;
  result.reserve(
      static_cast<size_t>(std::max<int64_t>(0, drainedPrefixCount_)));
  for (const auto& [prefix, ribEntry] : ribEntries_) {
    if (!ribEntry.getIsPartialDrain()) {
      continue;
    }
    result.emplace_back(buildPartialDrainPrefixEntry(prefix, ribEntry));
  }
  return result;
}

TPartialDrainState RibDC::getPartialDrainState() const {
  TPartialDrainState state;
  state.partial_drain_state() = getPartialDrainStatus();
  state.drained_prefixes() = getPartiallyDrainedPrefixes();
  return state;
}

bool RibDC::recordPartialDrainTransition(
    bool oldIsPartialDrain,
    bool newIsPartialDrain) {
  if (oldIsPartialDrain == newIsPartialDrain) {
    return false;
  }

  const auto prevCount = drainedPrefixCount_;

  if (newIsPartialDrain) {
    ++drainedPrefixCount_;
  } else {
    if (drainedPrefixCount_ <= 0) {
      /*
       * Under correct bookkeeping, every false->true transition increments
       * drainedPrefixCount_, so any true->false transition is expected to
       * find a positive count. Reaching here means a caller passed a stale
       * oldIsPartialDrain or a prior increment was missed at startup.
       * Skip the decrement to keep the counter non-negative — a negative
       * int64_t silently converts to a huge size_t at the reserve() call
       * in getPartiallyDrainedPrefixes() (clamped there as a backstop).
       */
      XLOGF(
          ERR,
          "drainedPrefixCount_ underflow guard: count={}, oldIsPartialDrain={}, newIsPartialDrain={}",
          drainedPrefixCount_,
          oldIsPartialDrain,
          newIsPartialDrain);
      return false;
    }
    --drainedPrefixCount_;
  }

  /* Edge trigger: count crossed the zero boundary in either direction. */
  if (prevCount == 0 || drainedPrefixCount_ == 0) {
    ++partialDrainTransitionCount_;
    /*
     * Surface the flip on ODS: reflect the new device-level drain state in the
     * gauge, so a true<->false flip is observable and alertable independent of
     * the FSDB publish path.
     */
    RibStatsDC::setIsPartialDrain(newIsPartialDrain);
  }
  return true;
}

} // namespace facebook::bgp
