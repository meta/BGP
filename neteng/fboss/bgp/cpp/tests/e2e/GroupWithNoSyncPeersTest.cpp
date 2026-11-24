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
 * E2E tests: update group left with NO IN-SYNC PEERS, and the peer PROMOTION
 * that recovers it.
 *
 * unregisterPeer -> recoverIfNoSyncPeers() -> handleNoSyncPeers() clears the
 * packing list and cancels the consume timer (the group is "frozen"), then
 * tries to promote a detached member: DFP accepted directly, DSP via
 * tryAcceptPeersToGroup collapse; else stay frozen while a peer that detached
 * AFTER joining still shares the group's entries; else
 * promoteDetachedPeerToSync() on the first DEP-A, so its change-list position
 * and RIB-OUT become the group's truth; else stay frozen until a peer
 * self-promotes at the end of its own drain.
 *
 * Only the END STATE is asserted: every peer ends with every RIB route in its
 * RIB-OUT and on the wire, carrying the latest attributes. Each publish is a
 * numbered "round" that bumps LOCAL_PREF by 100 and stamps a matching community
 * (65501:100, ...); assertions key off the community because these peers are
 * eBGP, where LOCAL_PREF is not propagated. The decision tree is already unit
 * tested; what these add is end-to-end proof that a promoted peer becomes the
 * group's sync peer and every survivor converges. Prefix range 36.0.x.0/24.
 */

#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupPolicyReEvalE2ECommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

class GroupWithNoSyncPeersE2ETest : public UpdateGroupPolicyReEvalE2EBase {
 protected:
  /*
   * Assert a set of peers matches an egress policy on BOTH sides: the RIB-OUT
   * and the routes they actually received. Used by the multi-group cases to
   * check that the operated group moved to the new policy while the untouched
   * groups stayed on permit-all.
   */
  void expectPolicyOnPeers(
      const std::vector<BgpPeerId>& peerIds,
      const std::string& policyName) {
    for (const auto& peerId : peerIds) {
      waitForRibOutAdvertisedCount(peerId.peerAddr, kNumRoutes);
      expectRibOutForPolicy(peerId.peerAddr, policyName, kNumRoutes);
      expectReceivedRoutesForPolicy(peerId, policyName, injectedPrefixes_);
    }
  }

  /* Every peer outside the given group index. */
  static std::vector<BgpPeerId> peersOutsideGroup(
      const std::vector<std::vector<BgpPeerId>>& groups,
      size_t skipGroupIdx) {
    std::vector<BgpPeerId> others;
    for (size_t g = 0; g < groups.size(); ++g) {
      if (g == skipGroupIdx) {
        continue;
      }
      others.insert(others.end(), groups[g].begin(), groups[g].end());
    }
    return others;
  }

  /* Drain every peer in every group once. */
  void drainAllGroups(const std::vector<std::vector<BgpPeerId>>& groups) {
    for (const auto& group : groups) {
      drainAll(group);
    }
  }

  /*
   * Add numPeers peers to one peer group and build the components, but bring up
   * only the first numInitial of them. The rest are brought up later with
   * bringUpLatePeer(): because the group is already initialized by then, a late
   * joiner registers as DETACHED_INIT_DUMP with DETACHED_ON_REGISTRATION and
   * detachedRibVersion == 0 -- i.e. a DEP-A, which is what the
   * promoteDetachedPeerToSync path requires (it only runs when no peer detached
   * after joining).
   */
  std::vector<BgpPeerId> setupGroupDeferredJoin(
      int numPeers,
      int numInitial,
      int queueCapacity,
      int queueHighWm,
      int queueLowWm) {
    const auto allSpecs = allPeerSpecs();
    EXPECT_LE(numPeers, static_cast<int>(allSpecs.size()));
    std::vector<BgpPeerId> peerIds;
    for (int i = 0; i < numPeers; ++i) {
      auto spec = allSpecs[i];
      spec.peerGroupName = kReEvalPeerGroupName;
      addPeer(spec);
      peerIds.emplace_back(spec.peerAddr, spec.peerAddr.asV4().toLongHBO());
    }
    setupComponentsWithBgpService(
        queueCapacity, queueHighWm, queueLowWm, /*enableUpdateGroup=*/true);
    for (int i = 0; i < numInitial; ++i) {
      bringUpPeer(peerIds[i].peerAddr);
      sendEoRToPeer(peerIds[i]);
      /* Dual-stack peers send two EoRs. */
      EXPECT_TRUE(waitForEoR(peerIds[i]));
      EXPECT_TRUE(waitForEoR(peerIds[i]));
      EXPECT_TRUE(waitForPeerState(
          peerIds[i].peerAddr, PeerUpdateState::JOINED_RUNNING));
    }
    return peerIds;
  }

  /*
   * Publish the whole route set again as the next "round", returning the round
   * community to verify against. LOCAL_PREF alone is invisible to these eBGP
   * peers, so bumping it produces no re-advertisement at all; the round
   * community both drives the re-advertisement and marks the round.
   */
  std::string publishNextRound(int count = kNumRoutes, int numAttrBuckets = 0) {
    nextLocalPref_ += 100;
    const auto roundComm = fmt::format("{}:{}", kRoundCommAsn, nextLocalPref_);
    /*
     * Per-route community combos, as in injectCommunityTaggedRoutes: they pack
     * the round into several attribute groups. A single uniform UPDATE would
     * never fill a tiny peer queue, so no peer would block or detach.
     */
    std::map<std::vector<std::string>, std::vector<std::string>>
        comboToPrefixes;
    injectedPrefixes_.clear();
    for (int i = 0; i < count; ++i) {
      std::vector<std::string> communities{roundComm};
      if (numAttrBuckets > 0) {
        /*
         * Spread the round over numAttrBuckets attribute groups instead of ~4,
         * so a test can block a peer PART WAY through a round.
         */
        communities.push_back(
            fmt::format("{}:{}", kBucketCommAsn, i % numAttrBuckets));
      } else {
        if (i % 2 == 1) {
          communities.emplace_back(kCommNoAdvt);
        }
        if (i % 3 == 0) {
          communities.emplace_back(kCommModify);
        }
      }
      const auto prefix = fmt::format("36.0.{}.0/24", i);
      comboToPrefixes[communities].push_back(prefix);
      injectedPrefixes_.push_back(folly::IPAddress::createNetwork(prefix));
    }
    for (const auto& [communities, prefixes] : comboToPrefixes) {
      injectLocalRoutesAtRuntime(prefixes, communities, nextLocalPref_);
    }
    EXPECT_TRUE(waitForRouteInShadowRib(injectedPrefixes_.back()))
        << "published routes did not reach the shadow RIB";
    return roundComm;
  }

  /*
   * RIB-OUT predicate: entry advertised and carrying the given community. Not
   * verifyCommOnAdvertisedRoute, which EXPECT_TRUEs on every non-matching entry
   * and so records a failure per prefix per retry inside a polling loop.
   */
  static std::function<bool(const AdjRibEntry&, const folly::CIDRNetwork&)>
  advertisedWithCommunity(const std::string& community) {
    const auto expected =
        *nettools::bgplib::BgpAttrCommunityC::createBgpAttrCommunity(community);
    return [expected](
               const AdjRibEntry& entry, const folly::CIDRNetwork&) -> bool {
      const auto postAttr = entry.getPostAttr();
      if (!postAttr) {
        return false;
      }
      for (const auto& comm : postAttr->getCommunities().get()) {
        if (comm == expected) {
          return true;
        }
      }
      return false;
    };
  }

  /*
   * Poll until the peer's RIB-OUT advertises every injected prefix on the given
   * round. Promotion and rejoin are async, so the end state is awaited.
   */
  void expectRibOutConverged(
      const folly::IPAddress& peerAddr,
      const std::string& roundComm) {
    const auto expectedCount = injectedPrefixes_.size();
    /* Short gaps: WITH_RETRIES_N's 1s default would blow the test timeout at
     * this peer count whenever a peer fails to converge. */
    WITH_RETRIES_N_TIMED(40, std::chrono::milliseconds(100), {
      EXPECT_EVENTUALLY_EQ(
          verifyRibOutEntries(
              peerAddr,
              [](int) { return true; },
              advertisedWithCommunity(roundComm)),
          expectedCount);
    });
  }

  /* Assert the peer received every injected prefix on the wire, on the given
   * round (i.e. it is not left on a stale one). */
  void expectReceivedRoutesConverged(
      const BgpPeerId& peerId,
      const std::string& roundComm) {
    const auto& received = receivedRoutes(peerId);
    for (const auto& prefix : injectedPrefixes_) {
      const auto prefixStr = folly::IPAddress::networkToString(prefix);
      const auto it = received.find(prefix);
      if (it == received.end()) {
        ADD_FAILURE() << "peer " << peerId.peerAddr.str() << " never received "
                      << prefixStr;
        continue;
      }
      EXPECT_TRUE(attrsHaveCommunity(it->second, roundComm))
          << "peer " << peerId.peerAddr.str() << " " << prefixStr
          << " is on a stale round (missing community " << roundComm << ")";
    }
  }

  /* Non-asserting RIB-OUT check, for polling a convergence loop. */
  bool ribOutMatchesRound(
      const folly::IPAddress& peerAddr,
      const std::string& roundComm) {
    return verifyRibOutEntries(
               peerAddr,
               [](int) { return true; },
               advertisedWithCommunity(roundComm)) == injectedPrefixes_.size();
  }

  /*
   * Drain each peer repeatedly while waiting for JOINED_RUNNING, then assert
   * RIB-OUT and wire convergence. A catching-up peer must drain before it can
   * rejoin, and a DEP-A is served by its OWN dump, so deliveries land at any
   * point during recovery and must be recorded rather than discarded.
   */
  void expectAllPeersConverged(
      const std::vector<BgpPeerId>& peerIds,
      const std::string& roundComm) {
    drainUntil(
        peerIds,
        [&]() {
          for (const auto& peerId : peerIds) {
            if (getPeerState(peerId.peerAddr) !=
                    PeerUpdateState::JOINED_RUNNING ||
                !ribOutMatchesRound(peerId.peerAddr, roundComm)) {
              return false;
            }
          }
          return true;
        },
        "peers never converged on the expected round",
        60);
    /* Final drain so late deliveries are recorded before verifying. */
    for (const auto& peerId : peerIds) {
      recordDrainedRoutes(peerId);
    }
    for (const auto& peerId : peerIds) {
      EXPECT_TRUE(
          waitForPeerState(peerId.peerAddr, PeerUpdateState::JOINED_RUNNING))
          << "peer " << peerId.peerAddr.str()
          << " did not reach JOINED_RUNNING";
      EXPECT_TRUE(isPeerInSync(peerId.peerAddr))
          << "peer " << peerId.peerAddr.str() << " is not in sync";
      expectRibOutConverged(peerId.peerAddr, roundComm);
      expectReceivedRoutesConverged(peerId, roundComm);
    }
  }

  /* In-sync peer count of the update group the peer belongs to. */
  size_t getNumInSyncPeers(const folly::IPAddress& peerAddr) {
    auto& evb = peerManager_->getEventBase();
    return folly::via(
               &evb,
               [this, peerAddr]() {
                 auto group = getUpdateGroupForPeer(peerAddr);
                 return group ? group->getNumInSyncPeers() : 0;
               })
        .get();
  }

  /* The group's cached change-list position. */
  uint64_t getGroupRibVersion(const folly::IPAddress& peerAddr) {
    auto& evb = peerManager_->getEventBase();
    return folly::via(
               &evb,
               [this, peerAddr]() {
                 auto group = getUpdateGroupForPeer(peerAddr);
                 return group ? group->getLastSeenRibVersion() : 0;
               })
        .get();
  }

  /*
   * The shadow RIB's own version. Group and peer cached versions are stamped
   * from this counter, not from RibBase::getRibVersion(), so this is what a
   * group's position must be compared against.
   */
  uint64_t getRibVersion() {
    auto& evb = peerManager_->getEventBase();
    auto* vrf = peerManager_->findVrf(fboss::kDefaultVrf);
    if (!vrf) {
      XLOGF(ERR, "Non-existent VRF {}", fboss::kDefaultVrf);
      return 0;
    }
    return folly::via(&evb, [vrf]() { return vrf->getMaxRibVersion(); }).get();
  }

  /* Bring a set of peers down, one at a time. */
  void bringDownPeers(const std::vector<BgpPeerId>& peerIds) {
    for (const auto& peerId : peerIds) {
      bringDownPeer(peerId.peerAddr);
    }
  }

  /*
   * Detach a blocked peer via the production slow-peer path without waiting for
   * the group-wide threshold. detachSlowPeer is the same call the detection
   * makes; this just lets the test choose WHICH blocked peer detaches.
   */
  void detachBlockedPeerDirectly(const folly::IPAddress& peerAddr) {
    auto group = getUpdateGroupForPeer(peerAddr);
    ASSERT_NE(group, nullptr);
    auto adjRib = getAdjRibByAddr(peerAddr);
    ASSERT_NE(adjRib, nullptr);
    peerManager_->getEventBase().runInEventBaseThreadAndWait(
        [&group, &adjRib]() { group->detachSlowPeer(adjRib); });
  }

  /* True when every listed peer is in the given state. */
  bool allPeersInState(
      const std::vector<BgpPeerId>& peerIds,
      PeerUpdateState state) {
    for (const auto& peerId : peerIds) {
      if (getPeerState(peerId.peerAddr) != state) {
        return false;
      }
    }
    return true;
  }

  /* True if the peer is flagged IS_DETACHED_FAST_PEER: it finished draining
   * before the group moved, so it can rejoin without collapse. */
  bool isDetachedFastPeer(const folly::IPAddress& peerAddr) {
    auto& evb = peerManager_->getEventBase();
    return folly::via(
               &evb,
               [this, peerAddr]() -> bool {
                 auto adjRib = getAdjRibByAddr(peerAddr);
                 return adjRib &&
                     adjRib->isAdjRibFlagSet(AdjRib::IS_DETACHED_FAST_PEER);
               })
        .get();
  }

  /*
   * True if the peer registered into an ALREADY INITIALIZED group.
   * AdjRibGroup::registerPeer only sets DETACHED_ON_REGISTRATION when the group
   * is not UNINITIALIZED; that plus a zero detach version makes a peer a DEP-A.
   */
  bool isDetachedOnRegistration(const folly::IPAddress& peerAddr) {
    auto& evb = peerManager_->getEventBase();
    return folly::via(
               &evb,
               [this, peerAddr]() -> bool {
                 auto adjRib = getAdjRibByAddr(peerAddr);
                 return adjRib &&
                     adjRib->isAdjRibFlagSet(AdjRib::DETACHED_ON_REGISTRATION);
               })
        .get();
  }

  /* The peer's frozen detach version; 0 for a peer that never detached. */
  uint64_t getPeerDetachedRibVersion(const folly::IPAddress& peerAddr) {
    auto& evb = peerManager_->getEventBase();
    return folly::via(
               &evb,
               [this, peerAddr]() -> uint64_t {
                 auto adjRib = getAdjRibByAddr(peerAddr);
                 return adjRib ? adjRib->getDetachedRibVersion() : 0;
               })
        .get();
  }

  /* The peer's own change-list position (the group's, if it is in sync). */
  uint64_t getPeerLastSeenRibVersion(const folly::IPAddress& peerAddr) {
    auto& evb = peerManager_->getEventBase();
    return folly::via(
               &evb,
               [this, peerAddr]() -> uint64_t {
                 auto adjRib = getAdjRibByAddr(peerAddr);
                 return adjRib ? adjRib->getLastSeenRibVersion() : 0;
               })
        .get();
  }

  /*
   * Assert every listed peer sits strictly AHEAD of the group on the change
   * list. transitionPeerUpdateState parks an ahead-of-group peer and a
   * caught-up DSP in the SAME state, and adopting a peer's position in
   * promoteDetachedPeerToSync is vacuous if that peer is merely level.
   */
  void expectAllAheadOfGroup(const std::vector<BgpPeerId>& peerIds) {
    const auto groupVersion = getGroupRibVersion(peerIds[0].peerAddr);
    for (const auto& peerId : peerIds) {
      EXPECT_GT(getPeerLastSeenRibVersion(peerId.peerAddr), groupVersion)
          << "peer " << peerId.peerAddr.str()
          << " is not ahead of the group (group at " << groupVersion
          << "), so it took the DSP branch rather than the DEP-A one";
    }
  }

  /* The group's count of members that detached AFTER joining (DEP-B). */
  size_t getNumPeersDetachedAfterJoin(const folly::IPAddress& peerAddr) {
    auto& evb = peerManager_->getEventBase();
    return folly::via(
               &evb,
               [this, peerAddr]() {
                 auto group = getUpdateGroupForPeer(peerAddr);
                 return group ? group->getNumPeersDetachedAfterJoin() : 0;
               })
        .get();
  }

  /*
   * Assert every listed peer really is a DEP-A. State cannot answer this -- a
   * DEP-A and a DSP both park in DETACHED_READY_TO_JOIN -- so check the
   * registration flag and zero detach version. numPeersDetachedAfterJoin_ must
   * be 0 too: handleNoSyncPeers checks it before promoting at all.
   */
  void expectAllDepA(const std::vector<BgpPeerId>& peerIds) {
    for (const auto& peerId : peerIds) {
      EXPECT_TRUE(isDetachedOnRegistration(peerId.peerAddr))
          << "peer " << peerId.peerAddr.str()
          << " is not DETACHED_ON_REGISTRATION, so it is not a DEP-A";
      EXPECT_EQ(getPeerDetachedRibVersion(peerId.peerAddr), 0u)
          << "peer " << peerId.peerAddr.str()
          << " has a non-zero detach version, so it detached after joining";
    }
    EXPECT_EQ(getNumPeersDetachedAfterJoin(peerIds[0].peerAddr), 0u)
        << "a member detached after joining, so handleNoSyncPeers will stay "
           "frozen instead of taking the DEP-A promotion branch";
  }

  /*
   * Leave the group with one JOINED_BLOCKED member and every other member
   * re-registered as a DEP-A.
   *
   * Bringing a peer up "late" is not enough: where its own dump lands decides
   * the branch transitionPeerUpdateState takes -- level with the group makes it
   * a DSP, past the group a DEP-A. Flapping EXISTING members of a stalled group
   * removes that ambiguity. Queue reads are disabled for every member, but the
   * callers give stallPeer a lower watermark so a serialized send suspends on
   * it deterministically; the peers to convert are then dropped, extra rounds
   * run the RIB past the group, and they come back as DEP-A. stallPeer stays
   * blocked as the group's only sync peer.
   */
  void flapPeersIntoDepA(
      const BgpPeerId& stallPeer,
      const std::vector<BgpPeerId>& toFlap,
      const std::vector<BgpPeerId>& returnBlocked,
      int extraRounds,
      std::string* roundCommOut,
      const std::vector<BgpPeerId>& drainDuring = {},
      bool singleEoR = false) {
    std::vector<BgpPeerId> allMembers{stallPeer};
    allMembers.insert(allMembers.end(), toFlap.begin(), toFlap.end());

    /*
     * Slow-peer detection off for every member: one that auto-detaches while
     * blocked detaches AFTER joining, making it a DEP-B and leaving
     * numPeersDetachedAfterJoin_ non-zero, which blocks all promotion.
     */
    for (const auto& peerId : allMembers) {
      setSlowPeerThresholds(
          peerId.peerAddr,
          std::chrono::milliseconds(600000),
          1000000,
          std::chrono::milliseconds(600000));
      blockPeer(peerId.peerAddr);
    }

    /*
     * A serialized group send suspends when the lower-watermark stallPeer
     * blocks. Without serialization, each peer producer progresses
     * independently, so every member must block before the peers are flapped;
     * otherwise the completed producers can accept returning DEP-As early.
     */
    *roundCommOut = publishNextRound();
    if (GetParam().enableSerializeGroupPdu) {
      ASSERT_TRUE(waitForPeerQueueBlocked(stallPeer))
          << "stall peer " << stallPeer.peerAddr.str()
          << " never filled its queue";
    } else {
      for (const auto& peerId : allMembers) {
        ASSERT_TRUE(waitForPeerQueueBlocked(peerId))
            << "member " << peerId.peerAddr.str() << " never filled its queue";
      }
    }
    ASSERT_TRUE(
        waitForPeerState(stallPeer.peerAddr, PeerUpdateState::JOINED_BLOCKED));
    const auto stalledVersion = getGroupRibVersion(stallPeer.peerAddr);

    /* Drop the members that are to come back as DEP-A. */
    for (const auto& peerId : toFlap) {
      bringDownPeer(peerId.peerAddr);
    }

    /* Run the RIB past the group: it cannot consume these rounds while its
     * send is suspended on stallPeer. */
    for (int i = 0; i < extraRounds; ++i) {
      *roundCommOut = publishNextRound();
      drainAllFast(drainDuring);
    }
    drainAll(drainDuring);
    peerManager_->getEventBase().runInEventBaseThreadAndWait([]() {});
    /*
     * Barrier: publishNextRound only waits for the last prefix to reach the
     * shadow RIB, and the shadow RIB VERSION bump can still be in flight behind
     * it. A peer dumping before it lands snapshots the version the group is
     * stalled at, and comes back level with the group instead of ahead of it.
     */
    WITH_RETRIES_N(
        10, { EXPECT_EVENTUALLY_TRUE(getRibVersion() > stalledVersion); });
    ASSERT_EQ(getGroupRibVersion(stallPeer.peerAddr), stalledVersion)
        << "group kept consuming the change list; the peers coming back will "
           "not be ahead of it and will not be DEP-A";

    /* Bring them back: each re-registers into the running, stalled group. */
    auto comesBackBlocked = [&returnBlocked](const folly::IPAddress& addr) {
      for (const auto& peerId : returnBlocked) {
        if (peerId.peerAddr == addr) {
          return true;
        }
      }
      return false;
    };
    for (const auto& peerId : toFlap) {
      if (comesBackBlocked(peerId.peerAddr)) {
        bringUpPeerBlockedLate(peerId);
        continue;
      }
      /* Clear the block left over from the stall before it dumps again. */
      unblockPeer(peerId.peerAddr, /*maxRetries=*/0);
      if (singleEoR) {
        bringUpLatePeerSingleEoR(peerId);
      } else {
        bringUpLatePeer(peerId);
      }
    }
  }

  /*
   * Drain the given peers repeatedly until `done` holds, failing if it never
   * does. A catching-up peer must drain before it can change state, and its
   * deliveries must be recorded rather than discarded.
   */
  void drainUntil(
      const std::vector<BgpPeerId>& peersToDrain,
      const std::function<bool()>& done,
      const std::string& what,
      int maxRetries = 60) {
    WITH_RETRIES_N_TIMED(maxRetries, std::chrono::milliseconds(100), {
      drainAllFast(peersToDrain);
      peerManager_->getEventBase().runInEventBaseThreadAndWait([]() {});
      EXPECT_EVENTUALLY_TRUE(done()) << what;
    });
  }

  /* Drain every peer once, recording deliveries. */
  void drainAll(const std::vector<BgpPeerId>& peerIds) {
    for (const auto& peerId : peerIds) {
      recordDrainedRoutes(peerId);
    }
  }

  /*
   * Drain-and-record with a minimal idle budget, for use INSIDE polling loops.
   * recordDrainedRoutes' default 20 idle retries at 50ms cost a second per
   * already-empty peer. The loop retries anyway, so one is enough; drainAll is
   * still used for the final drain before assertions.
   */
  void drainAllFast(const std::vector<BgpPeerId>& peerIds) {
    for (const auto& peerId : peerIds) {
      recordDrainedRoutes(peerId, /*idleRetries=*/1, /*maxMessages=*/200);
    }
  }

  /*
   * Detach `victims` via frequency-based slow-peer detection (block-count
   * threshold 1). They detach AFTER joining, so they are DEP-B (counted in
   * numPeersDetachedAfterJoin_) and the group will not promote past them.
   */
  void detachPeersBlocked(
      const std::vector<BgpPeerId>& victims,
      const std::vector<BgpPeerId>& others,
      std::string* roundCommOut,
      int numAttrBuckets = 0) {
    for (const auto& victim : victims) {
      setSlowPeerThresholds(
          victim.peerAddr,
          std::chrono::milliseconds(600000),
          1,
          std::chrono::milliseconds(60000));
      blockPeer(victim.peerAddr);
    }
    *roundCommOut = publishNextRound(kNumRoutes, numAttrBuckets);
    drainAll(others);
    for (const auto& victim : victims) {
      EXPECT_TRUE(waitForPeerQueueBlocked(victim));
      ASSERT_TRUE(
          waitForPeerState(victim.peerAddr, PeerUpdateState::DETACHED_BLOCKED));
      ASSERT_TRUE(isPeerDetached(victim.peerAddr));
    }
  }

  /*
   * Late-join a peer with its queue blocked from the start, so its own initial
   * dump cannot drain and it stays detached (never becomes promotable).
   */
  void bringUpPeerBlockedLate(const BgpPeerId& peerId) {
    bringUpPeerBlocked(peerId.peerAddr);
    sendEoRToPeer(peerId);
    /*
     * Deliberately no waitForEoR here: it reads from the peer's queue, which
     * would drain the very initial dump this peer is meant to be stalled on and
     * let it rejoin instead of staying detached.
     */
    peerManager_->getEventBase().runInEventBaseThreadAndWait([]() {});
  }

  /*
   * Late-join a v4-only peer (makePeerSpec, used by the multi-group cases):
   * a single AFI is negotiated, so exactly one EoR is expected.
   */
  void bringUpLatePeerSingleEoR(const BgpPeerId& peerId) {
    bringUpPeer(peerId.peerAddr);
    sendEoRToPeer(peerId);
    EXPECT_TRUE(waitForEoR(peerId));
  }

  /* Bring up a peer after the group is initialized. */
  void bringUpLatePeer(const BgpPeerId& peerId) {
    bringUpPeer(peerId.peerAddr);
    sendEoRToPeer(peerId);
    EXPECT_TRUE(waitForEoR(peerId));
    EXPECT_TRUE(waitForEoR(peerId));
  }

  /* Unblock without discarding: deliveries are recorded, not dropped. */
  void unblockPeersRecording(const std::vector<BgpPeerId>& peerIds) {
    for (const auto& peerId : peerIds) {
      unblockPeer(peerId.peerAddr, /*maxRetries=*/0);
    }
  }

  /* ASN used for the per-round marker community (65501:<localPref>). */
  static constexpr int kRoundCommAsn = 65501;
  /* ASN used to split a round across extra attribute groups (see
   * publishNextRound's numAttrBuckets). */
  static constexpr int kBucketCommAsn = 65502;

  uint32_t nextLocalPref_{0};
  std::vector<folly::CIDRNetwork> injectedPrefixes_;
};

/*
 * A3: last sync peers down with only DEP-B (slow-peer detached) members left.
 *
 * 5 of 10 peers are slow-peer detached and the other 5 brought DOWN. Because
 * the detached ones detached AFTER joining (numPeersDetachedAfterJoin_ > 0),
 * handleNoSyncPeers deliberately promotes nobody and the group freezes; once
 * they are unblocked and drain, exactly one is promoted and the rest rejoin.
 */
TEST_P(GroupWithNoSyncPeersE2ETest, LastSyncPeersDownDetachedBlockedPromotion) {
  XLOGF(INFO, "=== TEST: LastSyncPeersDownDetachedBlockedPromotion ===");

  setupPolicies();
  /* Tiny queues on the future detached peers so they block under load. */
  const std::vector<int> victimIdx = {0, 1, 2, 3, 4};
  const auto allSpecs = allPeerSpecs();
  for (const auto idx : victimIdx) {
    setQueueSizeForPeer(
        allSpecs[idx].peerAddr, /*capacity=*/3, /*highWm=*/2, /*lowWm=*/0);
  }
  auto peerIds = setupNPeersInGroupJoined(
      kNumPeers, /*queueCapacity=*/10, /*queueHighWm=*/8, /*queueLowWm=*/2);
  ASSERT_EQ(peerIds.size(), static_cast<size_t>(kNumPeers));

  std::vector<BgpPeerId> detachedPeers(peerIds.begin(), peerIds.begin() + 5);
  std::vector<BgpPeerId> syncPeers(peerIds.begin() + 5, peerIds.end());

  auto group = getUpdateGroupForPeer(peerIds[0].peerAddr);
  ASSERT_NE(group, nullptr);
  EXPECT_EQ(getGroupMemberCount(peerIds[0].peerAddr), kNumPeers);

  /* Baseline route round; everyone is in sync and receives it. */
  publishNextRound();
  drainAll(peerIds);

  /*
   * Detach the 5 victims (DEP-B, DETACHED_BLOCKED). This publishes the round
   * that fills their queues, so it is the latest round the group has seen.
   */
  std::string detachRoundComm;
  detachPeersBlocked(detachedPeers, syncPeers, &detachRoundComm);
  EXPECT_EQ(getNumInSyncPeers(peerIds[0].peerAddr), syncPeers.size());

  /*
   * Take the 5 in-sync peers down so the group freezes. Nothing is published
   * while frozen: the group cannot advance its marker, so a round now would
   * park the detached peers "ahead of group" -- a different failure mode.
   */
  drainAll(syncPeers);
  bringDownPeers(syncPeers);

  EXPECT_EQ(getNumInSyncPeers(detachedPeers[0].peerAddr), 0u);
  EXPECT_EQ(
      getGroupMemberCount(detachedPeers[0].peerAddr), detachedPeers.size());

  /*
   * Unblock the detached peers: they drain, catch up to the frozen group's
   * marker, and exactly one is promoted to SYNC while the rest rejoin through
   * the normal collapse path.
   */
  unblockPeersRecording(detachedPeers);
  expectAllPeersConverged(detachedPeers, detachRoundComm);

  EXPECT_EQ(getNumInSyncPeers(detachedPeers[0].peerAddr), detachedPeers.size());

  /*
   * The group is serving again: a fresh round published after recovery must
   * reach every surviving peer.
   */
  const auto postRecoveryRoundComm = publishNextRound();
  expectAllPeersConverged(detachedPeers, postRecoveryRoundComm);

  XLOGF(INFO, "=== TEST PASSED: LastSyncPeersDownDetachedBlockedPromotion ===");
}

/*
 * A1: two DETACHED FAST PEERS are accepted straight back into a group that has
 * no in-sync peers left, with no collapse needed.
 *
 * isDFP() requires the peer's packing list drained, its detachedRibVersion ==
 * its own == the GROUP's lastSeenRibVersion, and the group's packing list
 * NON-empty. A third peer is therefore held at JOINED_BLOCKED throughout: that
 * keeps the group's send suspended, which both keeps the packing list non-empty
 * and pins the group's marker while the candidates drain.
 */
TEST_P(GroupWithNoSyncPeersE2ETest, DfpPeersAcceptedIntoGroupWithNoSyncPeers) {
  XLOGF(INFO, "=== TEST: DfpPeersAcceptedIntoGroupWithNoSyncPeers ===");

  setupPolicies();
  const auto allSpecs = allPeerSpecs();
  /* Peers 0,1 are the DFP candidates. */
  setQueueSizeForPeer(
      allSpecs[0].peerAddr, /*capacity=*/3, /*highWm=*/2, /*lowWm=*/0);
  setQueueSizeForPeer(
      allSpecs[1].peerAddr, /*capacity=*/4, /*highWm=*/3, /*lowWm=*/0);
  /*
   * Peer 2 is the packing-list holder. Its queue is deliberately DEEPER than
   * the candidates': they must block in the first messages of the round, while
   * the holder crosses its watermark later in the SAME round.
   */
  setQueueSizeForPeer(
      allSpecs[2].peerAddr, /*capacity=*/20, /*highWm=*/15, /*lowWm=*/0);
  /*
   * Bystanders get a deep default queue: the detach round below is split
   * across 20 attribute groups, and 20 UPDATEs overflow the usual cap-10 queue,
   * which would detach the bystanders along with the candidates.
   */
  auto peerIds = setupNPeersInGroupJoined(
      kNumPeers, /*queueCapacity=*/100, /*queueHighWm=*/80, /*queueLowWm=*/2);
  std::vector<BgpPeerId> dfpPeers(peerIds.begin(), peerIds.begin() + 2);
  const auto& holder = peerIds[2];
  std::vector<BgpPeerId> otherSyncPeers(peerIds.begin() + 2, peerIds.end());

  publishNextRound();
  drainAll(peerIds);

  std::vector<BgpPeerId> drainablePeers(peerIds.begin() + 3, peerIds.end());

  /*
   * Slow-peer auto-detach stays disabled throughout: the block-count threshold
   * lives on the GROUP, so any value low enough to detach the candidates also
   * detaches the holder. The candidates are detached explicitly instead.
   */
  setSlowPeerThresholds(
      holder.peerAddr,
      std::chrono::milliseconds(600000),
      1000000,
      std::chrono::milliseconds(600000));

  /*
   * Block the candidates and the holder, then publish ONE round split across
   * 20 attribute groups: the candidates block early, the holder later in the
   * same round, leaving the packing list non-empty and the marker pinned --
   * both isDFP() preconditions.
   */
  blockPeer(holder.peerAddr);
  for (const auto& peerId : dfpPeers) {
    blockPeer(peerId.peerAddr);
  }
  const auto roundComm = publishNextRound(kNumRoutes, /*numAttrBuckets=*/20);

  /*
   * The candidates have different watermarks so they block in order. Detach
   * each candidate as soon as its queue fills, allowing the send to advance
   * to the next candidate and eventually the holder. Wait on the QUEUE, not
   * the state, which flips only on a deferral.
   */
  for (size_t idx = 0; idx < dfpPeers.size(); ++idx) {
    const auto& peerId = dfpPeers[idx];
    ASSERT_TRUE(waitForPeerQueueBlocked(peerId))
        << "DFP candidate " << peerId.peerAddr.str()
        << " never filled its queue";

    if (idx == 0) {
      /*
       * Let the group catch up to the shadow RIB before detaching. The
       * consume-timer callback assigns setLastSeenRibVersion(*maxRibVersion_)
       * unconditionally, so a group still behind it jumps forward on its
       * very next tick -- which would break isDFP()'s group-hasn't-moved clause
       * after the detach, with no publish involved. Waiting here removes that
       * race rather than running against it.
       */
      WITH_RETRIES_N(30, {
        EXPECT_EVENTUALLY_EQ(
            getGroupRibVersion(holder.peerAddr), getRibVersion());
      });
    }

    detachBlockedPeerDirectly(peerId.peerAddr);
    ASSERT_TRUE(
        waitForPeerState(peerId.peerAddr, PeerUpdateState::DETACHED_BLOCKED));
  }

  /* With the candidates gone the group distributes the REST of the same round,
   * which is what finally blocks the holder. */
  drainUntil(
      drainablePeers,
      [&]() {
        return getPeerState(holder.peerAddr) == PeerUpdateState::JOINED_BLOCKED;
      },
      "holder never blocked on the remainder of the detach round",
      40);
  ASSERT_FALSE(isPeerDetached(holder.peerAddr))
      << "the holder must stay in sync so it keeps the group's packing list "
         "non-empty";

  /*
   * NOTHING IS PUBLISHED from here on: consuming a new round would move the
   * candidates' lastSeenRibVersion past their detachedRibVersion, failing
   * isDFP() and silently sending them down the DSP collapse path.
   */

  /*
   * DRJ acceptance is deliberately NOT deferred here: DFPs are merged by
   * checkAndAcceptReadyToJoinPeers off the send guard, so deferring would make
   * that pass skip both peers. The group is still suspended on the holder, so
   * neither marker moves while the candidates drain into DFPs.
   */

  /*
   * Pin isDFP()'s preconditions here, where they are established, rather than
   * inferring them from the DFP flag once the peers have already parked: a
   * lost race then fails at the cause instead of surfacing later as an
   * unexplained "parked as a DSP".
   */
  const auto groupVersion = getGroupRibVersion(holder.peerAddr);
  for (const auto& peerId : dfpPeers) {
    ASSERT_EQ(getPeerLastSeenRibVersion(peerId.peerAddr), groupVersion)
        << "peer " << peerId.peerAddr.str()
        << " moved on the change list after the detach, so it can no longer "
           "be a DFP";
    ASSERT_EQ(getPeerDetachedRibVersion(peerId.peerAddr), groupVersion)
        << "the group moved after peer " << peerId.peerAddr.str()
        << " detached, so it can no longer be a DFP";
  }

  unblockPeersRecording(dfpPeers);
  drainUntil(
      dfpPeers,
      [&]() {
        return allPeersInState(
            dfpPeers, PeerUpdateState::DETACHED_READY_TO_JOIN);
      },
      "peers never parked at DETACHED_READY_TO_JOIN",
      40);
  for (const auto& peerId : dfpPeers) {
    ASSERT_TRUE(waitForPeerState(
        peerId.peerAddr, PeerUpdateState::DETACHED_READY_TO_JOIN));
    /*
     * The whole point of this case: these must be DFPs, not DSPs. If the group
     * or the peers moved after the detach they would silently take the DSP
     * collapse path instead and this test would duplicate the DSP one.
     */
    ASSERT_TRUE(isDetachedFastPeer(peerId.peerAddr))
        << "peer " << peerId.peerAddr.str()
        << " parked at DRJ as a DSP, not a DFP -- the group or the peer moved "
           "after the detach";
  }

  /*
   * Take every in-sync peer down, holder LAST and still blocked. Tearing it
   * down fails its deferred push and lets the suspended send finish, and the
   * exit guard then runs checkAndAcceptReadyToJoinPeers and merges the DFPs --
   * handleNoSyncPeers does not merge DFPs.
   */
  drainAll(drainablePeers);
  bringDownPeers(drainablePeers);
  ASSERT_TRUE(
      allPeersInState(dfpPeers, PeerUpdateState::DETACHED_READY_TO_JOIN))
      << "the DFPs must still be detached when the last sync peer goes down";
  bringDownPeer(holder.peerAddr);
  EXPECT_EQ(getGroupMemberCount(dfpPeers[0].peerAddr), dfpPeers.size());

  /*
   * Both DFPs are back in sync, accepted DIRECTLY with no collapse, from
   * checkAndAcceptReadyToJoinPeers as the group's send completes.
   */
  EXPECT_EQ(getNumInSyncPeers(dfpPeers[0].peerAddr), dfpPeers.size())
      << "the DFPs were not accepted back when the last sync peer went down";
  for (const auto& peerId : dfpPeers) {
    EXPECT_TRUE(
        waitForPeerState(peerId.peerAddr, PeerUpdateState::JOINED_RUNNING))
        << "peer " << peerId.peerAddr.str() << " was not accepted back";
  }

  /* The recovered group serves a fresh round to both. */
  const auto postRecoveryRoundComm = publishNextRound();
  expectAllPeersConverged(dfpPeers, postRecoveryRoundComm);

  XLOGF(INFO, "=== TEST PASSED: DfpPeersAcceptedIntoGroupWithNoSyncPeers ===");
}

/*
 * A4 shared shape: leave the group with 9 DEP-A members and no sync peer.
 *
 * All 10 peers join, then all are blocked so the group's send -- and its
 * change-list consumption -- is suspended. 9 are FLAPPED while extra rounds run
 * the RIB past the group's frozen marker, so each re-registers as a DEP-A ahead
 * of the group. Peer 0 stays JOINED_BLOCKED as the last sync peer; bringing it
 * DOWN leaves zero in-sync peers with numPeersDetachedAfterJoin_ == 0, the one
 * situation where handleNoSyncPeers promotes a DEP-A. See flapPeersIntoDepA for
 * why the flap is what makes that composition deterministic.
 */
TEST_P(GroupWithNoSyncPeersE2ETest, DepAPromotedWhenLastSyncPeerDown) {
  XLOGF(INFO, "=== TEST: DepAPromotedWhenLastSyncPeerDown ===");

  setupPolicies();
  const auto allSpecs = allPeerSpecs();
  /* The first member must deterministically suspend the serialized send. */
  setQueueSizeForPeer(
      allSpecs[0].peerAddr, /*capacity=*/3, /*highWm=*/2, /*lowWm=*/0);
  for (int i = 1; i < kNumPeers; ++i) {
    setQueueSizeForPeer(
        allSpecs[i].peerAddr, /*capacity=*/4, /*highWm=*/3, /*lowWm=*/0);
  }

  auto peerIds = setupNPeersInGroupJoined(
      kNumPeers,
      /*queueCapacity=*/10,
      /*queueHighWm=*/8,
      /*queueLowWm=*/2);
  const auto& blockedSyncPeer = peerIds[0];

  publishNextRound();
  drainAll(peerIds);

  /* Flap the other 9 into DEP-A. Peer 9 comes back blocked and non-promotable,
   * so the promotion must pick a READY peer; it rejoins later. */
  std::vector<BgpPeerId> lateJoiners(peerIds.begin() + 1, peerIds.end());
  std::vector<BgpPeerId> readyLateJoiners(
      peerIds.begin() + 1, peerIds.begin() + 9);
  std::string roundComm;
  flapPeersIntoDepA(
      blockedSyncPeer,
      lateJoiners,
      /*returnBlocked=*/{peerIds[9]},
      /*extraRounds=*/2,
      &roundComm);
  expectAllDepA(lateJoiners);

  /* Let the 8 unblocked joiners finish their own dumps and park at DRJ. */
  drainUntil(
      readyLateJoiners,
      [&]() {
        return allPeersInState(
            readyLateJoiners, PeerUpdateState::DETACHED_READY_TO_JOIN);
      },
      "peers never parked at DETACHED_READY_TO_JOIN",
      40);
  expectAllAheadOfGroup(readyLateJoiners);

  /* Last sync peer down: one ready DEP-A is promoted in place. */
  bringDownPeer(blockedSyncPeer.peerAddr);
  EXPECT_EQ(getGroupMemberCount(peerIds[1].peerAddr), lateJoiners.size());
  EXPECT_EQ(getNumInSyncPeers(peerIds[1].peerAddr), 1u)
      << "exactly one DEP-A should have been promoted to carry the group";

  /* Unblock the 9th: everyone rejoins. */
  unblockPeersRecording({peerIds[9]});

  /* The recovered group serves a fresh round to every surviving peer. */
  const auto postRecoveryRoundComm = publishNextRound();
  expectAllPeersConverged(lateJoiners, postRecoveryRoundComm);
  EXPECT_EQ(getNumInSyncPeers(peerIds[1].peerAddr), lateJoiners.size());

  XLOGF(INFO, "=== TEST PASSED: DepAPromotedWhenLastSyncPeerDown ===");
}

/*
 * A4b: the same DEP-A shape, but EVERY flapped member comes back blocked, so
 * nothing is promotable when the last sync peer goes down and the group
 * freezes. On unblock the first to finish draining promotes ITSELF from
 * transitionPeerUpdateState and un-freezes the group; the rest then rejoin.
 */
TEST_P(GroupWithNoSyncPeersE2ETest, DepASelfPromotesAfterFrozenGroupUnblock) {
  XLOGF(INFO, "=== TEST: DepASelfPromotesAfterFrozenGroupUnblock ===");

  setupPolicies();
  const auto allSpecs = allPeerSpecs();
  setQueueSizeForPeer(
      allSpecs[0].peerAddr, /*capacity=*/3, /*highWm=*/2, /*lowWm=*/0);
  for (int i = 1; i < kNumPeers; ++i) {
    setQueueSizeForPeer(
        allSpecs[i].peerAddr, /*capacity=*/4, /*highWm=*/3, /*lowWm=*/0);
  }

  auto peerIds = setupNPeersInGroupJoined(
      kNumPeers,
      /*queueCapacity=*/10,
      /*queueHighWm=*/8,
      /*queueLowWm=*/2);
  const auto& blockedSyncPeer = peerIds[0];

  publishNextRound();
  drainAll(peerIds);

  /* Every flapped member returns blocked, so none of them can drain. */
  std::vector<BgpPeerId> lateJoiners(peerIds.begin() + 1, peerIds.end());
  std::string roundComm;
  flapPeersIntoDepA(
      blockedSyncPeer,
      lateJoiners,
      /*returnBlocked=*/lateJoiners,
      /*extraRounds=*/2,
      &roundComm);
  expectAllDepA(lateJoiners);

  /*
   * All 9 must be blocked on their OWN init dump BEFORE the last sync peer
   * goes down: if even one had drained, handleNoSyncPeers would promote it and
   * the self-promotion path would never run.
   */
  for (const auto& peerId : lateJoiners) {
    ASSERT_TRUE(waitForPeerStateAny(
        peerId.peerAddr,
        {PeerUpdateState::DETACHED_INIT_DUMP,
         PeerUpdateState::DETACHED_BLOCKED}))
        << "peer " << peerId.peerAddr.str()
        << " never entered its own init dump after the flap";
    ASSERT_TRUE(waitForPeerQueueBlocked(peerId))
        << "peer " << peerId.peerAddr.str()
        << " never filled its queue on its own init dump";
    ASSERT_TRUE(
        waitForPeerState(peerId.peerAddr, PeerUpdateState::DETACHED_BLOCKED))
        << "peer " << peerId.peerAddr.str()
        << " did not stall at DETACHED_BLOCKED, so it may be promotable";
  }

  /* Last sync peer down: nothing is promotable, so the group freezes. */
  bringDownPeer(blockedSyncPeer.peerAddr);
  EXPECT_EQ(getNumInSyncPeers(peerIds[1].peerAddr), 0u)
      << "no blocked DEP-A should have been promotable";
  EXPECT_EQ(getGroupMemberCount(peerIds[1].peerAddr), lateJoiners.size());

  /*
   * Unblock them all: the first to finish draining promotes itself and the
   * group resumes, after which the rest rejoin.
   */
  unblockPeersRecording(lateJoiners);
  drainUntil(
      lateJoiners,
      [&]() { return getNumInSyncPeers(peerIds[1].peerAddr) > 0; },
      "no DEP-A promoted itself to un-freeze the group",
      60);
  EXPECT_GE(getNumInSyncPeers(peerIds[1].peerAddr), 1u)
      << "no DEP-A promoted itself to un-freeze the group";

  const auto postRecoveryRoundComm = publishNextRound();
  expectAllPeersConverged(lateJoiners, postRecoveryRoundComm);
  EXPECT_EQ(getNumInSyncPeers(peerIds[1].peerAddr), lateJoiners.size());

  XLOGF(INFO, "=== TEST PASSED: DepASelfPromotesAfterFrozenGroupUnblock ===");
}

/*
 * A blocking DSP goes DOWN before it can rejoin: the group must not stay stuck.
 *
 * handleNoSyncPeers refuses to promote while numPeersDetachedAfterJoin_ > 0: a
 * peer that detached AFTER joining still shares the group's entries, and
 * promoting a diverged DEP-A ahead of it would delete group-only entries and
 * corrupt that peer's pending rejoin. One sharing DSP is therefore enough to
 * keep a group with no sync peers frozen even with 8 promotable DEP-A members
 * waiting in DETACHED_READY_TO_JOIN.
 *
 * Removing the blocker must resolve it: bringing the DSP down before it ever
 * rejoins decrements the counter in removePeer(), and unregisterPeer() then
 * calls recoverIfNoSyncPeers(), so that very pass must promote a waiting peer.
 * If the bookkeeping ever leaked the group would stay frozen forever with
 * promotable peers available, which is the failure this guards against.
 */
TEST_P(GroupWithNoSyncPeersE2ETest, BlockingDspDownLetsDrjPeerBePromoted) {
  XLOGF(INFO, "=== TEST: BlockingDspDownLetsDrjPeerBePromoted ===");

  setupPolicies();
  const auto allSpecs = allPeerSpecs();
  for (int i = 0; i < kNumPeers; ++i) {
    setQueueSizeForPeer(
        allSpecs[i].peerAddr, /*capacity=*/3, /*highWm=*/2, /*lowWm=*/0);
  }

  auto peerIds = setupNPeersInGroupJoined(
      kNumPeers,
      /*queueCapacity=*/10,
      /*queueHighWm=*/8,
      /*queueLowWm=*/2);
  const auto& stallPeer = peerIds[0];
  const auto& dspPeer = peerIds[1];
  std::vector<BgpPeerId> drjPeers(peerIds.begin() + 2, peerIds.end());

  publishNextRound();
  drainAll(peerIds);

  /*
   * Slow-peer detach peer 1 FIRST, while the block-count threshold is still
   * low: it detaches AFTER joining, so it carries a non-zero
   * detachedRibVersion and is the sharing DSP that will block promotion. The
   * flap below raises the threshold group-wide, so nothing else can detach.
   */
  std::string detachRoundComm;
  std::vector<BgpPeerId> others(peerIds.begin() + 2, peerIds.end());
  others.push_back(stallPeer);
  detachPeersBlocked({dspPeer}, others, &detachRoundComm);
  ASSERT_TRUE(isPeerDetached(dspPeer.peerAddr));

  /* Flap the other 8 into DEP-A behind the stalled group. */
  std::string roundComm;
  flapPeersIntoDepA(
      stallPeer,
      drjPeers,
      /*returnBlocked=*/{},
      /*extraRounds=*/2,
      &roundComm);
  drainUntil(
      drjPeers,
      [&]() {
        return allPeersInState(
            drjPeers, PeerUpdateState::DETACHED_READY_TO_JOIN);
      },
      "peers never parked at DETACHED_READY_TO_JOIN",
      40);
  /*
   * The standoff under test is "promotable DEP-A peers held back by ONE sharing
   * DSP", so the sharing count must come from dspPeer alone.
   */
  for (const auto& peerId : drjPeers) {
    EXPECT_TRUE(isDetachedOnRegistration(peerId.peerAddr))
        << "peer " << peerId.peerAddr.str() << " is not a DEP-A";
    EXPECT_EQ(getPeerDetachedRibVersion(peerId.peerAddr), 0u)
        << "peer " << peerId.peerAddr.str() << " detached after joining";
  }
  EXPECT_EQ(getNumPeersDetachedAfterJoin(drjPeers[0].peerAddr), 1u)
      << "only the sharing DSP should count as detached after joining";
  expectAllAheadOfGroup(drjPeers);

  /* Hold the ordinary rejoin path so handleNoSyncPeers decides what happens. */
  for (const auto& peerId : drjPeers) {
    testOnlyDeferDrjAcceptance(peerId.peerAddr, true);
  }

  /* Last sync peer down: the sharing DSP keeps the group frozen. */
  bringDownPeer(stallPeer.peerAddr);
  EXPECT_EQ(getNumInSyncPeers(drjPeers[0].peerAddr), 0u)
      << "the sharing DSP should have blocked promotion, leaving it frozen";
  EXPECT_TRUE(isPeerDetached(dspPeer.peerAddr))
      << "the DSP must still be detached -- it must not have rejoined";

  /* Remove the blocker before it ever rejoins. */
  bringDownPeer(dspPeer.peerAddr);
  EXPECT_EQ(getNumInSyncPeers(drjPeers[0].peerAddr), 1u)
      << "group is stuck: no DRJ peer was promoted after the blocking DSP left";
  EXPECT_EQ(getGroupMemberCount(drjPeers[0].peerAddr), drjPeers.size());

  /* Release the hold: the rest rejoin and the group serves a fresh round. */
  for (const auto& peerId : drjPeers) {
    testOnlyDeferDrjAcceptance(peerId.peerAddr, false);
  }
  const auto postRecoveryRoundComm = publishNextRound();
  expectAllPeersConverged(drjPeers, postRecoveryRoundComm);
  EXPECT_EQ(getNumInSyncPeers(drjPeers[0].peerAddr), drjPeers.size());

  XLOGF(INFO, "=== TEST PASSED: BlockingDspDownLetsDrjPeerBePromoted ===");
}

/*
 * The only surviving member is pinned in DETACHED_INIT_DUMP when the last sync
 * peer goes down, so nothing is promotable and the group freezes. Releasing the
 * pin lets its dump finish; it reaches DETACHED_READY_TO_JOIN and promotes
 * itself, and the group serves again.
 *
 * handleNoSyncPeers treats DETACHED_INIT_DUMP and DETACHED_BLOCKED alike (both
 * are simply "not DRJ"), so the freeze and the self-promotion are the same
 * branches DepASelfPromotesAfterFrozenGroupUnblock covers. What is specific
 * here is the INIT DUMP lifecycle: the peer's dump has not been produced at all
 * when the group loses its last sync peer, and it must still be able to
 * complete -- and promote -- against a group whose packing list was cleared and
 * whose consume timer was cancelled.
 */
TEST_P(GroupWithNoSyncPeersE2ETest, DetachedInitDumpPeerPromotesAfterRelease) {
  XLOGF(INFO, "=== TEST: DetachedInitDumpPeerPromotesAfterRelease ===");

  setupPolicies();
  auto peerIds = setupGroupDeferredJoin(
      kNumPeers,
      /*numInitial=*/9,
      /*queueCapacity=*/10,
      /*queueHighWm=*/8,
      /*queueLowWm=*/2);
  std::vector<BgpPeerId> initialPeers(peerIds.begin(), peerIds.begin() + 9);
  const auto& didPeer = peerIds[9];

  publishNextRound();
  drainAll(initialPeers);

  /*
   * Pin the peer in DETACHED_INIT_DUMP. testOnlyDeferInitDump looks the peer up
   * by its AdjRib, which only exists once it has been up, so the peer joins
   * first and is then dropped, pinned and reconnected -- the reconnect
   * registers into the already-initialized group and stalls in DID.
   */
  bringUpLatePeer(didPeer);
  ASSERT_TRUE(
      waitForPeerState(didPeer.peerAddr, PeerUpdateState::JOINED_RUNNING));
  recordDrainedRoutes(didPeer);
  bringDownPeer(didPeer.peerAddr);
  unblockPeer(didPeer.peerAddr, /*maxRetries=*/0);
  testOnlyDeferInitDump(didPeer.peerAddr, true);
  bringUpPeer(didPeer.peerAddr);
  sendEoRToPeer(didPeer);
  ASSERT_TRUE(waitForPeerState(
      didPeer.peerAddr,
      PeerUpdateState::DETACHED_INIT_DUMP,
      /*maxRetries=*/30));

  /* Last sync peers down: the DID peer is not promotable, so the group freezes.
   */
  drainAll(initialPeers);
  bringDownPeers(initialPeers);
  EXPECT_EQ(getNumInSyncPeers(didPeer.peerAddr), 0u);
  EXPECT_EQ(getGroupMemberCount(didPeer.peerAddr), 1u);
  EXPECT_NE(getPeerState(didPeer.peerAddr), PeerUpdateState::DOWN);

  /*
   * Run the RIB past the frozen group BEFORE releasing the pin. A frozen group
   * cannot advance its own marker, so the dump this peer finally produces
   * snapshots a strictly higher version -- which is what makes it a DEP-A ahead
   * of the group and sends transitionPeerUpdateState down the self-promotion
   * branch. Without this the dump lands level with the group, isReadyToRejoin
   * Group() is true, and the peer is quietly accepted by maybeAcceptDSPPeer
   * instead: the group would stay frozen waiting for an ahead peer that never
   * arrives, and this case would silently duplicate the DSP collapse one.
   */
  const auto frozenVersion = getGroupRibVersion(didPeer.peerAddr);
  publishNextRound();
  WITH_RETRIES_N(
      10, { EXPECT_EVENTUALLY_TRUE(getRibVersion() > frozenVersion); });

  /* Release the pin: the dump completes and the peer promotes itself. */
  testOnlyDeferInitDump(didPeer.peerAddr, false);
  drainUntil(
      {didPeer},
      [&]() {
        return getPeerState(didPeer.peerAddr) ==
            PeerUpdateState::JOINED_RUNNING;
      },
      "peer never reached JOINED_RUNNING",
      60);
  EXPECT_TRUE(
      waitForPeerState(didPeer.peerAddr, PeerUpdateState::JOINED_RUNNING))
      << "the DETACHED_INIT_DUMP peer did not promote itself after release";

  /*
   * The group adopted the promoted peer's change-list position. A frozen group
   * cannot move on its own, so a marker past the frozen one can only have come
   * from promoteDetachedPeerToSync -- which is what separates self-promotion
   * from the peer merely being collapsed back in at the group's own marker.
   */
  EXPECT_GT(getGroupRibVersion(didPeer.peerAddr), frozenVersion)
      << "the group's marker did not advance to the promoted peer's, so the "
         "peer was accepted by collapse rather than promoting itself";

  /* The recovered group serves a fresh round. */
  const auto postRecoveryRoundComm = publishNextRound();
  expectAllPeersConverged({didPeer}, postRecoveryRoundComm);

  XLOGF(INFO, "=== TEST PASSED: DetachedInitDumpPeerPromotesAfterRelease ===");
}

/*
 * Sibling of DetachedInitDumpPeerPromotesAfterRelease, for the case that test
 * deliberately steers around: the released dump lands LEVEL with the frozen
 * group instead of ahead of it.
 *
 * A level peer satisfies neither of the two rejoin doors.
 * canWaitForGroupToRejoin requires both change-list consumers to be at the end
 * of the list, and a frozen group parked mid-list cannot get there; the DEP-A
 * self-promotion branch requires the peer to be strictly ahead, and a peer
 * stopped at the group's marker by iterateChangesUntilExcluding can never climb
 * above it. Without the level-with-frozen-group branch the peer and the group
 * wait on each other forever, so what this asserts is simply that the peer
 * still recovers.
 */
TEST_P(GroupWithNoSyncPeersE2ETest, LevelPeerPromotesAgainstFrozenGroup) {
  XLOGF(INFO, "=== TEST: LevelPeerPromotesAgainstFrozenGroup ===");

  setupPolicies();
  auto peerIds = setupGroupDeferredJoin(
      kNumPeers,
      /*numInitial=*/9,
      /*queueCapacity=*/10,
      /*queueHighWm=*/8,
      /*queueLowWm=*/2);
  std::vector<BgpPeerId> initialPeers(peerIds.begin(), peerIds.begin() + 9);
  const auto& didPeer = peerIds[9];

  publishNextRound();
  drainAll(initialPeers);

  /* Pin the peer in DETACHED_INIT_DUMP, as the sibling test does. */
  bringUpLatePeer(didPeer);
  ASSERT_TRUE(
      waitForPeerState(didPeer.peerAddr, PeerUpdateState::JOINED_RUNNING));
  recordDrainedRoutes(didPeer);
  bringDownPeer(didPeer.peerAddr);
  unblockPeer(didPeer.peerAddr, /*maxRetries=*/0);
  testOnlyDeferInitDump(didPeer.peerAddr, true);
  bringUpPeer(didPeer.peerAddr);
  sendEoRToPeer(didPeer);
  ASSERT_TRUE(waitForPeerState(
      didPeer.peerAddr,
      PeerUpdateState::DETACHED_INIT_DUMP,
      /*maxRetries=*/30));

  /* Last sync peers down: the DID peer is not promotable, so the group freezes.
   */
  drainAll(initialPeers);
  bringDownPeers(initialPeers);
  EXPECT_EQ(getNumInSyncPeers(didPeer.peerAddr), 0u);
  EXPECT_EQ(getGroupMemberCount(didPeer.peerAddr), 1u);
  EXPECT_NE(getPeerState(didPeer.peerAddr), PeerUpdateState::DOWN);

  /*
   * Pin acceptance before releasing the dump. The dump then lands level with
   * the frozen group -- both consumers at the end of the list -- and the peer
   * parks at DETACHED_READY_TO_JOIN instead of being taken straight back in.
   */
  const auto frozenVersion = getGroupRibVersion(didPeer.peerAddr);
  testOnlyDeferDrjAcceptance(didPeer.peerAddr, true);
  testOnlyDeferInitDump(didPeer.peerAddr, false);
  drainUntil(
      {didPeer},
      [&]() {
        return getPeerState(didPeer.peerAddr) ==
            PeerUpdateState::DETACHED_READY_TO_JOIN;
      },
      "pinned peer never reached DETACHED_READY_TO_JOIN",
      60);

  /*
   * Publish with the peer pinned and the group frozen. Neither consumer can
   * take these entries -- the group has no timer and the pinned peer has its
   * packing timers cancelled -- so both markers come to rest on the same
   * unconsumed entry. That is the level-with-a-mid-list-frozen-group state:
   * the peer is no longer at the end of the change list, so it cannot satisfy
   * canWaitForGroupToRejoin, and its RIB version still matches the group's, so
   * it is not a DEP-A either.
   */
  publishNextRound();
  WITH_RETRIES_N(
      10, { EXPECT_EVENTUALLY_TRUE(getRibVersion() > frozenVersion); });
  EXPECT_EQ(getGroupRibVersion(didPeer.peerAddr), frozenVersion)
      << "the frozen group consumed the new round, so it is not parked "
         "mid-list and this test is not covering the level case";

  /* Releasing the pin drops the peer to DETACHED_RUNNING; it recovers itself.
   */
  testOnlyDeferDrjAcceptance(didPeer.peerAddr, false);
  drainUntil(
      {didPeer},
      [&]() {
        return getPeerState(didPeer.peerAddr) ==
            PeerUpdateState::JOINED_RUNNING;
      },
      "level peer never reached JOINED_RUNNING against the frozen group",
      60);
  EXPECT_TRUE(
      waitForPeerState(didPeer.peerAddr, PeerUpdateState::JOINED_RUNNING))
      << "the level DETACHED_INIT_DUMP peer never recovered, so the group and "
         "the peer are waiting on each other";

  /*
   * The group is consuming again: promotion re-armed its timer, so it takes
   * the round it could not touch while frozen and moves past frozenVersion.
   */
  WITH_RETRIES_N(30, {
    EXPECT_EVENTUALLY_TRUE(
        getGroupRibVersion(didPeer.peerAddr) > frozenVersion);
  });

  /* The recovered group serves a fresh round. */
  const auto postRecoveryRoundComm = publishNextRound();
  expectAllPeersConverged({didPeer}, postRecoveryRoundComm);

  XLOGF(INFO, "=== TEST PASSED: LevelPeerPromotesAgainstFrozenGroup ===");
}

/*
 * The group loses its ONLY member, so it is left with no members at all rather
 * than with detached ones: recoverIfNoSyncPeers is a no-op because it requires
 * getMemberCount() > 0, so none of the promotion paths run. unregisterPeer
 * deregisters the group from the changeListTracker and drains its async scope.
 *
 * What must then happen is that the emptied group is NOT reused: the next peer
 * to establish gets a brand new update group container of its own, with its own
 * group-level initial dump, and is served normally. This is the case the
 * promotion tests cannot reach -- every one of them keeps at least one detached
 * member, so their group always survives to be recovered.
 */
TEST_P(GroupWithNoSyncPeersE2ETest, LastMemberDownGivesNextPeerNewGroup) {
  XLOGF(INFO, "=== TEST: LastMemberDownGivesNextPeerNewGroup ===");

  setupPolicies();
  const auto allSpecs = allPeerSpecs();
  /* Tiny queue + blocked from the start so the initial dump cannot drain. */
  setQueueSizeForPeer(
      allSpecs[0].peerAddr, /*capacity=*/3, /*highWm=*/2, /*lowWm=*/0);

  auto peerIds = setupGroupDeferredJoin(
      kNumPeers,
      /*numInitial=*/0,
      /*queueCapacity=*/10,
      /*queueHighWm=*/8,
      /*queueLowWm=*/2);
  const auto& soleMember = peerIds[0];

  /* Routes exist before the peer joins, so its group dump has work to do. */
  publishNextRound();

  /* Bring the sole member up blocked so its queue backs the dump up. */
  bringUpPeerBlocked(soleMember.peerAddr);
  sendEoRToPeer(soleMember);
  peerManager_->getEventBase().runInEventBaseThreadAndWait([]() {});

  /* Take it down: the group is left with no members at all. */
  unblockPeer(soleMember.peerAddr, /*maxRetries=*/0);
  bringDownPeer(soleMember.peerAddr);
  EXPECT_EQ(getPeerState(soleMember.peerAddr), PeerUpdateState::DOWN);
  EXPECT_EQ(getGroupMemberCount(soleMember.peerAddr), 0u);

  /*
   * A later peer must get its OWN new group rather than the emptied one, and
   * be served by it. Asserting on the group pointer is what makes "new
   * container" observable: peer state alone looks identical either way.
   */
  auto* const emptiedGroup = getUpdateGroupForPeer(soleMember.peerAddr).get();
  bringUpLatePeer(peerIds[1]);
  EXPECT_NE(getUpdateGroupForPeer(peerIds[1].peerAddr).get(), emptiedGroup)
      << "the new peer reused the emptied update group instead of forming its "
         "own";
  const auto roundComm = publishNextRound();
  expectAllPeersConverged({peerIds[1]}, roundComm);

  XLOGF(INFO, "=== TEST PASSED: LastMemberDownGivesNextPeerNewGroup ===");
}

/*
 * C1: an egress policy re-evaluation lands on a group that is ALREADY frozen
 * (members, but no in-sync peer).
 *
 * 4 peer groups of 10; only group 0 is operated on. Group 0 is first driven to
 * zero sync peers (5 members slow-peer detached, the other 5 brought DOWN), so
 * it is frozen. The MODIFY/APPEND policy is then applied to group 0's peer
 * group. With no in-sync peers the group walk still refreshes the group's
 * RIB-OUT but writes no packing list, each detached member is re-evaluated
 * inline, and the re-eval sweep tries to promote (nothing is promotable yet, so
 * the group stays frozen). Unblocking the detached members then promotes one
 * and the rest rejoin, and the whole group must converge on the NEW policy
 * while the three untouched groups stay on permit-all.
 */
TEST_P(GroupWithNoSyncPeersE2ETest, PolicyReEvalOnFrozenGroupThenPromotion) {
  XLOGF(INFO, "=== TEST: PolicyReEvalOnFrozenGroupThenPromotion ===");

  setupPolicies();
  /* Tiny queues on the 5 peers of group 0 that will detach. */
  for (int i = 0; i < 5; ++i) {
    setQueueSizeForPeer(
        makePeerSpec(i).peerAddr, /*capacity=*/3, /*highWm=*/2, /*lowWm=*/0);
  }
  auto groups = setupPeersInGroups(
      kNumGroups,
      kPeersPerGroup,
      /*queueCapacity=*/10,
      /*queueHighWm=*/8,
      /*queueLowWm=*/2);
  ASSERT_EQ(groups.size(), static_cast<size_t>(kNumGroups));

  const auto& operatedGroup = groups[0];
  std::vector<BgpPeerId> detachedPeers(
      operatedGroup.begin(), operatedGroup.begin() + 5);
  std::vector<BgpPeerId> downPeers(
      operatedGroup.begin() + 5, operatedGroup.end());
  const auto untouchedPeers = peersOutsideGroup(groups, 0);

  publishNextRound();
  drainAllGroups(groups);

  /* Freeze group 0: detach 5, bring the other 5 down. */
  std::string roundComm;
  detachPeersBlocked(detachedPeers, downPeers, &roundComm);
  drainAll(downPeers);
  drainAll(untouchedPeers);
  bringDownPeers(downPeers);
  EXPECT_EQ(getNumInSyncPeers(detachedPeers[0].peerAddr), 0u);

  /* Apply MODIFY/APPEND to the frozen group's peer group only. */
  const auto result =
      setPeerGroupPolicy(reEvalPeerGroupName(0), kMatchModifyAppendPolicyName);
  EXPECT_EQ(
      result,
      neteng::fboss::bgp::thrift::BgpPolicyChangeResult::POLICIES_APPLIED);

  /* Unblock the detached members: one is promoted and the rest rejoin. */
  unblockPeersRecording(detachedPeers);
  drainUntil(
      detachedPeers,
      [&]() {
        return allPeersInState(detachedPeers, PeerUpdateState::JOINED_RUNNING);
      },
      "peers never returned to JOINED_RUNNING",
      60);
  EXPECT_EQ(getNumInSyncPeers(detachedPeers[0].peerAddr), detachedPeers.size());

  /*
   * Republish so the recovered group distributes under the new policy, then
   * check both sides for every peer: group 0 on APPEND, the rest on permit-all.
   */
  publishNextRound();
  drainAllGroups(groups);
  /*
   * Second pass over the recovered group: drainAllGroups walks group 0
   * first, so deliveries that land while the other groups are being
   * drained are only recorded here.
   */
  drainAll(detachedPeers);
  expectPolicyOnPeers(detachedPeers, kMatchModifyAppendPolicyName);
  expectPolicyOnPeers(untouchedPeers, kPermitAllPolicyName);

  XLOGF(INFO, "=== TEST PASSED: PolicyReEvalOnFrozenGroupThenPromotion ===");
}

/*
 * C2: the reverse ordering of C1 -- the group first loses its sync peers and is
 * recovered, and only THEN is the egress policy changed. Whatever the group is
 * serving from afterwards is the recovered members' view rather than one the
 * group built itself, so the point of these cases is that a re-evaluation
 * applied later still reaches every member, while the three untouched groups
 * stay on permit-all. One flavour per recovery mechanism, mirroring the
 * single-group A cases.
 *
 * Note the mechanisms differ in whether handleNoSyncPeers runs at all. DFPs are
 * merged by checkAndAcceptReadyToJoinPeers off the send guard as the last sync
 * peer departs, so that flavour never logs a freeze; the DSP and DEP-A ones do
 * reach zero sync peers and recover from there.
 *
 * This first flavour also covers a PARTIALLY recovered group: only 2 of the 5
 * detached members are released before the re-evaluation, so BOTH halves of the
 * re-eval run in the same pass. The group RIB walk covers the 2 in-sync members
 * ("for 2 in-sync peers"), while each of the 3 still-DETACHED_BLOCKED members
 * is re-evaluated individually (PeerManagerBase: "Re-evaluating detached
 * peer"), which runs its processRibDumpReq and rebuilds its RIB-OUT under the
 * new policy even though it cannot drain. Releasing them afterwards must
 * therefore deliver the already-re-evaluated routes, and all 5 must end up
 * identical on the wire.
 */
TEST_P(GroupWithNoSyncPeersE2ETest, PolicyReEvalAfterDepBRecovery) {
  XLOGF(INFO, "=== TEST: PolicyReEvalAfterDepBRecovery ===");

  setupPolicies();
  for (int i = 0; i < 5; ++i) {
    setQueueSizeForPeer(
        makePeerSpec(i).peerAddr, /*capacity=*/3, /*highWm=*/2, /*lowWm=*/0);
  }
  auto groups = setupPeersInGroups(
      kNumGroups,
      kPeersPerGroup,
      /*queueCapacity=*/10,
      /*queueHighWm=*/8,
      /*queueLowWm=*/2);
  const auto& operatedGroup = groups[0];
  std::vector<BgpPeerId> detachedPeers(
      operatedGroup.begin(), operatedGroup.begin() + 5);
  std::vector<BgpPeerId> downPeers(
      operatedGroup.begin() + 5, operatedGroup.end());
  /*
   * Only these 2 are recovered before the re-evaluation. The other 3 stay
   * DETACHED_BLOCKED across it, so the group is re-evaluated while it is part
   * in-sync and part detached -- the walk covers the 2 sync members while the
   * 3 detached ones have to pick the new policy up when they rejoin.
   */
  std::vector<BgpPeerId> recoveredPeers(
      detachedPeers.begin(), detachedPeers.begin() + 2);
  std::vector<BgpPeerId> stillDetachedPeers(
      detachedPeers.begin() + 2, detachedPeers.end());
  const auto untouchedPeers = peersOutsideGroup(groups, 0);

  publishNextRound();
  drainAllGroups(groups);

  /* Freeze group 0 with all 5 detached. */
  std::string roundComm;
  detachPeersBlocked(detachedPeers, downPeers, &roundComm);
  drainAll(downPeers);
  drainAll(untouchedPeers);
  bringDownPeers(downPeers);
  EXPECT_EQ(getNumInSyncPeers(detachedPeers[0].peerAddr), 0u);

  /* Recover exactly 2 of them; the other 3 stay blocked and un-promoted. */
  unblockPeersRecording(recoveredPeers);
  drainUntil(
      recoveredPeers,
      [&]() {
        return allPeersInState(recoveredPeers, PeerUpdateState::JOINED_RUNNING);
      },
      "the two recovered peers never returned to JOINED_RUNNING",
      60);
  EXPECT_EQ(getNumInSyncPeers(detachedPeers[0].peerAddr), recoveredPeers.size())
      << "only the two unblocked members should have rejoined";
  for (const auto& peerId : stillDetachedPeers) {
    ASSERT_TRUE(isPeerDetached(peerId.peerAddr))
        << "peer " << peerId.peerAddr.str()
        << " rejoined early; the re-eval must run while it is still detached";
  }

  /*
   * detachPeersBlocked left the block-count threshold at 1 for these peers,
   * which is what detached them in the first place. Raise it now they are back:
   * the re-evaluation re-advertises every route, and into a cap-3 queue that
   * would immediately detach them a second time -- shrinking the in-sync set
   * under the walk rather than testing the walk.
   */
  for (const auto& peerId : recoveredPeers) {
    setSlowPeerThresholds(
        peerId.peerAddr,
        std::chrono::milliseconds(600000),
        1000000,
        std::chrono::milliseconds(600000));
  }

  /* Re-evaluate with 2 members in sync and 3 still detached. */
  const auto result =
      setPeerGroupPolicy(reEvalPeerGroupName(0), kMatchModifyAppendPolicyName);
  EXPECT_EQ(
      result,
      neteng::fboss::bgp::thrift::BgpPolicyChangeResult::POLICIES_APPLIED);
  EXPECT_EQ(getNumInSyncPeers(detachedPeers[0].peerAddr), recoveredPeers.size())
      << "the re-eval must not have promoted the still-blocked members";

  /*
   * The detached members were re-evaluated in place, so their RIB-OUT already
   * carries the new policy while they are still blocked and cannot drain a
   * single byte of it. Asserting this BEFORE releasing them is what separates
   * "re-evaluated in place" from "re-evaluated on rejoin" -- checking only
   * after release would pass either way.
   */
  for (const auto& peerId : stillDetachedPeers) {
    ASSERT_TRUE(isPeerDetached(peerId.peerAddr))
        << "peer " << peerId.peerAddr.str() << " must still be detached here";
    expectRibOutForPolicy(
        peerId.peerAddr, kMatchModifyAppendPolicyName, kNumRoutes);
  }

  /* Release the remaining 3: the re-evaluated routes are delivered. */
  unblockPeersRecording(stillDetachedPeers);
  drainUntil(
      detachedPeers,
      [&]() {
        return allPeersInState(detachedPeers, PeerUpdateState::JOINED_RUNNING);
      },
      "the late-released peers never returned to JOINED_RUNNING",
      60);
  EXPECT_EQ(getNumInSyncPeers(detachedPeers[0].peerAddr), detachedPeers.size());

  publishNextRound();
  drainAllGroups(groups);
  /*
   * Second pass over the recovered group: drainAllGroups walks group 0
   * first, so deliveries that land while the other groups are being
   * drained are only recorded here.
   */
  drainAll(detachedPeers);
  expectPolicyOnPeers(detachedPeers, kMatchModifyAppendPolicyName);
  expectPolicyOnPeers(untouchedPeers, kPermitAllPolicyName);

  XLOGF(INFO, "=== TEST PASSED: PolicyReEvalAfterDepBRecovery ===");
}

TEST_P(GroupWithNoSyncPeersE2ETest, PolicyReEvalAfterDspCollapseRecovery) {
  XLOGF(INFO, "=== TEST: PolicyReEvalAfterDspCollapseRecovery ===");

  setupPolicies();
  setQueueSizeForPeer(
      makePeerSpec(0).peerAddr, /*capacity=*/3, /*highWm=*/2, /*lowWm=*/0);
  setQueueSizeForPeer(
      makePeerSpec(1).peerAddr, /*capacity=*/4, /*highWm=*/3, /*lowWm=*/0);
  auto groups = setupPeersInGroups(
      kNumGroups,
      kPeersPerGroup,
      /*queueCapacity=*/10,
      /*queueHighWm=*/8,
      /*queueLowWm=*/2);
  const auto& operatedGroup = groups[0];
  std::vector<BgpPeerId> dspPeers(
      operatedGroup.begin(), operatedGroup.begin() + 2);
  std::vector<BgpPeerId> downPeers(
      operatedGroup.begin() + 2, operatedGroup.end());
  const auto untouchedPeers = peersOutsideGroup(groups, 0);

  publishNextRound();
  drainAllGroups(groups);

  /* Detach the two, then park them at DRJ with acceptance held. */
  std::string roundComm;
  detachPeersBlocked(dspPeers, downPeers, &roundComm);
  for (const auto& peerId : dspPeers) {
    testOnlyDeferDrjAcceptance(peerId.peerAddr, true);
  }
  unblockPeersRecording(dspPeers);
  drainUntil(
      dspPeers,
      [&]() {
        return allPeersInState(
            dspPeers, PeerUpdateState::DETACHED_READY_TO_JOIN);
      },
      "peers never parked at DETACHED_READY_TO_JOIN",
      40);

  /* Group 0 loses its remaining sync peers, then the DSPs collapse back in. */
  drainAll(downPeers);
  drainAll(untouchedPeers);
  bringDownPeers(downPeers);
  EXPECT_EQ(getNumInSyncPeers(dspPeers[0].peerAddr), 0u);
  for (const auto& peerId : dspPeers) {
    testOnlyDeferDrjAcceptance(peerId.peerAddr, false);
  }
  for (const auto& peerId : dspPeers) {
    EXPECT_TRUE(
        waitForPeerState(peerId.peerAddr, PeerUpdateState::JOINED_RUNNING));
  }

  /* Policy change after the recovery. */
  const auto result =
      setPeerGroupPolicy(reEvalPeerGroupName(0), kMatchModifyAppendPolicyName);
  EXPECT_EQ(
      result,
      neteng::fboss::bgp::thrift::BgpPolicyChangeResult::POLICIES_APPLIED);

  publishNextRound();
  drainAllGroups(groups);
  /*
   * Second pass over the recovered group: drainAllGroups walks group 0
   * first, so deliveries that land while the other groups are being
   * drained are only recorded here.
   */
  drainAll(dspPeers);
  expectPolicyOnPeers(dspPeers, kMatchModifyAppendPolicyName);
  expectPolicyOnPeers(untouchedPeers, kPermitAllPolicyName);

  XLOGF(INFO, "=== TEST PASSED: PolicyReEvalAfterDspCollapseRecovery ===");
}

TEST_P(GroupWithNoSyncPeersE2ETest, PolicyReEvalAfterDfpRecovery) {
  XLOGF(INFO, "=== TEST: PolicyReEvalAfterDfpRecovery ===");

  setupPolicies();
  /*
   * isDFP() requires the candidates' detachedRibVersion to still equal both
   * their own and the GROUP's lastSeenRibVersion, and the group's packing list
   * to be non-empty. Peer 2 is therefore a deliberately DEEPER-queued holder:
   * the candidates block early in a round and are detached, and the holder only
   * crosses its watermark later in the SAME round, pinning the group's send --
   * and so its marker -- for the rest of the test. Everyone else gets a deep
   * queue so the 20-UPDATE round below cannot back them up too.
   */
  for (int i = 0; i < 2; ++i) {
    setQueueSizeForPeer(
        makePeerSpec(i).peerAddr, /*capacity=*/3, /*highWm=*/2, /*lowWm=*/0);
  }
  setQueueSizeForPeer(
      makePeerSpec(2).peerAddr, /*capacity=*/20, /*highWm=*/15, /*lowWm=*/0);
  auto groups = setupPeersInGroups(
      kNumGroups,
      kPeersPerGroup,
      /*queueCapacity=*/100,
      /*queueHighWm=*/80,
      /*queueLowWm=*/2);
  const auto& operatedGroup = groups[0];
  std::vector<BgpPeerId> dfpPeers(
      operatedGroup.begin(), operatedGroup.begin() + 2);
  const auto& holder = operatedGroup[2];
  std::vector<BgpPeerId> drainablePeers(
      operatedGroup.begin() + 3, operatedGroup.end());
  const auto untouchedPeers = peersOutsideGroup(groups, 0);

  publishNextRound();
  drainAllGroups(groups);

  /*
   * Slow-peer auto-detach stays disabled: the block-count threshold lives on
   * the GROUP, so any value low enough to detach the candidates also detaches
   * the holder. The candidates are detached explicitly instead.
   */
  setSlowPeerThresholds(
      holder.peerAddr,
      std::chrono::milliseconds(600000),
      1000000,
      std::chrono::milliseconds(600000));

  /* One round split across 20 attribute groups blocks candidates then holder.
   */
  blockPeer(holder.peerAddr);
  for (const auto& peerId : dfpPeers) {
    blockPeer(peerId.peerAddr);
  }
  const auto roundComm = publishNextRound(kNumRoutes, /*numAttrBuckets=*/20);
  /*
   * Detach each candidate as soon as its distinct watermark is reached. This
   * lets a serialized send advance to the next candidate and then the holder.
   */
  for (size_t idx = 0; idx < dfpPeers.size(); ++idx) {
    const auto& peerId = dfpPeers[idx];
    ASSERT_TRUE(waitForPeerQueueBlocked(peerId))
        << "DFP candidate " << peerId.peerAddr.str() << " never filled up";

    if (idx == 0) {
      /*
       * Let the group catch up before detaching so the consume timer cannot
       * move its version after the detach and invalidate isDFP().
       */
      WITH_RETRIES_N(30, {
        EXPECT_EVENTUALLY_EQ(
            getGroupRibVersion(holder.peerAddr), getRibVersion());
      });
    }

    detachBlockedPeerDirectly(peerId.peerAddr);
    ASSERT_TRUE(
        waitForPeerState(peerId.peerAddr, PeerUpdateState::DETACHED_BLOCKED));
  }

  /* With the candidates gone the group resumes the REST of the same round,
   * which is what finally blocks the holder. */
  {
    std::vector<BgpPeerId> toDrain(drainablePeers);
    toDrain.insert(toDrain.end(), untouchedPeers.begin(), untouchedPeers.end());
    drainUntil(
        toDrain,
        [&]() {
          return getPeerState(holder.peerAddr) ==
              PeerUpdateState::JOINED_BLOCKED;
        },
        "holder never blocked on the remainder of the detach round",
        40);
  }
  ASSERT_FALSE(isPeerDetached(holder.peerAddr))
      << "the holder must stay in sync to keep the group's packing list "
         "non-empty";

  /*
   * NOTHING IS PUBLISHED from here on, and DRJ acceptance is NOT deferred:
   * consuming another round would move the candidates past their
   * detachedRibVersion and fail isDFP(), and deferring would make the send
   * guard's checkAndAcceptReadyToJoinPeers skip them -- either one silently
   * turns this into the DSP collapse case.
   */
  unblockPeersRecording(dfpPeers);
  drainUntil(
      dfpPeers,
      [&]() {
        return allPeersInState(
            dfpPeers, PeerUpdateState::DETACHED_READY_TO_JOIN);
      },
      "peers never parked at DETACHED_READY_TO_JOIN",
      40);
  for (const auto& peerId : dfpPeers) {
    ASSERT_TRUE(isDetachedFastPeer(peerId.peerAddr))
        << "peer " << peerId.peerAddr.str()
        << " parked at DRJ as a DSP, not a DFP -- the group or the peer moved "
           "after the detach";
  }

  /* Everything else in group 0 goes down, holder LAST and still blocked. */
  drainAll(drainablePeers);
  bringDownPeers(drainablePeers);
  ASSERT_TRUE(
      allPeersInState(dfpPeers, PeerUpdateState::DETACHED_READY_TO_JOIN))
      << "the DFPs must still be detached when the last sync peer goes down";
  bringDownPeer(holder.peerAddr);
  EXPECT_EQ(getNumInSyncPeers(dfpPeers[0].peerAddr), dfpPeers.size())
      << "the DFPs were not accepted back when the last sync peer went down";
  for (const auto& peerId : dfpPeers) {
    EXPECT_TRUE(
        waitForPeerState(peerId.peerAddr, PeerUpdateState::JOINED_RUNNING));
  }

  /* Policy change after the recovery. */
  const auto result =
      setPeerGroupPolicy(reEvalPeerGroupName(0), kMatchModifyAppendPolicyName);
  EXPECT_EQ(
      result,
      neteng::fboss::bgp::thrift::BgpPolicyChangeResult::POLICIES_APPLIED);

  publishNextRound();
  drainAllGroups(groups);
  /*
   * Second pass over the recovered group: drainAllGroups walks group 0
   * first, so deliveries that land while the other groups are being
   * drained are only recorded here.
   */
  drainAll(dfpPeers);
  expectPolicyOnPeers(dfpPeers, kMatchModifyAppendPolicyName);
  expectPolicyOnPeers(untouchedPeers, kPermitAllPolicyName);

  XLOGF(INFO, "=== TEST PASSED: PolicyReEvalAfterDfpRecovery ===");
}

/*
 * C2 flavour 4: DEP-A promotion, then the policy change.
 *
 * The A4a shape inside a 4-group setup. Group 0's members are all blocked so
 * one of them can be pinned at JOINED_BLOCKED and the group stops consuming the
 * change list; the other 9 are flapped while extra rounds run the RIB ahead of
 * the group, so they come back as DEP-A (ahead of the group, nothing detached
 * after joining). Taking the pinned peer down promotes one DEP-A in place --
 * its RIB-OUT becomes the group's truth -- and only AFTER that recovery is
 * MODIFY/APPEND applied, to prove the re-evaluation still reaches a group whose
 * truth came from a promoted peer.
 */
TEST_P(GroupWithNoSyncPeersE2ETest, PolicyReEvalAfterDepAPromotionRecovery) {
  XLOGF(INFO, "=== TEST: PolicyReEvalAfterDepAPromotionRecovery ===");

  setupPolicies();
  /* The first member must deterministically suspend the serialized send. */
  setQueueSizeForPeer(
      makePeerSpec(0).peerAddr, /*capacity=*/3, /*highWm=*/2, /*lowWm=*/0);
  for (int i = 1; i < kPeersPerGroup; ++i) {
    setQueueSizeForPeer(
        makePeerSpec(i).peerAddr, /*capacity=*/4, /*highWm=*/3, /*lowWm=*/0);
  }

  auto groups = setupPeersInGroups(
      kNumGroups,
      kPeersPerGroup,
      /*queueCapacity=*/10,
      /*queueHighWm=*/8,
      /*queueLowWm=*/2);
  const auto& operatedGroup = groups[0];
  const auto& blockedSyncPeer = operatedGroup[0];
  std::vector<BgpPeerId> lateJoiners(
      operatedGroup.begin() + 1, operatedGroup.end());
  std::vector<BgpPeerId> readyLateJoiners(
      operatedGroup.begin() + 1, operatedGroup.begin() + 9);
  const auto untouchedPeers = peersOutsideGroup(groups, 0);

  publishNextRound();
  drainAllGroups(groups);

  /*
   * Flap group 0's other 9 members into DEP-A, draining the other groups so
   * they stay healthy through the extra rounds. Peer 9 returns blocked.
   */
  std::string roundComm;
  flapPeersIntoDepA(
      blockedSyncPeer,
      lateJoiners,
      /*returnBlocked=*/{operatedGroup[9]},
      /*extraRounds=*/2,
      &roundComm,
      untouchedPeers,
      /*singleEoR=*/true);
  expectAllDepA(lateJoiners);
  {
    std::vector<BgpPeerId> toDrain(readyLateJoiners);
    toDrain.insert(toDrain.end(), untouchedPeers.begin(), untouchedPeers.end());
    drainUntil(
        toDrain,
        [&]() {
          return allPeersInState(
              readyLateJoiners, PeerUpdateState::DETACHED_READY_TO_JOIN);
        },
        "peers never parked at DETACHED_READY_TO_JOIN",
        40);
  }
  expectAllAheadOfGroup(readyLateJoiners);

  /* Hold the ordinary rejoin path so the DEP-A promotion branch is reached. */
  for (const auto& peerId : readyLateJoiners) {
    testOnlyDeferDrjAcceptance(peerId.peerAddr, true);
  }

  /* Last sync peer down: one DEP-A is promoted in place. */
  bringDownPeer(blockedSyncPeer.peerAddr);
  EXPECT_EQ(getNumInSyncPeers(operatedGroup[1].peerAddr), 1u)
      << "exactly one DEP-A should have been promoted to carry the group";

  /* Finish the recovery: the rest rejoin and the blocked one is released. */
  for (const auto& peerId : readyLateJoiners) {
    testOnlyDeferDrjAcceptance(peerId.peerAddr, false);
  }
  unblockPeersRecording({operatedGroup[9]});

  /*
   * Change the policy now that a promoted DEP-A carries the group.
   */
  const auto result =
      setPeerGroupPolicy(reEvalPeerGroupName(0), kMatchModifyAppendPolicyName);
  EXPECT_EQ(
      result,
      neteng::fboss::bgp::thrift::BgpPolicyChangeResult::POLICIES_APPLIED);

  publishNextRound();
  drainAllGroups(groups);
  /*
   * Second pass over the recovered group: drainAllGroups walks group 0
   * first, so deliveries that land while the other groups are being
   * drained are only recorded here.
   */
  drainAll(lateJoiners);
  expectPolicyOnPeers(lateJoiners, kMatchModifyAppendPolicyName);
  expectPolicyOnPeers(untouchedPeers, kPermitAllPolicyName);

  XLOGF(INFO, "=== TEST PASSED: PolicyReEvalAfterDepAPromotionRecovery ===");
}

INSTANTIATE_TEST_SUITE_P(
    SerializationModes,
    GroupWithNoSyncPeersE2ETest,
    ::testing::Values(kNoSerialization, kWithSerialization),
    [](const ::testing::TestParamInfo<SerializationParams>& info) {
      return info.param.name;
    });

} // namespace bgp
} // namespace facebook
