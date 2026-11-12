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

/**
 * E2ETestFixture.h
 *
 * Test fixture for E2E testing of RIB and PeerManagerBase.
 * Tests complete BGP flows without mocking core components.
 */

#pragma once

#define PeerManager_TEST_FRIENDS friend class E2ETestFixture;
#define AdjRib_TEST_FRIENDS friend class E2ETestFixture;
#define RibBase_TEST_FRIENDS friend class E2ETestFixture;
#define AdjRibOutGroup_TEST_FRIENDS friend class E2ETestFixture;

#include <gtest/gtest.h>

#include <set>

#include <folly/container/F14Set.h>
#include <folly/coro/BlockingWait.h>
#include <folly/logging/xlog.h>
#include <folly/testing/TestUtil.h>

#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/config/Config.h"
#include "neteng/fboss/bgp/cpp/config/ConfigManager.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopStatus.h"
#include "neteng/fboss/bgp/cpp/peer/PeerManagerBase.h"
#include "neteng/fboss/bgp/cpp/tests/MockSessionManager.h"
#include "neteng/fboss/bgp/cpp/tests/PeerManagerTestUtils.h"
#include "neteng/fboss/bgp/cpp/tests/PolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestUtils.h"
#include "neteng/fboss/bgp/cpp/watchdog/MonitoredQueue.h"
#include "neteng/fboss/bgp/cpp/watchdog/Watchdog.h"
#include "neteng/fboss/bgp/if/gen-cpp2/BgpStructs_types.h"

namespace facebook {
namespace bgp {

class FsdbSyncer;

using nettools::bgplib::BgpEndOfRib;
using nettools::bgplib::BgpPeerDisplayInfo;
using nettools::bgplib::BgpSessionState;
using nettools::bgplib::BgpUpdate2;
using nettools::bgplib::VersionNumber;

/*
 * Specification for creating a BGP peer configuration
 */
struct BgpPeerSpec {
  uint32_t asn;
  folly::IPAddress localAddr;
  folly::IPAddress peerAddr;
  folly::IPAddress v4Nexthop;
  folly::IPAddress v6Nexthop;
  std::string description;
  bool allowAsIn = true;
  std::string peerType = kPeerTypeCsw;
  bool disableIpv4Afi = false;
  bool disableIpv6Afi = false;
  /*
   * Whether the peer advertised the MP-EXT capability. Default true. Set false
   * to model a capability-less peer (RFC 1771 / IxANVL strict mode): the
   * session still negotiates v4-unicast (per negotiateCapabilities), but the
   * remote (pre-negotiation) MP-EXT bit is false, which drives
   * legacyV4NlriEncoding in the update-group key.
   */
  bool mpExtCapable = true;
  std::optional<int32_t> outDelaySeconds; /* out-delay timer in seconds */
  /*
   * ADD-PATH capability for this peer.
   * - std::nullopt: No ADD-PATH capability (default, non-ADD-PATH peer)
   * - RECEIVE: Peer can receive multiple paths
   * - SEND: Peer can send multiple paths
   * - BOTH: Peer can both send and receive multiple paths
   */
  std::optional<nettools::bgplib::BgpAddPathSendRec> addPathCapability =
      std::nullopt;
  /*
   * LBW (Link Bandwidth) extended community configuration for this peer.
   * - advertiseLinkBandwidth: Controls egress LBW behavior
   * - receiveLinkBandwidth: Controls ingress LBW behavior
   * - std::nullopt means unconfigured (uses default behavior)
   */
  std::optional<AdvertiseLinkBandwidth> advertiseLinkBandwidth = std::nullopt;
  std::optional<ReceiveLinkBandwidth> receiveLinkBandwidth = std::nullopt;
  std::optional<std::string> egressPolicyName = std::nullopt;
  /*
   * Confederation peer settings. When isConfedPeer=true the peer is treated
   * as a ConfedEBGP session. localConfedAsn must be set on at least one
   * confed peer in the fixture and identifies the parent confederation AS;
   * the value is plumbed into thriftConfig.local_confed_as_4_byte by
   * getConfig(). Conflicting localConfedAsn values across peers throw.
   */
  bool isConfedPeer = false;
  std::optional<uint32_t> localConfedAsn = std::nullopt;

  /*
   * Route-Reflector client toggle. When true, an IBGP peer is treated as
   * an RR client and the peer-level canAnnounce / group-level
   * canAnnounceForGroup paths skip the IBGP split-horizon early-return,
   * letting downstream filters (e.g., RFC 1997 NO_ADVERTISE) decide.
   */
  bool isRrClient = false;

  /*
   * Peer-group membership. When set, the peer is added to the named peer
   * group (peer.peer_group_name) and getConfig() registers the group in
   * thriftConfig.peer_groups. Required for peer-group-level runtime policy
   * changes via BgpServiceBB::co_setPeerGroupsPolicy — that API rejects
   * with INPUT_ERROR unless the group exists in config.
   */
  std::optional<std::string> peerGroupName = std::nullopt;

  /* Additional remote ASN accepted during a migration. */
  std::optional<uint32_t> additionalRemoteAs = std::nullopt;

  /* Enforce that received AS paths begin with the session's remote ASN. */
  std::optional<bool> enforceFirstAs = std::nullopt;
  bool enableRouteRefresh = false;
};

/* Inline default peer specs for common test scenarios */
inline const BgpPeerSpec kDefaultPeerSpec3 = {
    .asn = kPeerAsn3,
    .localAddr = kLocalAddr1,
    .peerAddr = kPeerAddr3,
    .v4Nexthop = kNextHopV4_3,
    .v6Nexthop = kNextHopV6_3,
    .description = kDescription1,
};

inline const BgpPeerSpec kDefaultPeerSpec4 = {
    .asn = kPeerAsn4,
    .localAddr = kLocalAddr1,
    .peerAddr = kPeerAddr4,
    .v4Nexthop = kNextHopV4_4,
    .v6Nexthop = kNextHopV6_4,
};

inline const BgpPeerSpec kDefaultPeerSpec5 = {
    .asn = kPeerAsn5,
    .localAddr = kLocalAddr5,
    .peerAddr = kPeerAddr5,
    .v4Nexthop = kNextHopV4_5,
    .v6Nexthop = kNextHopV6_5,
};

inline const BgpPeerSpec kDefaultPeerSpec3_v6 = {
    .asn = kPeerAsn3,
    .localAddr = kLocalAddr1,
    .peerAddr = kPeerAddr3,
    .v4Nexthop = kEmptyV4Nexthop,
    .v6Nexthop =
        kNextHopV6_3, /* 2401:db00:e011:411:1000::29 - expected by tests */
    .description = kDescription1,
    .disableIpv4Afi = true, /* v6-only peer - single EoR */
};

inline const BgpPeerSpec kDefaultPeerSpec3_v4only = {
    .asn = kPeerAsn3,
    .localAddr = kLocalAddr1,
    .peerAddr = kPeerAddr3,
    .v4Nexthop = kNextHopV4_3,
    .v6Nexthop = kEmptyV6Nexthop,
    .description = kDescription1,
    .disableIpv6Afi = true,
};

inline const BgpPeerSpec kDefaultPeerSpec4_v4only = {
    .asn = kPeerAsn4,
    .localAddr = kLocalAddr1,
    .peerAddr = kPeerAddr4,
    .v4Nexthop = kNextHopV4_4,
    .v6Nexthop = kEmptyV6Nexthop,
    .disableIpv6Afi = true,
};

inline const BgpPeerSpec kDefaultPeerSpec4_v6 = {
    .asn = kPeerAsn4,
    .localAddr = kLocalAddr1,
    .peerAddr = kPeerAddr4,
    .v4Nexthop = kEmptyV4Nexthop,
    .v6Nexthop =
        kNextHopV6_4, /* 2401:db00:e011:411:1000::2b - expected by tests */
    .disableIpv4Afi = true, /* v6-only peer - single EoR */
};

/*
 * Peer specs with out-delay configured for testing AdjRibOut delay behavior.
 * Peer3: Receives routes (0s out-delay - immediate advertisement)
 * Peer4: Receives routes (0s out-delay - immediate advertisement)
 * Peer5: Advertises routes with 1s out-delay
 */
inline const BgpPeerSpec kDefaultPeerSpec3_outDelay0s = {
    .asn = kPeerAsn3,
    .localAddr = kLocalAddr1,
    .peerAddr = kPeerAddr3,
    .v4Nexthop = kNextHopV4_3,
    .v6Nexthop = kNextHopV6_3,
    .description = kDescription1,
    .outDelaySeconds = 0,
};

inline const BgpPeerSpec kDefaultPeerSpec4_outDelay0s = {
    .asn = kPeerAsn4,
    .localAddr = kLocalAddr1,
    .peerAddr = kPeerAddr4,
    .v4Nexthop = kNextHopV4_4,
    .v6Nexthop = kNextHopV6_4,
    .outDelaySeconds = 0,
};

inline const BgpPeerSpec kDefaultPeerSpec5_outDelay1s = {
    .asn = kPeerAsn5,
    .localAddr = kLocalAddr5,
    .peerAddr = kPeerAddr5,
    .v4Nexthop = kNextHopV4_5,
    .v6Nexthop = kNextHopV6_5,
    .outDelaySeconds = 1,
};

/*
 * ADD-PATH enabled peer specs
 * These peers have ADD-PATH capability enabled for receiving multiple paths.
 */
inline const BgpPeerSpec kDefaultPeerSpec3_AddPath = {
    .asn = kPeerAsn3,
    .localAddr = kLocalAddr1,
    .peerAddr = kPeerAddr3,
    .v4Nexthop = kNextHopV4_3,
    .v6Nexthop = kNextHopV6_3,
    .description = kDescription1,
    .addPathCapability = nettools::bgplib::BgpAddPathSendRec::BOTH,
};

inline const BgpPeerSpec kDefaultPeerSpec4_AddPath = {
    .asn = kPeerAsn4,
    .localAddr = kLocalAddr1,
    .peerAddr = kPeerAddr4,
    .v4Nexthop = kNextHopV4_4,
    .v6Nexthop = kNextHopV6_4,
    .addPathCapability = nettools::bgplib::BgpAddPathSendRec::BOTH,
};

inline const BgpPeerSpec kDefaultPeerSpec5_AddPath = {
    .asn = kPeerAsn5,
    .localAddr = kLocalAddr5,
    .peerAddr = kPeerAddr5,
    .v4Nexthop = kNextHopV4_5,
    .v6Nexthop = kNextHopV6_5,
    .addPathCapability = nettools::bgplib::BgpAddPathSendRec::BOTH,
};

/*
 * E2ETestFixture
 * Base fixture for RIB + PeerManagerBase E2E tests.
 * Manages component lifecycle and provides helpers for BGP operations.
 *
 * ============================ Where does my test go? ======================
 * RibBB and RibDC cannot coexist in one binary (their PlatformConstant.h
 * headers define conflicting kBgpPlatformType inline constexprs — ODR). So
 * the platform is chosen per Buck TARGET by which fixture library it links:
 *   - link ":e2e_test_fixture_bb"  -> RibBB  (EBB/border platform; default)
 *   - link ":e2e_test_fixture_dc"  -> RibDC  (data-center platform)
 * createRib() builds the RIB via makeTestRib(), whose definition is supplied
 * by the linked library (E2ETestRibBB.cpp / E2ETestRibDC.cpp).
 *
 * Decide where a new E2E test belongs:
 *   - Behavior shared by both platforms (routing, path selection, CRF, FIB,
 *     GR, sessions): write a normal TEST_F(E2ETestFixture, ...) and give it a
 *     BB target AND a "<name>_dc" sibling target so it runs on both bases.
 *   - DC-only behavior (partial drain, MNH-drain, CPS, CTE, FSDB): link
 *     ":e2e_test_fixture_dc" and reach RibDC APIs via
 *     static_cast<RibDC*>(rib_.get()).
 *   - BB-only behavior (rejecting DC policy messages, EBB FIB): link
 *     ":e2e_test_fixture_bb" (BB) and use sendUnsupportedDcPolicyMsgs().
 * ==========================================================================
 */
class E2ETestFixture : public ::testing::Test {
 protected:
  // Clean up all components
  void TearDown() override;

  /*
   * Create and start RIB with TestFib. The RIB base (RibBB or RibDC) is
   * selected at link time via makeTestRib() — see "Where does my test go?".
   */
  void createRib(
      bool enableNexthopTracking = false,
      const std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>&
          localRoutes = {},
      FsdbSyncer* fsdbSyncer = nullptr);

  // Access the injected TestFib regardless of which RIB base is linked.
  TestFib* getTestFib();

  /*
   * Push the DC-only policy messages (CPS/CTE) onto the RIB policy queue so a
   * BB-linked test can verify RibBB rejects them (increments the
   * numUnsupportedPolicyMsg counter without mutating RIB state). Defined on
   * this fixture because friendship with RibBase is not inherited by derived
   * fixtures. Pushes 3 messages: PathSelectionPolicyClear,
   * RouteAttributePolicyClear, RouteAttributePolicyTimer.
   */
  void sendUnsupportedDcPolicyMsgs();

  // ==================== PEER MANAGEMENT ====================

  /*
   * Add a peer to the configuration (call before createPeerManager)
   */
  void addPeer(const BgpPeerSpec& spec);

  void enableRouteRefreshForAllPeers() {
    enableRouteRefreshForAllPeers_ = true;
  }

  /*
   * Remove a peer from the configuration
   */
  void deletePeer(const folly::IPAddress& peerAddr);

  /*
   * Drive PeerManagerBase::delPeers at runtime.
   */
  folly::Expected<folly::Unit, nettools::bgplib::FiberBgpPeerManager::ErrorCode>
  delPeerAtRuntime(const folly::IPAddress& peerAddr);

  /*
   * Establish a BGP session for a peer (bring peer up)
   * Uses default queue sizes set via setDefaultQueueSizes()
   */
  void bringUpPeer(
      const folly::IPAddress& peerAddr,
      uint64_t versionNumber = 1);

  /*
   * Bring up a peer with its queue pre-blocked.
   *
   * Blocks the peer BEFORE establishing the session so the init dump
   * PL cannot drain. This holds the peer in DETACHED_INIT_DUMP until
   * unblockPeer() is called, making DID reliably observable in tests.
   */
  void bringUpPeerBlocked(
      const folly::IPAddress& peerAddr,
      uint64_t versionNumber = 1);

  /*
   * Test-only: defer init dump for a specific reconnecting peer.
   * When deferred, processRibDumpReq re-buffers the request instead of
   * walking the shadow RIB, keeping that peer in DETACHED_INIT_DUMP.
   * Call with false to resume normal dump processing for the peer.
   */
  void testOnlyDeferInitDump(const folly::IPAddress& peerAddr, bool defer);

  /*
   * Test-only: defer DRJ acceptance for a specific peer.
   * When deferred, the peer stays in DETACHED_READY_TO_JOIN.
   * On release, triggers acceptance so the peer can rejoin the group.
   */
  void testOnlyDeferDrjAcceptance(const folly::IPAddress& peerAddr, bool defer);

  /*
   * Tear down a BGP session for a peer (bring peer down).
   */
  void bringDownPeer(const folly::IPAddress& peerAddr, bool peerDelete = false);

  /* Wait for both AdjRib message loops to finish the production teardown. */
  bool waitForSessionTerminationBaton(
      const folly::IPAddress& peerAddr,
      std::chrono::milliseconds timeout = std::chrono::seconds(30));

  /*
   * Deliver the AdjRib half of session termination and wait for it to finish,
   * leaving the PeerManager termination event pending. This exposes the same
   * event gap that exists in production without fabricating peer state.
   */
  void beginPeerSessionTermination(const folly::IPAddress& peerAddr);

  /*
   * Deliver the PeerManager half of a termination started by
   * beginPeerSessionTermination().
   */
  void completePeerSessionTermination(
      const folly::IPAddress& peerAddr,
      bool peerDelete = false);

  /*
   * Tear down a BGP session for a peer with gracefulRestart=true so the
   * AdjRib enters GR helper mode (schedules remoteGrRestartTimer_ and
   * keeps stale routes in adjRibInStale_). Otherwise mirrors
   * bringDownPeer().
   */
  void bringDownPeerWithGr(const folly::IPAddress& peerAddr);

  /*
   * Run AdjRib::stop() synchronously on the PeerManagerBase event base. Tests
   * that exercise AdjRib teardown (e.g. the pendingRibInPushes_ drain)
   * call this directly instead of going through cleanupPeerState().
   */
  void runAdjRibStop(const std::shared_ptr<AdjRib>& adjRib);

  /*
   * Get the peer's current update-group state.
   */
  PeerUpdateState getPeerState(const nettools::bgplib::BgpPeerId& peerId);

  /*
   * Dispatch a STALE sessionEstablished event for a peer (S619541 test helper).
   *
   * Constructs a sessionEstablished event with a version mismatch: the event
   * carries versionNumber=X but the underlying VersionNumber is bumped to a
   * higher value before dispatch. This simulates FiberBgpPeer creating the
   * event, then the session flapping and FiberBgpPeer bumping the version for
   * a new incarnation before PeerManagerBase processes the old event.
   *
   * PeerManagerBase::sessionEstablished() will:
   *  1. Wait on the terminate baton (passes if already posted)
   *  2. Detect the version mismatch -> early return
   *  3. Leave the baton posted (no reset on early return)
   *
   * Call after bringDownPeer() and before the next bringUpPeer() to simulate
   * the rapid session flap with version compression scenario.
   */
  void dispatchStaleSessionEstablished(const folly::IPAddress& peerAddr);

  // ==================== LOCAL ROUTE MANAGEMENT ====================

  /*
   * Add a local route to be originated (call before createRib)
   */
  void addLocalRoute(
      const std::string& prefix,
      const std::vector<std::string>& communities = {},
      uint32_t localPref = kLocalPref,
      const std::string& nexthop = "",
      uint32_t minSupportingRoutes = 0);

  /*
   * Inject local routes at runtime (call after RIB is running)
   */
  void injectLocalRoutesAtRuntime(
      const std::vector<std::string>& prefixes,
      const std::vector<std::string>& communities = {},
      uint32_t localPref = kLocalPref);

  /*
   * Withdraw local routes at runtime (call after RIB is running)
   */
  void withdrawLocalRoutesAtRuntime(const std::vector<std::string>& prefixes);

  // ==================== POLICY CONFIGURATION ====================

  /*
   * Set the ingress policy configuration (call before createRib)
   * The policy will be applied to routes received from peers.
   *
   * @param policies The BgpPolicies configuration to use
   */
  void setPolicyConfig(const bgp_policy::BgpPolicies& policies);

  /*
   * Add a policy that denies routes matching a prefix list
   * Helper for common deny-by-prefix-list scenario.
   * Call before createRib().
   *
   * @param prefixes Vector of prefix strings to deny (e.g., "10.0.0.0/8")
   */
  void addPrefixDenyPolicy(const std::vector<std::string>& prefixes);

  /*
   * Add a policy that denies routes matching a community
   * Helper for common deny-by-community scenario.
   * Call before createRib().
   *
   * @param community Community string to match and deny (e.g., "65000:100")
   */
  void addCommunityDenyPolicy(const std::string& community);

  /*
   * Add a policy that sets local preference for routes matching a community
   * Helper for common set-local-pref-by-community scenario.
   * Call before createRib().
   *
   * @param matchCommunity Community to match
   * @param localPref Local preference value to set
   */
  void addCommunitySetLocalPrefPolicy(
      const std::string& matchCommunity,
      uint32_t localPref);

  /*
   * Add a permit-all EGRESS policy that does NOT match or set communities.
   * Such a policy leaves PolicyAttributesMask::communities unset, so the
   * egress policy cache key excludes communities. This is the configuration
   * that exposes the partial-drain cache-collision regression. Unlike the
   * ingress helpers above, this only registers the policy in policyConfig_;
   * the caller must set egressPolicyName on the receiver peer spec before
   * addPeer(). Call before createRib().
   *
   * @param name Name to register the egress policy under
   */
  void addAcceptAllEgressPolicy(const std::string& name);

  /*
   * Wait for a route to appear in PeerManagerBase's shadowRIB
   * Thread-safe: runs check on PeerManagerBase's event base
   * Returns true if route found within retries, false otherwise
   *
   * If fromPeer is specified, waits for a route from that specific peer
   * (checks bestpath->peer.addr matches). This is useful when a prefix
   * may already exist but we're waiting for a bestpath change.
   */
  bool waitForRouteInShadowRib(
      const folly::CIDRNetwork& prefix,
      std::optional<folly::IPAddress> fromPeer = std::nullopt,
      int maxRetries = 50);

  /*
   * Verify a route does NOT appear in PeerManagerBase's shadowRIB after waiting
   * Thread-safe: runs check on PeerManagerBase's event base
   * Returns true if route is NOT found after waiting, false if route exists
   * Unlike waitForRouteInShadowRib, this does NOT record test failures
   */
  bool verifyRouteNotInShadowRib(
      const folly::CIDRNetwork& prefix,
      int waitRetries = 5);

  /*
   * Wait for a route to have at least minPathCount paths in the RIB
   * Thread-safe: runs check on RIB's event base
   * Returns true if path count reached within retries, false otherwise
   *
   * @param prefixStr The prefix string (e.g., "10.0.0.0/8")
   * @param minPathCount Minimum number of paths to wait for
   * @param maxRetries Number of retry attempts (default 20)
   */
  bool waitForPathCountInRib(
      const std::string& prefixStr,
      size_t minPathCount,
      int maxRetries = 20);

  /*
   * Wait for a route to be withdrawn from the RIB (no paths remaining)
   * Thread-safe: runs check on RIB's event base
   * Returns true if route is withdrawn within retries, false otherwise
   *
   * @param prefixStr The prefix string (e.g., "10.0.0.0/8")
   * @param maxRetries Number of retry attempts (default 20)
   */
  bool waitForRouteWithdrawnFromRib(
      const std::string& prefixStr,
      int maxRetries = 20);

  /*
   * Verify multipath (ECMP) nexthops for a prefix in the RIB.
   * Returns the number of nexthops in the weighted nexthop map.
   * Use this to verify ECMP/UCMP behavior.
   *
   * @param prefixStr The prefix string (e.g., "10.0.0.0/8")
   * @return Number of nexthops, or 0 if prefix not found
   */
  size_t getMultipathNexthopCount(const std::string& prefixStr);

  /*
   * Wait for a route to have at least minNexthops in the weighted nexthop map.
   * This verifies ECMP/UCMP multipath behavior.
   *
   * @param prefixStr The prefix string (e.g., "10.0.0.0/8")
   * @param minNexthops Minimum number of nexthops to wait for
   * @param maxRetries Number of retry attempts (default 20)
   * @return true if minNexthops reached, false otherwise
   */
  bool waitForMultipathNexthopCount(
      const std::string& prefixStr,
      size_t minNexthops,
      int maxRetries = 20);

  /*
   * Get the weighted nexthops for a prefix in the RIB.
   * Returns a copy of the weighted nexthop map (nexthop IP -> weight).
   * Use this to verify specific ECMP/UCMP legs and their weights.
   *
   * @param prefixStr The prefix string (e.g., "10.0.0.0/8")
   * @return Map of nexthop IP addresses to weights, empty if prefix not found
   */
  std::map<folly::IPAddress, uint32_t> getWeightedNexthops(
      const std::string& prefixStr);

  // ==================== COMPONENT LIFECYCLE ====================

  /*
   * Set default queue sizes for all peers
   * Call before bringUpPeer() to use custom queue sizes
   * For blocking tests: setDefaultQueueSizes(1, 1, 0) or (2, 1, 0)
   */
  void setDefaultQueueSizes(int capacity = 8, int highWm = 6, int lowWm = 2) {
    defaultQueueCapacity_ = capacity;
    defaultQueueHighWm_ = highWm;
    defaultQueueLowWm_ = lowWm;
  }

  /*
   * Set queue sizes for a specific peer, overriding the defaults.
   * Call before bringUpPeer() for the target peer.
   */
  void setQueueSizeForPeer(
      const folly::IPAddress& peerAddr,
      int capacity,
      int highWm,
      int lowWm) {
    perPeerQueueSizes_[peerAddr] = {capacity, highWm, lowWm};
  }

  /* Create and start PeerManagerBase with MockSessionManager
   * enableUpdateGroup: Enable update group optimization
   * enableSerializeGroupPdu: Enable group PDU serialization for zero-copy
   * NOTE: All features default to TRUE for comprehensive E2E testing
   */
  void createPeerManager(
      bool enableUpdateGroup = true,
      bool enableSerializeGroupPdu = false);

  // Establish a BGP session for a peer
  void establishSession(const BgpPeerId& peerId, uint64_t versionNumber = 1);

  // Send BGP UPDATE to a peer
  void sendUpdateToPeer(
      const BgpPeerId& peerId,
      const folly::CIDRNetwork& prefix);

  // Send End-of-RIB marker to a peer
  void sendEoRToPeer(const BgpPeerId& peerId);

  // Send Route Refresh request to a peer (RFC 2918)
  void sendRouteRefreshToPeer(
      const BgpPeerId& peerId,
      nettools::bgplib::BgpUpdateAfi afi,
      nettools::bgplib::BgpUpdateSafi safi);

  void processRouteRefreshEventForTesting(
      const BgpPeerId& peerId,
      nettools::bgplib::BgpUpdateAfi afi);

  // Read outbound UPDATE from peer's queue
  std::optional<std::shared_ptr<const BgpUpdate2>> readOutboundUpdateToPeer(
      const BgpPeerId& peerId);

  // Read outbound BgpRouteRefresh from peer's queue
  std::optional<nettools::bgplib::BgpRouteRefresh>
  readOutboundRouteRefreshFromPeer(const BgpPeerId& peerId);

  /*
   * Wait for an UPDATE to appear in peer's outbound queue, then read it.
   * Uses event loop pumping for deterministic wait (no sleep).
   * Returns the UPDATE if found within maxRetries, nullopt otherwise.
   */
  std::optional<std::shared_ptr<const BgpUpdate2>> waitForOutboundUpdate(
      const BgpPeerId& peerId,
      int maxRetries = 50);

  /*
   * Wait for and consume End-of-RIB marker from peer's outbound queue
   * Returns true if EoR was received, false if timeout/queue closed
   */
  bool waitForEoR(const BgpPeerId& peerId);

  // ==================== QUEUE BLOCKING HELPERS ====================

  /*
   * Check if a peer's bounded egress queue is blocked (full)
   * Returns true if queue is at or above high watermark
   */
  bool isPeerQueueBlocked(const BgpPeerId& peerId);

  /*
   * Wait for a peer's bounded egress queue to become blocked
   * Uses retries to wait for routes to reach the queue
   * Returns true if queue becomes blocked within timeout, false otherwise
   */
  bool waitForPeerQueueBlocked(const BgpPeerId& peerId, int maxRetries = 50);

  /*
   * Get current size of peer's bounded queue
   */
  size_t getPeerQueueSize(const BgpPeerId& peerId);

  /*
   * Wait until the peer's egress AdjRib change-list consumer is ready, i.e. it
   * has reached the end of the change list (all published updates consumed into
   * the packing list). Polls PeerManagerBase for the peer's AdjRib. Returns
   * true once the consumer is ready, false on timeout. Pair with
   * waitForRouteInShadowRib (which confirms the update was published) to gate
   * on "the update has been consumed into the packing list".
   */
  bool waitForChangeListConsumerReady(
      const folly::IPAddress& peerAddr,
      int maxRetries = 50);

  /*
   * Wait until the peer's egress AdjRib change-list consumer is pended on the
   * change-list item for the given prefix, i.e. its marker points at the
   * ShadowRibEntry change for that prefix (published but not yet consumed into
   * the packing list). Use after injecting an update while the egress queue is
   * backpressured to confirm the update is sitting unconsumed in the change
   * list. Returns true once the marker is on that prefix, false on timeout.
   */
  bool waitForChangeListConsumerPended(
      const folly::IPAddress& peerAddr,
      const folly::CIDRNetwork& prefix,
      int maxRetries = 50);

  /*
   * Read the egress (RIB-OUT) AdjRibEntry's isNexthopSetByPolicy() flag for a
   * prefix on the peer. Returns std::nullopt if no egress entry exists. Runs on
   * PeerManagerBase's event base.
   */
  std::optional<bool> checkRibOutNexthopSetByPolicy(
      const folly::IPAddress& peerAddr,
      const folly::CIDRNetwork& prefix);

  /*
   * One message drained from a peer's egress queue, preserving order. Either an
   * EoR (isEoR=true, update=null) or an UPDATE (update set).
   */
  struct OutboundMessage {
    bool isEoR{false};
    std::shared_ptr<const BgpUpdate2> update;
  };

  /*
   * Pop every message currently obtainable from the peer's egress queue into a
   * vector, in order, without dropping any. Waits (event-base flush + brief
   * sleep) for asynchronously / timer-emitted messages, and stops once the
   * queue stays empty for idleRetries consecutive checks. Use to verify the
   * exact order and cardinality of emitted messages.
   */
  std::vector<OutboundMessage> drainAllOutboundMessagesToOrderedVec(
      const BgpPeerId& peerId,
      int idleRetries = 10,
      int maxMessages = 0,
      int sleepMsBetweenRetries = 50);

  /*
   * Drain a peer's queue completely by reading all messages
   * Useful for unblocking peers in tests
   *
   * maxRetries: Number of times to retry checking the queue when it's empty.
   *             This allows time for in-flight messages to arrive.
   *             Each retry pumps the RIB and PeerManagerBase event loops.
   *             Default is 5 retries.
   *
   * maxMessages: Maximum number of messages to drain before stopping.
   *              This prevents infinite draining if messages keep arriving.
   *              Default is 100. Set to 0 for unlimited (use with caution).
   *
   * Returns: Number of messages actually drained from the queue.
   */
  size_t drainPeerQueueCompletely(
      const BgpPeerId& peerId,
      int maxRetries = 5,
      int maxMessages = 100);

  /*
   * A peer's currently-advertised routes, reconstructed from the UPDATEs
   * drained off its egress queue: prefix -> the path attributes last announced
   * for it. An announcement sets/overwrites the entry, a withdrawal erases it,
   * so the map converges to the peer's Adj-RIB-Out and never needs to be reset
   * between policy phases.
   */
  using ReceivedRoutes =
      std::map<folly::CIDRNetwork, nettools::bgplib::BgpAttributes>;

  /*
   * Drain a peer's egress queue (via drainAllOutboundMessagesToOrderedVec) and
   * fold each drained UPDATE into the peer's received-routes map: announced
   * prefixes set/overwrite their attributes, withdrawn prefixes are erased
   * (EoRs are skipped). Lets a test verify exactly which prefixes -- and which
   * attributes (e.g. communities) -- a peer currently has, not just the group
   * RIB-OUT state.
   */
  void recordDrainedRoutes(
      const BgpPeerId& peerId,
      int idleRetries = 20,
      int maxMessages = 500);

  /*
   * The routes currently advertised to a peer as recorded by
   * recordDrainedRoutes (empty if the peer has never been drained via it).
   */
  const ReceivedRoutes& receivedRoutes(const BgpPeerId& peerId) const;

  /*
   * Drain all messages from a peer's outbound queue and report whether
   * any EoR messages were found. Counts of updates and EoRs are returned
   * via output parameters.
   */
  void drainAndClassifyMessages(
      const BgpPeerId& peerId,
      size_t& updateCount,
      size_t& eorCount,
      int maxRetries = 5,
      int maxMessages = 100);

  /*
   * Per-prefix message counts collected while draining a peer's queue.
   */
  /* Which event for the queried prefix was seen last on the wire. */
  enum class PrefixEvent { None, Announce, Withdraw };

  struct PrefixOccurrenceCounts {
    /* Number of UPDATE messages announcing the queried prefix. */
    size_t announceCount{0};
    /* Number of UPDATE messages withdrawing the queried prefix. */
    size_t withdrawCount{0};
    /* Total messages drained from the queue (announcements, withdrawals,
     * EoR markers, route-refresh messages, notifications). */
    size_t totalMessages{0};
    /*
     * Last event observed for the prefix, in queue order. Counts alone cannot
     * express the peer's final state -- one announce and one withdraw yield
     * the same counts whichever arrived last -- so callers asserting
     * wire-final state must use this.
     *
     * None if the prefix did not appear at all in this drain.
     */
    PrefixEvent lastEvent{PrefixEvent::None};
  };

  /*
   * Drain a peer's queue and classify each message with respect to the
   * given prefix. Mirrors drainPeerQueueCompletely's empty-check + retry
   * + evb-pump pattern (so it cannot block on an EoR-trail), and adds
   * per-message inspection so callers can distinguish "1 UPDATE + 1
   * WITHDRAW + 2 EoRs" from "2 UPDATEs + 2 EoRs" — drain count alone
   * conflates these.
   *
   * isV4: pass true for v4 prefixes, false for v6 (matches
   *       findPrefixInAnnouncements / findPrefixInWithdrawals).
   */
  PrefixOccurrenceCounts countPrefixOccurrencesAndDrain(
      const BgpPeerId& peerId,
      const folly::CIDRNetwork& prefix,
      bool isV4,
      int maxRetries = 5,
      int maxMessages = 100);

  /*
   * Drain a peer's queue and return every UPDATE message as a vector.
   * EoR / RouteRefresh / Notification messages are popped and discarded
   * (counted internally but not surfaced — the caller cares about UPDATEs).
   * Mirrors drainPeerQueueCompletely's empty-check + retry + evb-pump
   * pattern so it cannot block on an EoR-trail.
   *
   * Use when a test needs to inspect per-emission contents — e.g. to
   * verify wire-final attrs of the last UPDATE for a prefix. Prefer
   * countPrefixOccurrencesAndDrain when only counts matter.
   */
  std::vector<std::shared_ptr<const nettools::bgplib::BgpUpdate2>>
  drainPeerQueueAndCollectUpdates(
      const BgpPeerId& peerId,
      int maxRetries = 5,
      int maxMessages = 100);

  /*
   * Block a peer's egress queue
   *
   * Sets a flag that prevents reading from this peer's queue.
   * The queue will fill up naturally and cause backpressure.
   *
   * Usage pattern:
   * 1. Set small queue sizes with setDefaultQueueSizes(1, 1, 0) BEFORE
   * bringUpPeer()
   * 2. blockPeer(peerAddr) - prevents queue reads
   * 3. Send routes - queue fills and blocks
   * 4. unblockPeer(peerAddr) - clears flag and drains queue
   */
  void blockPeer(const folly::IPAddress& peerAddr);

  /*
   * Unblock a peer by clearing the block flag and draining its egress queue
   *
   * maxRetries: Number of retries when draining (default 5)
   * maxMessages: Maximum messages to drain (default 100)
   *
   * Returns true if peer was previously blocked, false otherwise
   */
  bool unblockPeer(
      const folly::IPAddress& peerAddr,
      int maxRetries = 5,
      int maxMessages = 100);

  /*
   * Check if a peer is blocked (has block flag set)
   */
  bool isPeerBlocked(const folly::IPAddress& peerAddr) const;

  /*
   * Check if a peer's egress queue is empty (no pending routes to advertise)
   * This checks the ACTUAL egress queue, not shadowRib.
   * Used for out-delay testing to verify routes are deferred.
   */
  bool isPeerEgressQueueEmpty(const folly::IPAddress& peerAddr);

  /*
   * Wait for a peer's egress queue to become non-empty
   * Returns true if message arrives within retries, false otherwise
   */
  bool waitForPeerEgressQueueNonEmpty(
      const folly::IPAddress& peerAddr,
      int maxRetries = 50);

  // ==================== NEW HELPER APIS ====================

  /*
   * Add or update a BGP route from a peer
   * Required: protocol, prefix, prefixLen, nexthop
   * Optional: asPath (empty if not specified), community (empty if not
   * specified)
   */
  void addRoute(
      const std::string& protocol,
      const std::string& prefix,
      uint8_t prefixLen,
      const folly::IPAddress& peer,
      const std::string& nexthop,
      const std::string& asPath = "",
      const std::string& community = "",
      uint32_t addPathId = 0,
      uint32_t localPref = 100,
      uint32_t med = 0);

  /*
   * Add or update a BGP route with Link Bandwidth extended community.
   * Use this for UCMP weight verification tests.
   *
   * @param linkBandwidthGbps Link bandwidth in Gbps (e.g., 10.0 for 10 Gbps)
   */
  void addRouteWithLbw(
      const std::string& protocol,
      const std::string& prefix,
      uint8_t prefixLen,
      const folly::IPAddress& peer,
      const std::string& nexthop,
      const std::string& asPath,
      float linkBandwidthGbps,
      uint32_t localPref = 100);

 public:
  /*
   * Add or update a BGP route with arbitrary extended communities.
   */
  void addRouteWithExtCommunities(
      const std::string& protocol,
      const std::string& prefix,
      uint8_t prefixLen,
      const folly::IPAddress& peer,
      const std::string& nexthop,
      const std::string& asPath,
      const std::vector<nettools::bgplib::BgpAttrExtCommunityC>& extCommunities,
      uint32_t localPref = 100);

  /*
   * Get an AdjRibEntry from a peer's AdjRib.
   * Wraps private PeerManagerBase::adjRibs_ access (requires friend access).
   */

  AdjRibEntry* getAdjRibEntry(
      const BgpPeerId& peerId,
      bool ingress,
      const folly::CIDRNetwork& prefix);

  /*
   * Get AdjRib for a peer by IP address.
   * Iterates PeerManagerBase::adjRibs_ to find the matching entry.
   */
  std::shared_ptr<AdjRib> getAdjRibByAddr(const folly::IPAddress& peerAddr);

  /*
   * ============ STREAM SUBSCRIBER (MP-BGP MONITOR) HELPERS ============
   *
   * The subscriber-side mirror of the peer-side queue helpers above. Every
   * one of them hops onto PeerManagerBase's event base: streamSubscribers_,
   * adjRibs_ and the AdjRib timers are owned by that thread and reading them
   * directly races session transitions.
   */

  /*
   * Current depth of a subscriber's bounded egress queue. std::nullopt if the
   * subscriber or its bounded queue is absent -- callers MUST distinguish
   * that from "empty", otherwise a subscriber that was torn down looks
   * perfectly drained.
   */
  std::optional<size_t> getSubscriberQueueSize(
      const std::string& subscriberName);

  /*
   * Whether a subscriber's bounded egress queue is at/above its high
   * watermark. std::nullopt if the subscriber or its bounded queue is absent;
   * see getSubscriberQueueSize() for why that is not folded into `false`.
   */
  std::optional<bool> isSubscriberQueueBlocked(
      const std::string& subscriberName);

  // Wait for a subscriber's bounded egress queue to reach the blocked state.
  bool waitForSubscriberQueueBlocked(
      const std::string& subscriberName,
      int maxRetries = 100);

  // Wait for a subscriber's bounded egress queue to leave the blocked state.
  bool waitForSubscriberQueueUnblocked(
      const std::string& subscriberName,
      int maxRetries = 100);

  // Wait for a subscriber's bounded egress queue to drain to at most maxSize.
  bool waitForSubscriberQueueSizeAtMost(
      const std::string& subscriberName,
      size_t maxSize,
      int maxRetries = 100);

  /*
   * Whether the subscriber AdjRib's change-list consume timer is scheduled.
   * False while the AdjRib is backpressured (waitForQueueSpace() cancels it),
   * true once it resumes. std::nullopt if the subscriber or its AdjRib is
   * not present. Requires AdjRib_TEST_FRIENDS.
   */
  std::optional<bool> isSubscriberChangeListTimerScheduled(
      const std::string& subscriberName);

  // Wait for the subscriber's change-list consume timer to reach `scheduled`.
  bool waitForSubscriberChangeListTimer(
      const std::string& subscriberName,
      bool scheduled,
      int maxRetries = 100);

  // Whether a subscriber is present and in ESTABLISHED state.
  bool isSubscriberEstablished(const std::string& subscriberName);

  // Wait for a subscriber to reach (or leave) ESTABLISHED state.
  bool waitForSubscriberEstablished(
      const std::string& subscriberName,
      bool established,
      int maxRetries = 100);

  /*
   * Walk a peer's RIB-OUT entries in its update group's LiteTree on the
   * PeerManager event base and return the count of entries for which
   * verifyOnRouteIndexFunc returns true, restricted to routes whose index octet
   * (the 3rd octet of the prefix) satisfies routeIndexPredicate.
   *
   * For each prefix the peer's own owner-key entry wins; otherwise the
   * group-owned (shared) entry is used, subject to isEntryShared for a detached
   * peer. Ported from the UT UpdateGroupPolicyReEvalUTCommon.h so e2e tests can
   * assert RIB-OUT *state* (not just outbound messages) at scale.
   */
  size_t verifyRibOutEntries(
      const folly::IPAddress& peerAddr,
      std::function<bool(int)> routeIndexPredicate = [](int) { return true; },
      std::function<bool(const AdjRibEntry&, const folly::CIDRNetwork&)>
          verifyOnRouteIndexFunc =
              [](const AdjRibEntry&, const folly::CIDRNetwork&) {
                return true;
              }) {
    auto& evb = peerManager_->getEventBase();
    return folly::via(
               &evb,
               [&]() -> size_t {
                 auto adjRib = getAdjRibByAddr(peerAddr);
                 if (!adjRib) {
                   return 0;
                 }
                 auto group = adjRib->getUpdateGroup();
                 if (!group) {
                   return 0;
                 }
                 auto peerOwnerKey = adjRib->getPeerOwnerKey();
                 auto groupOwnerKey = group->getGroupOwnerKey();
                 bool isDetached = adjRib->isDetachedPeer();
                 auto detachedRibVersion = adjRib->getDetachedRibVersion();

                 size_t count = 0;
                 for (auto it = group->LiteTree_.begin();
                      it != group->LiteTree_.end();
                      ++it) {
                   auto& ownerMap = it->value();
                   AdjRibEntry* entry = nullptr;

                   auto peerIt = ownerMap.find(peerOwnerKey);
                   if (peerIt != ownerMap.end()) {
                     entry = peerIt->second.get();
                   } else {
                     auto groupIt = ownerMap.find(groupOwnerKey);
                     if (groupIt != ownerMap.end() &&
                         (!isDetached ||
                          AdjRibOutGroup::isEntryShared(
                              /*peerHasOwnEntry=*/false,
                              detachedRibVersion,
                              groupIt->second->getRibVersion()))) {
                       entry = groupIt->second.get();
                     }
                   }

                   if (!entry) {
                     continue;
                   }

                   int routeIndexOctet = it.ipAddress().asV4().getNthMSByte(2);
                   if (!routeIndexPredicate(routeIndexOctet)) {
                     continue;
                   }
                   folly::CIDRNetwork prefix{it.ipAddress(), it.masklen()};
                   if (verifyOnRouteIndexFunc(*entry, prefix)) {
                     ++count;
                   }
                 }
                 return count;
               })
        .get();
  }

  static std::function<bool(const AdjRibEntry&, const folly::CIDRNetwork&)>
  verifyAdvertised() {
    return [](const AdjRibEntry& entry, const folly::CIDRNetwork& prefix) {
      EXPECT_NE(entry.getPostAttr(), nullptr)
          << prefix.first.str() << "/" << prefix.second
          << " was not advertised";
      return entry.getPostAttr() != nullptr;
    };
  }

  static std::function<bool(const AdjRibEntry&, const folly::CIDRNetwork&)>
  verifyNotAdvertised() {
    return [](const AdjRibEntry& entry, const folly::CIDRNetwork& prefix) {
      EXPECT_EQ(entry.getPostAttr(), nullptr)
          << prefix.first.str() << "/" << prefix.second
          << " should be withdrawn or not advertised";
      return entry.getPostAttr() == nullptr;
    };
  }

  static std::function<bool(const AdjRibEntry&, const folly::CIDRNetwork&)>
  verifyCommOnAdvertisedRoute(const std::string& community) {
    auto expectedComm =
        *nettools::bgplib::BgpAttrCommunityC::createBgpAttrCommunity(community);
    return [expectedComm](
               const AdjRibEntry& entry,
               const folly::CIDRNetwork& prefix) -> bool {
      auto postPolicy = entry.getPostAttr();
      EXPECT_NE(postPolicy, nullptr) << prefix.first.str() << "/"
                                     << prefix.second << " was not advertised";
      if (!postPolicy) {
        return false;
      }
      const auto& communities = postPolicy->getCommunities();
      bool found = false;
      for (const auto& comm : communities.get()) {
        if (comm == expectedComm) {
          found = true;
          break;
        }
      }
      EXPECT_TRUE(found) << prefix.first.str() << "/" << prefix.second
                         << " missing expected community";
      return found;
    };
  }

 protected:
  /*
   * Delete a BGP route from a peer
   */
  void deleteRoute(
      const std::string& protocol,
      const std::string& prefix,
      uint8_t prefixLen,
      const folly::IPAddress& peer,
      uint32_t addPathId = 0);

  /*
   * Verify a route announcement (REACH NLRI) in peer's outbound queue
   * Required: protocol, prefix, prefixLen, peer, expectedNexthop
   * Optional: expectedAsPath (not matched if empty), expectedCommunity (not
   * matched if empty)
   * maxWaitRetries: if > 0, use event loop pumping to wait for update
   *                 (deterministic, no sleep); if 0, block on queue
   */
  bool verifyRouteAdd(
      const std::string& protocol,
      const std::string& prefix,
      uint8_t prefixLen,
      const folly::IPAddress& peer,
      const std::string& expectedNexthop,
      const std::string& expectedAsPath = "",
      const std::string& expectedCommunity = "",
      uint32_t addPathId = 0,
      int maxWaitRetries = 0);

  /*
   * Drain peer's outbound queue looking for ONE announcement matching the
   * target prefix + nexthop (and optionally AS path / community).
   *
   * Use this in tests where the queue may contain noise (other route
   * pushes, withdrawals, EoRs) from earlier test phases — e.g., after
   * a session-flap thrash. Unlike verifyRouteAdd which reads one message
   * and checks, this consumes messages until the target is found OR the
   * queue is provably empty after flushing rib_/peerManager_ event bases.
   *
   * maxFlushRetries: how many times to flush the evbs when the queue is
   *                  empty before concluding "not coming". Each flush is
   *                  a runInEventBaseThreadAndWait — deterministic sync.
   *                  Default 10 covers normal multi-stage async pipelines.
   *
   * Returns true if a matching announcement was popped, false if the
   * queue drained to empty without finding it.
   */
  bool drainAndFindRouteAdvertised(
      const std::string& protocol,
      const std::string& prefix,
      uint8_t prefixLen,
      const folly::IPAddress& peer,
      const std::string& expectedNexthop,
      const std::string& expectedAsPath = "",
      const std::string& expectedCommunity = "",
      uint32_t addPathId = 0,
      int maxFlushRetries = 10);

  /*
   * Verify a route withdrawal (UNREACH NLRI) in peer's outbound queue
   */
  bool verifyRouteWithdraw(
      const std::string& protocol,
      const std::string& prefix,
      uint8_t prefixLen,
      const folly::IPAddress& peer,
      uint32_t addPathId = 0);

  // ==================== NEXTHOP TRACKING APIS ====================

  /*
   * Inject nexthop statuses into RIB's NexthopCache
   * Used for testing nexthop tracking behavior
   *
   * @param nexthopStatuses Vector of NexthopStatus objects to inject
   */
  void injectNexthopStatuses(const std::vector<NexthopStatus>& nexthopStatuses);

  /*
   * Verify nexthop route count in RIB's nexthopInfoMap_
   * Thread-safe: runs check on RIB's event base
   *
   * @param nexthopIp IP address of the nexthop to check
   * @param expectedRouteCount Expected number of routes using this nexthop.
   *                           If std::nullopt, verifies nexthop does NOT exist.
   *                           If has_value(), verifies nexthop exists with
   *                           exactly that many routes.
   * @return true if verification passes, false otherwise
   */
  bool verifyNexthopRouteCount(
      const folly::IPAddress& nexthopIp,
      std::optional<size_t> expectedRouteCount = std::nullopt);

  /*
   * Get the bestpath for a prefix from RIB
   * Thread-safe: runs check on RIB's event base
   *
   * @param prefix The prefix to query
   * @return shared_ptr to RouteInfo if bestpath exists, nullptr otherwise
   */
  std::shared_ptr<RouteInfo> getBestPath(const folly::CIDRNetwork& prefix);

  /*
   * Get the weighted nexthops for a prefix from the FIB
   * Used for ECMP verification - checks what nexthops were programmed to FIB
   *
   * @param prefixStr Prefix string in CIDR notation (e.g., "10.0.0.0/8")
   * @return shared_ptr to WeightedNexthopMap if found, nullptr otherwise
   */
  std::shared_ptr<const WeightedNexthopMap> getFibWeightedNexthops(
      const std::string& prefixStr);

  /*
   * Get the cached RIB version for a peer.
   * Thread-safe: runs check on PeerManagerBase's event base.
   *
   * @param peerAddr The peer's IP address
   * @return The peer's cached RIB version, or 0 if peer not found
   */
  uint64_t getPeerCachedRibVersion(const folly::IPAddress& peerAddr);

  /*
   * Number of entries currently in PeerManagerBase's shadow RIB
   * (shadowRibEntries_.size()).
   *
   * Thread-safe: runs on PeerManagerBase's event base. Requires friendship
   * (PeerManager_TEST_FRIENDS) to read the private member.
   */
  size_t getShadowRibEntriesCount();

  /*
   * Run PeerManagerBase::processRibDumpReq for an already-connected peer on the
   * PeerManager event base, simulating a re-dump (e.g. from egress-policy
   * re-evaluation). Requires friendship to call the private method.
   */
  void triggerRibDumpForPeer(
      const folly::IPAddress& peerAddr,
      bool sendAddPath = false,
      bool sendWithEoR = false);

 public:
  /*
   * Route specification for batch operations
   */
  struct RouteSpec {
    std::string prefix;
    uint8_t prefixLen;
    std::string nexthop;
    std::string asPath;
    std::string community;
    uint32_t addPathId = 0;
    uint32_t localPref = 100;
    uint32_t med = 0;
  };

  /*
   * Verification specification for batch route verification
   */
  struct VerifySpec {
    std::string prefix;
    uint8_t prefixLen;
    std::string expectedNexthop;
    std::string expectedAsPath;
    std::string expectedCommunity;
    uint32_t addPathId = 0;
  };

  /*
   * Withdrawal specification for batch withdrawal verification
   */
  struct WithdrawSpec {
    std::string prefix;
    uint8_t prefixLen;
    uint32_t addPathId = 0;
  };

  struct PeerQueues {
    std::shared_ptr<AdjRib::AdjRibInQueueT> adjRibInQ;
    std::shared_ptr<AdjRib::AdjRibOutQueueT> boundedAdjRibOutQ;
  };

  /*
   * Look up the per-peer queues.
   *
   * Base impl reads from peerQueues_ (populated at establishSession). Derived
   * fixtures whose underlying SessionManager owns the queues (e.g.
   * E2ESessionTestFixture, where TestSessionManager swaps queues on hard
   * reset) override these to read fresh from the session manager so callers
   * never see stale shared_ptr refs to closed queues. T270956942.
   */
  virtual std::optional<PeerQueues> getPeerQueues(
      const BgpPeerId& peerId) const;

  virtual std::unordered_map<BgpPeerId, PeerQueues> getAllPeerQueues() const;

  /*
   * Find the BgpPeerId for a given peer address. Walks the source-of-truth
   * directly (no temporary map copy), unlike the older
   * findPeerIdByAddress(getAllPeerQueues(), ...) pattern. Override in
   * derived fixtures whose source-of-truth differs from peerQueues_.
   */
  virtual std::optional<BgpPeerId> findPeerIdByAddress(
      const folly::IPAddress& peerAddr) const;

 protected:
  /*
   * Add multiple routes from a peer
   */
  void addRoutes(
      const std::string& protocol,
      const folly::IPAddress& peer,
      const std::vector<RouteSpec>& routes);

  /*
   * Delete multiple routes from a peer
   */
  void deleteRoutes(
      const std::string& protocol,
      const folly::IPAddress& peer,
      const std::vector<RouteSpec>& routes);

  /*
   * Verify multiple route announcements from a peer
   */
  bool verifyRoutes(
      const std::string& protocol,
      const folly::IPAddress& peer,
      const std::vector<VerifySpec>& routes);

  /*
   * Verify multiple route withdrawals from a peer
   */
  bool verifyRouteWithdraws(
      const std::string& protocol,
      const folly::IPAddress& peer,
      const std::vector<WithdrawSpec>& routes);

  /*
   * Enable UCMP weight calculation from LBW extended community.
   * Call before createRib() to enable UCMP weight calculation.
   */
  void enableComputeUcmpFromLbw(bool enable = true);

  /*
   * Enable eiBGP multipath feature.
   * Call before createRib() to enable eiBGP multipath.
   * When enabled, EXTERNAL_ROUTE preference filter is skipped in best path
   * selection, equalizing eBGP and iBGP paths.
   */
  void enableEiBgpMultipath(bool enable = true);

  /*
   * Enable RIB allocated path IDs for add-path. Call before createRib().
   */
  void enableRibAllocatedPathId(bool enable = true);

  /*
   * Enable legacy IPv4-unicast NLRI encoding for capability-less peers.
   * Call before createRib(). Pass true to set the thrift config
   * enable_legacy_v4_nlri_encoding = true so a capability-less peer receives
   * classic v4 NLRI + NEXT_HOP; pass false to leave it unset/disabled so every
   * peer keeps MP_REACH. No default -- the caller must state the gate state.
   */
  void enableLegacyV4NlriEncoding(bool enable);

  /*
   * Set the thrift config enable_dynamic_policy_evaluation. Call before
   * createRib(). When true, an ingress route filter or ingress policy change
   * re-evaluates the routes already learned from affected peers.
   */
  void enableDynamicPolicyEvaluation(bool enable);

  /*
   * Override the update group config used by createPeerManager().
   * Call before createPeerManager() to set slow peer thresholds, etc.
   */
  void setUpdateGroupConfig(const thrift::UpdateGroupConfig& cfg) {
    updateGroupConfigOverride_ = cfg;
  }

  /*
   * Override graceful_restart_convergence_seconds in the BgpConfig.
   */
  void setGrConvergenceSeconds(uint32_t seconds) {
    grConvergenceSecondsOverride_ = seconds;
  }

  /*
   * Override eor_time_s (the initialization EoR convergence timer) in the
   * BgpConfig. Call before getConfig() runs (i.e. before the peer manager is
   * created). Tests that bring a peer down before it ever sends an EoR
   * otherwise block for the full default timer waiting for initialization to
   * complete.
   */
  void setEorTimeSeconds(uint32_t seconds) {
    eorTimeSecondsOverride_ = seconds;
  }

  /*
   * Sets the peer's negotiated GR capability and remote restart time used
   * when establishing sessions. Call before bringUpPeer().
   */
  void setPeerGrRestartTimeSeconds(uint16_t seconds) {
    peerGrRestartTimeSeconds_ = seconds;
  }

  void setLocalClusterId(const folly::IPAddressV4& clusterId) {
    localClusterIdOverride_ = clusterId;
  }

  /* Helper to build config with peers from peers_ vector */
  std::shared_ptr<Config> getConfig(
      bool enableUpdateGroup = false,
      bool enableSerializeGroupPdu = false);

  // Message queues for RIB/PeerManagerBase communication
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ_{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<RibOutMessage> ribOutQ_;
  std::optional<MonitoredMPMCQueue<NeighborWatcherMessage>> nbrRouteChangeQ_ =
      std::make_optional<MonitoredMPMCQueue<NeighborWatcherMessage>>();

  // Configuration
  std::shared_ptr<Config> config_;
  std::shared_ptr<ConfigManager> configManager_;

  // Nexthop tracking configuration (shared between RIB and PeerManagerBase)
  bool enableNexthopTracking_ = false;

  // UCMP configuration: compute UCMP weights from LBW extended community
  bool enableComputeUcmpFromLbw_ = false;

  // eiBGP configuration: equalize eBGP and iBGP paths
  bool enableEiBgpMultipath_ = false;

  // RIB allocated path IDs for add-path
  bool enableRibAllocatedPathId_ = false;

  // Legacy v4 NLRI encoding for capability-less peers (thrift config gate)
  bool enableLegacyV4NlriEncoding_ = false;

  // Dynamic policy evaluation (thrift config gate)
  bool enableDynamicPolicyEvaluation_ = false;

  /*
   * Update group config override (call setUpdateGroupConfig before
   * createPeerManager to override slow peer thresholds etc.)
   */
  std::optional<thrift::UpdateGroupConfig> updateGroupConfigOverride_;

  /*
   * Parent confederation AS, set when any peer added via addPeer() supplies
   * BgpPeerSpec::localConfedAsn. Plumbed into
   * thriftConfig.local_confed_as_4_byte() in getConfig().
   */
  std::optional<uint32_t> pendingLocalConfedAsn_;

  /*
   * Peer-group names supplied via BgpPeerSpec::peerGroupName. Registered in
   * thriftConfig.peer_groups() by getConfig() so peer-group-level runtime
   * policy changes (BgpServiceBB::co_setPeerGroupsPolicy) can resolve them.
   */
  std::set<std::string> pendingPeerGroupNames_;

  // GR convergence override
  std::optional<uint32_t> grConvergenceSecondsOverride_;

  // Initialization EoR convergence timer (eor_time_s) override
  std::optional<uint32_t> eorTimeSecondsOverride_;

  // Per-peer GR restart time advertised in negotiated capabilities
  std::optional<uint16_t> peerGrRestartTimeSeconds_;

  /*
   * Some tests need a cluster ID distinct from the router ID before the
   * corresponding config schema is available. Normal fixture users retain
   * the cluster ID parsed into BgpGlobalConfig.
   */
  std::optional<folly::IPAddressV4> localClusterIdOverride_;

  /*
   * Owns this fixture's RIB policy files, and removes them when the fixture
   * dies. See createRib() for why the default paths cannot be used.
   */
  folly::test::TemporaryDirectory ribPolicyDir_{"bgp_e2e_rp"};

  // Dynamically added peers (via addPeer)
  std::vector<thrift::BgpPeer> peers_;

  // ADD-PATH capabilities per peer address (set via addPeer with BgpPeerSpec)
  std::unordered_map<
      folly::IPAddress,
      std::optional<nettools::bgplib::BgpAddPathSendRec>>
      peerAddPathCapabilities_;

  /*
   * Per-peer MP-EXT capability flag (set via addPeer with BgpPeerSpec). When
   * false, the peer's remote (pre-negotiation) MP-EXT bit is modeled as false,
   * driving legacyV4NlriEncoding in the update-group key.
   */
  std::unordered_map<folly::IPAddress, bool> peerMpExtCapable_;

  /* Route Refresh capability per peer address, set via addPeer(). */
  folly::F14FastSet<folly::IPAddress> peerRouteRefreshEnabled_;
  bool enableRouteRefreshForAllPeers_{false};

  // Dynamically added local routes (via addLocalRoute)
  std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork> localRoutes_;

  // Policy configuration (via setPolicyConfig or helper methods)
  std::optional<bgp_policy::BgpPolicies> policyConfig_;

  // Policy statement name to be set on peers (auto-set by policy helpers)
  std::optional<std::string> ingressPolicyName_;

  /*
   * Component instances. Typed as the platform-neutral RibBase; the concrete
   * RIB (TestRibT<RibBB> or TestRibT<RibDC>) is built by makeTestRib() and
   * selected at link time — see the "Where does my test go?" note above.
   */
  std::unique_ptr<RibBase> rib_;
  std::thread ribThread_;

  /*
   * Created on demand by getWatchdog(); only the tests that construct a
   * BgpServiceBase need one, and it must outlive that service.
   */
  std::unique_ptr<Watchdog> watchdog_;

  Watchdog& getWatchdog() {
    if (!watchdog_) {
      watchdog_ = std::make_unique<Watchdog>(config_);
    }
    return *watchdog_;
  }

  std::unique_ptr<PeerManagerBase> peerManager_;
  std::thread peerMgrThread_;

  std::shared_ptr<MockSessionManager> sessionManager_;
  std::shared_ptr<std::thread> sessionMgrThread_;

  // Per-peer message queues
  std::unordered_map<BgpPeerId, PeerQueues> peerQueues_;

  // Blocked peers (for testing backpressure scenarios)
  std::unordered_set<folly::IPAddress> blockedPeers_;

  /*
   * Per-peer view of the routes currently advertised to each peer, populated by
   * recordDrainedRoutes and keyed by peer id. Converges to each peer's
   * Adj-RIB-Out as UPDATEs are drained (announce sets, withdraw erases).
   */
  std::unordered_map<BgpPeerId, ReceivedRoutes> drainedRoutesByPeer_;

  // Default queue sizes for peers (can be overridden via setDefaultQueueSizes)
  int defaultQueueCapacity_{8};
  int defaultQueueHighWm_{6};
  int defaultQueueLowWm_{2};

  // Per-peer queue size overrides (set via setQueueSizeForPeer)
  struct QueueSizes {
    int capacity;
    int highWm;
    int lowWm;
  };
  std::unordered_map<folly::IPAddress, QueueSizes> perPeerQueueSizes_;

  // Resolve queue sizes for a peer: per-peer override if set, else defaults
  QueueSizes getQueueSizesForPeer(const folly::IPAddress& peerAddr) const {
    auto it = perPeerQueueSizes_.find(peerAddr);
    if (it != perPeerQueueSizes_.end()) {
      return it->second;
    }
    return {defaultQueueCapacity_, defaultQueueHighWm_, defaultQueueLowWm_};
  }
};

/*
 * E2ERibTestFixture
 * Common base class for RIB-focused E2E tests that use 3 peers (peer3, peer4,
 * peer5). Provides common SetUp and helper methods to reduce code duplication.
 */
class E2ERibTestFixture : public E2ETestFixture {
 protected:
  void SetUp() override {
    addPeer(kDefaultPeerSpec3);
    addPeer(kDefaultPeerSpec4);
    addPeer(kDefaultPeerSpec5);
    createRib();
    createPeerManager(/*enableUpdateGroup=*/false);
  }

  /*
   * Bring up all 3 peers (without EoR)
   */
  void bringUpAllPeers() {
    bringUpPeer(kPeerAddr3);
    bringUpPeer(kPeerAddr4);
    bringUpPeer(kPeerAddr5);
  }

  /*
   * Bring up all 3 peers and send EoR to all of them
   */
  void bringUpAllPeersWithEor() {
    bringUpAllPeers();
    sendEoRToAllPeers();
  }

  /*
   * Send EoR to all 3 peers and wait for confirmation
   */
  void sendEoRToAllPeers() {
    BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
    BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
    BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
    sendEoRToPeer(peerId3);
    sendEoRToPeer(peerId4);
    sendEoRToPeer(peerId5);
    EXPECT_TRUE(waitForEoR(peerId3));
    EXPECT_TRUE(waitForEoR(peerId4));
    EXPECT_TRUE(waitForEoR(peerId5));
  }
};

} // namespace bgp
} // namespace facebook
