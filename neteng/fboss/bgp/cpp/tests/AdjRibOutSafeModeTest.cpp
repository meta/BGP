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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#define AdjRib_TEST_FRIENDS                                                  \
  friend class AdjRibOutboundFixture;                                        \
  FRIEND_TEST(AdjRibOutboundFixture, EgressTotalPathLimitEntersSafeMode);    \
  FRIEND_TEST(                                                               \
      AdjRibOutboundFixture, EgressTotalPathLimitDropsExcessPrefixesOnly);   \
  FRIEND_TEST(AdjRibOutboundFixture, EgressLocalRoutesAdvertisedOverLimit);  \
  FRIEND_TEST(AdjRibOutboundFixture, EgressAddPathExplosionEntersSafeMode);  \
  FRIEND_TEST(AdjRibOutboundFixture, EgressUnaffectedWithoutSwitchLimit);    \
  FRIEND_TEST(AdjRibOutboundFixture, EgressLimitNotEnforcedWithUpdateGroup); \
  FRIEND_TEST(AdjRibOutboundFixture, EgressSafeModeBlocksAllAdjRibs);        \
  FRIEND_TEST(AdjRibOutboundFixture, EgressExistingEntryUpdatedOverLimit);

#include <fmt/core.h>
#include <folly/coro/BlockingWait.h>

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibStats.h"
#include "neteng/fboss/bgp/cpp/rib/RibPolicy.h"
#include "neteng/fboss/bgp/cpp/tests/AdjRibOutUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibPolicyUtils.h"

using namespace facebook::bgp;
using namespace facebook::nettools::bgplib;
using namespace facebook::neteng::fboss::bgp::thrift;

namespace facebook::bgp {

namespace {

const auto kPrefix = kV4Prefix1;
const auto kNonGoldenPrefix = kV4Prefix2;

std::shared_ptr<thrift::BgpSwitchLimitConfig> makeSwitchLimitConfig(
    thrift::OverloadProtectionMode mode,
    int64_t totalPathLimit) {
  auto config = std::make_shared<thrift::BgpSwitchLimitConfig>();
  config->overload_protection_mode() = mode;
  config->total_path_limit() = totalPathLimit;
  return config;
}

/*
 * observerQ_ also carries the egress EoR notifications raised by an initial
 * dump, so assert on the presence of a TriggerSafeMode specifically rather
 * than on the queue being empty.
 */
bool hasTriggerSafeMode(MonitoredMPMCQueue<AdjRib::ObservableMessageT>& queue) {
  while (!queue.empty()) {
    auto msg = folly::coro::blockingWait(queue.pop());
    if (std::holds_alternative<AdjRib::TriggerSafeMode>(msg.message)) {
      return true;
    }
  }
  return false;
}

} // namespace

/*
 * Egress overload protection. Before S696431 the switch limits were only
 * enforced at RIB-IN, so a RIB-OUT expansion could push the device past
 * total_path_limit without ever entering safe mode. The egress side drops the
 * excess and hands off to PeerManagerBase; the golden prefix policy is applied
 * only at RIB-IN, by the re-evaluation that handoff kicks off.
 */

/*
 * Over total_path_limit under APPLY_GOLDEN_PREFIX_POLICY: the excess prefix is
 * dropped and TriggerSafeMode reaches PeerManagerBase, which persists safe mode
 * and starts the RIB-IN re-evaluation.
 */
TEST_F(AdjRibOutboundFixture, EgressTotalPathLimitEntersSafeMode) {
  totalRcvdPrefixCount = 0;
  totalSentPrefixCount = 0;
  setupAdjRib();
  adjRib_->switchLimitConfig_ = makeSwitchLimitConfig(
      thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY,
      0 /* totalPathLimit */);

  fm_->addTask([&] {
    pushRibOutMsgToAdjRib(createRibMultipleAnnounce(
        {kPrefix}, kV4Nexthop1, eBgpPeer_, true /* sendWithEoR */));

    EXPECT_TRUE(adjRib_->isSafeModeOn());
    EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/false, kPrefix));

    terminateAdjRib();
  });
  evb_.loop();

  EXPECT_TRUE(hasTriggerSafeMode(observerQ_));
}

/*
 * Under DROP_EXCESS_PREFIXES the excess prefix is dropped just the same, but
 * safe mode is not persisted and PeerManagerBase is not notified.
 *
 * This mirrors ingress: in dropPrefixForOverloadProtection the
 * triggerSafeMode() call sits inside the APPLY_GOLDEN_PREFIX_POLICY case, and
 * DROP_EXCESS_PREFIXES falls straight through to returning whether the limit
 * was exceeded. Safe mode exists to apply the golden prefix policy, and in drop
 * mode there is no policy to re-evaluate against --
 * processAdjRibReEvaluationForSafeMode early-returns when goldenPrefixPolicy_
 * is null. Drop mode is also self-correcting: it is recomputed per prefix from
 * the live count and resumes once withdrawals bring the count back down, which
 * latching would defeat.
 */
TEST_F(AdjRibOutboundFixture, EgressTotalPathLimitDropsExcessPrefixesOnly) {
  totalRcvdPrefixCount = 0;
  totalSentPrefixCount = 0;
  setupAdjRib();
  adjRib_->switchLimitConfig_ = makeSwitchLimitConfig(
      thrift::OverloadProtectionMode::DROP_EXCESS_PREFIXES,
      0 /* totalPathLimit */);

  fm_->addTask([&] {
    pushRibOutMsgToAdjRib(createRibMultipleAnnounce(
        {kPrefix}, kV4Nexthop1, eBgpPeer_, true /* sendWithEoR */));

    EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/false, kPrefix));
    EXPECT_FALSE(adjRib_->isSafeModeOn());

    terminateAdjRib();
  });
  evb_.loop();

  EXPECT_FALSE(hasTriggerSafeMode(observerQ_));
}

/*
 * Locally originated routes are advertised even over the limit. This device is
 * authoritative for them, so withdrawing an aggregate would black-hole
 * everything behind it, and the RIB-IN purge cannot reach them anyway. Safe
 * mode is still triggered, so the peer-learned routes get re-evaluated.
 */
TEST_F(AdjRibOutboundFixture, EgressLocalRoutesAdvertisedOverLimit) {
  totalRcvdPrefixCount = 0;
  totalSentPrefixCount = 0;
  setupAdjRib();
  adjRib_->switchLimitConfig_ = makeSwitchLimitConfig(
      thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY,
      0 /* totalPathLimit */);

  fm_->addTask([&] {
    // localPeerV4_ carries the zero peer address that marks a local route.
    ASSERT_TRUE(localPeerV4_.addr.isZero());
    pushRibOutMsgToAdjRib(createRibMultipleAnnounce(
        {kPrefix}, kV4Nexthop1, localPeerV4_, true /* sendWithEoR */));

    EXPECT_NE(nullptr, adjRib_->getRibEntry(/*ingress=*/false, kPrefix));
    EXPECT_TRUE(adjRib_->isSafeModeOn());

    terminateAdjRib();
  });
  evb_.loop();

  EXPECT_TRUE(hasTriggerSafeMode(observerQ_));
}

/*
 * S696431 shape. Add-path advertises one prefix under many nexthops, so the
 * RIB-OUT path count blows past total_path_limit while the prefix count stays
 * at one. A prefix-keyed golden policy is blind to this -- every path carries
 * the same prefix -- so only the egress path count catches it. Before this
 * change the limit was checked solely at RIB-IN and a RIB-OUT expansion never
 * entered safe mode at all.
 */
TEST_F(AdjRibOutboundFixture, EgressAddPathExplosionEntersSafeMode) {
  totalRcvdPrefixCount = 0;
  totalSentPrefixCount = 0;
  setupAdjRib(true /* sendAddPath */);

  constexpr uint32_t kNumPaths = 8;
  constexpr int64_t kTotalPathLimit = 4;

  adjRib_->switchLimitConfig_ = makeSwitchLimitConfig(
      thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY,
      kTotalPathLimit);

  fm_->addTask([&] {
    RibOutAnnouncement announcement;
    announcement.initialDump = true;
    /*
     * RIB-OUT allocates a path ID per (prefix, nexthop) in
     * tryInsertRibOutEntry, so distinct nexthops are what multiply the egress
     * entries -- one path per next-hop router, as add-path does.
     */
    for (uint32_t path = 1; path <= kNumPaths; ++path) {
      announcement.addPathEntries.push_back(createRibOutAnnounceEntry(
          kPrefix,
          folly::IPAddress(fmt::format("10.1.1.{}", path)),
          eBgpPeer_,
          BgpAttrOrigin::BGP_ORIGIN_IGP,
          {} /* communities */,
          std::nullopt /* aspaths */,
          std::nullopt /* locPref */,
          path /* pathIdToSend */));
    }
    pushRibOutMsgToAdjRib(announcement);

    EXPECT_TRUE(adjRib_->isSafeModeOn());

    /*
     * All the paths share one prefix, so the RIB-OUT tree holds a single node
     * throughout -- the explosion is in the per-path count underneath it, which
     * postOutPrefixCount tracks. Paths are admitted while
     * totalSentPrefixCount + 1 <= total_path_limit, so exactly kTotalPathLimit
     * land and the rest are refused.
     */
    EXPECT_EQ(
        1,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/true));
    EXPECT_EQ(kTotalPathLimit, adjRib_->getStats().getPostOutPrefixCount());

    terminateAdjRib();
  });
  evb_.loop();

  EXPECT_TRUE(hasTriggerSafeMode(observerQ_));
}

/*
 * No switch limit configured: egress announcements are unaffected.
 */
TEST_F(AdjRibOutboundFixture, EgressUnaffectedWithoutSwitchLimit) {
  totalRcvdPrefixCount = 0;
  totalSentPrefixCount = 0;
  setupAdjRib();
  ASSERT_EQ(nullptr, adjRib_->switchLimitConfig_);

  fm_->addTask([&] {
    pushRibOutMsgToAdjRib(createRibMultipleAnnounce(
        {kPrefix}, kV4Nexthop1, eBgpPeer_, true /* sendWithEoR */));

    EXPECT_NE(nullptr, adjRib_->getRibEntry(/*ingress=*/false, kPrefix));
    EXPECT_FALSE(adjRib_->isSafeModeOn());

    terminateAdjRib();
  });
  evb_.loop();

  EXPECT_FALSE(hasTriggerSafeMode(observerQ_));
}

/*
 * Update group collapses RIB-OUT to O(Groups) while totalSentPrefixCount still
 * counts one per in-sync peer, so the limit would be more restrictive than
 * intended. It is not enforced until that is defined for update group.
 */
TEST_F(AdjRibOutboundFixture, EgressLimitNotEnforcedWithUpdateGroup) {
  totalRcvdPrefixCount = 0;
  totalSentPrefixCount = 0;
  setupAdjRib();
  adjRib_->setEnableUpdateGroup(true);
  adjRib_->switchLimitConfig_ = makeSwitchLimitConfig(
      thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY,
      0 /* totalPathLimit */);

  fm_->addTask([&] {
    pushRibOutMsgToAdjRib(createRibMultipleAnnounce(
        {kPrefix}, kV4Nexthop1, eBgpPeer_, true /* sendWithEoR */));

    EXPECT_NE(nullptr, adjRib_->getRibEntry(/*ingress=*/false, kPrefix));
    EXPECT_FALSE(adjRib_->isSafeModeOn());

    terminateAdjRib();
  });
  evb_.loop();

  EXPECT_FALSE(hasTriggerSafeMode(observerQ_));
}

/*
 * Safe mode is one flag shared by every AdjRib, so tripping the limit on one
 * peer's egress puts an unrelated peer into safe mode too, and that peer's
 * ingress admission path starts rejecting non-golden prefixes. Checked with the
 * limit relaxed afterwards, so the ingress decision turns purely on safe mode
 * having latched rather than on the path count.
 */
TEST_F(AdjRibOutboundFixture, EgressSafeModeBlocksAllAdjRibs) {
  totalRcvdPrefixCount = 0;
  totalSentPrefixCount = 0;
  setupAdjRib();
  adjRib_->switchLimitConfig_ = makeSwitchLimitConfig(
      thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY,
      0 /* totalPathLimit */);

  /*
   * An unrelated peer's AdjRib, sharing the isSafeModeOn_ that PeerManagerBase
   * hands to every AdjRib it creates. The fixture gives each AdjRib its own
   * flag, so the sharing has to be wired up explicitly here.
   */
  auto otherAdjRib = std::make_shared<AdjRib>(
      BgpPeerId(
          folly::IPAddress("10.0.0.2"),
          folly::IPAddressV4("255.0.0.2").toLongHBO()),
      PeeringParams{fboss::kDefaultVrf},
      evb_,
      ribInQ_,
      observerQ_,
      std::make_shared<folly::coro::Baton>(),
      nullptr /* policyManager */,
      adjRib_->isSafeModeOn_);
  otherAdjRib->setGoldenPrefixPolicy(
      std::make_shared<GoldenPrefixPolicy>(createTGoldenPrefixPolicy(
          {kPrefix}, 10 /* maxSubnets */, {24} /* allowedMaskLengths */)));
  ASSERT_FALSE(otherAdjRib->isSafeModeOn());

  fm_->addTask([&] {
    // Trip the limit on this peer's egress path.
    pushRibOutMsgToAdjRib(createRibMultipleAnnounce(
        {kPrefix}, kV4Nexthop1, eBgpPeer_, true /* sendWithEoR */));
    ASSERT_TRUE(adjRib_->isSafeModeOn());

    // The unrelated peer is in safe mode as a result.
    EXPECT_TRUE(otherAdjRib->isSafeModeOn());

    const auto relaxed = makeSwitchLimitConfig(
        thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY,
        1000 /* totalPathLimit, plenty of headroom */);
    const BgpPath attrs; // empty

    // ... and its ingress admission path now rejects non-golden prefixes.
    EXPECT_TRUE(otherAdjRib->dropPrefixForOverloadProtection(
        AdjRibStats::getTotalSwitchPathCount() + 1,
        kNonGoldenPrefix,
        attrs,
        relaxed));
    // Golden prefixes still get through.
    EXPECT_FALSE(otherAdjRib->dropPrefixForOverloadProtection(
        AdjRibStats::getTotalSwitchPathCount() + 1, kPrefix, attrs, relaxed));

    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * The limit only gates new RIB-OUT entries. Updating one that already exists
 * adds no entry, so it goes through even over the limit -- dropping it would
 * leave the peer holding stale attributes for a prefix it still has.
 */
TEST_F(AdjRibOutboundFixture, EgressExistingEntryUpdatedOverLimit) {
  totalRcvdPrefixCount = 0;
  totalSentPrefixCount = 0;
  setupAdjRib();
  adjRib_->switchLimitConfig_ = makeSwitchLimitConfig(
      thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY,
      1000 /* totalPathLimit, plenty of headroom */);

  fm_->addTask([&] {
    pushRibOutMsgToAdjRib(createRibMultipleAnnounce(
        {kPrefix}, kV4Nexthop1, eBgpPeer_, true /* sendWithEoR */));
    auto* entry = adjRib_->getRibEntry(/*ingress=*/false, kPrefix);
    ASSERT_NE(nullptr, entry);
    ASSERT_EQ(kV4Nexthop1, entry->getPreOut()->getNexthop());

    /*
     * Now over the limit, re-announce the same prefix with a different
     * nexthop. Without add-path this maps to the same path id, so it updates
     * the existing entry rather than creating one.
     */
    adjRib_->switchLimitConfig_ = makeSwitchLimitConfig(
        thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY,
        0 /* totalPathLimit */);
    pushRibOutMsgToAdjRib(createRibMultipleAnnounce(
        {kPrefix}, kV4Nexthop2, eBgpPeer_, true /* sendWithEoR */));

    // The entry survives and took the update.
    entry = adjRib_->getRibEntry(/*ingress=*/false, kPrefix);
    ASSERT_NE(nullptr, entry);
    EXPECT_EQ(kV4Nexthop2, entry->getPreOut()->getNexthop());

    terminateAdjRib();
  });
  evb_.loop();
}

} // namespace facebook::bgp
