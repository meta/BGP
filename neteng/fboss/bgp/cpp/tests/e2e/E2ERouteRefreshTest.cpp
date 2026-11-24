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

/*
 * E2E tests for Route Refresh (RFC 2918).
 *
 * These tests verify that when a peer sends a ROUTE_REFRESH_REQUEST message,
 * we re-announce all routes from our shadow RIB to that peer:
 * - Basic route refresh triggers re-announcement of routes
 * - Multiple routes are all re-announced after route refresh
 * - Route refresh from one peer only triggers re-announcement to that peer
 * - Route refresh honors the requested AFI (RFC 2918 §3): a v4 RR re-dumps
 *   only v4 prefixes; v6 prefixes stay quiet
 */

#include <gtest/gtest.h>

#include <fmt/format.h>
#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/tests/AdjRibOutUtils.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestFixture.h"

using namespace facebook::bgp;

namespace facebook::bgp {

class E2ERouteRefreshTest : public E2ETestFixture {
 protected:
  void SetUp() override {
    auto spec3 = kDefaultPeerSpec3;
    spec3.enableRouteRefresh = true;
    auto spec4 = kDefaultPeerSpec4;
    spec4.enableRouteRefresh = true;
    auto spec5 = kDefaultPeerSpec5;
    spec5.enableRouteRefresh = true;
    addPeer(spec3);
    addPeer(spec4);
    addPeer(spec5);
    createRib();
    createPeerManager(/*enableUpdateGroup=*/false);
  }

  void bringUpAllPeersWithEor() {
    bringUpPeer(kPeerAddr3);
    bringUpPeer(kPeerAddr4);
    bringUpPeer(kPeerAddr5);
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

/*
 * RFC 2918: a Route Refresh from peer X re-announces every shadow-RIB route
 * back to X. Guards that the walk emits the complete set, not just the first
 * matching entry.
 */
TEST_F(E2ERouteRefreshTest, MultipleRoutesAllReAnnounced) {
  bringUpAllPeersWithEor();

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001", "", 0, 100, 0);
  addRoute("v4", "20.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001", "", 0, 100, 0);
  addRoute("v4", "30.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001", "", 0, 100, 0);

  auto prefix1 = folly::IPAddress::createNetwork("10.0.0.0/8");
  auto prefix2 = folly::IPAddress::createNetwork("20.0.0.0/8");
  auto prefix3 = folly::IPAddress::createNetwork("30.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix1));
  ASSERT_TRUE(waitForRouteInShadowRib(prefix2));
  ASSERT_TRUE(waitForRouteInShadowRib(prefix3));

  /* Drain initial announcements */
  drainPeerQueueCompletely(peerId4);
  drainPeerQueueCompletely(peerId5);

  /* peer5 sends Route Refresh — we should re-announce all routes to peer5 */
  sendRouteRefreshToPeer(
      peerId5,
      nettools::bgplib::BgpUpdateAfi::AFI_IPv4,
      nettools::bgplib::BgpUpdateSafi::SAFI_UNICAST);

  /* All three routes should be re-announced to peer5 */
  EXPECT_TRUE(verifyRoutes(
      "v4",
      kPeerAddr5,
      {{"10.0.0.0", 8, "127.5.0.4", "", "", 0},
       {"20.0.0.0", 8, "127.5.0.4", "", "", 0},
       {"30.0.0.0", 8, "127.5.0.4", "", "", 0}}));
}

/*
 * A Route Refresh re-announces only to the requesting peer; every other
 * peer's queue must stay empty.
 */
TEST_F(E2ERouteRefreshTest, RouteRefreshTargetsOnlyRequestingPeer) {
  bringUpAllPeersWithEor();

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001", "", 0, 100, 0);

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));

  /* Drain initial announcements from all peers */
  drainPeerQueueCompletely(peerId3);
  drainPeerQueueCompletely(peerId4);
  drainPeerQueueCompletely(peerId5);

  /*
   * Send Route Refresh from peer4 (EBGP peer). The RIB dump triggered by
   * route refresh should re-announce routes only to peer4, not to other peers.
   */
  sendRouteRefreshToPeer(
      peerId4,
      nettools::bgplib::BgpUpdateAfi::AFI_IPv4,
      nettools::bgplib::BgpUpdateSafi::SAFI_UNICAST);

  /* peer4 should get the re-announced route */
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "10.0.0.0",
      8,
      kPeerAddr4,
      "127.5.0.3",
      "4200000001 65001",
      "",
      0,
      50));

  /*
   * peer5 should NOT get a re-announcement (route refresh was from peer4).
   * Drain peer5's queue and verify no route updates were received.
   */
  auto drained = drainPeerQueueCompletely(peerId5, 3, 10);
  EXPECT_EQ(drained, 0);
}

/*
 * Bounds duplicate emissions when a prefix is already pending on the peer's
 * changeList and the RR re-dump covers it too (the changeListTracker bypass
 * in processRibDumpReq).
 *
 * Up to 2 announcements are RFC 2918 §4 compliant: the natural incremental
 * emission, plus the re-dump's unconditional re-emit. Suppressing the latter
 * when the peer is in sync would break peer-side recovery, so it stays. More
 * than 2 means a third emission path is leaking.
 */
TEST_F(E2ERouteRefreshTest, RouteRefreshDoesNotDuplicateInflightChanges) {
  bringUpAllPeersWithEor();

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  /* Drain initial announcements so we count only the post-RR traffic */
  drainPeerQueueCompletely(peerId3);
  drainPeerQueueCompletely(peerId4);
  drainPeerQueueCompletely(peerId5);

  /* Pin peer5's outbound — next announce will sit on the changeList */
  blockPeer(kPeerAddr5);

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001", "", 0, 100, 0);
  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));

  /* RR while the prefix is still on the changeList for peer5 */
  sendRouteRefreshToPeer(
      peerId5,
      nettools::bgplib::BgpUpdateAfi::AFI_IPv4,
      nettools::bgplib::BgpUpdateSafi::SAFI_UNICAST);

  /* maxRetries=0 skips the built-in drain so we control the count below */
  unblockPeer(kPeerAddr5, /*maxRetries=*/0);

  /*
   * Count via drain rather than walking UPDATEs: tryReadUpdateFromQueue
   * skips EoR markers in its own loop, then blocks on pop() once only EoRs
   * remain.
   */
  auto counts = countPrefixOccurrencesAndDrain(
      peerId5, prefix, /*isV4=*/true, /*maxRetries=*/10, /*maxMessages=*/100);
  EXPECT_LE(counts.announceCount, 2u)
      << "Prefix 10.0.0.0/8 announced more than RFC 2918 strict re-emit + "
         "natural incremental allows. announceCount="
      << counts.announceCount;
  EXPECT_EQ(counts.withdrawCount, 0u)
      << "No WITHDRAW expected; got " << counts.withdrawCount;
}

/*
 * Phantom-route guard: a withdraw racing an RR re-dump must still leave the
 * peer holding no route for the prefix.
 *
 * Withdraw rather than re-update gives an unambiguous signature -- the shadow
 * RIB ends with no entry, so any surviving UPDATE is a phantom.
 *
 * Asserts the wire-final event, not just counts: one announce and one
 * withdraw give identical counts whichever landed last, and only the last
 * one decides whether the peer holds a phantom. Two drain passes separate
 * "delayed" from "permanently lost".
 */
TEST_F(E2ERouteRefreshTest, RouteRefreshFollowedByWithdrawLeavesNoPhantom) {
  bringUpAllPeersWithEor();

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  /* Step 1: deliver X with attrs A; drain so postAttr=A on peer5 */
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001", "", 0, 100, 0);
  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  drainPeerQueueCompletely(peerId3);
  drainPeerQueueCompletely(peerId4);
  drainPeerQueueCompletely(peerId5);

  /* Step 2: block peer5 */
  blockPeer(kPeerAddr5);

  /* Step 3: kick off RR — re-dump will walk shadow RIB while X is present */
  sendRouteRefreshToPeer(
      peerId5,
      nettools::bgplib::BgpUpdateAfi::AFI_IPv4,
      nettools::bgplib::BgpUpdateSafi::SAFI_UNICAST);

  /* Step 4: immediately after, withdraw X from peer3 — races against RR
   * but the final shadow RIB state must have no X */
  deleteRoute("v4", "10.0.0.0", 8, kPeerAddr3);

  unblockPeer(kPeerAddr5, /*maxRetries=*/0);

  /* First drain pass: immediately after unblock */
  auto countsFirst = countPrefixOccurrencesAndDrain(
      peerId5, prefix, /*isV4=*/true, /*maxRetries=*/10, /*maxMessages=*/100);

  /*
   * Second pass: if the WITHDRAW missed the first drain, re-drain after a
   * sleep. Arriving here means delivery delay; never arriving means it was
   * lost and peer5 holds the phantom until some external trigger fires.
   */
  /* NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for) */
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  auto countsSecond = countPrefixOccurrencesAndDrain(
      peerId5, prefix, /*isV4=*/true, /*maxRetries=*/10, /*maxMessages=*/100);

  XLOGF(
      INFO,
      "withdraw-race: pass1 announce={} withdraw={} total={}, "
      "pass2 announce={} withdraw={} total={}",
      countsFirst.announceCount,
      countsFirst.withdrawCount,
      countsFirst.totalMessages,
      countsSecond.announceCount,
      countsSecond.withdrawCount,
      countsSecond.totalMessages);

  size_t totalAnnounces =
      countsFirst.announceCount + countsSecond.announceCount;
  size_t totalWithdraws =
      countsFirst.withdrawCount + countsSecond.withdrawCount;
  EXPECT_LE(totalAnnounces, 1u)
      << "Bypass leaked extra UPDATEs of a withdrawn prefix across both "
         "drain passes. totalAnnounces="
      << totalAnnounces;
  EXPECT_EQ(totalWithdraws, 1u)
      << "WITHDRAW must reach peer5 across both drain passes. "
         "totalWithdraws="
      << totalWithdraws
      << ". 0 means the WITHDRAW was permanently lost and peer5 holds a "
         "phantom route the shadow RIB dropped.";

  /*
   * Wire-final state: the last event for the prefix across both passes must
   * be the withdraw. An announce landing after it leaves peer5 holding a
   * route the shadow RIB no longer has.
   */
  const auto finalEvent = countsSecond.lastEvent != PrefixEvent::None
      ? countsSecond.lastEvent
      : countsFirst.lastEvent;
  EXPECT_EQ(finalEvent, PrefixEvent::Withdraw)
      << "Wire-final event for 10.0.0.0/8 was an announce, so peer5 holds a "
         "phantom route the shadow RIB dropped.";
}

/*
 * A prefix added mid-re-dump must reach the wire exactly once, whichever way
 * the race lands: the RR walk misses it and the changeList emits alone, or
 * the walk catches it and the changeList entry is deduped.
 *
 * 0 means the changeList entry was lost and the peer never converges without
 * an external trigger; >1 means the RR window suppressed dedup. Two drain
 * passes separate loss from delay.
 */
TEST_F(E2ERouteRefreshTest, NewPrefixAddedMidRouteRefreshEmittedExactlyOnce) {
  bringUpAllPeersWithEor();

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  /* Step 1: deliver an unrelated existing prefix X so shadow RIB has
   * content for the RR walk to iterate over */
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001", "", 0, 100, 0);
  auto prefixX = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefixX));
  drainPeerQueueCompletely(peerId3);
  drainPeerQueueCompletely(peerId4);
  drainPeerQueueCompletely(peerId5);

  /* Step 2: block peer5 */
  blockPeer(kPeerAddr5);

  /* Step 3: kick off RR */
  sendRouteRefreshToPeer(
      peerId5,
      nettools::bgplib::BgpUpdateAfi::AFI_IPv4,
      nettools::bgplib::BgpUpdateSafi::SAFI_UNICAST);

  /* Step 4: immediately after, add a NEW prefix Y from peer3.
   * Races against RR's shadow RIB walk. */
  addRoute("v4", "20.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001", "", 0, 100, 0);
  auto prefixY = folly::IPAddress::createNetwork("20.0.0.0/8");

  unblockPeer(kPeerAddr5, /*maxRetries=*/0);

  /* First drain pass: immediately after unblock */
  auto countsYFirst = countPrefixOccurrencesAndDrain(
      peerId5, prefixY, /*isV4=*/true, /*maxRetries=*/10, /*maxMessages=*/100);

  /*
   * Recovery probe: re-add the route again to verify whether the
   * suppressed Y will ever reach peer5 — sleep generously, then
   * re-drain. If Y arrives in this second pass, RR-window suppression
   * is "delayed" but recoverable. If Y still doesn't arrive, the
   * changeList entry was permanently lost and only an explicit
   * external trigger (e.g. another RR, a re-add) would surface Y.
   */
  /* NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for) */
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  auto countsYSecond = countPrefixOccurrencesAndDrain(
      peerId5, prefixY, /*isV4=*/true, /*maxRetries=*/10, /*maxMessages=*/100);

  XLOGF(
      INFO,
      "new-prefix: pass1 announce={} total={}, pass2 announce={} total={}",
      countsYFirst.announceCount,
      countsYFirst.totalMessages,
      countsYSecond.announceCount,
      countsYSecond.totalMessages);

  size_t totalAnnouncements =
      countsYFirst.announceCount + countsYSecond.announceCount;
  EXPECT_EQ(totalAnnouncements, 1u)
      << "New prefix Y (20.0.0.0/8) added mid-RR must be announced exactly "
         "once on peer5's wire across both drain passes. firstPass="
      << countsYFirst.announceCount
      << " secondPass=" << countsYSecond.announceCount
      << ". 0 means Y was permanently lost — peer never converges with the "
         "shadow RIB until an external trigger fires. > 1 means duplicated.";
  EXPECT_EQ(countsYFirst.withdrawCount + countsYSecond.withdrawCount, 0u)
      << "Y was never withdrawn";
}

/*
 * Wire-final-attrs guard: with the prefix updated A -> B while the peer is
 * blocked, the LAST emission must carry B.
 *
 * Both the RR walk and changeList processing read the shadow RIB at emit
 * time, so no emission should carry stale A. An intermediate stale emission
 * is tolerable if a latest-attrs one follows -- last-write-wins leaves the
 * peer correct; a stale emission landing last does not.
 *
 * asPath is the A-vs-B discriminator (nexthop is rewritten to the local
 * egress address either way). peer5 is EBGP, so the local AS is prepended:
 * the expected sequence is "4200000001 65002".
 */
TEST_F(E2ERouteRefreshTest, RouteRefreshPreservesLatestAttrsOnInflightChange) {
  bringUpAllPeersWithEor();

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  /* Step 1: deliver X with attrs A; drain so postAttr=A on peer5 */
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001", "", 0, 100, 0);
  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  drainPeerQueueCompletely(peerId3);
  drainPeerQueueCompletely(peerId4);
  drainPeerQueueCompletely(peerId5);

  /* Step 2: block peer5 */
  blockPeer(kPeerAddr5);

  /* Step 3: update X to attrs B (different asPath + nexthop). Short
   * settle wait so the B-update propagates through peer3's AdjRibIn →
   * bestpath → shadow RIB BEFORE RR walks; otherwise RR may snapshot
   * shadow RIB while it still has A, which would test a different
   * scenario (RR-races-update, not RR-after-update-staged). */
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.2", "65002", "", 0, 200, 0);
  /* NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for) */
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  /* Step 4: RR while the B-update is pending on the changeList */
  sendRouteRefreshToPeer(
      peerId5,
      nettools::bgplib::BgpUpdateAfi::AFI_IPv4,
      nettools::bgplib::BgpUpdateSafi::SAFI_UNICAST);

  unblockPeer(kPeerAddr5, /*maxRetries=*/0);

  /*
   * Drain twice (with sleep between) to capture both the immediate RR
   * emissions AND any delayed post-RR changeList emissions. Concatenate
   * UPDATEs for inspection.
   */
  auto updates = drainPeerQueueAndCollectUpdates(
      peerId5, /*maxRetries=*/10, /*maxMessages=*/100);
  /* NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for) */
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  auto delayed = drainPeerQueueAndCollectUpdates(
      peerId5, /*maxRetries=*/10, /*maxMessages=*/100);
  updates.insert(updates.end(), delayed.begin(), delayed.end());

  /* Filter to UPDATEs that announce our prefix */
  std::vector<std::shared_ptr<const nettools::bgplib::BgpUpdate2>>
      updatesForPrefix;
  for (const auto& u : updates) {
    if (findPrefixInAnnouncements(*u, /*isV4=*/true, prefix, /*addPathId=*/0)) {
      updatesForPrefix.push_back(u);
    }
  }
  ASSERT_FALSE(updatesForPrefix.empty())
      << "No announcement for 10.0.0.0/8 reached peer5 after RR";

  /* Diagnostic: log each emission's asPath so failures show the actual
   * wire-observed sequence instead of just "stale". */
  for (size_t i = 0; i < updatesForPrefix.size(); ++i) {
    std::string asPathStr;
    if (!updatesForPrefix[i]->attrs()->asPath()->empty()) {
      for (const auto& seg : updatesForPrefix[i]->attrs()->asPath().value()) {
        if (!seg.asSequence()->empty()) {
          for (auto asn : seg.asSequence().value()) {
            asPathStr += fmt::format("{} ", asn);
          }
        }
      }
    }
    XLOGF(INFO, "emission #{} for 10.0.0.0/8: asPath=[{}]", i, asPathStr);
  }

  EXPECT_TRUE(verifyRouteAttributes(
      *updatesForPrefix.back(),
      /*expectedNexthop=*/"127.5.0.4",
      /*expectedAsPath=*/"4200000001 65002",
      /*expectedCommunity=*/""))
      << "Wire-final emission for 10.0.0.0/8 carries stale attrs: a stale "
         "emission landed after the latest, so peer5 ends with the wrong "
         "route despite the shadow RIB having moved to attrs B.";
}

/*
 * Phantom guard for the case where the delete lands BEFORE the RR snapshot.
 *
 * Distinct from RouteRefreshFollowedByWithdrawLeavesNoPhantom: there the
 * delete follows the RR, so an explicit withdrawal is generated and can
 * retract a stale UPDATE. Here the shadow RIB has already dropped the prefix
 * when the walk runs, so the dump omits it -- and if nothing emits a
 * withdrawal, an UPDATE already sitting in the peer's outbound queue has
 * nothing behind it to retract it.
 *
 * Queued UPDATEs are materialized snapshots: blocking a peer stops the test
 * reading its queue, not the DUT producing into it, so the shadow-RIB-is-
 * source-of-truth argument does not reach messages already emitted.
 *
 * Passing means the delete still produces a withdrawal for the peer despite
 * the RR dump omitting the prefix. Failing means peer5 holds a route the
 * shadow RIB dropped, with nothing scheduled to retract it.
 */
TEST_F(E2ERouteRefreshTest, RouteRefreshAfterDeleteLeavesNoPhantom) {
  bringUpAllPeersWithEor();

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  /* X advertised and drained, so peer5 has it and postAttr is set. */
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001", "", 0, 100, 0);
  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  drainPeerQueueCompletely(peerId3);
  drainPeerQueueCompletely(peerId4);
  drainPeerQueueCompletely(peerId5);

  blockPeer(kPeerAddr5);

  /* Re-advertise X so an UPDATE queues up unread behind the block. */
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.2", "65002", "", 0, 200, 0);
  /* NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for) */
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  /*
   * Delete X and let it settle BEFORE the RR, so the walk sees no entry for
   * it. This is what separates this test from the withdraw-after-RR case.
   */
  deleteRoute("v4", "10.0.0.0", 8, kPeerAddr3);
  /* NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for) */
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  sendRouteRefreshToPeer(
      peerId5,
      nettools::bgplib::BgpUpdateAfi::AFI_IPv4,
      nettools::bgplib::BgpUpdateSafi::SAFI_UNICAST);

  unblockPeer(kPeerAddr5, /*maxRetries=*/0);

  auto countsFirst = countPrefixOccurrencesAndDrain(
      peerId5, prefix, /*isV4=*/true, /*maxRetries=*/10, /*maxMessages=*/100);
  /* NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for) */
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  auto countsSecond = countPrefixOccurrencesAndDrain(
      peerId5, prefix, /*isV4=*/true, /*maxRetries=*/10, /*maxMessages=*/100);

  XLOGF(
      INFO,
      "delete-before-rr: pass1 announce={} withdraw={} total={}, "
      "pass2 announce={} withdraw={} total={}",
      countsFirst.announceCount,
      countsFirst.withdrawCount,
      countsFirst.totalMessages,
      countsSecond.announceCount,
      countsSecond.withdrawCount,
      countsSecond.totalMessages);

  /*
   * peer5 installed X from the pre-block drain, so a withdrawal is mandatory,
   * not merely permitted -- silence would leave the route installed forever.
   */
  const auto finalEvent = countsSecond.lastEvent != PrefixEvent::None
      ? countsSecond.lastEvent
      : countsFirst.lastEvent;
  EXPECT_EQ(finalEvent, PrefixEvent::Withdraw)
      << "peer5 installed 10.0.0.0/8 before the block, so the pre-RR delete "
         "must end in a withdrawal. Anything else leaves a phantom route with "
         "nothing scheduled to retract it.";
}

/*
 * RFC 2918 §3: a Route Refresh names an <AFI, SAFI>, and the re-dump must
 * honor it -- a v4 RR must not re-advertise v6 routes, or vice-versa.
 */
TEST_F(E2ERouteRefreshTest, RouteRefreshHonorsRequestedAfi) {
  bringUpAllPeersWithEor();

  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  /* One v4 + one v6 route, both learnt from peer3, both should reach peer5
   * during the initial dump. */
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001", "", 0, 100, 0);
  addRoute("v6", "2001:db8::", 32, kPeerAddr3, "2001:db8::1", "65001");

  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("10.0.0.0/8")));
  ASSERT_TRUE(waitForRouteInShadowRib(
      folly::IPAddress::createNetwork("2001:db8::/32")));

  /* Drain the initial announcements so the queue holds only post-RR output. */
  drainPeerQueueCompletely(peerId4);
  drainPeerQueueCompletely(peerId5);

  /* peer5 sends Route Refresh for v4 ONLY. Per RFC 2918 §3 we should
   * re-announce the v4 route and stay silent on v6. */
  sendRouteRefreshToPeer(
      peerId5,
      nettools::bgplib::BgpUpdateAfi::AFI_IPv4,
      nettools::bgplib::BgpUpdateSafi::SAFI_UNICAST);

  /* v4 route MUST come back. */
  EXPECT_TRUE(verifyRouteAdd(
      "v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4", "", "", 0, 50));

  /* Only the requested v4 AFI's EoR should remain. */
  auto drained = drainPeerQueueCompletely(peerId5, 3, 10);
  EXPECT_EQ(drained, 1);

  /* peer4 did not request RR; its queue must remain empty. Anything here
   * means peer5's RR re-dump leaked to peer4. */
  auto peer4Drained = drainPeerQueueCompletely(peerId4, 3, 10);
  EXPECT_EQ(peer4Drained, 0)
      << "peer4 received messages but did not request RR";
}

/*
 * Mirror of RouteRefreshHonorsRequestedAfi in the v6 direction. Catches
 * asymmetry in the filter: an inverted wantV4, a bad AFI enum mapping, or a
 * broken prefix-family check.
 */
TEST_F(E2ERouteRefreshTest, RouteRefreshHonorsRequestedAfiV6) {
  bringUpAllPeersWithEor();

  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001", "", 0, 100, 0);
  addRoute("v6", "2001:db8::", 32, kPeerAddr3, "2001:db8::1", "65001");

  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("10.0.0.0/8")));
  ASSERT_TRUE(waitForRouteInShadowRib(
      folly::IPAddress::createNetwork("2001:db8::/32")));

  drainPeerQueueCompletely(peerId4);
  drainPeerQueueCompletely(peerId5);

  /* peer5 sends Route Refresh for v6 ONLY. We should re-announce the v6
   * route and stay silent on v4. */
  sendRouteRefreshToPeer(
      peerId5,
      nettools::bgplib::BgpUpdateAfi::AFI_IPv6,
      nettools::bgplib::BgpUpdateSafi::SAFI_UNICAST);

  /* v6 route MUST come back. */
  EXPECT_TRUE(verifyRouteAdd(
      "v6",
      "2001:db8::",
      32,
      kPeerAddr5,
      "2401:db00:e011:411:1000::2d",
      "",
      "",
      0,
      50));

  /* Only the requested v6 AFI's EoR should remain. */
  auto drained = drainPeerQueueCompletely(peerId5, 3, 10);
  EXPECT_EQ(drained, 1);

  /* peer4 did not request RR; its queue must remain empty. Anything here
   * means peer5's RR re-dump leaked to peer4. */
  auto peer4Drained = drainPeerQueueCompletely(peerId4, 3, 10);
  EXPECT_EQ(peer4Drained, 0)
      << "peer4 received messages but did not request RR";
}

} // namespace facebook::bgp
