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
#include <folly/fibers/Baton.h>
#include <folly/fibers/FiberManagerMap.h>
#include <folly/futures/Future.h>
#include <folly/logging/xlog.h>
#include <re2/set.h>
#include "fboss/agent/Constants.h"

#include <openr/common/NetworkUtil.h>
#include <openr/if/gen-cpp2/Platform_types_custom_protocol.h>
#include <thrift/lib/cpp2/protocol/DebugProtocol.h>
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/common/ThriftClientUtils.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/Utils.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NetlinkWrapper.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopStatus.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBB.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

DEFINE_int32(
    bgp_netlink_link_up_hold_initial_ms,
    200,
    "The length of the first link-up hold, in milliseconds. This applies only "
    "when BgpSettingConfig.enable_netlink_dampening is true.");

DEFINE_int32(
    bgp_netlink_link_up_hold_max_ms,
    1000,
    "The maximum length of a link-up hold, in milliseconds. A link that stays "
    "quiet for this time returns to the first hold length.");

namespace facebook::bgp {

namespace {

NexthopStatus makeNexthopStatus(
    const NexthopKey& nexthop,
    bool isReachable,
    std::optional<uint32_t> igpCost,
    std::optional<std::string> ifName = std::nullopt) {
  return NexthopStatus(
      nexthop,
      isReachable,
      igpCost,
      /*isConnected*/ true,
      /*excludeNexthopWithoutCost*/ true,
      std::move(ifName));
}

openr::thrift::ConnectedNextHopStatus makeConnectedNextHopStatus(
    const folly::IPAddress& remoteAddress,
    const std::string& interfaceName,
    bool isReachable) {
  openr::thrift::ConnectedNextHopStatus thriftNextHopStatus;
  thriftNextHopStatus.remoteAddress() = openr::toBinaryString(remoteAddress);
  thriftNextHopStatus.interfaceName() = interfaceName;
  thriftNextHopStatus.isReachable() = isReachable;
  return thriftNextHopStatus;
}

/*
 * Append one published status per seeded host IP to both batches. The start-up
 * sync and the per-event publish build the same batch, so they share this.
 *
 * The now parameter has a default value, so a caller that does not use the
 * hold does not have to supply it.
 */
void appendPublishedStatuses(
    const InterfaceEntry& interface,
    std::vector<NexthopStatus>& vec,
    std::vector<openr::thrift::ConnectedNextHopStatus>& thriftStatuses,
    std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::now()) {
  const auto ifName = interface.getIfName();
  const auto append = [&](const folly::IPAddress& ip, bool reachable) {
    vec.emplace_back(makeNexthopStatus(
        NexthopKey{.ip = ip, .nextHopVrfId = fboss::kDefaultVrf},
        reachable,
        kDirectlyConnectedNexthopWeight));

    thriftStatuses.push_back(makeConnectedNextHopStatus(ip, ifName, reachable));

    XLOGF(
        DBG3,
        "DEBUG: ConnectedNexthopStatus: {}",
        apache::thrift::debugString(thriftStatuses.back()));
  };
  interface.forEachPublishedReachability(append, now);
}

} // namespace

NetlinkWrapper::NetlinkWrapper(
    std::shared_ptr<NexthopCache> nexthopCache,
    nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ,
    openr::messaging::ReplicateQueue<openr::fbnl::NetlinkEvent>&
        netlinkEventsQueue,
    const std::vector<std::string>& includeInterfaceRegexes,
    const EnableNetlinkDampening enableNetlinkDampening,
    std::optional<std::chrono::milliseconds> syncInterval,
    const int32_t openrFibAgentPort)
    : BgpModuleBase(kModuleNetlinkWrapper),
      fm_(folly::fibers::getFiberManager(evb_)),
      nexthopCache_(nexthopCache),
      ribInQ_(ribInQ),
      netlinkEventsRQueue_(netlinkEventsQueue.getReader(kModuleNetlinkWrapper)),
      nlSock_(
          std::make_shared<openr::fbnl::NetlinkProtocolSocket>(
              &evb_,
              netlinkEventsQueue)),
      enableNetlinkDampening_(
          enableNetlinkDampening &&
          !FLAGS_bgp_resolve_nexthops_from_interface_state),
      includeInterfaceRegexSet_(compileRegexSet(includeInterfaceRegexes)),
      syncReadTimeout_(syncInterval ? *syncInterval : kNetlinkSyncReadTimeout),
      openrFibAgentPort_(openrFibAgentPort),
      pendingFibOpenrUpdates_(kFibOpenrMaxPendingUpdates) {
  /*
   * The hold has no effect on the interface-state path. ipReachabilityMap_ is
   * always empty there, and a release publishes nothing. The feature is off
   * on that path, and bgpd starts.
   */
  if (enableNetlinkDampening &&
      FLAGS_bgp_resolve_nexthops_from_interface_state) {
    XLOG(
        ERR,
        "enable_netlink_dampening is ignored, because "
        "bgp_resolve_nexthops_from_interface_state is set. The hold applies "
        "only on the legacy neighbor-state publish path.");
  }

  fb303::ThreadCachedServiceData::get()->setCounter(
      BgpStatsBB::kNetlinkDampeningEnabled, enableNetlinkDampening_ ? 1 : 0);

  if (FLAGS_bgp_resolve_nexthops_from_interface_state) {
    /*
     * Drive the pull (RIB-registration) resolution path: when the RIB registers
     * a nexthop that has no answer yet, evaluate it against interface link
     * state on our own fiber. The hook runs on the RIB thread, so it only
     * schedules work here via addTaskRemote (thread-safe) and never touches our
     * state directly. Set once here, before any thread registers nexthops, so
     * the cache needs no synchronization for the hook pointer; we never clear
     * it because our EventBase outlives the RIB (see MainBB shutdown order),
     * and a post-stop addTaskRemote is a harmless no-op.
     */
    nexthopCache_->setOnNexthopRegistered([this](NexthopKey nexthop) {
      if (nexthop.nextHopVrfId != fboss::kDefaultVrf) {
        return;
      }
      fm_.addTaskRemote([this, ip = nexthop.ip]() { evaluateNexthop(ip); });
    });
  }
}

void NetlinkWrapper::run() noexcept {
  XLOG(INFO, "Start NetlinkWrapper event-base loop");

  // Connect to OpenR FIB agent
  connectOpenrFibAgent();

  {
    /*
     * NOTE: We wan to maintain the order for Netlink Messages so that the final
     * state is correct.
     */

    auto fiber = fm_.addTaskFuture([this]() mutable noexcept {
      XLOG(DBG1, "Starting sync interfaces task");
      // Start sync interfaces task
      syncInterfaceTask();

      XLOG(DBG1, "Starting netlink event queue task");

      // Start netlink event queue processing
      while (true) {
        auto maybeEvent = netlinkEventsRQueue_.get();
        if (maybeEvent.hasError()) {
          break;
        }

        /*
         * Release each hold that ended. holdReleaseTimer_ does this too. This
         * second path makes sure that a hold ends, if the timer does not run.
         * The cost is one atomic load when no interface has a hold.
         */
        if (enableNetlinkDampening_ &&
            interfaceHoldCount_.load(std::memory_order_relaxed) > 0) {
          releaseEndedHolds();
        }

        folly::variant_match(
            maybeEvent.value(),
            [this](openr::fbnl::Link& link) {
              XLOGF(DBG3, "Received link event: {}", link.str());
              processLinkEvent(std::move(link));
            },
            [this](openr::fbnl::IfAddress& addr) {
              XLOGF(DBG3, "Received address event: {}", addr.str());
              processIfAddressEvent(std::move(addr));
            },
            [this](openr::fbnl::Neighbor& neighbor) {
              XLOGF(DBG3, "Received neighbor event: {}", neighbor.str());
              processNeighborEvent(std::move(neighbor));
            },
            [](openr::fbnl::Rule& rule) {
              XLOGF(DBG3, "Received rule event: {}", rule.str());
            });
      }
      XLOG(INFO, "[Exit] Netlink event processing fiber finished");
    });
    workers_.emplace_back(std::move(fiber));
  }

  // processLinkEvent starts this timer when a link-up starts a hold.
  if (enableNetlinkDampening_) {
    holdReleaseTimer_ = folly::AsyncTimeout::make(evb_, [this]() noexcept {
      fm_.addTask([this]() noexcept { releaseAndReschedule(); });
    });
  }

  // Start fiber to process OpenR FIB agent updates
  {
    auto fiber = fm_.addTaskFuture([this]() mutable noexcept {
      XLOG(DBG1, "Starting OpenR update queue processor fiber");

      while (true) {
        /*
         * Blocks this fiber (not the thread) if queue is empty
         * Returns nullopt when queue is closed
         */
        auto maybeRequest = pendingFibOpenrUpdates_.get();
        if (!maybeRequest) {
          XLOG(INFO, "Queue closed, OpenR update processor fiber exiting");
          break;
        }

        XLOGF(
            DBG2,
            "Processing OpenR update, {} remaining in queue (approx)",
            pendingFibOpenrUpdates_.size());

        // Send update to OpenR - blocks this fiber (not the thread)
        updateOpenrConnectedNextHopStatus(std::move(*maybeRequest));
      }

      XLOG(INFO, "OpenR update queue processor fiber exiting");
    });
    workers_.emplace_back(std::move(fiber));
  }

  evb_.loopForever();
  XLOG(INFO, "[Exit] Successfully terminated NetlinkWrapper event-base");
}

void NetlinkWrapper::stop() noexcept {
  XLOG(INFO, "[Exit] Stopping NetlinkWrapper");

  // Cancel the sync interface task
  syncInterfaceTaskCanceler_.requestCancellation();

  /*
   * Stop the timer before the close below, so that no new release task starts.
   * The timer object stays alive, because releaseAndReschedule reads
   * holdReleaseTimer_ with no guard until the join below.
   */
  evb_.runInEventBaseThreadAndWait([this]() noexcept {
    if (holdReleaseTimer_) {
      holdReleaseTimer_->cancelTimeout();
    }
  });

  /*
   * Close the queue to signal the OpenR update processing fiber to stop
   * This will cause get() to return nullopt, allowing the fiber to exit cleanly
   */
  pendingFibOpenrUpdates_.close();

  disconnectOpenrFibAgent();

  // Cancel and join the async scope (for heartbeat loop) - blocking call
  folly::coro::blockingWait(asyncScope_.cancelAndJoinAsync());
  XLOG(INFO, "[Exit] All coroutine tasks finished.");

  // Wait for all fiber tasks to complete
  folly::collectAll(workers_.begin(), workers_.end()).get();
  XLOG(INFO, "[Exit] All fiber tasks finished.");

  // Terminate the event base loop
  evb_.terminateLoopSoon();
  XLOG(INFO, "[Exit] All NetlinkWrapper tasks finished");
}

void NetlinkWrapper::updateEntryStats(
    neteng::fboss::bgp::thrift::TEntryStats& stats) const noexcept {
  stats.total_netlink_wrapper_interfaces() = interfaces_.size();
  /*
   * The thrift worker and the health thread call this function. They do not
   * run on evb_. Read the atomic. Do not walk interfaces_ here.
   */
  stats.total_netlink_wrapper_holds_active() =
      interfaceHoldCount_.load(std::memory_order_relaxed);
}

void NetlinkWrapper::syncInterfaceTask() {
  std::chrono::milliseconds sleepInterval{syncReadTimeout_};
  auto retry = 0;
  while (retry < kNetlinkSyncRetries) {
    // Check for cancellation before sleeping
    if (syncInterfaceTaskCanceler_.isCancellationRequested()) {
      XLOG(INFO, "Sync interface task cancelled");
      return;
    }

    // Sleep before each attempt (fiber-safe)
    nettools::bgplib::fiberSleepFor(sleepInterval);

    auto success = syncInterfaces();
    if (success) {
      XLOG(INFO, "Successfully synced interfaces");
      return;
    } else {
      XLOGF(
          WARN,
          "Failed to sync interfaces, retrying in {}ms (attempt {}/{})",
          sleepInterval.count(),
          ++retry,
          kNetlinkSyncRetries);
    }
  }
}

bool NetlinkWrapper::syncInterfaces() {
  folly::F14NodeMap<int, InterfaceEntry> interfaces;

  try {
    // getAllInterfaces blocks the fiber (not the thread) while waiting
    interfaces = getAllInterfaces();
  } catch (const std::exception& e) {
    XLOGF(
        ERR,
        "Failed to sync interfaces. Exception: {}",
        folly::exceptionStr(e));
    return false;
  }

  if (interfaces.empty()) {
    XLOG(ERR, "No interfaces found.");
    return false;
  }

  XLOGF(
      INFO,
      "Successfully retrieved {} interfaces from netlink.",
      interfaces.size());

  if (FLAGS_bgp_resolve_nexthops_from_interface_state) {
    /*
     * Interface-state mode: a directly-connected nexthop's reachability is
     * driven purely by interface link state. Record the interface snapshot,
     * then seed the cache by re-evaluating the RIB-registered nexthops covered
     * by each interface's prefixes. No host-IP enumeration and no OpenR FIB
     * agent feed on this path.
     */
    for (auto& [ifIndex, interface] : interfaces) {
      ifIndexToName_[ifIndex] = interface.getIfName();
      interfaces_.insert_or_assign(interface.getIfName(), interface);
    }
    for (const auto& [ifIndex, interface] : interfaces) {
      for (const auto& prefix : interface.getPrefixes()) {
        for (const auto& nexthop : nexthopCache_->getRegisteredNexthopsInSubnet(
                 NexthopKey{
                     .ip = prefix.first, .nextHopVrfId = fboss::kDefaultVrf},
                 prefix.second)) {
          evaluateNexthop(nexthop.ip);
        }
      }
    }
    return true;
  }

  /*
   * Legacy path: seed the cache from the host IPs enumerated in each interface
   * prefix and feed the OpenR FIB agent.
   */
  std::vector<NexthopStatus> vec;
  openr::thrift::ConnectedNextHopStatusRequest request;
  std::vector<openr::thrift::ConnectedNextHopStatus> thriftNextHopStatuses;

  for (const auto& interface : interfaces) {
    auto ifName = interface.second.getIfName();
    ifIndexToName_[interface.first] = ifName;

    appendPublishedStatuses(interface.second, vec, thriftNextHopStatuses);

    interfaces_.insert_or_assign(ifName, interface.second);
  }
  updateCacheAndNotifyRib(vec);

  request.nextHopStatuses() = thriftNextHopStatuses;
  enqueueConnectedNextHopStatus(std::move(request));

  return true;
}

folly::F14NodeMap<int, InterfaceEntry> NetlinkWrapper::getAllInterfaces() {
  folly::F14NodeMap<int, InterfaceEntry> result;

  XLOG(DBG2, "Fetching all links...");
  /*
   * via(&evb_).get() blocks the fiber (not the thread) while EventBase
   * processes the request
   */
  auto links = nlSock_->getAllLinks().via(&evb_).get();
  if (links.hasError()) {
    throw openr::fbnl::NlException("failed fetching links", links.error());
  }
  for (auto& link : links.value()) {
    // skipping interfaces that don't match regex
    if (!isIfNameRegexMatch(link.getLinkName())) {
      continue;
    }
    auto ifEntry = InterfaceEntry{link.getLinkName()};
    ifEntry.updateIfIndex(link.getIfIndex());
    ifEntry.setUp(link.isUp());
    result.emplace(link.getIfIndex(), std::move(ifEntry));
  }

  XLOG(DBG2, "Fetching all interface addresses...");
  auto addrs = nlSock_->getAllIfAddresses().via(&evb_).get();
  if (addrs.hasError()) {
    throw openr::fbnl::NlException(
        "failed fetching interface addresses", addrs.error());
  }
  if (FLAGS_bgp_resolve_nexthops_from_interface_state) {
    /*
     * Interface-state mode: rebuild the global directly-connected prefix table
     * (and each interface's reverse index) from this full snapshot.
     * Reachability is driven by interface link state, not address enumeration,
     * and the kernel neighbor (ARP/ND) table is not consulted -- so neighbors
     * are not fetched.
     */
    prefixes_.clear();
    for (auto& addr : addrs.value()) {
      auto value = folly::get_ptr(result, addr.getIfIndex());
      auto prefix = addr.getPrefix();
      if (!value || !prefix || !addr.isValid()) {
        continue;
      }
      prefixes_.addPrefix(prefix.value(), addr.getIfIndex());
      value->addPrefix(prefix.value());
    }
    return result;
  }

  /*
   * Legacy path: seed each interface's trackable host IPs from its addresses,
   * then resolve their reachability from the kernel neighbor (ARP/ND) table.
   */
  for (auto& addr : addrs.value()) {
    auto value = folly::get_ptr(result, addr.getIfIndex());
    auto prefix = addr.getPrefix();
    if (!value || !prefix) {
      continue;
    }
    value->updateAddr(prefix.value(), addr.isValid());
  }

  XLOG(DBG2, "Fetching all neighbors...");
  auto nbrs = nlSock_->getAllNeighbors().via(&evb_).get();
  if (nbrs.hasError()) {
    throw openr::fbnl::NlException(
        "failed fetching reachability information", nbrs.error());
  }
  for (auto& nbr : nbrs.value()) {
    if (auto value = folly::get_ptr(result, nbr.getIfIndex())) {
      value->updateReachability(nbr.getDestination(), nbr.isReachable());
    }
  }
  return result;
}

bool NetlinkWrapper::isIfNameRegexMatch(std::string const& ifName) const {
  return includeInterfaceRegexSet_->Match(ifName, nullptr);
}

std::shared_ptr<re2::RE2::Set> NetlinkWrapper::compileRegexSet(
    const std::vector<std::string>& includeInterfaceRegexes) {
  re2::RE2::Options options;
  options.set_case_sensitive(false);
  auto regexSet =
      std::make_shared<re2::RE2::Set>(options, re2::RE2::ANCHOR_BOTH);

  std::string regexErr;
  for (const auto& regexStr : includeInterfaceRegexes) {
    if (regexSet->Add(regexStr, &regexErr) == -1) {
      throw std::invalid_argument(
          fmt::format(
              "Failed to add regex: {}. Error: {}", regexStr, regexErr));
    }
  }
  CHECK(regexSet->Compile()) << "Regex compilation failed";
  return regexSet;
}

void NetlinkWrapper::processLinkEvent(openr::fbnl::Link&& link) {
  auto& ifName = link.getLinkName();
  auto ifIndex = link.getIfIndex();
  auto isUp = link.isUp();
  /*
   * For directly connected next hops, we only care about port channel
   * interfaces. All other interfaces we will ignore
   */
  if (!isIfNameRegexMatch(ifName)) {
    return;
  }

  ifIndexToName_[ifIndex] = ifName;
  auto& interfaceEntry = getOrCreateInterfaceEntry(ifName);
  interfaceEntry.updateIfIndex(ifIndex);

  if (FLAGS_bgp_resolve_nexthops_from_interface_state) {
    /*
     * Interface-state mode: link state is the source of truth for
     * directly-connected reachability. On a state change, re-evaluate the
     * RIB-registered nexthops covered by this interface's prefixes: link-up
     * makes a covered nexthop reachable, link-down makes it unreachable
     * (hidden) unless another covering interface is still up. No ARP/ND.
     */
    if (interfaceEntry.setUp(isUp)) {
      for (const auto& prefix : interfaceEntry.getPrefixes()) {
        for (const auto& nexthop : nexthopCache_->getRegisteredNexthopsInSubnet(
                 NexthopKey{
                     .ip = prefix.first, .nextHopVrfId = fboss::kDefaultVrf},
                 prefix.second)) {
          evaluateNexthop(nexthop.ip);
        }
      }
    }
    return;
  }

  const auto now = std::chrono::steady_clock::now();

  /*
   * A link-down reaches bgpd as one or two kinds of netlink event. Which ones
   * arrive depends on what the kernel does to the neighbor (ARP/ND) entries:
   *
   * | Case                      | Kernel deletes    | bgpd gets       |
   * |---------------------------|-------------------|-----------------|
   * | 1. admin-down             | every entry       | link + neighbor |
   * | 2. carrier-down           | all but permanent | link + neighbor |
   * | 3. carrier-down, debounce | nothing           | link only       |
   * |    or permanent ARP/ND    |                   |                 |
   *
   * Link::isUp() reads IFF_RUNNING, so bgpd sees all three as a link-down.
   * The kernel sends a link event in every case. It also sends one neighbor
   * event for each entry it deletes. In case 3 it deletes nothing, so the
   * link event is the only event.
   *
   * Case 1: an operator clears IFF_UP. The kernel calls neigh_ifdown, which
   *   deletes every entry, NUD_PERMANENT included.
   * Case 2: the port loses carrier, and IFF_RUNNING clears. The kernel calls
   *   neigh_carrier_down, which deletes every entry except NUD_PERMANENT.
   * Case 3: IFF_RUNNING clears the same way, but the kernel deletes no entry.
   *   Either the carrier returns before the kernel acts, or every entry on
   *   the port is NUD_PERMANENT.
   *
   * In case 1 and case 2 the two kinds of event arrive in either order, and
   * both orders give one withdrawal:
   * 1. Link event first: this handler clears the link half and publishes the
   *    withdrawal. The neighbor event then clears the neighbor half and
   *    calculates the same value, so NexthopCache sends nothing.
   * 2. Neighbor event first: it clears the neighbor half and publishes the
   *    withdrawal. This handler then clears the link half, but
   *    hasReachableNeighbor() is false, so it does not publish.
   * The link-up alone cannot make the nexthop reachable again, because the
   * kernel deleted the neighbor half. A new neighbor event does that.
   *
   * Case 3 is why this handler writes the link half only, and leaves the
   * neighbor half as it is. The publish ANDs the neighbor half with
   * canUseLink (link state and hold), so the link-down still withdraws. On
   * carrier-up the link event sets the link state, and the AND is true again
   * from the neighbor half bgpd kept, once the hold ends. Before this change
   * a link-down wrote false into the neighbor half. No neighbor event
   * followed the carrier-up to put it back, so the nexthop stayed
   * unreachable.
   */
  const bool linkChanged = interfaceEntry.setUp(isUp);
  if (!linkChanged) {
    XLOGF(
        DBG2,
        "processLinkEvent for {} no-op, isUp unchanged ({})",
        ifName,
        isUp);
    return;
  }

  if (enableNetlinkDampening_) {
    if (!isUp) {
      /*
       * A link-down has no hold. The withdrawal reaches the RIB at once, and
       * recordLinkDown sets the hold length that the next link-up serves.
       */
      const auto times = getLinkUpHoldTimes();
      interfaceEntry.recordLinkDown(now, times.initial, times.max);
    } else {
      if (interfaceEntry.startLinkUpHold(now)) {
        /*
         * The link returned inside the window that runs from lastDownTime_ for
         * the current hold length. Publish nothing now -- holdReleaseTimer_
         * fires at the hold end time and releaseEndedHolds publishes then.
         */
        XLOGF(
            INFO,
            "[LinkHold] interface {} in hold, remaining {}ms",
            ifName,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                *interfaceEntry.getHoldEndTime() - now)
                .count());
        RibStatsBB::incrNhtLinkHoldStarted();
        // Re-arm, so an earlier hold does not wait behind a later one.
        releaseAndReschedule();
        return;
      }
      // The link-up served its window, so the publish below sends it at once.
    }
  }

  /*
   * A link event never changes reachableNeighborCount_. On a link-down the
   * count is still positive, so this call runs and publishes the withdrawal,
   * because canUseLink now reads false. The count reaches 0 only after the
   * kernel deletes the entries, and processNeighborEvent publishes the
   * withdrawal in that case, so this gate drops a duplicate.
   */
  if (interfaceEntry.hasReachableNeighbor()) {
    addOrUpdateNextHopStatus(interfaceEntry, now);
  }
}

void NetlinkWrapper::readNeighborsForInterface(InterfaceEntry& interfaceEntry) {
  const int ifIndex = interfaceEntry.getIfIndex();

  auto nbrs = nlSock_->getAllNeighbors().via(&evb_).get();
  if (nbrs.hasError()) {
    XLOGF(
        ERR,
        "Neighbor read for {} failed: {}",
        interfaceEntry.getIfName(),
        nbrs.error());
    return;
  }

  bool isUpdated = false;
  for (const auto& nbr : nbrs.value()) {
    if (nbr.getIfIndex() != ifIndex) {
      continue;
    }
    /*
     * A reachable neighbor means the link is up. processNeighborEvent applies
     * the same rule to a neighbor event.
     */
    if (nbr.isReachable()) {
      isUpdated |= interfaceEntry.setUp(true);
    }
    isUpdated |= interfaceEntry.updateReachability(
        nbr.getDestination(), nbr.isReachable());
  }

  XLOGF(
      DBG1,
      "Neighbor read for {} (ifIndex {}): {} kernel entries, changed={}",
      interfaceEntry.getIfName(),
      ifIndex,
      nbrs.value().size(),
      isUpdated);
}

void NetlinkWrapper::processIfAddressEvent(openr::fbnl::IfAddress&& addr) {
  auto ifIndex = addr.getIfIndex();
  auto prefix = addr.getPrefix();
  auto isValid = addr.isValid();

  auto it = ifIndexToName_.find(ifIndex);
  if (it == ifIndexToName_.end()) {
    XLOGF(DBG1, "Address event for untracked iface index: {}", ifIndex);
    return;
  }
  if (!prefix) {
    return;
  }

  if (!FLAGS_bgp_resolve_nexthops_from_interface_state) {
    /*
     * Legacy: enumerate the host IPs of the prefix into the per-interface
     * reachability map and push the change.
     */
    auto& interfaceEntry = getOrCreateInterfaceEntry(it->second);
    if (interfaceEntry.updateAddr(prefix.value(), isValid)) {
      if (isValid) {
        /*
         * updateAddr added host IPs that carry no neighbor state. Get that
         * state from the kernel before the publish, so the update carries one
         * value for each IP.
         */
        readNeighborsForInterface(interfaceEntry);
      }
      addOrUpdateNextHopStatus(interfaceEntry);
    }
    return;
  }

  /*
   * Interface-state mode: maintain the global directly-connected prefix table
   * and this interface's reverse index. Reachability is driven by link state
   * and is not touched by address events. Only a coverage change (a prefix node
   * created or destroyed in the global table) warrants re-evaluation -- a
   * same-subnet sibling add/remove on another interface does not.
   */
  auto& interfaceEntry = getOrCreateInterfaceEntry(it->second);
  bool coverageChanged;
  if (isValid) {
    interfaceEntry.addPrefix(prefix.value());
    coverageChanged = prefixes_.addPrefix(prefix.value(), ifIndex);
  } else {
    interfaceEntry.removePrefix(prefix.value());
    coverageChanged = prefixes_.removePrefix(prefix.value());
  }
  if (!coverageChanged) {
    return;
  }

  /*
   * Coverage changed: re-evaluate the registered nexthops that fall within this
   * prefix, since their directly-connected classification may have changed.
   */
  for (const auto& nexthop : nexthopCache_->getRegisteredNexthopsInSubnet(
           NexthopKey{
               .ip = prefix.value().first, .nextHopVrfId = fboss::kDefaultVrf},
           prefix.value().second)) {
    if (isValid) {
      /*
       * Prefix added: the nexthop may now be directly connected.
       * evaluateNexthop pushes its current reachability if so, or leaves it to
       * the FIB path.
       */
      evaluateNexthop(nexthop.ip);
    } else if (!evaluateNexthop(nexthop.ip)) {
      /*
       * Prefix removed and the nexthop is no longer directly connected on any
       * interface (no sibling prefix covers it). Relinquish connected ownership
       * so the recursive/FIB path can take over, and notify the RIB.
       */
      auto cleared = nexthopCache_->clearConnectedStatus(nexthop);
      if (cleared.has_value()) {
        ribInQ_.fiberPush(RibInNexthopUpdate({*cleared}));
      }
    }
  }
}

void NetlinkWrapper::processNeighborEvent(openr::fbnl::Neighbor&& nbr) {
  if (FLAGS_bgp_resolve_nexthops_from_interface_state) {
    /*
     * Interface-state mode ignores the kernel neighbor (ARP/ND) table entirely;
     * a directly-connected nexthop's reachability is driven by interface link
     * state, so neighbor events are dropped.
     */
    return;
  }

  auto ifIndex = nbr.getIfIndex();
  auto destination = nbr.getDestination();
  auto reachable = nbr.isReachable();

  auto it = ifIndexToName_.find(ifIndex);
  if (it == ifIndexToName_.end()) {
    XLOGF(DBG1, "Neighbor event for untracked iface index: {}", ifIndex);
    return;
  }

  bool isUpdated = false;
  auto& interfaceEntry = getOrCreateInterfaceEntry(it->second);
  /*
   * Netlink: a reachable neighbor event does NOT arrive when a link is down.
   * The entries are gone on an admin-down or carrier-down, and on a debounce
   * they are untouched so nothing fires.
   *
   * bgpd: a resolved neighbor means packets are getting through, so the link
   * is up. This is a cautionary assignment: netlink multicast can drop an
   * RTM_NEWLINK under buffer pressure, and syncInterfaces runs only at startup.
   */
  if (reachable) {
    isUpdated |= interfaceEntry.setUp(true);
  }
  isUpdated |= interfaceEntry.updateReachability(destination, reachable);

  if (isUpdated) {
    addOrUpdateNextHopStatus(interfaceEntry);
  }
}

void NetlinkWrapper::updateCacheAndNotifyRib(
    const std::vector<NexthopStatus>& updates) {
  auto statuses = nexthopCache_->addOrUpdateNextHopStatus(updates);
  if (!statuses.empty()) {
    ribInQ_.fiberPush(RibInNexthopUpdate(std::move(statuses)));
  }
}

void NetlinkWrapper::addOrUpdateNextHopStatus(
    const InterfaceEntry& interface,
    std::chrono::steady_clock::time_point now) {
  std::vector<NexthopStatus> vec;
  std::vector<openr::thrift::ConnectedNextHopStatus> thriftNextHopStatuses;

  appendPublishedStatuses(interface, vec, thriftNextHopStatuses, now);

  std::lock_guard<folly::fibers::TimedMutex> g(publishLock_);
  publishStatusesWithFiberContext(
      std::move(vec), std::move(thriftNextHopStatuses));
}

void NetlinkWrapper::publishStatusesWithFiberContext(
    std::vector<NexthopStatus>&& vec,
    std::vector<openr::thrift::ConnectedNextHopStatus>&& thriftStatuses) {
  if (vec.empty()) {
    // No host IPs tracked on this interface -- nothing to publish.
    return;
  }

  /*
   * Update the local cache, and send the statuses that changed to the RIB.
   * Can suspend due to enqueuing into a bounded queue.
   */
  updateCacheAndNotifyRib(vec);

  openr::thrift::ConnectedNextHopStatusRequest request;
  request.nextHopStatuses() = std::move(thriftStatuses);
  /*
   * Update the OpenR FIB agent. Can suspend due to enqueuing into a bounded
   * queue.
   */
  enqueueConnectedNextHopStatus(std::move(request));
}

void NetlinkWrapper::setLinkUpHoldTimes(
    std::chrono::milliseconds initial,
    std::chrono::milliseconds max) noexcept {
  *holdTimes_.wlock() = HoldTimes{initial, max};
  XLOGF(
      INFO,
      "[LinkHold] hold times set to initial {}ms, max {}ms",
      initial.count(),
      max.count());
}

NetlinkWrapper::HoldTimes NetlinkWrapper::getLinkUpHoldTimes() const noexcept {
  return *holdTimes_.rlock();
}

NetlinkWrapper::LinkHoldState NetlinkWrapper::getLinkHoldState(
    std::chrono::steady_clock::time_point now) const {
  LinkHoldState state;
  for (const auto& [ifName, iface] : interfaces_) {
    const auto endTime = iface.getHoldEndTime();
    if (!endTime.has_value()) {
      continue;
    }
    ++state.heldCount;
    if (now >= *endTime) {
      state.ifNamesToRelease.push_back(ifName);
    } else if (!state.nextEndTime || *endTime < *state.nextEndTime) {
      state.nextEndTime = *endTime;
    }
  }
  return state;
}

std::optional<std::chrono::steady_clock::time_point>
NetlinkWrapper::releaseEndedHolds() {
  std::lock_guard<folly::fibers::TimedMutex> g(publishLock_);
  const auto now = std::chrono::steady_clock::now();

  const auto state = getLinkHoldState(now);
  interfaceHoldCount_.store(state.heldCount, std::memory_order_relaxed);
  RibStatsBB::setNhtLinkHoldActive(state.heldCount);
  if (!state.ifNamesToRelease.empty()) {
    std::vector<NexthopStatus> vec;
    std::vector<openr::thrift::ConnectedNextHopStatus> thriftStatuses;
    for (const auto& ifName : state.ifNamesToRelease) {
      auto it = interfaces_.find(ifName);
      if (it == interfaces_.end()) {
        continue;
      }
      it->second.forEachPublishedReachability(
          [&](const folly::IPAddress& ip, bool reachable) {
            vec.emplace_back(makeNexthopStatus(
                NexthopKey{.ip = ip, .nextHopVrfId = fboss::kDefaultVrf},
                reachable,
                kDirectlyConnectedNexthopWeight));
            thriftStatuses.push_back(
                makeConnectedNextHopStatus(ip, ifName, reachable));
          },
          now);
    }

    publishStatusesWithFiberContext(std::move(vec), std::move(thriftStatuses));

    // Release the link holds after the publish to OpenR FIB agent is complete.
    for (const auto& ifName : state.ifNamesToRelease) {
      if (auto it = interfaces_.find(ifName); it != interfaces_.end()) {
        it->second.removeHold();
        XLOGF(INFO, "[LinkHold] {} hold released", ifName);
        RibStatsBB::incrNhtLinkHoldReleased();
      }
    }
    const auto stillHeld =
        state.heldCount - static_cast<int64_t>(state.ifNamesToRelease.size());
    interfaceHoldCount_.store(stillHeld, std::memory_order_relaxed);
    RibStatsBB::setNhtLinkHoldActive(stillHeld);
  }
  return state.nextEndTime;
}

void NetlinkWrapper::releaseAndReschedule() noexcept {
  const auto nextEndTime = releaseEndedHolds();
  if (!nextEndTime) {
    return;
  }
  holdReleaseTimer_->scheduleTimeout(
      std::max(
          std::chrono::ceil<std::chrono::milliseconds>(
              *nextEndTime - std::chrono::steady_clock::now()),
          std::chrono::milliseconds(0)));
}

bool NetlinkWrapper::evaluateNexthop(const folly::IPAddress& nexthopIp) {
  /*
   * Classify the nexthop as directly connected via a global longest-prefix
   * (best) match over all interface prefixes. If no interface prefix covers
   * it, it is not directly connected -- leave it to the recursive/FIB path,
   * untouched. Otherwise its reachability is driven purely by interface link
   * state: reachable iff a covering interface is up. A directly-connected
   * nexthop whose covering interfaces are all down is pushed
   * unreachable-but-connected, which hides the route until a covering
   * interface comes back up.
   */
  auto ifIndex = prefixes_.coveringIfIndex(nexthopIp);
  if (!ifIndex.has_value()) {
    XLOGF(
        DBG2,
        "evaluateNexthop: {} is not directly connected on any tracked "
        "interface; leaving it to the recursive/FIB path",
        nexthopIp.str());
    return false;
  }

  /*
   * Record the covering interface name on the status. The fib agent needs
   * the name when it puts the route into an EOS nexthop-group. This function
   * is the only place that knows the interface.
   */
  std::optional<std::string> ifName = ifNameForIndex(*ifIndex);
  bool reachable = ifName.has_value() && isInterfaceUp(*ifName);
  XLOGF(
      DBG2,
      "evaluateNexthop: {} is directly connected via {}, reachable: {}",
      nexthopIp.str(),
      ifName.value_or("<unknown ifIndex>"),
      reachable);
  updateCacheAndNotifyRib({makeNexthopStatus(
      NexthopKey{.ip = nexthopIp, .nextHopVrfId = fboss::kDefaultVrf},
      reachable,
      kDirectlyConnectedNexthopWeight,
      ifName)});
  return true;
}

std::optional<std::string> NetlinkWrapper::ifNameForIndex(int ifIndex) const {
  auto nameIt = ifIndexToName_.find(ifIndex);
  if (nameIt == ifIndexToName_.end()) {
    return std::nullopt;
  }
  return nameIt->second;
}

bool NetlinkWrapper::isInterfaceUp(const std::string& ifName) const {
  auto ifaceIt = interfaces_.find(ifName);
  return ifaceIt != interfaces_.end() && ifaceIt->second.isUp();
}

InterfaceEntry& NetlinkWrapper::getOrCreateInterfaceEntry(
    const std::string& ifName) {
  auto it = interfaces_.find(ifName);
  if (it != interfaces_.end()) {
    return it->second;
  }
  return interfaces_.emplace(ifName, InterfaceEntry{ifName}).first->second;
}

void NetlinkWrapper::enqueueConnectedNextHopStatus(
    openr::thrift::ConnectedNextHopStatusRequest&& request) {
  /*
   * Blocks this fiber (not the thread) if there is no space in the queue
   * Other fibers on this thread can continue running
   */
  pendingFibOpenrUpdates_.put(std::move(request));

  XLOGF(
      DBG3,
      "Enqueued FibOpenr update, queue size: {}",
      pendingFibOpenrUpdates_.size());
}

void NetlinkWrapper::connectOpenrFibAgent() {
  client_ =
      createThriftClient<apache::thrift::Client<openr::thrift::FibService>>(
          evb_,
          kLoopBackAddressV6,
          openrFibAgentPort_,
          kFibOpenrConnTimeout,
          kFibOpenrSendTimeout,
          kFibOpenrRecvTimeout);
}

void NetlinkWrapper::disconnectOpenrFibAgent() {
  XLOG(INFO, "Disconnecting OpenR FIB agent");

  // Thrift client must be destroyed on the EventBase thread
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait(
      [this]() { client_.reset(); });
}

void NetlinkWrapper::updateOpenrConnectedNextHopStatus(
    const openr::thrift::ConnectedNextHopStatusRequest& request) {
  try {
    if (!client_) {
      XLOG(WARN, "Client not connected, connecting to OpenR FIB agent");
      connectOpenrFibAgent();
    }

    XLOG(DBG2, "Updating OpenR FIB agent with connected nexthop status");

    // Other fibers on this thread can continue running while we wait
    client_->semifuture_updateConnectedNextHopStatus(request).via(&evb_).get();

    XLOG(DBG3, "Successfully updated OpenR FIB agent");

  } catch (const openr::thrift::PlatformError& ex) {
    XLOGF(WARNING, "OpenR FIB agent platform error: {}", *ex.message());
  } catch (const std::exception& ex) {
    XLOGF(ERR, "Failed to update OpenR FIB agent: {}", ex.what());
    // Disconnect agent and reconnect on error
    disconnectOpenrFibAgent();
  }
}

} // namespace facebook::bgp
