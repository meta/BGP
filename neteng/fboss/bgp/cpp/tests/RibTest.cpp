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

#define RibBase_TEST_FRIENDS                                                   \
  FRIEND_TEST(RibFixture, EorSentInitializationEvent);                         \
  FRIEND_TEST(RibFixture, GetRibEntries);                                      \
  FRIEND_TEST(RibFixture, LocalRouteIgpCostSerialization);                     \
  FRIEND_TEST(                                                                 \
      RibAllocatedPathIdFixture,                                               \
      FullAddPathWithdrawalClearsAdvertisedMultipaths);                        \
  FRIEND_TEST(RibFixture, GetRibEntriesCanonical);                             \
  FRIEND_TEST(RibFixture, GetRibPrefixCanonical);                              \
  FRIEND_TEST(RibFixture, GetRibEntriesForCommunitiesCanonical);               \
  FRIEND_TEST(RibFixture, GetRibSubprefixesCanonical);                         \
  FRIEND_TEST(RibFixture, GetRibEntriesWithCommunityFilter);                   \
  FRIEND_TEST(RibFixture, GetRibEntryForPrefix);                               \
  FRIEND_TEST(RibFixture, GetRibEntryWeightedNexthopsTest);                    \
  FRIEND_TEST(RibFixture, GetRibEntriesForSubprefixes);                        \
  FRIEND_TEST(RibFixture, UpdateEntryStatsTest);                               \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathRemovedAndRibDumpReqTest);       \
  FRIEND_TEST(RibFixtureAddPathTestSuite, RouteWithdrawnAndRibDumpReqTest);    \
  FRIEND_TEST(                                                                 \
      RibFixtureAddPathTestSuite,                                              \
      PrepareFibProgrammingFullMultipathWithdrawal);                           \
  FRIEND_TEST(RibFixture, GetSelectionFilterCriteriaTest);                     \
  FRIEND_TEST(RibFixture, LbwCommunityForward);                                \
  FRIEND_TEST(RibFixture, LbwCommunitySuppression1);                           \
  FRIEND_TEST(RibFixture, LbwCommunitySuppression2);                           \
  FRIEND_TEST(RibFixture, LbwCommunityBestPath);                               \
  FRIEND_TEST(RibNoUcmpComputeFixture, LbwCommunityBestPathNoUcmpCompute);     \
  FRIEND_TEST(RibFixture, AggregateUcmpWeight);                                \
  FRIEND_TEST(RibFixture, ProgramTopoInfoTest);                                \
  FRIEND_TEST(RibFixture, NoLbwECMP);                                          \
  FRIEND_TEST(RibFixtureAddPathTestSuite, EoR);                                \
  FRIEND_TEST(RibFixture, InitialDumpChunkTest);                               \
  FRIEND_TEST(RibFixture, MultipleFullSyncRequest);                            \
  FRIEND_TEST(RibFixture, GetOriginatedRoutes);                                \
  FRIEND_TEST(RibFixture, RibAnnouncementFromSamePeerWithoutBestPathChange);   \
  FRIEND_TEST(RibFixture, NoBestPathNexthopChangeAddPath);                     \
  FRIEND_TEST(RibFixture, NoBestPathNexthopChangeAddPathWithRibPolicyMNHTest); \
  FRIEND_TEST(RibFixture, PartialDrainAnnouncesWhenBestpathPointerUnchanged);  \
  FRIEND_TEST(RibFixture, PartialDrainStatusReflectsRibState);                 \
  FRIEND_TEST(RibFixture, PartialDrainTransitionCountOnlyOnDeviceFlip);        \
  FRIEND_TEST(RibFixture, PartialDrainTransitionCountMultiPrefix);             \
  FRIEND_TEST(RibFixture, PartialDrainMnhThresholdUpdatedAcrossDrainCycle);    \
  FRIEND_TEST(RibFixture, PartialDrainUnderflowGuard);                         \
  FRIEND_TEST(RibFixture, PartialDrainOdsStateOnTransition);                   \
  FRIEND_TEST(RibFixture, BestPathWithAddPathEnabled_OnlyPathIdChange);        \
  FRIEND_TEST(                                                                 \
      RibFixture, IncrementalWithdrawnChunkTestAfterReadOnlyForAddPath);       \
  FRIEND_TEST(RibFixture, RibInAddPath);                                       \
  FRIEND_TEST(RibWithLocalRouteFixture, RouteAggregationStress);               \
  FRIEND_TEST(RibFixture, RoutesAnnouncedPerAttr);                             \
  FRIEND_TEST(RibFixture, WithdrawRouteWithAgentReconnectTest);                \
  FRIEND_TEST(RibFixture, EnableUnicastRouteLoggingTest);                      \
  FRIEND_TEST(RibFixture, TestStop);                                           \
  FRIEND_TEST(RibFixture, ScubaLoggingTest);                                   \
  FRIEND_TEST(RibFixture, ReplaceRouteFilterPolicyLoggingTest);                \
  FRIEND_TEST(RibFixture, ReplacePathSelectionPolicyLoggingTest);              \
  FRIEND_TEST(RibFixture, RibPauseTimeOutTest);                                \
  FRIEND_TEST(RibFixture, RibPauseTimeOutMultipleTasksTest);                   \
  FRIEND_TEST(RibFixture, RouteChurnDetectionTest);                            \
  FRIEND_TEST(RibFixture, RouteChurnDetectionTestBeforeRibInitialization);     \
  FRIEND_TEST(RibFixture, AnnounceIncludesPathIdOnRouteInfo);                  \
  FRIEND_TEST(                                                                 \
      RibFixture, RouteChurnDetectionTestWithAnnouncementsAndWithdraws);       \
  FRIEND_TEST(                                                                 \
      RibNexthopTrackingFixture, RibInAnnouncementWithNexthopTracking);        \
  FRIEND_TEST(                                                                 \
      RibNexthopTrackingFixture, PathUpdatesMaintainNexthopAssociations);      \
  FRIEND_TEST(                                                                 \
      RibNexthopTrackingFixture,                                               \
      RibInAnnouncementRequestsNexthopSubscription);                           \
  FRIEND_TEST(RibNexthopTrackingFixture, RibInNexthopUpdate);                  \
  FRIEND_TEST(                                                                 \
      RibNexthopTrackingFixture, ReachabilityAndResolutionAreIndependent);     \
  FRIEND_TEST(                                                                 \
      RibNexthopTrackingFixture, SourceOnlyReachabilityChangeDoesNotReselect); \
  FRIEND_TEST(                                                                 \
      RibNexthopTrackingFixture,                                               \
      StaleQueuedNexthopUpdateDoesNotRecreateUntrackedState);                  \
  FRIEND_TEST(                                                                 \
      RibNexthopTrackingFixture,                                               \
      QueuedUnreachableUpdateDoesNotOverwriteReregisteredStatus);              \
  FRIEND_TEST(                                                                 \
      RibNexthopTrackingFixture,                                               \
      QueuedReachableUpdateDoesNotOverwriteReregisteredStatus);                \
  FRIEND_TEST(                                                                 \
      RibNexthopTrackingFixture,                                               \
      OutOfOrderNexthopUpdatesUseLatestCachedStatus);                          \
  FRIEND_TEST(                                                                 \
      RibNexthopTrackingFixture, StaleNexthopUpdateIsSkippedAndCounted);       \
  FRIEND_TEST(                                                                 \
      RibNexthopTrackingFixture, NexthopUpdatesForTwoNexthopsApplyOutOfOrder); \
  FRIEND_TEST(RibNexthopTrackingFixture, UnstampedNexthopUpdateAlwaysRereads); \
  FRIEND_TEST(RibNexthopTrackingFixture, NexthopUpdateWithEqualEpochRereads);  \
  FRIEND_TEST(                                                                 \
      RibNexthopTrackingFixture,                                               \
      SkippedNexthopUpdateStillRunsInitialPathComputation);                    \
  FRIEND_TEST(                                                                 \
      RibNexthopTrackingFixture,                                               \
      ReplacedNexthopStaysInMapUntilPendingBatchIsTaken);                      \
  FRIEND_TEST(                                                                 \
      RibNexthopTrackingFixture,                                               \
      WithdrawnNexthopStaysInMapUntilPendingBatchIsTaken);                     \
  FRIEND_TEST(                                                                 \
      RibNexthopTrackingFixture,                                               \
      RelinkedNexthopIsKeptAfterPendingBatchIsTaken);                          \
  FRIEND_TEST(RibNexthopTrackingFixture, RouteInfoStoresRibEntryPointer);      \
  FRIEND_TEST(RibNexthopTrackingFixture, RouteFlappingWithUniqueNexthop);      \
  FRIEND_TEST(RibNexthopTrackingFixture, GetNexthopInfoForNexthop);            \
  FRIEND_TEST(                                                                 \
      RibWithLocalRouteFixture,                                                \
      ConditionalLocalRoute_NotOriginatedWithoutNexthopResolution);            \
  FRIEND_TEST(                                                                 \
      RibWithLocalRouteFixture, ConditionalLocalRoute_OriginatedWhenResolved); \
  FRIEND_TEST(                                                                 \
      RibWithLocalRouteFixture,                                                \
      ConditionalLocalRoute_WithdrawnWhenUnresolved);                          \
  FRIEND_TEST(                                                                 \
      RibWithLocalRouteFixture,                                                \
      ConditionalLocalRoute_MultipleRoutesForSameNexthop);                     \
  FRIEND_TEST(                                                                 \
      RibWithLocalRouteFixture,                                                \
      ConditionalLocalRoute_ReResolutionAfterWithdrawal);                      \
  FRIEND_TEST(                                                                 \
      RibWithLocalRouteFixture,                                                \
      ConditionalLocalRoute_MixedResolvedAndUnresolved);                       \
  FRIEND_TEST(                                                                 \
      RibWithLocalRouteFixture, ConditionalLocalRoute_UnknownNexthopNoOp);     \
  FRIEND_TEST(RibFixture, RibVersionIncrementsOnBestpathChange);               \
  FRIEND_TEST(RibFixture, RibVersionNoChangeOnDuplicateRoute);                 \
  FRIEND_TEST(RibFixture, RibVersionDistinctPerPrefixInWithdrawal);            \
  FRIEND_TEST(                                                                 \
      RibFixture, RibVersionAddPathWithdrawalsSpanningChunksBumpAcrossChunks); \
  FRIEND_TEST(RibFixture, HandleDeletedUnicastRouteIgnoresProgrammedRoute);    \
  FRIEND_TEST(RibFixture, HandleDeletedUnicastRouteErasesEntryWithNoPaths);    \
  FRIEND_TEST(RibFixture, HandleDeletedUnicastRouteKeepsEntryOnFibBatchList);  \
  FRIEND_TEST(RibFixture, HandleStaleUnicastRouteSameNexthops);                \
  FRIEND_TEST(RibFixture, HandleStaleUnicastRouteDifferentNexthops);           \
  FRIEND_TEST(RibFixture, HandleStaleUnicastRouteComparesPointers);            \
  FRIEND_TEST(RibFixture, HandleAddUpdatedUnicastRouteUnchangedBumpsVersion);  \
  FRIEND_TEST(                                                                 \
      RibFixture, HandleAddUpdatedUnicastRouteNewPrefixNewlyInstalled);        \
  FRIEND_TEST(                                                                 \
      RibFixture, HandleAddUpdatedUnicastRouteAnnouncesBestpathChange);        \
  FRIEND_TEST(RibFixture, HandleAddUpdatedUnicastRouteAnnouncesMultipaths);    \
  FRIEND_TEST(                                                                 \
      RibFixture, HandleAddUpdatedUnicastRouteWithdrawsDroppedNexthop);        \
  FRIEND_TEST(                                                                 \
      RibFixture, HandleAddUpdatedUnicastRouteNoAddPathWithoutBestpath);       \
  FRIEND_TEST(RibFixture, HandleAddUpdatedUnicastRouteAddPathSpansChunks);     \
  FRIEND_TEST(RibFixture, HandleAddUpdatedUnicastRouteWithdrawalSpansChunks);  \
  FRIEND_TEST(RibFixture, FibProgrammedSkippedRoutesDoNotConsumeRibVersion);   \
  FRIEND_TEST(RibFixture, CreateTRibEntryBestGroupReflectsBestPathPresence);   \
  FRIEND_TEST(RibFibNexthopDatabaseFixture, KeepsCompleteNexthopSetsDistinct); \
  FRIEND_TEST(RibFibNexthopDatabaseFixture, PropagatesConversionFailure);      \
  FRIEND_TEST(RibFibNexthopDatabaseFixture, TracksEmptySubmittedNexthopSet);   \
  FRIEND_TEST(                                                                 \
      RibAllocatedPathIdFixture, FullAddPathWithdrawalUsesAdvertisedPathIds);  \
  FRIEND_TEST(                                                                 \
      RibAllocatedPathIdFixture,                                               \
      FullAddPathWithdrawalSharedNexthopWithdrawsEachPathId);                  \
  FRIEND_TEST(                                                                 \
      RibAllocatedPathIdFixture,                                               \
      MultipathDeltaFirstAdvertisementAnnouncesAll);                           \
  FRIEND_TEST(RibAllocatedPathIdFixture, MultipathDeltaSkipsUnchangedPaths);   \
  FRIEND_TEST(                                                                 \
      RibAllocatedPathIdFixture, MultipathDeltaReannouncesUpdatedPath);        \
  FRIEND_TEST(                                                                 \
      RibAllocatedPathIdFixture, MultipathDeltaWithdrawsDroppedPathId);        \
  FRIEND_TEST(                                                                 \
      RibAllocatedPathIdFixture, MultipathDeltaSharedNexthopWithdrawsPathId);  \
  FRIEND_TEST(RibAllocatedPathIdFixture, MultipathDeltaNoChangeSendsNothing);  \
  FRIEND_TEST(RibAllocatedPathIdFixture, MultipathDeltaNoBestpathSkipsCommit); \
  FRIEND_TEST(RibAllocatedPathIdFixture, MultipathDeltaAddPathSpansChunks);    \
  FRIEND_TEST(RibAllocatedPathIdFixture, MultipathDeltaWithdrawalSpansChunks); \
  FRIEND_TEST(                                                                 \
      RibAllocatedPathIdFixture,                                               \
      MultipathDeltaReannouncesPathsKeptAfterFullWithdrawal);                  \
  FRIEND_TEST(                                                                 \
      RibAllocatedPathIdFixture,                                               \
      MultipathDeltaPartialDrainChangeReannouncesAllPaths);                    \
  FRIEND_TEST(                                                                 \
      RibAllocatedPathIdFixture,                                               \
      MultipathDeltaUcmpWeightChangeReannouncesAllPaths);                      \
  FRIEND_TEST(                                                                 \
      RibAllocatedPathIdFixture,                                               \
      MultipathDeltaRibPolicyWeightChangeReannouncesAllPaths);

#define RibEntry_TEST_FRIENDS                                                  \
  FRIEND_TEST(                                                                 \
      RibFixture, HandleAddUpdatedUnicastRouteNewPrefixNewlyInstalled);        \
  FRIEND_TEST(                                                                 \
      RibFixture, HandleAddUpdatedUnicastRouteNoAddPathWithoutBestpath);       \
  FRIEND_TEST(RibAllocatedPathIdFixture, MultipathDeltaNoBestpathSkipsCommit); \
  FRIEND_TEST(                                                                 \
      RibAllocatedPathIdFixture,                                               \
      MultipathDeltaPartialDrainChangeReannouncesAllPaths);

#define RibDC_TEST_FRIENDS \
  FRIEND_TEST(RibFixture, CreateTRibEntryBestGroupReflectsBestPathPresence);

#include <boost/algorithm/string.hpp>
#include <algorithm>
#include <functional>
#include <initializer_list>
#include <limits>
#include <optional>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <folly/IPAddress.h>
#include <folly/Overload.h>
#include <folly/futures/Future.h>
#include <folly/logging/xlog.h>

#include <fb303/ThreadCachedServiceData.h>
#include "common/network/AddressUtil.h"
#include "neteng/fboss/bgp/cpp/BgpServiceBase.h"
#include "neteng/fboss/bgp/cpp/BgpServiceUtil.h"
#include "neteng/fboss/bgp/cpp/fsdb/FsdbSyncer.h"
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/Utils.h"
#include "neteng/fboss/bgp/cpp/rib/RibBase.h"
#include "neteng/fboss/bgp/cpp/rib/RibDC.h"
#include "neteng/fboss/bgp/cpp/rib/facebook/RibPolicyLogger.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/stats/StatsDC.h"
#include "neteng/fboss/bgp/cpp/tests/BoundedWaitUtils.h"
#include "neteng/fboss/bgp/cpp/tests/MockScubaData.h"
#include "neteng/fboss/bgp/cpp/tests/PolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibPolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibUtils.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

using namespace facebook::bgp;
using namespace facebook::neteng::fboss::bgp_attr;
using namespace facebook::nettools::bgplib;
using namespace std::chrono;
using folly::IPAddress;
using folly::Promise;
using folly::Unit;
using ::testing::_;
using ::testing::AllOf;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Pair;
using ::testing::Pointee;
using ::testing::ResultOf;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;

namespace facebook {
namespace bgp {
using namespace neteng::fboss::bgp::thrift;
using namespace rib_policy;

INSTANTIATE_TEST_SUITE_P(
    RibFixture,
    RibFixtureAddPathTestSuite,
    testing::Values(true /* addPath */));

/*
 * 1. Ribout Announcement message should be sent after announcement is made
 * 2. Ribout Withdrawal message should be sent after withdrawal is made
 */
TEST_F(RibFixture, WithdrawRouteWithAgentReconnectTest) {
  // Setup: install a route to rib
  rib_->setFibBatchTime(milliseconds(50));
  auto prefixBatch1 = PrefixPathIds{
      {folly::IPAddress::createNetwork("1::/64"), kDefaultPathID}};
  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
  sendInitialPathComputation();
  fibFuture.wait();

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));
  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));

  // Withdraw this route
  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(prefixBatch1, iBgpPeer_);
  // Must wait for the withdraw to be processed in rib and put it on batch list
  while (ribInQ_.size() != 0) {
  }

  // Agent reconnect and ask full sync, before batched list is being processed
  rib_->fromFibMessageQ_.push(Fib::FibSyncReq{});

  // Wait for fib programming
  fibFuture.wait();

  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
}

/*
 * getRibSummary reports per-AFI totals, the per-prefix-length histogram, and
 * the eBGP/iBGP source breakdown, all populated incrementally during path
 * selection (RibCounters via runBestPathSelection).
 */
TEST_F(RibFixture, GetRibSummarySourceBreakdown) {
  auto ebgpPrefix = IPAddress::createNetwork("10::/64");
  auto ibgpPrefix = IPAddress::createNetwork("20::/64");

  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(
      PrefixPathIds{{ebgpPrefix, kDefaultPathID}}, eBgpPeer1_, attr_);
  sendAnnouncement(
      PrefixPathIds{{ibgpPrefix, kDefaultPathID}}, iBgpPeer_, attr_);
  sendInitialPathComputation();
  fibFuture.wait();

  auto summary = rib_->getRibSummary(TBgpAfi::AFI_IPV6);
  EXPECT_EQ(2, summary.total_prefixes().value());
  // Two prefixes, one path each (no add-path), so two total paths.
  EXPECT_EQ(2, summary.total_paths().value());
  // Both next-hops resolve, so every path entered best-path selection.
  EXPECT_EQ(0, summary.inactive_paths().value());
  EXPECT_EQ(1, summary.ebgp_prefixes().value());
  EXPECT_EQ(1, summary.ibgp_prefixes().value());
  EXPECT_EQ(0, summary.confed_ebgp_prefixes().value());
  EXPECT_EQ(0, summary.local_prefixes().value());
  EXPECT_EQ(0, summary.unresolvable_nexthops_count().value());
  // Both announced routes have resolvable next-hops, so none are unresolved.
  EXPECT_EQ(0, summary.routes_with_unresolved_nexthops().value());
  EXPECT_EQ(2, summary.prefix_length_counts().value()[64]);

  // The other address family is empty.
  auto v4Summary = rib_->getRibSummary(TBgpAfi::AFI_IPV4);
  EXPECT_EQ(0, v4Summary.total_prefixes().value());
  EXPECT_EQ(0, v4Summary.total_paths().value());
}

class RibFibNexthopDatabaseFixture : public RibFixture {
 public:
  /** Start a RIB with FIB-out tracking enabled. */
  void SetUp() override {
    ribFixtureDefaultSetup(
        ComputeUcmpFromLbwComm{true},
        CountConfedsInAsPathLen{false},
        EnableNexthopTracking{false},
        nullptr,
        true /* enableFibOutTracking */);
  }
};

/** Build one expected normalized nexthop for FIB-out database assertions. */
TFibOutNextHop makeExpectedFibNexthop(
    const folly::IPAddress& address,
    uint32_t weight = 0,
    TFibOutNextHopRole role = TFibOutNextHopRole::PRIMARY,
    std::optional<std::string> interfaceName = std::nullopt) {
  TFibOutNextHop nexthop;
  nexthop.next_hop() = createTIpPrefix(address);
  nexthop.weight() = weight;
  nexthop.role() = role;
  if (interfaceName) {
    nexthop.interface_name() = std::move(*interfaceName);
  }
  return nexthop;
}

/** Build one expected canonical set and its live route reference count. */
TFibNexthopSet makeExpectedFibNexthopSet(
    std::vector<TFibOutNextHop> nexthops,
    int64_t routeRefCount) {
  TFibNexthopSet set;
  set.next_hops() = std::move(nexthops);
  set.ref_count() = routeRefCount;
  return set;
}

TEST_F(RibFixture, FibNexthopDatabaseDisabledByDefault) {
  const auto database = rib_->getFibNexthopDatabase();
  EXPECT_FALSE(*database.enabled());
  EXPECT_TRUE(database.nexthop_sets()->empty());
}

TEST_F(RibFibNexthopDatabaseFixture, TracksSubmittedNexthopSets) {
  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(
      PrefixPathIds{{kV4Prefix1, kDefaultPathID}, {kV4Prefix2, kDefaultPathID}},
      eBgpPeer1_,
      attr_);
  sendInitialPathComputation();
  fibFuture.wait();

  const std::vector<TFibNexthopSet> expectedTwoPrefixes{
      makeExpectedFibNexthopSet({makeExpectedFibNexthop(kV4Nexthop1)}, 2)};
  auto database = rib_->getFibNexthopDatabase();
  ASSERT_TRUE(*database.enabled());
  EXPECT_EQ(*database.nexthop_sets(), expectedTwoPrefixes);
  const auto firstPrefix =
      rib_->getFibOutPrefix(folly::IPAddress::networkToString(kV4Prefix1));
  ASSERT_EQ(firstPrefix.entries()->size(), 1);
  ASSERT_TRUE(firstPrefix.entries()->front().fib_out().has_value());
  EXPECT_EQ(
      *firstPrefix.entries()->front().fib_out()->nexthop_set_ref_count(), 2);

  auto updatedAttrs = attr_->clone();
  updatedAttrs->setNexthop(kV4Nexthop2);
  updatedAttrs->publish();
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(
      PrefixPathIds{{kV4Prefix1, kDefaultPathID}}, eBgpPeer1_, updatedAttrs);
  fibFuture.wait();
  const std::vector<TFibNexthopSet> expectedUpdated{
      makeExpectedFibNexthopSet({makeExpectedFibNexthop(kV4Nexthop1)}, 1),
      makeExpectedFibNexthopSet({makeExpectedFibNexthop(kV4Nexthop2)}, 1),
  };
  EXPECT_THAT(
      *rib_->getFibNexthopDatabase().nexthop_sets(),
      UnorderedElementsAreArray(expectedUpdated));
  const auto updatedPrefix =
      rib_->getFibOutPrefix(folly::IPAddress::networkToString(kV4Prefix1));
  ASSERT_EQ(updatedPrefix.entries()->size(), 1);
  ASSERT_TRUE(updatedPrefix.entries()->front().fib_out().has_value());
  EXPECT_EQ(
      *updatedPrefix.entries()->front().fib_out()->nexthop_set_ref_count(), 1);

  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(PrefixPathIds{{kV4Prefix1, kDefaultPathID}}, eBgpPeer1_);
  fibFuture.wait();
  const std::vector<TFibNexthopSet> expectedOnePrefix{
      makeExpectedFibNexthopSet({makeExpectedFibNexthop(kV4Nexthop1)}, 1)};
  database = rib_->getFibNexthopDatabase();
  EXPECT_EQ(*database.nexthop_sets(), expectedOnePrefix);

  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(PrefixPathIds{{kV4Prefix2, kDefaultPathID}}, eBgpPeer1_);
  fibFuture.wait();
  WITH_RETRIES_N_TIMED(200, milliseconds(5), {
    EXPECT_EVENTUALLY_TRUE(
        rib_->getFibNexthopDatabase().nexthop_sets()->empty());
  });
}

TEST_F(RibFibNexthopDatabaseFixture, KeepsCompleteNexthopSetsDistinct) {
  const auto sharedAddress = folly::IPAddress("192.0.2.1");
  const auto firstOnlyAddress = folly::IPAddress("192.0.2.2");
  const auto secondOnlyAddress = folly::IPAddress("192.0.2.3");
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto firstNexthops = rib_->fibNexthopSets_.getOrCreate({
        FibOutNexthop{
            .address = sharedAddress,
            .weight = 1,
            .role = FibOutNexthopRole::PRIMARY,
        },
        FibOutNexthop{
            .address = firstOnlyAddress,
            .weight = 2,
            .role = FibOutNexthopRole::BACKUP,
        },
    });
    auto secondNexthops = rib_->fibNexthopSets_.getOrCreate({
        FibOutNexthop{
            .address = sharedAddress,
            .weight = 1,
            .role = FibOutNexthopRole::PRIMARY,
        },
        FibOutNexthop{
            .address = secondOnlyAddress,
            .weight = 3,
            .role = FibOutNexthopRole::BACKUP,
        },
    });
    auto& firstEntry =
        rib_->ribEntries_
            .emplace(std::make_pair(kV4Prefix1, RibEntry(kV4Prefix1)))
            .first->second;
    auto& secondEntry =
        rib_->ribEntries_
            .emplace(std::make_pair(kV4Prefix2, RibEntry(kV4Prefix2)))
            .first->second;
    rib_->replaceFibOut(
        firstEntry,
        FibOutState{
            .nexthops = std::move(firstNexthops),
            .metadata = FibOutRouteMetadata::program(),
        });
    rib_->replaceFibOut(
        secondEntry,
        FibOutState{
            .nexthops = std::move(secondNexthops),
            .metadata = FibOutRouteMetadata::program(),
        });
  });

  const std::vector<TFibNexthopSet> expected{
      makeExpectedFibNexthopSet(
          {makeExpectedFibNexthop(sharedAddress, 1),
           makeExpectedFibNexthop(
               firstOnlyAddress, 2, TFibOutNextHopRole::BACKUP)},
          1),
      makeExpectedFibNexthopSet(
          {makeExpectedFibNexthop(sharedAddress, 1),
           makeExpectedFibNexthop(
               secondOnlyAddress, 3, TFibOutNextHopRole::BACKUP)},
          1),
  };
  EXPECT_THAT(
      *rib_->getFibNexthopDatabase().nexthop_sets(),
      UnorderedElementsAreArray(expected));

  const auto prefixFibOut =
      rib_->getFibOutPrefix(folly::IPAddress::networkToString(kV4Prefix1));
  ASSERT_EQ(prefixFibOut.entries()->size(), 1);
  ASSERT_TRUE(prefixFibOut.entries()->front().fib_out().has_value());
  EXPECT_EQ(
      *prefixFibOut.entries()->front().fib_out()->nexthop_set_ref_count(), 1);

  rib_->evb_.runInEventBaseThreadAndWait(
      [&]() { rib_->eraseFibOut(rib_->ribEntries_.at(kV4Prefix1)); });
  const std::vector<TFibNexthopSet> expectedSecondOnly{expected.back()};
  EXPECT_EQ(*rib_->getFibNexthopDatabase().nexthop_sets(), expectedSecondOnly);

  rib_->evb_.runInEventBaseThreadAndWait(
      [&]() { rib_->eraseFibOut(rib_->ribEntries_.at(kV4Prefix2)); });
  EXPECT_TRUE(rib_->getFibNexthopDatabase().nexthop_sets()->empty());
}

TEST_F(RibFibNexthopDatabaseFixture, TracksEmptySubmittedNexthopSet) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto emptyNexthops = rib_->fibNexthopSets_.getOrCreate({});
    auto& entry = rib_->ribEntries_
                      .emplace(std::make_pair(kV4Prefix1, RibEntry(kV4Prefix1)))
                      .first->second;
    rib_->replaceFibOut(
        entry,
        FibOutState{
            .nexthops = std::move(emptyNexthops),
            .metadata = FibOutRouteMetadata{},
        });
  });

  TFibNexthopSet expectedSet;
  expectedSet.ref_count() = 1;
  const auto database = rib_->getFibNexthopDatabase();
  EXPECT_EQ(*database.nexthop_sets(), std::vector<TFibNexthopSet>{expectedSet});
  const auto prefixFibOut =
      rib_->getFibOutPrefix(folly::IPAddress::networkToString(kV4Prefix1));
  ASSERT_EQ(prefixFibOut.entries()->size(), 1);
  ASSERT_TRUE(prefixFibOut.entries()->front().fib_out().has_value());
  EXPECT_EQ(
      *prefixFibOut.entries()->front().fib_out()->nexthop_set_ref_count(), 1);
  EXPECT_EQ(
      *prefixFibOut.entries()->front().fib_out()->operation(),
      TFibOutOperation::NONE);
  EXPECT_TRUE(prefixFibOut.entries()->front().fib_out()->next_hops()->empty());
}

TEST_F(RibFibNexthopDatabaseFixture, PropagatesConversionFailure) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto invalidNexthops = rib_->fibNexthopSets_.getOrCreate({
        FibOutNexthop{
            .address = kV4Nexthop1,
            .weight = 1,
            .role = static_cast<FibOutNexthopRole>(-1),
        },
    });
    auto& entry = rib_->ribEntries_
                      .emplace(std::make_pair(kV4Prefix1, RibEntry(kV4Prefix1)))
                      .first->second;
    rib_->replaceFibOut(
        entry,
        FibOutState{
            .nexthops = std::move(invalidNexthops),
            .metadata = FibOutRouteMetadata::program(),
        });
  });

  EXPECT_THROW(rib_->getFibNexthopDatabase(), std::logic_error);
}

/*
 * Test if EoR is sent, then RIB_COMPUTED event should happen
 */
TEST_F(RibFixture, EORTest) {
  facebook::fb303::ThreadCachedServiceData::getShared()->setCounter(
      fmt::format(
          kInitEventCounterFormat,
          apache::thrift::util::enumNameSafe(
              BgpInitializationEvent::RIB_COMPUTED)),
      0);

  auto prefixBatch1 = PrefixPathIds{
      {folly::IPAddress::createNetwork("1::/64"), kDefaultPathID}};
  auto prefixBatch2 = PrefixPathIds{
      {folly::IPAddress::createNetwork("2::/64"), kDefaultPathID}};

  // send prefixes to Rib
  sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
  sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);

  auto fibFuture = fib_->getFibProgramFuture();

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
  {
    InSequence dummy;
    EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _)).Times(2);
    EXPECT_CALL(*fib_, program_(true)).Times(1);
  }

  EXPECT_EQ(
      0,
      facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
          fmt::format(
              kInitEventCounterFormat,
              apache::thrift::util::enumNameSafe(
                  BgpInitializationEvent::RIB_COMPUTED))));

  // send EOR now
  sendInitialPathComputation();

  // wait for fib programming happens
  fibFuture.wait();

  // initial RIB has been computed and initialization event published
  EXPECT_LT(
      0,
      facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
          fmt::format(
              kInitEventCounterFormat,
              apache::thrift::util::enumNameSafe(
                  BgpInitializationEvent::RIB_COMPUTED))));
}

/*
 * Test fib batch delay is honored
 */
TEST_F(RibFixture, FibBatchDelayTest) {
  const auto kBatchTime = milliseconds(8);
  auto prefix1 = folly::IPAddress::createNetwork("::/0");
  auto prefixBatch = PrefixPathIds{{prefix1, kDefaultPathID}};

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(2);
  {
    InSequence dummy;
    EXPECT_CALL(*fib_, program_(true)).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(prefix1), _, _, _, _, _))
        .Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);
  }

  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  rib_->setFibBatchTime(milliseconds(kBatchTime));
  auto batchTime = rib_->getFibBatchTime();
  EXPECT_EQ(batchTime, milliseconds(kBatchTime));

  // send prefixes to Rib
  fibFuture = fib_->getFibProgramFuture();
  auto start = steady_clock::now();
  sendAnnouncement(prefixBatch, iBgpPeer_, attr_);
  fibFuture.wait();
  auto end = steady_clock::now();
  auto elapsed = duration_cast<milliseconds>(end - start);
  EXPECT_GE(elapsed, batchTime);
}

/*
 * 1. Test announcing prefixes in batches works
 * 2. Test sending different batches with the same prefix works without crash
 */
TEST_F(RibFixture, FibBatchCombineTest) {
  const auto kBatchTime = milliseconds(8);
  auto prefixBatch1 = PrefixPathIds{
      {folly::IPAddress::createNetwork("1::/64"), kDefaultPathID}};
  auto prefixBatch2 = PrefixPathIds{
      {folly::IPAddress::createNetwork("2::/64"), kDefaultPathID}};

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(2);
  {
    InSequence dummy; // specifies the below ordering
    EXPECT_CALL(*fib_, program_(true)).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _)).Times(2);
    EXPECT_CALL(*fib_, program_(false)).Times(1);
  }

  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  rib_->setFibBatchTime(kBatchTime);

  // send prefixes to Rib
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
  sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);
  sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
  sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);
  fibFuture.wait();
}

/*
 * Test sending prefixes in separate batch works
 */
TEST_F(RibFixture, FibBatchSeparateTest) {
  const auto kBatchTime = milliseconds(8);
  auto prefix1 = folly::IPAddress::createNetwork("1::/64");
  auto prefix2 = folly::IPAddress::createNetwork("2::/64");
  auto prefixBatch1 = PrefixPathIds{{prefix1, kDefaultPathID}};
  auto prefixBatch2 = PrefixPathIds{{prefix2, kDefaultPathID}};

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(3);
  {
    InSequence dummy; // specifies the below ordering
    EXPECT_CALL(*fib_, program_(true)).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(prefix1, _, _, _, _, _)).Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(prefix2, _, _, _, _, _)).Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);
  }

  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  rib_->setFibBatchTime(kBatchTime);

  fibFuture = fib_->getFibProgramFuture();
  // send prefixes to Rib
  sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
  // wait for the batch to happen
  fibFuture.wait();

  fibFuture = fib_->getFibProgramFuture();
  // send prefixes to Rib
  sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);
  // wait for the batch to happen
  fibFuture.wait();
}

/*
 * Test fib call with different announcements/withdrawals
 * - Steps:
 * 1. prefix update from eBgpPeer1_ (installToFib = true)
 * 2. prefix update with high local pref from redistributePeer_ (installToFib =
 * false)
 * 3. prefix withdrawal from redistributePeer_ (eBgpPeer1_ become best again)
 * 4. prefix withdrawal from eBgpPeer1_
 */
TEST_F(RibFixture, FibCallTest) {
  auto prefix1 = folly::IPAddress::createNetwork("1::/64");
  auto prefix2 = folly::IPAddress::createNetwork("2::/64");
  auto prefix3 = folly::IPAddress::createNetwork("3::/64");
  auto prefixBatch = PrefixPathIds{
      {prefix1, kDefaultPathID},
      {prefix2, kDefaultPathID},
      {prefix3, kDefaultPathID}};

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(5);
  /*
   * In step 1 and 3, updateUnicastRoute_ called with installToFib = true,
   * bestpath = eBgpPeer1_
   * step 4 updateUnicastRoute_ called for prefix withdrawn with installToFib =
   * true
   */
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(prefix1), _, _, false, true, _))
      .Times(3);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(prefix2), _, _, false, true, _))
      .Times(3);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(prefix3), _, _, false, true, _))
      .Times(3);

  /*
   * 2. updateUnicastRoute_ called with installToFib = false, bestpath =
   * redistributePeer_
   */
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(prefix1), _, _, false, false, _))
      .Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(prefix2), _, _, false, false, _))
      .Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(prefix3), _, _, false, false, _))
      .Times(1);

  InSequence dummy;
  EXPECT_CALL(*fib_, program_(true)).Times(1);
  EXPECT_CALL(*fib_, program_(false)).Times(4);

  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  rib_->setFibBatchTime(milliseconds(2));

  // 1. prefix update from eBgpPeer1_ (installToFib = true)
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  fibFuture.wait();
  {
    auto bestPath = rib_->getBestPath(prefix1);
    EXPECT_EQ(eBgpPeer1_, bestPath->peer);
  }
  {
    auto bestPath = rib_->getBestPath(prefix2);
    EXPECT_EQ(eBgpPeer1_, bestPath->peer);
  }
  {
    auto bestPath = rib_->getBestPath(prefix3);
    EXPECT_EQ(eBgpPeer1_, bestPath->peer);
  }

  /*
   * 2. prefix update with high local pref from redistributePeer_ (installToFib
   * = false)
   * best path = redistributePeer_
   */
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, redistributePeer_, attrHighLocalPref_);
  fibFuture.wait();
  {
    auto bestPath = rib_->getBestPath(prefix1);
    EXPECT_EQ(redistributePeer_, bestPath->peer);
  }
  {
    auto bestPath = rib_->getBestPath(prefix2);
    EXPECT_EQ(redistributePeer_, bestPath->peer);
  }
  {
    auto bestPath = rib_->getBestPath(prefix3);
    EXPECT_EQ(redistributePeer_, bestPath->peer);
  }

  // 3. prefix withdrawal from redistributePeer_ => eBgpPeer1_ become best again
  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(prefixBatch, redistributePeer_);
  fibFuture.wait();
  {
    auto bestPath = rib_->getBestPath(prefix1);
    EXPECT_EQ(eBgpPeer1_, bestPath->peer);
  }
  {
    auto bestPath = rib_->getBestPath(prefix2);
    EXPECT_EQ(eBgpPeer1_, bestPath->peer);
  }
  {
    auto bestPath = rib_->getBestPath(prefix3);
    EXPECT_EQ(eBgpPeer1_, bestPath->peer);
  }

  // 4. prefix withdrawal from eBgpPeer1_
  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(prefixBatch, eBgpPeer1_);
  fibFuture.wait();
}

/**
 * @brief Tests correctness of Rib::stop()
 */
TEST_F(RibFixture, TestStop) {
  /*
   * Start a task that will access the timer cleared Rib::stop() has started
   * executing.
   */
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->asyncScope_.add(
        co_withExecutor(&rib_->evb_, delayedFibProgramSchedule()));
  });
}

namespace {

std::shared_ptr<facebook::bgp::BgpPath> buildEqualCostPath(
    const folly::IPAddress& nexthop,
    const std::optional<float> lbw = std::nullopt) {
  auto attrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs->setNexthop(nexthop);
  if (lbw.has_value()) {
    attrs->setNonTransitiveLbwExtCommunity(kLocalAs1, lbw.value());
  }
  attrs->publish();
  return attrs;
}

} // namespace

TEST_F(
    RibFixture,
    MultipathCardinalityChangeWithoutLbwDoesNotReadvertiseBestPath) {
  ASSERT_FALSE(eBgpPeer4_.ucmpWeight.has_value());
  ASSERT_FALSE(eBgpPeer5_.ucmpWeight.has_value());

  const auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  const auto attrs1 = buildEqualCostPath(kV4Nexthop1);
  const auto attrs2 = buildEqualCostPath(kV4Nexthop2);
  ASSERT_FALSE(attrs1->hasNonTransitiveLbwExtCommunity());
  ASSERT_FALSE(attrs2->hasNonTransitiveLbwExtCommunity());

  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer5_, attrs1);
  sendInitialPathComputation();
  fibFuture.wait();

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));
  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  const auto initialAnnouncement = std::get<RibOutAnnouncement>(msg);
  ASSERT_EQ(1, initialAnnouncement.entries.size());
  ASSERT_EQ(1, initialAnnouncement.addPathEntries.size());

  const auto bestpathBefore = rib_->getBestPath(kV4Prefix1);
  ASSERT_NE(nullptr, bestpathBefore);
  ASSERT_EQ(eBgpPeer5_, bestpathBefore->peer);

  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer4_, attrs2);
  fibFuture.wait();

  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  const auto announcement = std::get<RibOutAnnouncement>(msg);
  EXPECT_TRUE(announcement.entries.empty());
  ASSERT_EQ(2, announcement.addPathEntries.size());
  for (const auto& entry : announcement.addPathEntries) {
    ASSERT_TRUE(entry.multiPathSize.has_value());
    EXPECT_EQ(2, entry.multiPathSize.value());
  }
  EXPECT_EQ(bestpathBefore, rib_->getBestPath(kV4Prefix1));
}

TEST_F(
    RibFixture,
    LbwMultipathMembershipChangeWithoutSizeChangeDoesNotReadvertiseBestPath) {
  ASSERT_FALSE(eBgpPeer4_.ucmpWeight.has_value());
  ASSERT_FALSE(eBgpPeer5_.ucmpWeight.has_value());

  const auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  const auto attrs1 = buildEqualCostPath(kV4Nexthop1, kLbw10G);
  const auto attrs2 = buildEqualCostPath(kV4Nexthop2, kLbw10G);
  const auto updatedAttrs2 = buildEqualCostPath(kV4Nexthop3, kLbw10G);

  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer5_, attrs1);
  sendAnnouncement(prefixBatch, eBgpPeer4_, attrs2);
  sendInitialPathComputation();
  fibFuture.wait();

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));
  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  const auto initialAnnouncement = std::get<RibOutAnnouncement>(msg);
  ASSERT_EQ(1, initialAnnouncement.entries.size());
  ASSERT_EQ(2, initialAnnouncement.addPathEntries.size());

  const auto bestpathBefore = rib_->getBestPath(kV4Prefix1);
  ASSERT_NE(nullptr, bestpathBefore);
  ASSERT_EQ(eBgpPeer5_, bestpathBefore->peer);

  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer4_, updatedAttrs2);
  fibFuture.wait();

  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
  const auto withdrawal = std::get<RibOutWithdrawal>(msg);
  EXPECT_TRUE(withdrawal.entries.empty());
  ASSERT_EQ(1, withdrawal.addPathEntries.size());
  ASSERT_TRUE(withdrawal.addPathEntries.front().nh.has_value());
  EXPECT_EQ(kV4Nexthop2, withdrawal.addPathEntries.front().nh.value());

  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  const auto announcement = std::get<RibOutAnnouncement>(msg);
  EXPECT_TRUE(announcement.entries.empty());
  ASSERT_EQ(2, announcement.addPathEntries.size());
  for (const auto& entry : announcement.addPathEntries) {
    ASSERT_TRUE(entry.multiPathSize.has_value());
    EXPECT_EQ(2, entry.multiPathSize.value());
  }
  EXPECT_EQ(bestpathBefore, rib_->getBestPath(kV4Prefix1));
}

/*
 * If addpath is enabled, then there is a path update which doesn't triger path
 * programming (no best path change and no nexthop change), but the nexthop path
 * attributes change. In such case, we still need to announce this change to
 * AdjRib.
 */
TEST_F(RibFixture, NoBestPathNexthopChangeAddPath) {
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  rib_->setFibBatchTime(milliseconds(2));

  /*
   * first we let the initial dump finish:
   * eventually we get a RibInitialAnnouncementStart msg and one
   * RibOutAnnouncement with initialDump=true. Pop them both and then proceed
   * with test
   */
  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(ribOutQ_.size(), 2); });
  REPEAT_N(2, folly::coro::blockingWait(ribOutQ_.pop()));

  /*
   * Simulate a route to kV4Prefix1 received from eBgpPeer1_ with nexthop
   * kV4Nexthop1 and add to ribEntries_
   */
  RibEntry entry(kV4Prefix1);
  // Use the default attributes, which sets nexthop to kV4Nexthop1
  entry.updatePath(eBgpPeer1_, attr_, false);
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->ribEntries_.clear();
    rib_->ribEntries_.emplace(kV4Prefix1, std::move(entry));
    /*
     * Now simulate a second route to same prefix received from eBgpPeer2_ with
     * different nexthop kV4Nexthop2
     */
    auto newAttr = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    newAttr->setNexthop(kV4Nexthop2);
    newAttr->publish();
    rib_->ribEntries_.find(kV4Prefix1)
        ->second.updatePath(eBgpPeer2_, newAttr, false);
    RibBase::selectBestPath(
        rib_->ribEntries_.find(kV4Prefix1)->second,
        multipathSelector,
        bestpathSelector,
        false,
        0);
  });
  /*
   * Now simulate recepit of announcement from eBgpPeer2_ for same prefix,
   * kV4Prefix1, same nexthop, kV4Nexthop2, but different communities
   */
  auto prefixBatch2 = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};

  fibFuture = fib_->getFibProgramFuture();
  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 5, 4, 4));
  attr->setNexthop(kV4Nexthop2);
  attr->publish();
  sendAnnouncement(prefixBatch2, eBgpPeer2_, attr);

  // expect one announcement
  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  auto announcement3 = std::get<RibOutAnnouncement>(msg);
  ASSERT_EQ(2, announcement3.addPathEntries.size());
  checkRibOutEntriesAddPathIds(announcement3);

  // expect no more announcement
  REPEAT_N(5, { EXPECT_EQ(0, ribOutQ_.size()); });
}

/*
 * If addpath is enabled and we have RibPolicy with MNH set plus
 * drain_on_min_nexthop_violation=true, partial drain retains the bestpath
 * under MNH violation. A subsequent path-attribute change on a non-bestpath
 * peer leaves the bestpath unchanged but updates the multipath set, so we
 * announce add-path entries to AdjRib without a standard bestpath
 * re-announcement.
 */
TEST_F(RibFixture, NoBestPathNexthopChangeAddPathWithRibPolicyMNHTest) {
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  rib_->setFibBatchTime(milliseconds(2));

  /*
   * first we let the initial dump finish:
   * eventually we get a RibInitialAnnouncementStart msg and one
   * RibOutAnnouncement with initialDump=true. Pop them both and then proceed
   * with test
   */
  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(ribOutQ_.size(), 2); });
  REPEAT_N(2, folly::coro::blockingWait(ribOutQ_.pop()));

  /*
   * Simulate a route to kV4Prefix1 received from eBgpPeer1_ with nexthop
   * kV4Nexthop1 and add to ribEntries_
   */
  RibEntry entry(kV4Prefix1);
  // Use the default attributes, which sets nexthop to kV4Nexthop1
  entry.updatePath(eBgpPeer1_, attr_, false);
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->ribEntries_.clear();
    rib_->ribEntries_.emplace(kV4Prefix1, std::move(entry));
    /*
     * Now simulate a second route to same prefix received from eBgpPeer2_ with
     * different nexthop kV4Nexthop2
     */
    auto newAttr = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    newAttr->setNexthop(kV4Nexthop2);
    newAttr->publish();
    rib_->ribEntries_.find(kV4Prefix1)
        ->second.updatePath(eBgpPeer2_, newAttr, false);
    RibBase::selectBestPath(
        rib_->ribEntries_.find(kV4Prefix1)->second,
        multipathSelector,
        bestpathSelector,
        false,
        0);
  });

  /*
   * inject the cps policy with mnh of 3
   */

  // default min nexthop = 3
  TPathSelector tPathSelector;
  tPathSelector.bgp_native_path_selection_min_nexthop() = 3;
  tPathSelector.drain_on_min_nexthop_violation() = true;

  // - inject path selector with default MNH 3
  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();

  // send rib policy over
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  // With partial drain, bestpath is retained — re-announcement, not withdrawal
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
  }

  /*
   * Now simulate recepit of announcement from eBgpPeer2_ for same prefix,
   * kV4Prefix1, same nexthop, kV4Nexthop2, but different communities.
   * Under partial drain, the bestpath is retained but unchanged (eBgpPeer2_ is
   * not the bestpath). The multipath set changes (updated attrs), generating
   * add-path entries but no standard bestpath re-announcement.
   */
  auto prefixBatch2 = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};

  fibFuture = fib_->getFibProgramFuture();
  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 5, 4, 4));
  attr->setNexthop(kV4Nexthop2);
  attr->publish();
  sendAnnouncement(prefixBatch2, eBgpPeer2_, attr);
  fibFuture.wait();

  WITH_RETRIES({ ASSERT_EVENTUALLY_GE(ribOutQ_.size(), 1); });
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    EXPECT_EQ(0, announcement.entries.size());
    EXPECT_GE(announcement.addPathEntries.size(), 1);
  }
}

/*
 * Regression guard for partial-drain bestpathChanged signaling in
 * RibDC::selectBestPath (the DC-only CPS-aware path-selection orchestrator).
 *
 * Scenario: a single path is installed (bestpath = X). Then a CPS policy
 * with mnh=3 + drain_on_min_nexthop_violation=true is injected. With only 1
 * path, MNH is violated but partial drain retains X — so bestpath_ remains
 * the SAME shared_ptr across the policy injection. The bestpath pointer
 * comparison alone (oldBestpath == bestpath_) would yield bestpathChanged=
 * false.
 *
 * RibDC::selectBestPath() folds the drain transition into the returned
 * bestpathChanged via `(isPartialDrain_ != oldIsPartialDrain)` at the
 * full-return path (after computeChangePair) and at every early-return.
 * Without that fold, the entry would not enter the announce-withdraw block,
 * would not be pushed onto fibBatchList_, and the post-FIB announcement loop
 * would never visit it — silently skipping the re-advertisement that
 * downstream MNH diffs use to attach the drain community.
 */
TEST_F(RibFixture, PartialDrainAnnouncesWhenBestpathPointerUnchanged) {
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  rib_->setFibBatchTime(milliseconds(2));

  /*
   * Drain the initial-dump messages (RibInitialAnnouncementStart +
   * RibOutAnnouncement with initialDump=true).
   */
  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(ribOutQ_.size(), 2); });
  REPEAT_N(2, folly::coro::blockingWait(ribOutQ_.pop()));

  /*
   * Install a single path so there's exactly 1 multipath — guaranteed to
   * violate any mnh > 1 policy. attr_ uses kV4Nexthop1.
   */
  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  fibFuture.wait();

  // Drain the announcement for the initial single-path install.
  WITH_RETRIES({ ASSERT_EVENTUALLY_GE(ribOutQ_.size(), 1); });
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  }

  /*
   * Capture the bestpath shared_ptr so we can prove it stays the same
   * across the partial-drain transition.
   */
  std::shared_ptr<RouteInfo> bestpathBefore;
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto& entry = rib_->ribEntries_.find(kV4Prefix1)->second;
    bestpathBefore = entry.getBestPath();
    EXPECT_FALSE(entry.getIsPartialDrain());
  });
  ASSERT_NE(bestpathBefore, nullptr);

  /*
   * Inject CPS policy: mnh=3 (1 path < 3 → violation) +
   * drain_on_min_nexthop_violation.
   */
  TPathSelector tPathSelector;
  tPathSelector.bgp_native_path_selection_min_nexthop() = 3;
  tPathSelector.drain_on_min_nexthop_violation() = true;

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  /*
   * The transition is partial-drain false → true, but the bestpath pointer
   * stays the same (still the one path we installed). The drain-transition
   * fold in selectBestPath() must produce a re-announcement (NOT a
   * withdrawal) so the entry stays on the announce path.
   */
  WITH_RETRIES({ ASSERT_EVENTUALLY_GE(ribOutQ_.size(), 1); });
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
  }

  /*
   * Verify the invariant the test exists to guard: drain transitioned, but
   * the bestpath shared_ptr is unchanged.
   */
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto& entry = rib_->ribEntries_.find(kV4Prefix1)->second;
    EXPECT_TRUE(entry.getIsPartialDrain());
    EXPECT_TRUE(entry.getInstallToFib());
    EXPECT_EQ(entry.getBestPath(), bestpathBefore);
  });
}

/*
 * Positive-path coverage for the partial-drain accessors:
 *   - drainedPrefixCount_ increments when a prefix enters partial drain
 *   - getPartialDrainStatus() reports the live counters
 *   - getPartiallyDrainedPrefixes() returns the prefix with correct
 *     min_capacity / current_capacity
 *   - getPartialDrainState() bundles both
 * Mirrors the partial-drain setup from
 * PartialDrainAnnouncesWhenBestpathPointerUnchanged.
 */
TEST_F(RibFixture, PartialDrainStatusReflectsRibState) {
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  rib_->setFibBatchTime(milliseconds(2));

  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(ribOutQ_.size(), 2); });
  REPEAT_N(2, folly::coro::blockingWait(ribOutQ_.pop()));

  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  fibFuture.wait();

  WITH_RETRIES({ ASSERT_EVENTUALLY_GE(ribOutQ_.size(), 1); });
  folly::coro::blockingWait(ribOutQ_.pop());

  /*
   * Trigger partial drain: mnh=3 with 1 path → violation, drain retains
   * bestpath.
   */
  TPathSelector tPathSelector;
  tPathSelector.bgp_native_path_selection_min_nexthop() = 3;
  tPathSelector.drain_on_min_nexthop_violation() = true;

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto status = rib_->getPartialDrainStatus();
    EXPECT_TRUE(*status.is_partially_drained());
    EXPECT_EQ(1, *status.num_affected_prefixes());
    // Device flipped 0 → 1 exactly once.
    EXPECT_EQ(1, *status.partial_drain_transition_count());

    auto prefixes = rib_->getPartiallyDrainedPrefixes();
    ASSERT_EQ(1, prefixes.size());
    EXPECT_EQ(kV4Prefix1.second, *prefixes[0].prefix()->num_bits());
    /*
     * Trigger was MNH: the TCapacity union holds the next_hop_count arm,
     * captured during selectBestPath() and surfaced via the Thrift accessor
     * without a per-prefix policy lookup at RPC time. min_capacity carries the
     * violated threshold (3); current_capacity the live nexthop count (1).
     */
    ASSERT_TRUE(prefixes[0].min_capacity()->next_hop_count().has_value());
    EXPECT_EQ(3, *prefixes[0].min_capacity()->next_hop_count());
    ASSERT_TRUE(prefixes[0].current_capacity()->next_hop_count().has_value());
    EXPECT_EQ(1, *prefixes[0].current_capacity()->next_hop_count());

    auto state = rib_->getPartialDrainState();
    EXPECT_TRUE(*state.partial_drain_state()->is_partially_drained());
    EXPECT_EQ(1, *state.partial_drain_state()->num_affected_prefixes());
    ASSERT_EQ(1, state.drained_prefixes()->size());
    ASSERT_TRUE(state.drained_prefixes()
                    ->at(0)
                    .min_capacity()
                    ->next_hop_count()
                    .has_value());
    EXPECT_EQ(
        3, *state.drained_prefixes()->at(0).min_capacity()->next_hop_count());
  });
}

/*
 * Regression guard for M1 transition-counter semantics: per the IDL doc on
 * partial_drain_transition_count, the counter must bump only when the
 * device-level is_partially_drained flips (count crosses zero), not on every
 * per-prefix transition.
 *
 * We exercise: 0 → 1 (bump), then withdraw the path → 1 → 0 (bump). Two
 * device-level flips ⇒ transition_count must equal 2, while
 * num_affected_prefixes returns to 0.
 */
TEST_F(RibFixture, PartialDrainTransitionCountOnlyOnDeviceFlip) {
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  rib_->setFibBatchTime(milliseconds(2));

  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(ribOutQ_.size(), 2); });
  REPEAT_N(2, folly::coro::blockingWait(ribOutQ_.pop()));

  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  fibFuture.wait();

  WITH_RETRIES({ ASSERT_EVENTUALLY_GE(ribOutQ_.size(), 1); });
  folly::coro::blockingWait(ribOutQ_.pop());

  TPathSelector tPathSelector;
  tPathSelector.bgp_native_path_selection_min_nexthop() = 3;
  tPathSelector.drain_on_min_nexthop_violation() = true;

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto status = rib_->getPartialDrainStatus();
    EXPECT_EQ(1, *status.num_affected_prefixes());
    EXPECT_EQ(1, *status.partial_drain_transition_count());
  });

  /*
   * Withdraw the only path → entry has no routes → selectBestPath resets
   * isPartialDrain_ → device flips back from drained to not-drained.
   */
  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(prefixBatch, eBgpPeer1_);
  fibFuture.wait();

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto status = rib_->getPartialDrainStatus();
    EXPECT_FALSE(*status.is_partially_drained());
    EXPECT_EQ(0, *status.num_affected_prefixes());
    // 0→1 then 1→0: exactly two device flips.
    EXPECT_EQ(2, *status.partial_drain_transition_count());
    EXPECT_TRUE(rib_->getPartiallyDrainedPrefixes().empty());
  });
}

/*
 * Multi-prefix coverage for the device-flip semantics of
 * partial_drain_transition_count. With three prefixes draining and
 * recovering at different times, the counter must increment only on the
 * device-level 0->1 (first prefix drained) and 1->0 (last prefix
 * recovered) transitions — NOT on the intermediate per-prefix flips
 * while the device is already drained. Guards the `drainedPrefixCount_
 * == 0` predicates in RibDC::recordPartialDrainTransition.
 */
TEST_F(RibFixture, PartialDrainTransitionCountMultiPrefix) {
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  rib_->setFibBatchTime(milliseconds(2));

  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(ribOutQ_.size(), 2); });
  REPEAT_N(2, folly::coro::blockingWait(ribOutQ_.pop()));

  // Announce three prefixes each with one path
  for (const auto& prefix : {kV4Prefix1, kV4Prefix2, kV6Prefix1}) {
    auto prefixBatch = PrefixPathIds{{prefix, kDefaultPathID}};
    fibFuture = fib_->getFibProgramFuture();
    sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
    fibFuture.wait();
  }
  WITH_RETRIES({ ASSERT_EVENTUALLY_GE(ribOutQ_.size(), 3); });
  REPEAT_N(3, folly::coro::blockingWait(ribOutQ_.pop()));

  // mnh=3 with 1 path per prefix => all three violate => all three drain.
  TPathSelector tPathSelector;
  tPathSelector.bgp_native_path_selection_min_nexthop() = 3;
  tPathSelector.drain_on_min_nexthop_violation() = true;

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendPathSelectionPolicySet(createTPathSelectionPolicyWithPathSelector(
      {kV4Prefix1, kV4Prefix2, kV6Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto status = rib_->getPartialDrainStatus();
    EXPECT_TRUE(*status.is_partially_drained());
    EXPECT_EQ(3, *status.num_affected_prefixes());
    // All three prefixes drained, but the device crossed 0->non-zero only ONCE.
    EXPECT_EQ(1, *status.partial_drain_transition_count());
  });

  /*
   * Withdraw two of three prefixes — device stays drained, counter must NOT
   * bump.
   */
  for (const auto& prefix : {kV4Prefix1, kV4Prefix2}) {
    auto prefixBatch = PrefixPathIds{{prefix, kDefaultPathID}};
    fibFuture = fib_->getFibProgramFuture();
    sendWithdrawal(prefixBatch, eBgpPeer1_);
    fibFuture.wait();
  }

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto status = rib_->getPartialDrainStatus();
    EXPECT_TRUE(*status.is_partially_drained());
    EXPECT_EQ(1, *status.num_affected_prefixes());
    // Per-prefix transitions while still drained do NOT bump the counter.
    EXPECT_EQ(1, *status.partial_drain_transition_count());
  });

  /*
   * Withdraw the last drained prefix — device flips back, counter bumps once
   * more.
   */
  {
    auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
    fibFuture = fib_->getFibProgramFuture();
    sendWithdrawal(prefixBatch, eBgpPeer1_);
    fibFuture.wait();
  }

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto status = rib_->getPartialDrainStatus();
    EXPECT_FALSE(*status.is_partially_drained());
    EXPECT_EQ(0, *status.num_affected_prefixes());
    // 0->1 (first prefix drained) + 1->0 (last prefix recovered) = 2.
    EXPECT_EQ(2, *status.partial_drain_transition_count());
  });
}

/*
 * Regression guard for capacity-threshold staleness: when a prefix drains
 * at mnh=3, then recovers, then re-drains under a NEW policy with mnh=5,
 * the surfaced threshold must reflect the NEW value (5), not the stale
 * cached value (3). RibEntry::selectBestPath resets mnhThreshold_ and
 * aggLbwBpsThreshold_ to 0 at the top of the function before recomputing
 * from the active policy; this test exercises that reset across a
 * drain->recover->re-drain cycle.
 */
TEST_F(RibFixture, PartialDrainMnhThresholdUpdatedAcrossDrainCycle) {
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  rib_->setFibBatchTime(milliseconds(2));

  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(ribOutQ_.size(), 2); });
  REPEAT_N(2, folly::coro::blockingWait(ribOutQ_.pop()));

  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  fibFuture.wait();

  WITH_RETRIES({ ASSERT_EVENTUALLY_GE(ribOutQ_.size(), 1); });
  folly::coro::blockingWait(ribOutQ_.pop());

  // Phase 1: drain at mnh=3.
  TPathSelector tPathSelector;
  tPathSelector.bgp_native_path_selection_min_nexthop() = 3;
  tPathSelector.drain_on_min_nexthop_violation() = true;

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto prefixes = rib_->getPartiallyDrainedPrefixes();
    ASSERT_EQ(1, prefixes.size());
    ASSERT_TRUE(prefixes[0].min_capacity()->next_hop_count().has_value());
    EXPECT_EQ(3, *prefixes[0].min_capacity()->next_hop_count());
  });

  /*
   * Phase 2: recover by lowering mnh to 1 (1 path satisfies). The prefix
   * is no longer drained, and the cached threshold on RibEntry must reset
   * (verified indirectly via empty drained-prefix list).
   */
  TPathSelector noDrainSelector;
  noDrainSelector.bgp_native_path_selection_min_nexthop() = 1;
  noDrainSelector.drain_on_min_nexthop_violation() = true;

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendPathSelectionPolicySet(createTPathSelectionPolicyWithPathSelector(
      {kV4Prefix1}, noDrainSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto status = rib_->getPartialDrainStatus();
    EXPECT_FALSE(*status.is_partially_drained());
    EXPECT_EQ(0, *status.num_affected_prefixes());
    EXPECT_TRUE(rib_->getPartiallyDrainedPrefixes().empty());
  });

  /*
   * Phase 3: re-drain with mnh=5. The surfaced threshold must reflect 5,
   * not the stale 3 from phase 1.
   */
  TPathSelector reDrainSelector;
  reDrainSelector.bgp_native_path_selection_min_nexthop() = 5;
  reDrainSelector.drain_on_min_nexthop_violation() = true;

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendPathSelectionPolicySet(createTPathSelectionPolicyWithPathSelector(
      {kV4Prefix1}, reDrainSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto prefixes = rib_->getPartiallyDrainedPrefixes();
    ASSERT_EQ(1, prefixes.size());
    ASSERT_TRUE(prefixes[0].min_capacity()->next_hop_count().has_value());
    /*
     * Threshold updated to 5 — proves the reset+recompute in
     * RibEntry::selectBestPath refreshes the cached value across cycles.
     */
    EXPECT_EQ(5, *prefixes[0].min_capacity()->next_hop_count());
  });
}

/*
 * Defense-in-depth: RibDC::recordPartialDrainTransition has an underflow
 * guard that triggers when a caller passes a stale oldIsPartialDrain=true
 * while drainedPrefixCount_ is already 0 (e.g., at startup before any
 * announcement has driven a transition). The guard logs an XLOGF(ERR),
 * returns false without decrementing, and leaves the counters unchanged.
 * Production code should never reach this branch under correct
 * bookkeeping, but it exists to keep drainedPrefixCount_ non-negative —
 * a negative int64_t would silently convert to a huge size_t at the
 * std::vector::reserve() call in getPartiallyDrainedPrefixes() (clamped
 * there as a backstop). This test pins the guard in place so a future
 * refactor that drops the predicate without justification will fail CI.
 */
TEST_F(RibFixture, PartialDrainUnderflowGuard) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    // Fresh state: no partial-drain entries, count == 0.
    auto status = rib_->getPartialDrainStatus();
    ASSERT_EQ(0, *status.num_affected_prefixes());
    ASSERT_EQ(0, *status.partial_drain_transition_count());

    /*
     * Simulate the stale-oldIsPartialDrain scenario. Guard must return
     * false (no transition recorded) and leave the counters at 0.
     */
    bool transitioned = rib_->recordPartialDrainTransition(
        /*oldIsPartialDrain=*/true, /*newIsPartialDrain=*/false);
    EXPECT_FALSE(transitioned);

    auto statusAfter = rib_->getPartialDrainStatus();
    EXPECT_EQ(0, *statusAfter.num_affected_prefixes());
    EXPECT_EQ(0, *statusAfter.partial_drain_transition_count());
  });
}

/*
 * ODS visibility for partial drain: RibDC::recordPartialDrainTransition drives
 * the fb303 state gauge (kRibIsPartialDrain, 1 when the device is partially
 * drained, 0 otherwise) on every device-level flip (the 0<->1 boundary of
 * drainedPrefixCount_). Crossing between one and many drained prefixes must NOT
 * touch it, since the device-level state is unchanged. Everything runs on
 * rib_->evb_ because ThreadCachedServiceData caches per thread and
 * publishStats() only flushes the calling thread's buffer.
 */
TEST_F(RibFixture, PartialDrainOdsStateOnTransition) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibStatsDC::initCounters();
    auto* tcData = facebook::fb303::ThreadCachedServiceData::get();
    tcData->publishStats();
    ASSERT_EQ(0, tcData->getCounter(RibStatsDC::kRibIsPartialDrain));

    // false->true: first prefix drains, device enters partial drain (0->1).
    EXPECT_TRUE(rib_->recordPartialDrainTransition(
        /*oldIsPartialDrain=*/false, /*newIsPartialDrain=*/true));
    tcData->publishStats();
    EXPECT_EQ(1, tcData->getCounter(RibStatsDC::kRibIsPartialDrain));

    // A second prefix draining stays above the boundary: no device-level flip.
    EXPECT_TRUE(rib_->recordPartialDrainTransition(false, true));
    tcData->publishStats();
    EXPECT_EQ(1, tcData->getCounter(RibStatsDC::kRibIsPartialDrain));

    // One of the two prefixes undrains: still drained, still no flip.
    EXPECT_TRUE(rib_->recordPartialDrainTransition(true, false));
    tcData->publishStats();
    EXPECT_EQ(1, tcData->getCounter(RibStatsDC::kRibIsPartialDrain));

    // true->false: last prefix undrains, device leaves partial drain (1->0).
    EXPECT_TRUE(rib_->recordPartialDrainTransition(true, false));
    tcData->publishStats();
    EXPECT_EQ(0, tcData->getCounter(RibStatsDC::kRibIsPartialDrain));

    /*
     * false->true again after fully undraining: device re-enters partial drain
     * (0->1). The gauge must flip back to 1, proving re-drain is observable on
     * ODS just like the initial drain.
     */
    EXPECT_TRUE(rib_->recordPartialDrainTransition(false, true));
    tcData->publishStats();
    EXPECT_EQ(1, tcData->getCounter(RibStatsDC::kRibIsPartialDrain));
  });
}

/*
 * Tests scenario for GR with addPath enabled.
 *
 * After GR, peer resends routes and triggers bestpath recomputation.
 * Because RIB has not cleaned up the stale routes, it is possible to
 * hit the case where our bestpath selector returns more than 1 route,
 * if we see two identical best candidates whose only difference is pathId.
 * This implies that we are unable to tiebreak because the routes are
 * identical.
 *
 * We saw crashes S555824 due to this.
 *
 * This test verifies that we are able to return one of the two candidates
 * without crashing.
 */
TEST_F(RibFixture, BestPathWithAddPathEnabled_OnlyPathIdChange) {
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  rib_->setFibBatchTime(milliseconds(2));

  /*
   * first we let the initial dump finish:
   * eventually we get a RibInitialAnnouncementStart msg and one
   * RibOutAnnouncement with initialDump=true. Pop them both and then proceed
   * with test
   */
  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(ribOutQ_.size(), 2); });
  REPEAT_N(2, folly::coro::blockingWait(ribOutQ_.pop()));

  /*
   * Simulate a route to kV4Prefix1 received from eBgpPeer1_ with nexthop
   * kV4Nexthop1 and add to ribEntries_
   */
  RibEntry entry(kV4Prefix1);
  /*
   * Use the default attributes, which sets nexthop to kV4Nexthop1
   * and uses kDefaultPathID = 0.
   */
  entry.updatePath(eBgpPeer1_, attr_, false);

  // Trigger bestpath computation.
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->ribEntries_.clear();
    rib_->ribEntries_.emplace(kV4Prefix1, std::move(entry));
    RibBase::selectBestPath(
        rib_->ribEntries_.find(kV4Prefix1)->second,
        multipathSelector,
        bestpathSelector,
        false,
        0);
  });

  /*
   * Now simulate receipt of announcement from eBgpPeer1_ for the same route
   * but with different pathId.
   */
  auto prefixBatch2 = PrefixPathIds{{kV4Prefix1, 1 /* pathId */}};

  fibFuture = fib_->getFibProgramFuture();
  auto attr = attr_->clone();
  attr->publish();
  sendAnnouncement(prefixBatch2, eBgpPeer1_, attr);

  // expect one announcement
  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));

  // Verify that the bestpath is equivalent to attr_ (or attr).
  auto routeInfo = rib_->ribEntries_.find(kV4Prefix1)->second.getBestPath();
  auto& bestpath = routeInfo->attrs;
  EXPECT_TRUE(*attr == *bestpath);

  // expect no more announcement
  REPEAT_N(5, { EXPECT_EQ(0, ribOutQ_.size()); });
}

/*
 * Test when the best path is updated with different next hop,
 * we withdraw the previous one.
 */
TEST_F(RibFixture, BestpathChangeWithNexthopChangeTest) {
  rib_->setFibBatchTime(milliseconds(2));

  auto prefix1 = folly::IPAddress::createNetwork("1::/64");
  auto prefix2 = folly::IPAddress::createNetwork("2::/64");
  auto prefixBatch1 = PrefixPathIds{{prefix1, kDefaultPathID}};
  auto prefixBatch2 = PrefixPathIds{{prefix2, kDefaultPathID}};

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(2);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(prefix1), _, _, _, _, _)).Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(prefix2), _, _, _, _, _)).Times(2);
  EXPECT_CALL(*fib_, program_(true)).Times(1);
  EXPECT_CALL(*fib_, program_(false)).Times(1);

  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
  sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);
  sendInitialPathComputation();
  fibFuture.wait();

  /*
   * send local route for prefix2, this should:
   * 1. set prefix2 bestpath to local route,
   * 2. local route has different nexthop, withdraw previous one
   */
  fibFuture = fib_->getFibProgramFuture();
  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr->setNexthop(kLocalRouteV4Nexthop);
  attr->publish();
  sendAnnouncement(prefixBatch2, localPeer_, attr);
  fibFuture.wait();
}

/*
 * This tests rib flags update after EoR with add-path enabled
 * ribFlagUpdate: addPath = false
 */
TEST_P(RibFixtureAddPathTestSuite, EoR) {
  // send prefixes from iBgpPeer_(kPeerAddr2) to Rib
  auto prefixBatch1 = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto prefixBatch2 = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
  sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);

  EXPECT_EQ(0, ribOutQ_.size());

  // send EoR
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();

  // Expect RibInitialAnnouncementStart before initial dump.
  auto msg0 = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg0));

  // initial dump to all peers after syncFib
  auto msg1 = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg1));
  auto announcement1 = std::get<RibOutAnnouncement>(msg1);
  ASSERT_EQ(2, announcement1.entries.size());
  EXPECT_EQ(kDefaultPathID, announcement1.entries[0].pathIdToSend);
  EXPECT_EQ(kDefaultPathID, announcement1.entries[1].pathIdToSend);
  EXPECT_EQ(true, announcement1.sendWithEoR);
  EXPECT_EQ(true, announcement1.initialDump);
  EXPECT_EQ(GetParam() ? 2 : 0, announcement1.addPathEntries.size());
  if (GetParam()) {
    checkRibOutEntriesAddPathIds(announcement1);
  }
}
/*
Test the behavior when a prefix with different new best path injects and
withdrawn. a.k.a. Redistributing
Steps:
1. send EoR
2. ebgpPeer1_ announces prefix1 with attr_.
Here, we expect to see Fib programmed and RibOutAnnouncement with bestpath
from ebgpPeer1_.
3. redistributePeer_ injects the same prefix with attrHighLocalPref_.
Now, we expect to see Fib withdrawn previous programmed route, but
RibOutAnnouncement with bestpath from redistributePeer_
4. redistributePeer_ withdraws prefix. We expect Fib reprogram ebgpPeer1_ as
bestpath, RibOutAnnouncement with bestpath from ebgpPeer1_.
*/
TEST_F(RibFixture, RibAnnouncementRedistributePeerTest) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  // Step 1: We first send EoR
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  // Expect RibInitialAnnouncementStart before initial dump RibOutAnnouncements.
  auto ribInitialAnnouncementStart = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(
      std::holds_alternative<RibInitialAnnouncementStart>(
          ribInitialAnnouncementStart));

  auto msg1 = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg1));
  auto announcement1 = std::get<RibOutAnnouncement>(msg1);
  EXPECT_EQ(0, announcement1.entries.size());
  EXPECT_EQ(true, announcement1.sendWithEoR);
  EXPECT_EQ(true, announcement1.initialDump);
  checkRibOutEntriesAddPathIds(announcement1);

  // Step 2: ebgpPeer1_ with announce prefix 1
  fibFuture = fib_->getFibProgramFuture();
  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  fibFuture.wait();

  auto msg2 = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg2));
  auto announcement2 = std::get<RibOutAnnouncement>(msg2);
  ASSERT_EQ(1, announcement2.entries.size());
  EXPECT_EQ(kDefaultPathID, announcement2.entries[0].pathIdToSend);
  EXPECT_EQ(false, announcement2.sendWithEoR);
  EXPECT_EQ(false, announcement2.initialDump);
  EXPECT_EQ(eBgpPeer1_, announcement2.entries.at(0).peer);
  checkRibOutEntriesAddPathIds(announcement2);

  /*
   * Step 3: redistributePeer_ announce prefix 1 again; best path
   * changed
   */
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, redistributePeer_, attrHighLocalPref_);
  fibFuture.wait();

  auto msg3 = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg3));
  auto announcement3 = std::get<RibOutAnnouncement>(msg3);
  ASSERT_EQ(1, announcement3.entries.size());
  EXPECT_EQ(kDefaultPathID, announcement3.entries[0].pathIdToSend);
  EXPECT_EQ(false, announcement3.sendWithEoR);
  EXPECT_EQ(false, announcement3.initialDump);
  EXPECT_EQ(redistributePeer_, announcement3.entries.at(0).peer);
  /*
   * first path to be selected is no longer announced, hence just minId+1 is
   * announced here
   */
  checkRibOutEntriesAddPathIds(
      announcement3, PrefixToPathIdsMap{{kV6Prefix1, {kMinPathIDToSend + 1}}});

  /*
   * Step 4: redistributePeer_ withdrawn prefix 1; best path
   * changed
   */
  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(prefixBatch, redistributePeer_);
  fibFuture.wait();

  auto msg4 = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg4));
  auto announcement4 = std::get<RibOutAnnouncement>(msg4);
  ASSERT_EQ(1, announcement4.entries.size());
  EXPECT_EQ(kDefaultPathID, announcement4.entries[0].pathIdToSend);
  EXPECT_EQ(false, announcement4.sendWithEoR);
  EXPECT_EQ(false, announcement4.initialDump);
  EXPECT_EQ(eBgpPeer1_, announcement4.entries.at(0).peer);
  checkRibOutEntriesAddPathIds(announcement4);
}

/*
 * Test for VIP Injector, that is same peer with multiple sessions
 * First, a peer 2.2.2.2 with router ID 2.2.2.2 injects prefix 2001::/64. Here,
 * we are expecting to see the Fib programmed. Later, a peer (on the same
 * physical machine) 2.2.2.2 with router ID 1.1.1.1 injects the same prefix.
 * Now, we are also expecting to see Fib programmed because the policy
 * selects the one with lower router_id. Next, the second session sends a
 * withdrawal of the prefix, and the best path is changed again, and we are
 * expecting Fib programmed again.
 */
TEST_F(RibFixture, RibAnnouncementFromSamePeerWithBestPathChange) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  // Step 1: We first send EoR
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  // Expect RibInitialAnnouncementStart before initial dump RibOutAnnouncements.
  auto ribInitialAnnouncementStart = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(
      std::holds_alternative<RibInitialAnnouncementStart>(
          ribInitialAnnouncementStart));

  auto msg1 = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg1));
  auto announcement1 = std::get<RibOutAnnouncement>(msg1);
  EXPECT_EQ(0, announcement1.entries.size());
  EXPECT_EQ(true, announcement1.sendWithEoR);
  EXPECT_EQ(true, announcement1.initialDump);
  checkRibOutEntriesAddPathIds(announcement1);

  // Step 2: peer2 with higher routerId announce prefix 1
  fibFuture = fib_->getFibProgramFuture();
  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch, injector2_, attr_);
  fibFuture.wait();

  auto msg2 = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg2));
  auto announcement2 = std::get<RibOutAnnouncement>(msg2);
  ASSERT_EQ(1, announcement2.entries.size());
  EXPECT_EQ(kDefaultPathID, announcement2.entries[0].pathIdToSend);
  EXPECT_EQ(false, announcement2.sendWithEoR);
  EXPECT_EQ(false, announcement2.initialDump);
  EXPECT_EQ(injector2_, announcement2.entries.at(0).peer);
  checkRibOutEntriesAddPathIds(announcement2);

  /*
   * Step 3: peer1 with lower routerId announce prefix 1 again; best path
   * changed
   */
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, injector1_, attr_);
  fibFuture.wait();

  auto msg3 = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg3));
  auto announcement3 = std::get<RibOutAnnouncement>(msg3);
  ASSERT_EQ(1, announcement3.entries.size());
  EXPECT_EQ(kDefaultPathID, announcement3.entries[0].pathIdToSend);
  EXPECT_EQ(false, announcement3.sendWithEoR);
  EXPECT_EQ(false, announcement3.initialDump);
  EXPECT_EQ(injector1_, announcement3.entries.at(0).peer);
  checkRibOutEntriesAddPathIds(announcement3);

  // Step 4: peer1 with lower routerId withdraw prefix 1; best path changed.
  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(prefixBatch, injector1_);
  fibFuture.wait();

  auto msg4 = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg4));
  auto announcement4 = std::get<RibOutAnnouncement>(msg4);
  ASSERT_EQ(1, announcement4.entries.size());
  EXPECT_EQ(kDefaultPathID, announcement4.entries[0].pathIdToSend);
  EXPECT_EQ(false, announcement4.sendWithEoR);
  EXPECT_EQ(false, announcement4.initialDump);
  EXPECT_EQ(injector2_, announcement4.entries.at(0).peer);
  checkRibOutEntriesAddPathIds(announcement4);
}

/*
 * Test for VIP Injector, that is same peer with multiple sessions
 * In this testing, the best path will not change since the second session has
 * a higher router_id.
 * First, a peer 2.2.2.2 with router ID 1.1.1.1 injects prefix 2001::/64. Here,
 * we are expecting to see the Fib programmed. Later, a peer (on the same
 * physical machine) 2.2.2.2 with router ID 2.2.2.2 injects the same prefix.
 * Now, we are NOT expecting to see Fib programmed because the policy
 * selects the one with lower router_id. Next, the second session sends a
 * withdrawal of the prefix, and the best path doesn't change, and Fib is not
 * expected to be programmed.
 */
TEST_F(RibFixture, RibAnnouncementFromSamePeerWithoutBestPathChange) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  auto fibFuture = fib_->getFibProgramFuture();
  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch, injector1_, attr_);
  sendInitialPathComputation();
  fibFuture.wait();

  // Expect RibInitialAnnouncementStart before initial dump RibOutAnnouncements.
  auto ribInitialAnnouncementStart = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(
      std::holds_alternative<RibInitialAnnouncementStart>(
          ribInitialAnnouncementStart));

  auto msg1 = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg1));
  auto announcement1 = std::get<RibOutAnnouncement>(msg1);
  ASSERT_EQ(1, announcement1.entries.size());
  EXPECT_EQ(kDefaultPathID, announcement1.entries[0].pathIdToSend);
  EXPECT_EQ(true, announcement1.sendWithEoR);
  EXPECT_EQ(true, announcement1.initialDump);
  EXPECT_EQ(injector1_, announcement1.entries.at(0).peer);

  sendAnnouncement(prefixBatch, injector2_, attr_);
  auto bestPath = rib_->getBestPath(kV6Prefix1);
  EXPECT_EQ(injector1_, bestPath->peer);
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto& ribEntries = rib_->ribEntries_;
    EXPECT_EQ(1, ribEntries.size());
    auto& ribEntry = ribEntries.at(kV6Prefix1);
    auto routeInfos = ribEntry.getRouteInfos(
        nettools::bgplib::BgpPeerId(injector1_.addr, injector1_.routerId));
    EXPECT_NE(0, routeInfos.size());
    EXPECT_NE(nullptr, routeInfos.begin()->second);
    routeInfos = ribEntry.getRouteInfos(
        nettools::bgplib::BgpPeerId(injector2_.addr, injector2_.routerId));
    EXPECT_NE(0, routeInfos.size());
    EXPECT_NE(nullptr, routeInfos.begin()->second);
    EXPECT_EQ(2, ribEntry.getAllPathsCnt());
  });

  sendWithdrawal(prefixBatch, injector2_);
  bestPath = rib_->getBestPath(kV6Prefix1);
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    EXPECT_EQ(injector1_, bestPath->peer);
    auto& ribEntries = rib_->ribEntries_;
    auto& ribEntry = ribEntries.at(kV6Prefix1);
    auto routeInfos = ribEntry.getRouteInfos(
        nettools::bgplib::BgpPeerId(injector1_.addr, injector1_.routerId));
    EXPECT_NE(0, routeInfos.size());
    EXPECT_NE(nullptr, routeInfos.begin()->second);
    routeInfos = ribEntry.getRouteInfos(
        nettools::bgplib::BgpPeerId(injector2_.addr, injector2_.routerId));
    EXPECT_EQ(0, routeInfos.size());
    EXPECT_EQ(1, ribEntry.getAllPathsCnt());
  });
}

/*
 * Test: RIB version increments on bestpath changes.
 *
 * Verifies:
 * 1. RIB version starts at 0
 * 2. RIB version increments when bestpath changes (new route)
 * 3. RIB entry stores its version at time of change
 * 4. RIB version increments when bestpath changes due to withdrawal
 */
TEST_F(RibFixture, RibVersionIncrementsOnBestpathChange) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  // Verify initial RIB version is 0
  EXPECT_EQ(0, rib_->getRibVersion());

  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};

  // Step 1: Send EoR first
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  // Drain initial dump messages
  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));
  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));

  // RIB version should still be 0 (no routes yet)
  EXPECT_EQ(0, rib_->getRibVersion());

  /*
   * Step 2: Inject first route - should increment RIB version. The version is
   * now stamped at emission time (in handleFibProgrammedMessage, after the FIB
   * round-trip), so synchronize on the announcement being pushed before reading
   * the version.
   */
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, injector2_, attr_);
  fibFuture.wait();

  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));

  uint64_t versionAfterFirstRoute = rib_->getRibVersion();
  EXPECT_GT(versionAfterFirstRoute, 0);
  // The emitted entry carries the same version as the global counter.
  EXPECT_EQ(
      versionAfterFirstRoute,
      std::get<RibOutAnnouncement>(msg).entries.at(0).ribVersion);

  // Verify RibEntry has the version set
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto& ribEntries = rib_->ribEntries_;
    auto& ribEntry = ribEntries.at(kV6Prefix1);
    EXPECT_EQ(versionAfterFirstRoute, ribEntry.getRibVersion());
  });

  tcData->publishStats();
  /*
   * Temporarily commented out until we fix the RIB versioning in RIB. The
   * bgpcpp.rib.tableVersion counter is advanced by PeerManager now, and this
   * fixture drives the RIB directly with no PeerManager attached.
   *
   * EXPECT_EQ(
   *     versionAfterFirstRoute,
   *     tcData->getCounter(RibStats::kRibTableVersion));
   */

  // Verify prefix counter after first route
  EXPECT_EQ(1, tcData->getCounter(RibStats::kRibPrefixCount));

  /*
   * Step 3: Inject same prefix from peer with lower router ID - bestpath
   * changes. Synchronize on the announcement before reading the version.
   */
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, injector1_, attr_);
  fibFuture.wait();

  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));

  uint64_t versionAfterBetterRoute = rib_->getRibVersion();
  EXPECT_GT(versionAfterBetterRoute, versionAfterFirstRoute);
  EXPECT_EQ(
      versionAfterBetterRoute,
      std::get<RibOutAnnouncement>(msg).entries.at(0).ribVersion);

  // Verify RibEntry version updated
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto& ribEntries = rib_->ribEntries_;
    auto& ribEntry = ribEntries.at(kV6Prefix1);
    EXPECT_EQ(versionAfterBetterRoute, ribEntry.getRibVersion());
  });

  tcData->publishStats();
  /*
   * Temporarily commented out; see the note on the first table version check.
   *
   * EXPECT_EQ(
   *     versionAfterBetterRoute,
   *     tcData->getCounter(RibStats::kRibTableVersion));
   */

  // Verify prefix count unchanged (same prefix)
  EXPECT_EQ(1, tcData->getCounter(RibStats::kRibPrefixCount));

  /*
   * Step 4: Withdraw the winning path - bestpath reverts to injector2, which is
   * re-advertised in handleFibProgrammedMessage. Synchronize on the
   * announcement before reading the version.
   */
  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(prefixBatch, injector1_);
  fibFuture.wait();

  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));

  uint64_t versionAfterWithdrawal = rib_->getRibVersion();
  EXPECT_GT(versionAfterWithdrawal, versionAfterBetterRoute);
  EXPECT_EQ(
      versionAfterWithdrawal,
      std::get<RibOutAnnouncement>(msg).entries.at(0).ribVersion);

  // Verify RibEntry version updated again
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto& ribEntries = rib_->ribEntries_;
    auto& ribEntry = ribEntries.at(kV6Prefix1);
    EXPECT_EQ(versionAfterWithdrawal, ribEntry.getRibVersion());
  });

  tcData->publishStats();
  /*
   * Temporarily commented out; see the note on the first table version check.
   *
   * EXPECT_EQ(
   *     versionAfterWithdrawal,
   *     tcData->getCounter(RibStats::kRibTableVersion));
   */

  // Verify prefix count unchanged
  EXPECT_EQ(1, tcData->getCounter(RibStats::kRibPrefixCount));
}

/*
 * Test: the co_getRibVersion / co_getNumPrefixes thrift handlers reflect the
 * live RIB, read through the timeout-protected evb hop (co_runOnEvbWithTimeout,
 * mirroring co_getRibSummary).
 *
 * Uses RibFixture -- a real, running RIB event base plus a real BgpServiceDC --
 * so the handler's outer evb hop actually completes. The MockRib fixture in
 * BgpServiceBaseTest cannot exercise these handlers because it never starts the
 * RIB evb (the hop would stall until the timeout).
 *
 * DISABLED: co_getRibVersion now reports PeerManager's maxRibVersion_ rather
 * than RibBase's ribVersion_, so the version half of this test no longer
 * describes the handler. Re-enable once the RIB versioning fix lands and the
 * expectations are rewritten against whichever counter it should report.
 */
TEST_F(RibFixture, DISABLED_GetRibVersionAndNumPrefixesHandlers) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  // Empty RIB: both handlers report zero through the evb hop.
  EXPECT_EQ(0, folly::coro::blockingWait(service_->co_getRibVersion()));
  EXPECT_EQ(0, folly::coro::blockingWait(service_->co_getNumPrefixes()));

  // Send EoR, then drain the initial dump messages.
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));
  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));

  // Inject one route; synchronize on the emitted announcement before reading.
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(
      PrefixPathIds{{kV6Prefix1, kDefaultPathID}}, injector2_, attr_);
  fibFuture.wait();
  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));

  // Each handler delegates to the RIB getter and returns its live value.
  EXPECT_EQ(
      static_cast<int64_t>(rib_->getRibVersion()),
      folly::coro::blockingWait(service_->co_getRibVersion()));
  EXPECT_GT(folly::coro::blockingWait(service_->co_getRibVersion()), 0);
  EXPECT_EQ(
      static_cast<int64_t>(rib_->getNumPrefixes()),
      folly::coro::blockingWait(service_->co_getNumPrefixes()));
  EXPECT_EQ(1, folly::coro::blockingWait(service_->co_getNumPrefixes()));
}

/*
 * Test: RIB version does NOT increment on duplicate/no-op route.
 *
 * When the same route is re-announced without any changes, it should
 * not be considered a material change and version should not increment.
 */
TEST_F(RibFixture, RibVersionNoChangeOnDuplicateRoute) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};

  // Send EoR first
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  // Drain initial dump messages
  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));
  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));

  /*
   * Inject route. The version is stamped at emission time, so synchronize on
   * the announcement being pushed before reading the version.
   */
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  fibFuture.wait();

  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));

  uint64_t versionAfterFirst = rib_->getRibVersion();
  EXPECT_GT(versionAfterFirst, 0);

  /*
   * Re-inject same route with same attributes (no material change)
   * Note: We don't wait for FIB programming since duplicate routes don't
   * trigger FIB updates. Instead, wait for RIB to process the message.
   */
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    // Version should NOT have changed since bestpath didn't change
    EXPECT_EQ(versionAfterFirst, rib_->getRibVersion());
  });
}

/*
 * Test: the RIB version is stamped on the emitted RibOut entries (the message
 * carrier consumed by AdjRib), not just on the global counter, and a prefix's
 * version is monotonic from the reader's point of view across
 * announce -> withdraw.
 *
 * This guards the queue-push-time assignment: the version carried by each
 * RibOutAnnouncementEntry / RibOutWithdrawalEntry equals the RIB version at the
 * moment the prefix is pushed onto ribOutQ_, and a withdrawal (stamped in
 * prepareFibProgramming) carries a strictly greater version than the prior
 * announcement of the same prefix.
 */
TEST_F(RibFixture, RibVersionStampedOnEmittedEntriesMonotonicPerPrefix) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};

  // Send EoR first and drain the initial dump.
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));
  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));

  /*
   * Announce a prefix: the emitted announcement entry carries the current RIB
   * version (stamped at emission in handleFibProgrammedMessage).
   */
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  fibFuture.wait();

  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  auto announcement = std::get<RibOutAnnouncement>(msg);
  ASSERT_EQ(1, announcement.entries.size());
  const uint64_t announceVersion = announcement.entries.at(0).ribVersion;
  EXPECT_EQ(kV6Prefix1, announcement.entries.at(0).prefix);
  EXPECT_GT(announceVersion, 0);
  EXPECT_EQ(rib_->getRibVersion(), announceVersion);

  /*
   * Withdraw the prefix: the withdrawal entry carries a strictly greater
   * version (stamped at emission in prepareFibProgramming, before the FIB
   * round-trip).
   */
  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(prefixBatch, eBgpPeer1_);
  fibFuture.wait();

  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
  auto withdrawal = std::get<RibOutWithdrawal>(msg);
  ASSERT_EQ(1, withdrawal.entries.size());
  const uint64_t withdrawVersion = withdrawal.entries.at(0).ribVersion;
  EXPECT_EQ(kV6Prefix1, withdrawal.entries.at(0).prefix);
  EXPECT_EQ(rib_->getRibVersion(), withdrawVersion);
  EXPECT_GT(withdrawVersion, announceVersion);
}

/*
 * Test: a prefix with multiple ECMP / add-path entries is version-bumped
 * exactly once per pass. Every emitted entry for the prefix -- the best-path
 * entry and all add-path entries -- carries the same RIB version, and the
 * global counter advances by exactly one, not once per path.
 */
TEST_F(RibFixture, RibVersionSingleBumpAcrossMultipathAddPathEntries) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};

  /*
   * Two equal-cost paths for the same prefix (kV4Nexthop1 via eBgpPeer1_,
   * kV4Nexthop2 via eBgpPeer2_) so multipath selection yields two ECMP
   * nexthops -> one best-path entry plus two add-path entries.
   */
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_); // attr_ carries kV4Nexthop1
  auto newAttr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  newAttr->setNexthop(kV4Nexthop2);
  newAttr->publish();
  sendAnnouncement(prefixBatch, eBgpPeer2_, newAttr);

  // Nothing is advertised (and no version is bumped) before EoR.
  const uint64_t versionBeforeEoR = rib_->getRibVersion();
  EXPECT_EQ(0, versionBeforeEoR);

  // EoR triggers best-path selection, FIB programming and the announcement.
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));
  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  auto announcement = std::get<RibOutAnnouncement>(msg);
  ASSERT_EQ(1, announcement.entries.size());
  ASSERT_EQ(2, announcement.addPathEntries.size());

  // Best entry and both add-path entries share the single per-prefix version.
  const uint64_t version = announcement.entries.at(0).ribVersion;
  EXPECT_GT(version, 0);
  for (const auto& addPathEntry : announcement.addPathEntries) {
    EXPECT_EQ(kV4Prefix1, addPathEntry.prefix);
    EXPECT_EQ(version, addPathEntry.ribVersion);
  }

  // Exactly one bump for the single prefix, regardless of its path count.
  EXPECT_EQ(versionBeforeEoR + 1, rib_->getRibVersion());
  EXPECT_EQ(version, rib_->getRibVersion());
}

/*
 * Test: when several prefixes are announced in a single pass, the one
 * RibOutAnnouncement carries one entry per prefix, each with its own version,
 * and the versions are distinct and strictly increasing in emission (queue)
 * order -- i.e. the reader sees a monotonic sequence within the message.
 */
TEST_F(RibFixture, RibVersionDistinctPerPrefixInAnnouncement) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  /*
   * Announce two distinct prefixes before EoR so a single initial-dump
   * announcement carries both, each version-stamped once at emission.
   */
  sendAnnouncement(
      PrefixPathIds{{kV4Prefix1, kDefaultPathID}}, eBgpPeer1_, attr_);
  auto attr2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr2->setNexthop(kV4Nexthop2);
  attr2->publish();
  sendAnnouncement(
      PrefixPathIds{{kV4Prefix2, kDefaultPathID}}, eBgpPeer1_, attr2);

  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));
  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  auto announcement = std::get<RibOutAnnouncement>(msg);
  ASSERT_EQ(2, announcement.entries.size());

  /*
   * Two distinct prefixes -> two distinct versions, strictly increasing in
   * the order they were pushed onto the queue.
   */
  EXPECT_NE(
      announcement.entries.at(0).prefix, announcement.entries.at(1).prefix);
  EXPECT_LT(
      announcement.entries.at(0).ribVersion,
      announcement.entries.at(1).ribVersion);
}

/*
 * Test: when several prefixes are withdrawn in a single prepareFibProgramming
 * pass, the one RibOutWithdrawal carries one entry per prefix, each with its
 * own version, distinct and strictly increasing in emission (queue) order.
 * Drives prepareFibProgramming() directly so both prefixes are guaranteed to
 * be withdrawn in the same pass.
 */
TEST_F(RibFixture, RibVersionDistinctPerPrefixInWithdrawal) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    for (const auto& prefix : {kV4Prefix1, kV4Prefix2}) {
      rib_->ribEntries_.emplace(prefix, RibEntry(prefix));
      auto& entry = rib_->ribEntries_.at(prefix);
      auto pathAttr = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      pathAttr->publish();
      entry.updatePath(eBgpPeer1_, pathAttr, true, 0);
      RibBase::selectBestPath(
          entry, multipathSelector, bestpathSelector, false, 0);
      /*
       * Commit so the prefix is treated as already advertised, then withdraw
       * its only path so best path becomes null (a full withdrawal).
       */
      entry.commitMultipaths();
      entry.commitMultipathNexthops();
      entry.commitBestpath();
      entry.updatePath(eBgpPeer1_, nullptr, true, 0);
      entry.requirePathSelection();
    }
    rib_->prepareFibProgramming();
  });

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
  auto withdrawal = std::get<RibOutWithdrawal>(msg);
  ASSERT_EQ(2, withdrawal.entries.size());

  EXPECT_NE(withdrawal.entries.at(0).prefix, withdrawal.entries.at(1).prefix);
  EXPECT_LT(
      withdrawal.entries.at(0).ribVersion, withdrawal.entries.at(1).ribVersion);
}

/*
 * Scenario 1: in one prepareFibProgramming pass, one prefix is withdrawn and
 * another is (re)announced. The withdrawal is pushed to ribOutQ_ inside
 * prepareFibProgramming (before the FIB round-trip); the announcement is pushed
 * later, in handleFibProgrammedMessage (after it). Because the version is
 * stamped at emission time, the withdrawal -- advertised first -- always
 * carries a LOWER version than the announcement, regardless of the order in
 * which the two prefixes happened to be evaluated during path selection.
 */
TEST_F(RibFixture, RibVersionWithdrawalAdvertisedFirstHasLowerVersion) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  auto p1 = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto p2 = PrefixPathIds{{kV4Prefix2, kDefaultPathID}};

  // Establish both prefixes before EoR, then drain the initial dump.
  sendAnnouncement(p1, eBgpPeer1_, attr_);
  sendAnnouncement(p2, eBgpPeer1_, attr_);
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  REPEAT_N(2, folly::coro::blockingWait(ribOutQ_.pop()));

  /*
   * In one pass: fully withdraw p1 and change p2's best path (a higher
   * local-pref path from another peer). p1's withdrawal is advertised before
   * the FIB round-trip; p2's re-announcement after it.
   */
  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(p1, eBgpPeer1_);
  sendAnnouncement(p2, eBgpPeer2_, attrHighLocalPref_);
  fibFuture.wait();

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
  auto withdrawal = std::get<RibOutWithdrawal>(msg);
  ASSERT_EQ(1, withdrawal.entries.size());
  EXPECT_EQ(kV4Prefix1, withdrawal.entries.at(0).prefix);

  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  auto announcement = std::get<RibOutAnnouncement>(msg);
  ASSERT_EQ(1, announcement.entries.size());
  EXPECT_EQ(kV4Prefix2, announcement.entries.at(0).prefix);

  EXPECT_LT(
      withdrawal.entries.at(0).ribVersion,
      announcement.entries.at(0).ribVersion);
}

/*
 * Scenario 1b (revert guard): in one prepareFibProgramming pass, one block of
 * prefixes is withdrawn while another block is re-announced (a strictly better
 * path). Withdrawals are pushed to ribOutQ_ inside prepareFibProgramming
 * (before the FIB round-trip); announcements are pushed later, in
 * handleFibProgrammedMessage (after it). Because the version is stamped at
 * emission time, EVERY withdrawal carries a strictly LOWER version than EVERY
 * announcement, regardless of the (F14) order in which the prefixes were
 * path-selected.
 *
 * This is the reliable regression guard for the fix: under the old
 * path-selection-time stamping the two blocks' versions interleave in F14
 * order, so max(withdrawal) < min(announcement) would hold only if all
 * withdrawn prefixes happened to sort before all announced ones -- ~1/C(12,6)
 * (about 1/924) with the block size below -- so this test fails on a revert to
 * path-selection-time stamping with near-certainty, unlike the single-prefix
 * scenario 1 above (which can pass vacuously depending on hash order).
 */
TEST_F(RibFixture, RibVersionWithdrawalsPrecedeAllAnnouncementsInPass) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  constexpr uint32_t kBlock = 6;
  constexpr int kWithdrawBase = 0x0A000000; // 10.0.0.0
  constexpr int kAnnounceBase = 0x14000000; // 20.0.0.0

  // Establish both blocks from eBgpPeer1_, then drain the initial dump.
  sendBulkAnnouncement(kBlock, eBgpPeer1_, attr_, kWithdrawBase);
  sendBulkAnnouncement(kBlock, eBgpPeer1_, attr_, kAnnounceBase);
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  REPEAT_N(2, folly::coro::blockingWait(ribOutQ_.pop())); // start + dump

  /*
   * One pass: withdraw the first block and re-announce the second with a
   * strictly better path (higher local-pref from a different peer), so the
   * second block is re-advertised in handleFibProgrammedMessage.
   */
  fibFuture = fib_->getFibProgramFuture();
  sendBulkWithdrawal(kBlock, eBgpPeer1_, kWithdrawBase);
  sendBulkAnnouncement(kBlock, eBgpPeer2_, attrHighLocalPref_, kAnnounceBase);
  fibFuture.wait();

  // Collect every main withdrawal/announcement version from the pass.
  uint64_t maxWithdrawVersion = 0;
  uint64_t minAnnounceVersion = 0;
  bool haveAnnounce = false;
  uint32_t withdrawn = 0;
  uint32_t announced = 0;
  while (withdrawn < kBlock || announced < kBlock) {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop());
    if (std::holds_alternative<RibOutWithdrawal>(msg)) {
      for (const auto& entry : std::get<RibOutWithdrawal>(msg).entries) {
        if (entry.ribVersion > maxWithdrawVersion) {
          maxWithdrawVersion = entry.ribVersion;
        }
        withdrawn++;
      }
    } else if (std::holds_alternative<RibOutAnnouncement>(msg)) {
      for (const auto& entry : std::get<RibOutAnnouncement>(msg).entries) {
        if (!haveAnnounce || entry.ribVersion < minAnnounceVersion) {
          minAnnounceVersion = entry.ribVersion;
          haveAnnounce = true;
        }
        announced++;
      }
    }
  }

  ASSERT_EQ(kBlock, withdrawn);
  ASSERT_EQ(kBlock, announced);
  // The last-emitted withdrawal still precedes the first-emitted announcement.
  EXPECT_LT(maxWithdrawVersion, minAnnounceVersion);
}

/*
 * Scenario 2: a pure best-path change (no change to the multipath nexthop set)
 * bumps the RIB version exactly once. Two paths for one prefix share the same
 * nexthop (so the ECMP nexthop set never changes) and the best flips onto the
 * lower-router-id path. This proves a best-path change does NOT always come
 * with a multipath change, and that it still increments the version by exactly
 * one.
 */
TEST_F(RibFixture, RibVersionBestPathOnlyChangeBumpsExactlyOnce) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};

  // EoR with no routes; drain the initial-dump start marker + empty dump.
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  REPEAT_N(2, folly::coro::blockingWait(ribOutQ_.pop()));

  // First path (injector2) establishes the best path.
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, injector2_, attr_);
  fibFuture.wait();
  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  const uint64_t versionAfterFirst = rib_->getRibVersion();

  /*
   * Same prefix, same nexthop, lower router id (injector1) -> the best path
   * flips but the multipath nexthop set is unchanged. Exactly one more bump.
   */
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, injector1_, attr_);
  fibFuture.wait();
  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  const uint64_t versionAfterBestFlip = rib_->getRibVersion();

  EXPECT_EQ(versionAfterFirst + 1, versionAfterBestFlip);
  EXPECT_EQ(
      versionAfterBestFlip,
      std::get<RibOutAnnouncement>(msg).entries.at(0).ribVersion);
}

/*
 * Scenario 3: adding one path to an existing ECMP prefix re-advertises the
 * entire multipath (all paths) but bumps the RIB version exactly once -- not
 * once per path. Every re-advertised add-path entry shares the single new
 * version.
 */
TEST_F(RibFixture, RibVersionIncrementalAddPathBumpsExactlyOnce) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};

  // Establish two ECMP paths for the prefix (nh1 via peer1, nh2 via peer2).
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_); // attr_ carries kV4Nexthop1
  auto attr2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr2->setNexthop(kV4Nexthop2);
  attr2->publish();
  sendAnnouncement(prefixBatch, eBgpPeer2_, attr2);
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  REPEAT_N(2, folly::coro::blockingWait(ribOutQ_.pop()));
  const uint64_t versionAfterTwoPaths = rib_->getRibVersion();

  /*
   * Add a third ECMP path (nh3 via peer3). All three paths are re-advertised,
   * but the version bumps by exactly one and every add-path entry shares it.
   */
  auto attr3 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr3->setNexthop(kV4Nexthop3);
  attr3->publish();
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer3_, attr3);
  fibFuture.wait();

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  auto announcement = std::get<RibOutAnnouncement>(msg);
  ASSERT_EQ(3, announcement.addPathEntries.size());

  const uint64_t versionAfterThreePaths = rib_->getRibVersion();
  EXPECT_EQ(versionAfterTwoPaths + 1, versionAfterThreePaths);
  for (const auto& addPathEntry : announcement.addPathEntries) {
    EXPECT_EQ(versionAfterThreePaths, addPathEntry.ribVersion);
  }
}

/*
 * Scenario 4: shrinking an ECMP set (bestpath/multipath shrink) makes one
 * prefix emit BOTH an add-path withdrawal (for the removed nexthop) AND a
 * re-announcement (surviving paths) in the same pass. These are two separate
 * RibOut messages -- the withdrawal is pushed before the announcement -- but
 * they SHARE one version (one bump per pass). This same-version behavior is
 * safe: the reader coalesces both onto one change-list item and advances the
 * consumer's version once (see AdjRibOut.cpp:processShadowRibEntryChange), so
 * no two-phase (V, V+1) bump is needed.
 */
TEST_F(RibFixture, RibVersionEcmpShrinkWithdrawAndReannounceShareVersion) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};

  // Establish three ECMP paths (nh1/nh2/nh3 via peers 1/2/3).
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  auto attr2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr2->setNexthop(kV4Nexthop2);
  attr2->publish();
  sendAnnouncement(prefixBatch, eBgpPeer2_, attr2);
  auto attr3 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr3->setNexthop(kV4Nexthop3);
  attr3->publish();
  sendAnnouncement(prefixBatch, eBgpPeer3_, attr3);
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  REPEAT_N(2, folly::coro::blockingWait(ribOutQ_.pop()));
  const uint64_t versionBeforeShrink = rib_->getRibVersion();

  /*
   * Shrink the ECMP set: withdraw the nh3 path. The same prefix now emits an
   * add-path withdrawal (for nh3) then a re-announcement (nh1/nh2), two
   * separate RibOut messages that share ONE version.
   */
  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(prefixBatch, eBgpPeer3_);
  fibFuture.wait();

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
  auto withdrawal = std::get<RibOutWithdrawal>(msg);
  ASSERT_EQ(1, withdrawal.addPathEntries.size());

  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  auto announcement = std::get<RibOutAnnouncement>(msg);
  ASSERT_EQ(2, announcement.addPathEntries.size());

  const uint64_t versionAfterShrink = rib_->getRibVersion();
  /*
   * One bump for the whole shrink pass; withdrawal and re-announcement share
   * it.
   */
  EXPECT_EQ(versionBeforeShrink + 1, versionAfterShrink);
  EXPECT_EQ(versionAfterShrink, withdrawal.addPathEntries.at(0).ribVersion);
  for (const auto& addPathEntry : announcement.addPathEntries) {
    EXPECT_EQ(versionAfterShrink, addPathEntry.ribVersion);
  }
}

/*
 * Rule 2 (announcement path): a single prefix with more than kRibChunkSize
 * add-paths spans more than one add-path chunk. Entries within one chunk share
 * a version (rule 1); the chunk that follows a mid-prefix flush carries a
 * strictly higher version (rule 2), so a peer that detaches between the two
 * chunks sees the later paths as not-yet-shared and re-syncs them instead of
 * silently missing them.
 */
TEST_F(RibFixture, RibVersionAddPathsSpanningChunksBumpAcrossChunks) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  /*
   * Establish kRibChunkSize + 1 equal-cost paths for ONE prefix, from a single
   * peer via distinct received path-ids and distinct nexthops, so multipath
   * selection yields kRibChunkSize + 1 add-paths that span two chunks.
   */
  constexpr uint32_t kNumPaths = kRibChunkSize + 1;
  constexpr int kNexthopBase = 0x0B010000; // 11.1.0.0
  for (uint32_t i = 0; i < kNumPaths; i++) {
    auto attr = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attr->setNexthop(folly::IPAddress::fromLongHBO(kNexthopBase + i));
    attr->publish();
    sendAnnouncement(PrefixPathIds{{kV4Prefix1, i + 1}}, eBgpPeer1_, attr);
  }

  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  // Drain the initial-dump start marker.
  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));

  /*
   * Walk the emitted announcement chunks, collecting one representative version
   * per add-path chunk. The final chunk carries the EoR flag.
   */
  std::vector<uint64_t> chunkVersions;
  uint32_t addPathTotal = 0;
  bool haveEoR = false;
  while (!haveEoR) {
    msg = folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    const auto& ann = std::get<RibOutAnnouncement>(msg);
    haveEoR = ann.sendWithEoR;
    if (ann.addPathEntries.empty()) {
      continue;
    }
    const uint64_t chunkVersion = ann.addPathEntries.front().ribVersion;
    // rule 1: every add-path entry in this chunk shares one version.
    for (const auto& entry : ann.addPathEntries) {
      EXPECT_EQ(kV4Prefix1, entry.prefix);
      EXPECT_EQ(chunkVersion, entry.ribVersion);
      addPathTotal++;
    }
    // rule 2: each new chunk's version strictly exceeds the previous chunk's.
    if (!chunkVersions.empty()) {
      EXPECT_GT(chunkVersion, chunkVersions.back());
    }
    chunkVersions.push_back(chunkVersion);
  }

  // All paths advertised, across exactly two chunks (one flush boundary).
  EXPECT_EQ(kNumPaths, addPathTotal);
  ASSERT_EQ(2, chunkVersions.size());
  EXPECT_LT(chunkVersions[0], chunkVersions[1]);
}

/*
 * Rule 2 (withdrawal path): a single prefix with more than kRibChunkSize
 * advertised add-paths is fully withdrawn, so handleFullAddPathWithdrawal emits
 * the add-path withdrawals across more than one chunk. Entries within one chunk
 * share a version (rule 1) and each chunk after a flush carries a strictly
 * higher version (rule 2). Drives prepareFibProgramming() directly so the setup
 * and the chunk boundary are deterministic (and asserts the setup actually
 * produced > kRibChunkSize advertised paths).
 */
TEST_F(RibFixture, RibVersionAddPathWithdrawalsSpanningChunksBumpAcrossChunks) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  constexpr uint32_t kNumPaths = kRibChunkSize + 1;
  constexpr int kNexthopBase = 0x0B010000; // 11.1.0.0

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->ribEntries_.emplace(kV4Prefix1, RibEntry(kV4Prefix1));
    auto& entry = rib_->ribEntries_.at(kV4Prefix1);
    for (uint32_t i = 0; i < kNumPaths; i++) {
      auto attr = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      attr->setNexthop(folly::IPAddress::fromLongHBO(kNexthopBase + i));
      attr->publish();
      entry.updatePath(eBgpPeer1_, attr, true, i + 1);
    }
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    /*
     * The setup must actually produce > kRibChunkSize advertised paths for the
     * withdrawal to span chunks; otherwise the test would not exercise rule 2.
     */
    ASSERT_EQ(kNumPaths, entry.getMultipaths().size());

    /*
     * Commit as advertised, then fully withdraw every path so
     * prepareFibProgramming emits an add-path withdrawal per advertised
     * nexthop.
     */
    entry.commitMultipaths();
    entry.commitMultipathNexthops();
    entry.commitBestpath();
    for (uint32_t i = 0; i < kNumPaths; i++) {
      entry.updatePath(eBgpPeer1_, nullptr, true, i + 1);
    }
    entry.requirePathSelection();
    rib_->prepareFibProgramming();
  });

  std::vector<uint64_t> chunkVersions;
  uint32_t addPathTotal = 0;
  while (addPathTotal < kNumPaths) {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
    const auto& wd = std::get<RibOutWithdrawal>(msg);
    if (wd.addPathEntries.empty()) {
      continue;
    }
    const uint64_t chunkVersion = wd.addPathEntries.front().ribVersion;
    // rule 1: every add-path withdrawal in this chunk shares one version.
    for (const auto& entry : wd.addPathEntries) {
      EXPECT_EQ(kV4Prefix1, entry.prefix);
      EXPECT_EQ(chunkVersion, entry.ribVersion);
      addPathTotal++;
    }
    // rule 2: each new chunk's version strictly exceeds the previous chunk's.
    if (!chunkVersions.empty()) {
      EXPECT_GT(chunkVersion, chunkVersions.back());
    }
    chunkVersions.push_back(chunkVersion);
  }

  EXPECT_EQ(kNumPaths, addPathTotal);
  ASSERT_EQ(2, chunkVersions.size());
  EXPECT_LT(chunkVersions[0], chunkVersions[1]);
}

/*
 * A prefix still programmed in Fib (non-null nexthops) is not a deletion and
 * is left alone.
 */
TEST_F(RibFixture, HandleDeletedUnicastRouteIgnoresProgrammedRoute) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->ribEntries_.emplace(kV4Prefix1, RibEntry(kV4Prefix1));
    auto& entry = rib_->ribEntries_.at(kV4Prefix1);
    auto nexthops = std::make_shared<const WeightedNexthopMap>(
        WeightedNexthopMap{{kV4Nexthop1, 1}});

    EXPECT_FALSE(rib_->handleDeletedUnicastRoute(entry, nexthops));
    EXPECT_TRUE(rib_->ribEntries_.contains(kV4Prefix1));
  });
}

/*
 * A prefix deleted from Fib with no paths left, and not queued for Fib
 * programming, has its RibEntry erased.
 */
TEST_F(RibFixture, HandleDeletedUnicastRouteErasesEntryWithNoPaths) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->ribEntries_.emplace(kV4Prefix1, RibEntry(kV4Prefix1));
    rib_->ribCounters_.onPrefixAdded(true, kV4Prefix1.second);
    auto& entry = rib_->ribEntries_.at(kV4Prefix1);
    const auto prefixesBefore = rib_->ribCounters_.totalPrefixes(true);

    EXPECT_TRUE(rib_->handleDeletedUnicastRoute(entry, nullptr));
    EXPECT_FALSE(rib_->ribEntries_.contains(kV4Prefix1));
    EXPECT_EQ(prefixesBefore - 1, rib_->ribCounters_.totalPrefixes(true));
  });
}

/*
 * A prefix deleted from Fib with no paths left is kept while it is still on
 * the Fib batch list.
 */
TEST_F(RibFixture, HandleDeletedUnicastRouteKeepsEntryOnFibBatchList) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->ribEntries_.emplace(kV4Prefix1, RibEntry(kV4Prefix1));
    auto& entry = rib_->ribEntries_.at(kV4Prefix1);
    rib_->fibBatchList_.push_back(entry);

    EXPECT_TRUE(rib_->handleDeletedUnicastRoute(entry, nullptr));
    EXPECT_TRUE(rib_->ribEntries_.contains(kV4Prefix1));

    rib_->fibBatchList_.clear();
  });
}

/*
 * Programmed nexthops that are the entry's current nexthops are not stale.
 */
TEST_F(RibFixture, HandleStaleUnicastRouteSameNexthops) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs->publish();
    entry.updatePath(eBgpPeer1_, attrs);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_NE(entry.getMultipathWeightedNexthops(), nullptr);

    EXPECT_FALSE(rib_->handleStaleUnicastRoute(
        entry, entry.getMultipathWeightedNexthops()));
  });
}

/*
 * Programmed nexthops that differ from the entry's current nexthops are stale.
 */
TEST_F(RibFixture, HandleStaleUnicastRouteDifferentNexthops) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs->publish();
    entry.updatePath(eBgpPeer1_, attrs);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    auto programmed = std::make_shared<const WeightedNexthopMap>(
        WeightedNexthopMap{{kV4Nexthop2, 1}});

    EXPECT_TRUE(rib_->handleStaleUnicastRoute(entry, programmed));
  });
}

/*
 * Staleness compares the nexthop map pointers, so a copy with the same
 * contents is still stale.
 */
TEST_F(RibFixture, HandleStaleUnicastRouteComparesPointers) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs->publish();
    entry.updatePath(eBgpPeer1_, attrs);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_NE(entry.getMultipathWeightedNexthops(), nullptr);
    auto programmed = std::make_shared<const WeightedNexthopMap>(
        *entry.getMultipathWeightedNexthops());

    EXPECT_TRUE(rib_->handleStaleUnicastRoute(entry, programmed));
  });
}

/*
 * A prefix whose best path and multipaths were already advertised emits
 * nothing, but still consumes a RIB version and records it on the entry.
 */
TEST_F(RibFixture, HandleAddUpdatedUnicastRouteUnchangedBumpsVersion) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs->publish();
    entry.updatePath(eBgpPeer1_, attrs);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    // Commit as advertised.
    entry.commitMultipaths();
    entry.commitMultipathNexthops();
    entry.commitBestpath();
    const auto versionBefore = rib_->ribVersion_;
    RibOutAnnouncement announcement;
    RibOutAnnouncement announcementAddPath;
    RibOutWithdrawal withdrawalAddPath;

    rib_->handleAddUpdatedUnicastRoute(
        entry,
        false /* sendWithEoR */,
        announcement,
        announcementAddPath,
        withdrawalAddPath);

    EXPECT_TRUE(announcement.entries.empty());
    EXPECT_TRUE(announcementAddPath.addPathEntries.empty());
    EXPECT_TRUE(withdrawalAddPath.addPathEntries.empty());
    EXPECT_EQ(versionBefore + 1, rib_->ribVersion_);
    EXPECT_EQ(rib_->ribVersion_, entry.getRibVersion());
  });
}

/*
 * The first advertisement of a prefix is marked newly installed, with the
 * entry's install time stamp, for out-delay.
 */
TEST_F(RibFixture, HandleAddUpdatedUnicastRouteNewPrefixNewlyInstalled) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs->publish();
    entry.updatePath(eBgpPeer1_, attrs);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    RibOutAnnouncement announcement;
    RibOutAnnouncement announcementAddPath;
    RibOutWithdrawal withdrawalAddPath;

    rib_->handleAddUpdatedUnicastRoute(
        entry,
        false /* sendWithEoR */,
        announcement,
        announcementAddPath,
        withdrawalAddPath);

    ASSERT_EQ(1, announcement.entries.size());
    const auto& bestEntry = announcement.entries[0];
    EXPECT_EQ(kV4Prefix1, bestEntry.prefix);
    EXPECT_TRUE(bestEntry.newlyInstalledInLocalRib);
    EXPECT_EQ(entry.installTimeStamp_, bestEntry.installTimeStamp);
  });
}

/*
 * A best path change is announced with kDefaultPathID and the new best path's
 * peer and attributes.
 */
TEST_F(RibFixture, HandleAddUpdatedUnicastRouteAnnouncesBestpathChange) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs1->setNexthop(kV4Nexthop1);
    attrs1->publish();
    auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs2->setNexthop(kV4Nexthop2);
    attrs2->publish();
    entry.updatePath(eBgpPeer1_, attrs1);
    entry.updatePath(eBgpPeer2_, attrs2);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    // Commit as advertised, then withdraw the best path.
    entry.commitMultipaths();
    entry.commitMultipathNexthops();
    entry.commitBestpath();
    const bool peer1WasBest = entry.getBestPath()->attrs == attrs1;
    entry.updatePath(peer1WasBest ? eBgpPeer1_ : eBgpPeer2_, nullptr);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    RibOutAnnouncement announcement;
    RibOutAnnouncement announcementAddPath;
    RibOutWithdrawal withdrawalAddPath;

    rib_->handleAddUpdatedUnicastRoute(
        entry,
        false /* sendWithEoR */,
        announcement,
        announcementAddPath,
        withdrawalAddPath);

    ASSERT_EQ(1, announcement.entries.size());
    const auto& bestEntry = announcement.entries[0];
    EXPECT_EQ(kV4Prefix1, bestEntry.prefix);
    EXPECT_EQ(kDefaultPathID, bestEntry.pathIdToSend);
    EXPECT_EQ(
        (peer1WasBest ? eBgpPeer2_ : eBgpPeer1_).addr, bestEntry.peer.addr);
    EXPECT_EQ(peer1WasBest ? attrs2 : attrs1, bestEntry.attrs);
    EXPECT_FALSE(bestEntry.newlyInstalledInLocalRib);
    EXPECT_EQ(rib_->ribVersion_, bestEntry.ribVersion);
  });
}

/*
 * Changed multipaths are announced as one add-path entry per multipath, each
 * with its own path ID. A first advertisement has nothing to withdraw.
 */
TEST_F(RibFixture, HandleAddUpdatedUnicastRouteAnnouncesMultipaths) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs1->setNexthop(kV4Nexthop1);
    attrs1->publish();
    auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs2->setNexthop(kV4Nexthop2);
    attrs2->publish();
    entry.updatePath(eBgpPeer1_, attrs1);
    entry.updatePath(eBgpPeer2_, attrs2);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_EQ(2, entry.getMultipaths().size());
    std::set<std::pair<uint32_t, folly::IPAddress>> expected;
    for (const auto& [pathId, path] : entry.getMultipaths()) {
      expected.emplace(pathId, path->attrs->getNexthop());
    }
    RibOutAnnouncement announcement;
    RibOutAnnouncement announcementAddPath;
    RibOutWithdrawal withdrawalAddPath;

    rib_->handleAddUpdatedUnicastRoute(
        entry,
        false /* sendWithEoR */,
        announcement,
        announcementAddPath,
        withdrawalAddPath);

    std::set<std::pair<uint32_t, folly::IPAddress>> announced;
    for (const auto& addPathEntry : announcementAddPath.addPathEntries) {
      EXPECT_EQ(kV4Prefix1, addPathEntry.prefix);
      announced.emplace(
          addPathEntry.pathIdToSend, addPathEntry.attrs->getNexthop());
    }
    EXPECT_EQ(expected, announced);
    EXPECT_TRUE(withdrawalAddPath.addPathEntries.empty());
  });
}

/*
 * A multipath nexthop that is no longer selected is withdrawn by nexthop,
 * with kPlaceholderPathID.
 */
TEST_F(RibFixture, HandleAddUpdatedUnicastRouteWithdrawsDroppedNexthop) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs1->setNexthop(kV4Nexthop1);
    attrs1->publish();
    auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs2->setNexthop(kV4Nexthop2);
    attrs2->publish();
    entry.updatePath(eBgpPeer1_, attrs1);
    entry.updatePath(eBgpPeer2_, attrs2);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    // Commit as advertised, then withdraw the kV4Nexthop2 path.
    entry.commitMultipaths();
    entry.commitMultipathNexthops();
    entry.commitBestpath();
    entry.updatePath(eBgpPeer2_, nullptr);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    RibOutAnnouncement announcement;
    RibOutAnnouncement announcementAddPath;
    RibOutWithdrawal withdrawalAddPath;

    rib_->handleAddUpdatedUnicastRoute(
        entry,
        false /* sendWithEoR */,
        announcement,
        announcementAddPath,
        withdrawalAddPath);

    ASSERT_EQ(1, withdrawalAddPath.addPathEntries.size());
    const auto& withdrawn = withdrawalAddPath.addPathEntries[0];
    EXPECT_EQ(kV4Prefix1, withdrawn.prefix);
    EXPECT_EQ(kPlaceholderPathID, withdrawn.pathIdToSend);
    EXPECT_EQ(kV4Nexthop2, withdrawn.nh);
    EXPECT_EQ(rib_->ribVersion_, withdrawn.ribVersion);
  });
}

/*
 * Multipaths without a best path (e.g. a CPS policy failing the native
 * criteria) are committed but not announced.
 */
TEST_F(RibFixture, HandleAddUpdatedUnicastRouteNoAddPathWithoutBestpath) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs1->setNexthop(kV4Nexthop1);
    attrs1->publish();
    auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs2->setNexthop(kV4Nexthop2);
    attrs2->publish();
    entry.updatePath(eBgpPeer1_, attrs1);
    entry.updatePath(eBgpPeer2_, attrs2);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_EQ(2, entry.getMultipaths().size());
    entry.bestpath_ = nullptr;
    RibOutAnnouncement announcement;
    RibOutAnnouncement announcementAddPath;
    RibOutWithdrawal withdrawalAddPath;

    rib_->handleAddUpdatedUnicastRoute(
        entry,
        false /* sendWithEoR */,
        announcement,
        announcementAddPath,
        withdrawalAddPath);

    EXPECT_TRUE(announcement.entries.empty());
    EXPECT_TRUE(announcementAddPath.addPathEntries.empty());
    EXPECT_EQ(2, entry.getAdvertisedMultipaths().size());
  });
}

/*
 * Add-path announcements past kRibChunkSize are flushed to ribOutQ_. The rest
 * stay in the caller's message with a bumped RIB version (rule 2).
 */
TEST_F(RibFixture, HandleAddUpdatedUnicastRouteAddPathSpansChunks) {
  constexpr uint32_t kNumPaths = 170;
  constexpr int kNexthopBase = 0x0B010000; // 11.1.0.0
  uint64_t versionBefore = 0;
  std::set<std::pair<uint32_t, folly::IPAddress>> expected;
  RibOutAnnouncement announcement;
  RibOutAnnouncement announcementAddPath;
  RibOutWithdrawal withdrawalAddPath;

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    for (uint32_t i = 0; i < kNumPaths; i++) {
      auto attrs = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      attrs->setNexthop(folly::IPAddress::fromLongHBO(kNexthopBase + i));
      attrs->publish();
      entry.updatePath(eBgpPeer1_, attrs, true, i + 1);
    }
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_EQ(kNumPaths, entry.getMultipaths().size());
    for (const auto& [pathId, path] : entry.getMultipaths()) {
      expected.emplace(pathId, path->attrs->getNexthop());
    }
    versionBefore = rib_->ribVersion_;

    rib_->handleAddUpdatedUnicastRoute(
        entry,
        false /* sendWithEoR */,
        announcement,
        announcementAddPath,
        withdrawalAddPath);

    // One bump for the prefix, one for the chunk flush.
    EXPECT_EQ(versionBefore + 2, rib_->ribVersion_);
    EXPECT_EQ(versionBefore + 2, entry.getRibVersion());
  });

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  const auto& flushed = std::get<RibOutAnnouncement>(msg).addPathEntries;
  ASSERT_EQ(kRibChunkSize, flushed.size());
  ASSERT_EQ(
      kNumPaths - kRibChunkSize, announcementAddPath.addPathEntries.size());
  std::set<std::pair<uint32_t, folly::IPAddress>> announced;
  for (const auto& addPathEntry : flushed) {
    EXPECT_EQ(versionBefore + 1, addPathEntry.ribVersion);
    announced.emplace(
        addPathEntry.pathIdToSend, addPathEntry.attrs->getNexthop());
  }
  for (const auto& addPathEntry : announcementAddPath.addPathEntries) {
    EXPECT_EQ(versionBefore + 2, addPathEntry.ribVersion);
    announced.emplace(
        addPathEntry.pathIdToSend, addPathEntry.attrs->getNexthop());
  }
  EXPECT_EQ(expected, announced);
  // The best path is not flushed and carries the final version.
  ASSERT_EQ(1, announcement.entries.size());
  EXPECT_EQ(versionBefore + 2, announcement.entries[0].ribVersion);
  EXPECT_EQ(0, ribOutQ_.size());
}

/*
 * Add-path withdrawals past kRibChunkSize are flushed to ribOutQ_. The rest
 * stay in the caller's message with a bumped RIB version (rule 2).
 */
TEST_F(RibFixture, HandleAddUpdatedUnicastRouteWithdrawalSpansChunks) {
  constexpr uint32_t kNumPaths = 170;
  constexpr int kNexthopBase = 0x0B010000; // 11.1.0.0
  uint64_t versionBefore = 0;
  std::set<folly::IPAddress> expected;
  RibOutAnnouncement announcement;
  RibOutAnnouncement announcementAddPath;
  RibOutWithdrawal withdrawalAddPath;

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    for (uint32_t i = 0; i < kNumPaths; i++) {
      auto attrs = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      attrs->setNexthop(folly::IPAddress::fromLongHBO(kNexthopBase + i));
      attrs->publish();
      entry.updatePath(eBgpPeer1_, attrs, true, i + 1);
    }
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_EQ(kNumPaths, entry.getMultipaths().size());
    // Commit as advertised, then withdraw every path but the first.
    entry.commitMultipaths();
    entry.commitMultipathNexthops();
    entry.commitBestpath();
    for (uint32_t i = 1; i < kNumPaths; i++) {
      entry.updatePath(eBgpPeer1_, nullptr, true, i + 1);
      expected.emplace(folly::IPAddress::fromLongHBO(kNexthopBase + i));
    }
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_EQ(1, entry.getMultipaths().size());
    versionBefore = rib_->ribVersion_;

    rib_->handleAddUpdatedUnicastRoute(
        entry,
        false /* sendWithEoR */,
        announcement,
        announcementAddPath,
        withdrawalAddPath);

    // One bump for the prefix, one for the chunk flush.
    EXPECT_EQ(versionBefore + 2, rib_->ribVersion_);
    EXPECT_EQ(versionBefore + 2, entry.getRibVersion());
  });

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
  const auto& flushed = std::get<RibOutWithdrawal>(msg).addPathEntries;
  ASSERT_EQ(kRibChunkSize, flushed.size());
  ASSERT_EQ(
      kNumPaths - 1 - kRibChunkSize, withdrawalAddPath.addPathEntries.size());
  std::set<folly::IPAddress> withdrawn;
  for (const auto& wd : flushed) {
    EXPECT_EQ(kPlaceholderPathID, wd.pathIdToSend);
    EXPECT_EQ(versionBefore + 1, wd.ribVersion);
    ASSERT_TRUE(wd.nh.has_value());
    withdrawn.emplace(*wd.nh);
  }
  for (const auto& wd : withdrawalAddPath.addPathEntries) {
    EXPECT_EQ(kPlaceholderPathID, wd.pathIdToSend);
    EXPECT_EQ(versionBefore + 2, wd.ribVersion);
    ASSERT_TRUE(wd.nh.has_value());
    withdrawn.emplace(*wd.nh);
  }
  EXPECT_EQ(expected, withdrawn);
  // The remaining path is announced without a flush.
  EXPECT_EQ(1, announcementAddPath.addPathEntries.size());
  EXPECT_EQ(0, ribOutQ_.size());
}

/*
 * Prefixes skipped as deleted or stale do not consume a RIB version. Only the
 * announced prefix does.
 */
TEST_F(RibFixture, FibProgrammedSkippedRoutesDoNotConsumeRibVersion) {
  uint64_t versionBefore = 0;
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    // kV4Prefix1 is deleted from Fib and has no paths left.
    rib_->ribEntries_.emplace(kV4Prefix1, RibEntry(kV4Prefix1));
    rib_->ribCounters_.onPrefixAdded(true, kV4Prefix1.second);
    // kV4Prefix2 is stale and kV4Prefix3 is up to date.
    for (const auto& prefix : {kV4Prefix2, kV4Prefix3}) {
      rib_->ribEntries_.emplace(prefix, RibEntry(prefix));
      auto& entry = rib_->ribEntries_.at(prefix);
      auto attrs = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      attrs->publish();
      entry.updatePath(eBgpPeer1_, attrs);
      RibBase::selectBestPath(
          entry, multipathSelector, bestpathSelector, false, 0);
    }
    const auto& staleEntry = rib_->ribEntries_.at(kV4Prefix2);
    const auto& programmedEntry = rib_->ribEntries_.at(kV4Prefix3);
    Fib::FibProgrammedPfxs fibProgrammedPfxs;
    fibProgrammedPfxs[nullptr] = {
        {kV4Prefix1, nullptr},
        {kV4Prefix2,
         std::make_shared<const WeightedNexthopMap>(
             *staleEntry.getMultipathWeightedNexthops())},
        {kV4Prefix3, programmedEntry.getMultipathWeightedNexthops()}};
    versionBefore = rib_->ribVersion_;

    rib_->handleFibProgrammedMessage(
        Fib::FibProgrammedMessage(std::move(fibProgrammedPfxs), false));

    EXPECT_EQ(versionBefore + 1, rib_->ribVersion_);
    EXPECT_FALSE(rib_->ribEntries_.contains(kV4Prefix1));
    EXPECT_EQ(0, rib_->ribEntries_.at(kV4Prefix2).getRibVersion());
    EXPECT_EQ(
        versionBefore + 1, rib_->ribEntries_.at(kV4Prefix3).getRibVersion());
  });

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  const auto& entries = std::get<RibOutAnnouncement>(msg).entries;
  ASSERT_EQ(1, entries.size());
  EXPECT_EQ(kV4Prefix3, entries[0].prefix);
  EXPECT_EQ(versionBefore + 1, entries[0].ribVersion);
}

/*
 * With RIB allocated path IDs, a full add-path withdrawal sends one withdrawal
 * per advertised path, carrying the path ID and nexthop it was announced with.
 */
TEST_F(RibAllocatedPathIdFixture, FullAddPathWithdrawalUsesAdvertisedPathIds) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  const std::vector<folly::IPAddress> nexthops{
      kV4Nexthop1, kV4Nexthop2, kV4Nexthop3};
  std::set<std::pair<uint32_t, folly::IPAddress>> expected;

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->ribEntries_.emplace(kV4Prefix1, RibEntry(kV4Prefix1));
    auto& entry = rib_->ribEntries_.at(kV4Prefix1);
    for (uint32_t i = 0; i < nexthops.size(); i++) {
      auto attr = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      attr->setNexthop(nexthops[i]);
      attr->publish();
      entry.updatePath(eBgpPeer1_, attr, true, i + 1);
    }
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_EQ(nexthops.size(), entry.getMultipaths().size());

    // Commit as advertised, then fully withdraw every path.
    entry.commitMultipaths();
    entry.commitMultipathNexthops();
    entry.commitBestpath();
    for (const auto& [pathId, path] : entry.getAdvertisedMultipaths()) {
      expected.emplace(pathId, path->attrs->getNexthop());
    }
    for (uint32_t i = 0; i < nexthops.size(); i++) {
      entry.updatePath(eBgpPeer1_, nullptr, true, i + 1);
    }
    entry.requirePathSelection();
    rib_->prepareFibProgramming();
  });

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
  std::set<std::pair<uint32_t, folly::IPAddress>> withdrawn;
  for (const auto& wd : std::get<RibOutWithdrawal>(msg).addPathEntries) {
    EXPECT_EQ(kV4Prefix1, wd.prefix);
    ASSERT_TRUE(wd.nh.has_value());
    withdrawn.emplace(wd.pathIdToSend, *wd.nh);
  }
  EXPECT_EQ(expected, withdrawn);
}

/*
 * With RIB allocated path IDs, advertised paths that share a nexthop are each
 * withdrawn by their own path ID, instead of once per nexthop.
 */
TEST_F(
    RibAllocatedPathIdFixture,
    FullAddPathWithdrawalSharedNexthopWithdrawsEachPathId) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  constexpr uint32_t kNumPaths = 2;
  std::set<std::pair<uint32_t, folly::IPAddress>> expected;

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->ribEntries_.emplace(kV4Prefix1, RibEntry(kV4Prefix1));
    auto& entry = rib_->ribEntries_.at(kV4Prefix1);
    // Same peer and nexthop, different received path IDs
    for (uint32_t i = 0; i < kNumPaths; i++) {
      auto attr = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      attr->setNexthop(kV4Nexthop1);
      attr->publish();
      entry.updatePath(eBgpPeer1_, attr, true, i + 1);
    }
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_EQ(kNumPaths, entry.getMultipaths().size());

    // Commit as advertised, then fully withdraw every path.
    entry.commitMultipaths();
    entry.commitMultipathNexthops();
    entry.commitBestpath();
    for (const auto& [pathId, path] : entry.getAdvertisedMultipaths()) {
      expected.emplace(pathId, path->attrs->getNexthop());
    }
    for (uint32_t i = 0; i < kNumPaths; i++) {
      entry.updatePath(eBgpPeer1_, nullptr, true, i + 1);
    }
    entry.requirePathSelection();
    rib_->prepareFibProgramming();
  });

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
  std::set<std::pair<uint32_t, folly::IPAddress>> withdrawn;
  for (const auto& wd : std::get<RibOutWithdrawal>(msg).addPathEntries) {
    ASSERT_TRUE(wd.nh.has_value());
    withdrawn.emplace(wd.pathIdToSend, *wd.nh);
  }
  ASSERT_EQ(kNumPaths, expected.size());
  EXPECT_EQ(expected, withdrawn);
}

/*
 * With RIB allocated path IDs, the first advertisement announces every
 * multipath by its path ID and withdraws nothing.
 */
TEST_F(
    RibAllocatedPathIdFixture,
    MultipathDeltaFirstAdvertisementAnnouncesAll) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    ASSERT_TRUE(rib_->enableRibAllocatedPathId_);
    RibEntry entry(kV4Prefix1);
    const std::vector<folly::IPAddress> nexthops{kV4Nexthop1, kV4Nexthop2};
    for (uint32_t i = 0; i < nexthops.size(); i++) {
      auto attrs = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      attrs->setNexthop(nexthops[i]);
      attrs->publish();
      entry.updatePath(eBgpPeer1_, attrs, true, i + 1);
    }
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_EQ(nexthops.size(), entry.getMultipaths().size());
    std::set<std::pair<uint32_t, folly::IPAddress>> expected;
    for (const auto& [pathId, path] : entry.getMultipaths()) {
      expected.emplace(pathId, path->attrs->getNexthop());
    }
    RibOutAnnouncement announcement;
    RibOutAnnouncement announcementAddPath;
    RibOutWithdrawal withdrawalAddPath;

    rib_->handleAddUpdatedUnicastRoute(
        entry,
        false /* sendWithEoR */,
        announcement,
        announcementAddPath,
        withdrawalAddPath);

    std::set<std::pair<uint32_t, folly::IPAddress>> announced;
    for (const auto& addPathEntry : announcementAddPath.addPathEntries) {
      announced.emplace(
          addPathEntry.pathIdToSend, addPathEntry.attrs->getNexthop());
    }
    EXPECT_EQ(expected, announced);
    EXPECT_TRUE(withdrawalAddPath.addPathEntries.empty());
  });
}

/*
 * With RIB allocated path IDs, adding a multipath announces only the new path.
 * Already advertised, unchanged paths are not re-announced.
 */
TEST_F(RibAllocatedPathIdFixture, MultipathDeltaSkipsUnchangedPaths) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    // A peer without a local UCMP weight keeps the aggregate weights unchanged.
    const TinyPeerInfo peer(
        kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
    const std::vector<folly::IPAddress> nexthops{
        kV4Nexthop1, kV4Nexthop2, kV4Nexthop3};
    std::vector<std::shared_ptr<facebook::bgp::BgpPath>> attrs;
    for (const auto& nexthop : nexthops) {
      auto pathAttrs = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      pathAttrs->setNexthop(nexthop);
      pathAttrs->publish();
      attrs.push_back(std::move(pathAttrs));
    }
    entry.updatePath(peer, attrs[0], true, 1);
    entry.updatePath(peer, attrs[1], true, 2);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    // Commit as advertised, then add a third multipath.
    entry.commitMultipaths();
    entry.commitMultipathNexthops();
    entry.commitBestpath();
    entry.updatePath(peer, attrs[2], true, 3);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_EQ(3, entry.getMultipaths().size());
    std::optional<uint32_t> newPathId;
    for (const auto& [pathId, path] : entry.getMultipaths()) {
      if (path->attrs == attrs[2]) {
        newPathId = pathId;
      }
    }
    ASSERT_TRUE(newPathId.has_value());
    RibOutAnnouncement announcement;
    RibOutAnnouncement announcementAddPath;
    RibOutWithdrawal withdrawalAddPath;

    rib_->handleAddUpdatedUnicastRoute(
        entry,
        false /* sendWithEoR */,
        announcement,
        announcementAddPath,
        withdrawalAddPath);

    ASSERT_EQ(1, announcementAddPath.addPathEntries.size());
    EXPECT_EQ(*newPathId, announcementAddPath.addPathEntries[0].pathIdToSend);
    EXPECT_EQ(attrs[2], announcementAddPath.addPathEntries[0].attrs);
    EXPECT_TRUE(withdrawalAddPath.addPathEntries.empty());
  });
}

/*
 * With RIB allocated path IDs, a path updated with new attributes keeps its
 * path ID and is re-announced. The other paths are not.
 */
TEST_F(RibAllocatedPathIdFixture, MultipathDeltaReannouncesUpdatedPath) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    const std::vector<folly::IPAddress> nexthops{kV4Nexthop1, kV4Nexthop2};
    for (uint32_t i = 0; i < nexthops.size(); i++) {
      auto attrs = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      attrs->setNexthop(nexthops[i]);
      attrs->publish();
      entry.updatePath(eBgpPeer1_, attrs, true, i + 1);
    }
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    // Commit as advertised, then send new attributes for received path ID 1.
    entry.commitMultipaths();
    entry.commitMultipathNexthops();
    entry.commitBestpath();
    std::optional<uint32_t> updatedPathId;
    for (const auto& [pathId, path] : entry.getAdvertisedMultipaths()) {
      if (path->attrs->getNexthop() == kV4Nexthop1) {
        updatedPathId = pathId;
      }
    }
    ASSERT_TRUE(updatedPathId.has_value());
    auto newAttrs = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    newAttrs->setNexthop(kV4Nexthop1);
    newAttrs->publish();
    entry.updatePath(eBgpPeer1_, newAttrs, true, 1);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_EQ(2, entry.getMultipaths().size());
    RibOutAnnouncement announcement;
    RibOutAnnouncement announcementAddPath;
    RibOutWithdrawal withdrawalAddPath;

    rib_->handleAddUpdatedUnicastRoute(
        entry,
        false /* sendWithEoR */,
        announcement,
        announcementAddPath,
        withdrawalAddPath);

    ASSERT_EQ(1, announcementAddPath.addPathEntries.size());
    EXPECT_EQ(
        *updatedPathId, announcementAddPath.addPathEntries[0].pathIdToSend);
    EXPECT_EQ(newAttrs, announcementAddPath.addPathEntries[0].attrs);
    EXPECT_TRUE(withdrawalAddPath.addPathEntries.empty());
  });
}

/*
 * With RIB allocated path IDs, a dropped multipath is withdrawn by its path ID
 * and nexthop. The remaining path is not re-announced.
 */
TEST_F(RibAllocatedPathIdFixture, MultipathDeltaWithdrawsDroppedPathId) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    // A peer without a local UCMP weight keeps the aggregate weights unchanged.
    const TinyPeerInfo peer(
        kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
    const std::vector<folly::IPAddress> nexthops{kV4Nexthop1, kV4Nexthop2};
    for (uint32_t i = 0; i < nexthops.size(); i++) {
      auto attrs = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      attrs->setNexthop(nexthops[i]);
      attrs->publish();
      entry.updatePath(peer, attrs, true, i + 1);
    }
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    // Commit as advertised, then withdraw the kV4Nexthop2 path.
    entry.commitMultipaths();
    entry.commitMultipathNexthops();
    entry.commitBestpath();
    std::optional<uint32_t> droppedPathId;
    for (const auto& [pathId, path] : entry.getAdvertisedMultipaths()) {
      if (path->attrs->getNexthop() == kV4Nexthop2) {
        droppedPathId = pathId;
      }
    }
    ASSERT_TRUE(droppedPathId.has_value());
    entry.updatePath(peer, nullptr, true, 2);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_EQ(1, entry.getMultipaths().size());
    RibOutAnnouncement announcement;
    RibOutAnnouncement announcementAddPath;
    RibOutWithdrawal withdrawalAddPath;

    rib_->handleAddUpdatedUnicastRoute(
        entry,
        false /* sendWithEoR */,
        announcement,
        announcementAddPath,
        withdrawalAddPath);

    ASSERT_EQ(1, withdrawalAddPath.addPathEntries.size());
    const auto& withdrawn = withdrawalAddPath.addPathEntries[0];
    EXPECT_EQ(kV4Prefix1, withdrawn.prefix);
    EXPECT_EQ(*droppedPathId, withdrawn.pathIdToSend);
    EXPECT_EQ(kV4Nexthop2, withdrawn.nh);
    EXPECT_EQ(rib_->ribVersion_, withdrawn.ribVersion);
    EXPECT_TRUE(announcementAddPath.addPathEntries.empty());
  });
}

/*
 * With RIB allocated path IDs, dropping one of two paths that share a nexthop
 * withdraws that path ID, although the nexthop is still advertised.
 */
TEST_F(RibAllocatedPathIdFixture, MultipathDeltaSharedNexthopWithdrawsPathId) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    // Same peer and nexthop, different received path IDs
    for (uint32_t i = 0; i < 2; i++) {
      auto attrs = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      attrs->setNexthop(kV4Nexthop1);
      attrs->publish();
      entry.updatePath(eBgpPeer1_, attrs, true, i + 1);
    }
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_EQ(2, entry.getMultipaths().size());
    // Commit as advertised, then withdraw received path ID 2.
    entry.commitMultipaths();
    entry.commitMultipathNexthops();
    entry.commitBestpath();
    const auto oldAdvMultipaths = entry.getAdvertisedMultipaths();
    entry.updatePath(eBgpPeer1_, nullptr, true, 2);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_EQ(1, entry.getMultipaths().size());
    std::optional<uint32_t> droppedPathId;
    for (const auto& [pathId, _] : oldAdvMultipaths) {
      if (!entry.getMultipaths().contains(pathId)) {
        droppedPathId = pathId;
      }
    }
    ASSERT_TRUE(droppedPathId.has_value());
    RibOutAnnouncement announcement;
    RibOutAnnouncement announcementAddPath;
    RibOutWithdrawal withdrawalAddPath;

    rib_->handleAddUpdatedUnicastRoute(
        entry,
        false /* sendWithEoR */,
        announcement,
        announcementAddPath,
        withdrawalAddPath);

    ASSERT_EQ(1, withdrawalAddPath.addPathEntries.size());
    EXPECT_EQ(*droppedPathId, withdrawalAddPath.addPathEntries[0].pathIdToSend);
    EXPECT_EQ(kV4Nexthop1, withdrawalAddPath.addPathEntries[0].nh);
  });
}

/*
 * With RIB allocated path IDs, unchanged multipaths emit nothing.
 */
TEST_F(RibAllocatedPathIdFixture, MultipathDeltaNoChangeSendsNothing) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    const std::vector<folly::IPAddress> nexthops{kV4Nexthop1, kV4Nexthop2};
    for (uint32_t i = 0; i < nexthops.size(); i++) {
      auto attrs = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      attrs->setNexthop(nexthops[i]);
      attrs->publish();
      entry.updatePath(eBgpPeer1_, attrs, true, i + 1);
    }
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    // Commit as advertised.
    entry.commitMultipaths();
    entry.commitMultipathNexthops();
    entry.commitBestpath();
    uint64_t currentRibVersion = rib_->ribVersion_;
    RibOutAnnouncement announcementAddPath;
    RibOutWithdrawal withdrawalAddPath;

    rib_->handleAddUpdatedMultipathDelta(
        entry,
        false /* sendWithEoR */,
        false /* newlyInstalledInLocalRib */,
        currentRibVersion,
        announcementAddPath,
        withdrawalAddPath);

    EXPECT_TRUE(announcementAddPath.addPathEntries.empty());
    EXPECT_TRUE(withdrawalAddPath.addPathEntries.empty());
  });
}

/*
 * With RIB allocated path IDs, multipaths without a best path announce
 * nothing and are not committed as advertised.
 */
TEST_F(RibAllocatedPathIdFixture, MultipathDeltaNoBestpathSkipsCommit) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    const std::vector<folly::IPAddress> nexthops{
        kV4Nexthop1, kV4Nexthop2, kV4Nexthop3};
    for (uint32_t i = 0; i < 2; i++) {
      auto attrs = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      attrs->setNexthop(nexthops[i]);
      attrs->publish();
      entry.updatePath(eBgpPeer1_, attrs, true, i + 1);
    }
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    // Commit as advertised, then add a third multipath without a best path.
    entry.commitMultipaths();
    entry.commitMultipathNexthops();
    entry.commitBestpath();
    auto attrs3 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs3->setNexthop(nexthops[2]);
    attrs3->publish();
    entry.updatePath(eBgpPeer1_, attrs3, true, 3);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_EQ(3, entry.getMultipaths().size());
    const auto advertisedBefore = entry.getAdvertisedMultipaths();
    entry.bestpath_ = nullptr;
    uint64_t currentRibVersion = rib_->ribVersion_;
    RibOutAnnouncement announcementAddPath;
    RibOutWithdrawal withdrawalAddPath;

    rib_->handleAddUpdatedMultipathDelta(
        entry,
        false /* sendWithEoR */,
        false /* newlyInstalledInLocalRib */,
        currentRibVersion,
        announcementAddPath,
        withdrawalAddPath);

    EXPECT_TRUE(announcementAddPath.addPathEntries.empty());
    EXPECT_TRUE(withdrawalAddPath.addPathEntries.empty());
    EXPECT_EQ(advertisedBefore, entry.getAdvertisedMultipaths());
  });
}

/*
 * With RIB allocated path IDs, delta announcements past kRibChunkSize are
 * flushed to ribOutQ_. The rest stay in the caller's message with a bumped
 * RIB version (rule 2).
 */
TEST_F(RibAllocatedPathIdFixture, MultipathDeltaAddPathSpansChunks) {
  constexpr uint32_t kNumPaths = 170;
  constexpr int kNexthopBase = 0x0B010000; // 11.1.0.0
  uint64_t versionBefore = 0;
  std::set<std::pair<uint32_t, folly::IPAddress>> expected;
  RibOutAnnouncement announcement;
  RibOutAnnouncement announcementAddPath;
  RibOutWithdrawal withdrawalAddPath;

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    for (uint32_t i = 0; i < kNumPaths; i++) {
      auto attrs = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      attrs->setNexthop(folly::IPAddress::fromLongHBO(kNexthopBase + i));
      attrs->publish();
      entry.updatePath(eBgpPeer1_, attrs, true, i + 1);
    }
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_EQ(kNumPaths, entry.getMultipaths().size());
    for (const auto& [pathId, path] : entry.getMultipaths()) {
      expected.emplace(pathId, path->attrs->getNexthop());
    }
    versionBefore = rib_->ribVersion_;

    rib_->handleAddUpdatedUnicastRoute(
        entry,
        false /* sendWithEoR */,
        announcement,
        announcementAddPath,
        withdrawalAddPath);

    // One bump for the prefix, one for the chunk flush.
    EXPECT_EQ(versionBefore + 2, rib_->ribVersion_);
    EXPECT_EQ(versionBefore + 2, entry.getRibVersion());
  });

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  const auto& flushed = std::get<RibOutAnnouncement>(msg).addPathEntries;
  ASSERT_EQ(kRibChunkSize, flushed.size());
  ASSERT_EQ(
      kNumPaths - kRibChunkSize, announcementAddPath.addPathEntries.size());
  std::set<std::pair<uint32_t, folly::IPAddress>> announced;
  for (const auto& addPathEntry : flushed) {
    EXPECT_EQ(versionBefore + 1, addPathEntry.ribVersion);
    announced.emplace(
        addPathEntry.pathIdToSend, addPathEntry.attrs->getNexthop());
  }
  for (const auto& addPathEntry : announcementAddPath.addPathEntries) {
    EXPECT_EQ(versionBefore + 2, addPathEntry.ribVersion);
    announced.emplace(
        addPathEntry.pathIdToSend, addPathEntry.attrs->getNexthop());
  }
  EXPECT_EQ(expected, announced);
  EXPECT_EQ(0, ribOutQ_.size());
}

/*
 * With RIB allocated path IDs, delta withdrawals past kRibChunkSize are
 * flushed to ribOutQ_, each carrying its path ID. The rest stay in the
 * caller's message with a bumped RIB version (rule 2).
 */
TEST_F(RibAllocatedPathIdFixture, MultipathDeltaWithdrawalSpansChunks) {
  constexpr uint32_t kNumPaths = 170;
  constexpr int kNexthopBase = 0x0B010000; // 11.1.0.0
  uint64_t versionBefore = 0;
  std::set<std::pair<uint32_t, folly::IPAddress>> expected;
  RibOutAnnouncement announcement;
  RibOutAnnouncement announcementAddPath;
  RibOutWithdrawal withdrawalAddPath;

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    // A peer without a local UCMP weight keeps the aggregate weights unchanged.
    const TinyPeerInfo peer(
        kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
    for (uint32_t i = 0; i < kNumPaths; i++) {
      auto attrs = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      attrs->setNexthop(folly::IPAddress::fromLongHBO(kNexthopBase + i));
      attrs->publish();
      entry.updatePath(peer, attrs, true, i + 1);
    }
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_EQ(kNumPaths, entry.getMultipaths().size());
    // Commit as advertised, then withdraw every path but the first.
    entry.commitMultipaths();
    entry.commitMultipathNexthops();
    entry.commitBestpath();
    for (const auto& [pathId, path] : entry.getAdvertisedMultipaths()) {
      if (path->attrs->getNexthop() !=
          folly::IPAddress::fromLongHBO(kNexthopBase)) {
        expected.emplace(pathId, path->attrs->getNexthop());
      }
    }
    for (uint32_t i = 1; i < kNumPaths; i++) {
      entry.updatePath(peer, nullptr, true, i + 1);
    }
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_EQ(1, entry.getMultipaths().size());
    versionBefore = rib_->ribVersion_;

    rib_->handleAddUpdatedUnicastRoute(
        entry,
        false /* sendWithEoR */,
        announcement,
        announcementAddPath,
        withdrawalAddPath);

    // One bump for the prefix, one for the chunk flush.
    EXPECT_EQ(versionBefore + 2, rib_->ribVersion_);
    EXPECT_EQ(versionBefore + 2, entry.getRibVersion());
  });

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
  const auto& flushed = std::get<RibOutWithdrawal>(msg).addPathEntries;
  ASSERT_EQ(kRibChunkSize, flushed.size());
  ASSERT_EQ(
      kNumPaths - 1 - kRibChunkSize, withdrawalAddPath.addPathEntries.size());
  std::set<std::pair<uint32_t, folly::IPAddress>> withdrawn;
  for (const auto& wd : flushed) {
    EXPECT_EQ(versionBefore + 1, wd.ribVersion);
    ASSERT_TRUE(wd.nh.has_value());
    withdrawn.emplace(wd.pathIdToSend, *wd.nh);
  }
  for (const auto& wd : withdrawalAddPath.addPathEntries) {
    EXPECT_EQ(versionBefore + 2, wd.ribVersion);
    ASSERT_TRUE(wd.nh.has_value());
    withdrawn.emplace(wd.pathIdToSend, *wd.nh);
  }
  EXPECT_EQ(expected, withdrawn);
  // The remaining path is unchanged, so it is not re-announced.
  EXPECT_TRUE(announcementAddPath.addPathEntries.empty());
  EXPECT_EQ(0, ribOutQ_.size());
}

/*
 * With RIB allocated path IDs, a relaxed min aggregate LBW policy withdraws the
 * prefix but keeps its multipaths in the RibEntry (and in FIB). When the best
 * path returns, the kept multipaths are announced again, together with the new
 * one, instead of being skipped as unchanged.
 */
TEST_F(
    RibAllocatedPathIdFixture,
    MultipathDeltaReannouncesPathsKeptAfterFullWithdrawal) {
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  rib_->setFibBatchTime(milliseconds(2));
  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(ribOutQ_.size(), 2); });
  REPEAT_N(2, folly::coro::blockingWait(ribOutQ_.pop()));

  // Pop ribOutQ_ messages until one matches pred.
  auto popUntil = [&](const std::function<bool(const RibOutMessage&)>& pred)
      -> std::optional<RibOutMessage> {
    for (int i = 0; i < 500; i++) {
      WITH_RETRIES({ EXPECT_EVENTUALLY_GE(ribOutQ_.size(), 1); });
      if (ribOutQ_.size() == 0) {
        return std::nullopt;
      }
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      if (pred(msg)) {
        return msg;
      }
    }
    return std::nullopt;
  };
  auto multipathIds = [&](bool advertised) {
    std::set<uint32_t> pathIds;
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      auto it = rib_->ribEntries_.find(kV4Prefix1);
      if (it == rib_->ribEntries_.end()) {
        return;
      }
      for (const auto& [pathId, _] : advertised
               ? it->second.getAdvertisedMultipaths()
               : it->second.getMultipaths()) {
        pathIds.insert(pathId);
      }
    });
    return pathIds;
  };

  /* Two multipaths with 10G LBW each are advertised. */
  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs1->setNexthop(kV4Nexthop1);
  attrs1->setNonTransitiveLbwExtCommunity(kLocalAs1, kLbw10G);
  attrs1->publish();
  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs2->setNexthop(kV4Nexthop2);
  attrs2->setNonTransitiveLbwExtCommunity(kLocalAs1, kLbw10G);
  attrs2->publish();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attrs1);
  sendAnnouncement(prefixBatch, eBgpPeer2_, attrs2);
  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(2, multipathIds(true).size()); });
  const auto keptPathIds = multipathIds(true);

  /*
   * A min aggregate LBW of 25G, relaxed and without drain, withdraws the
   * prefix (20G) but keeps both multipaths.
   */
  TPathSelector tPathSelector;
  tPathSelector.bgp_min_aggregate_lbw_bps() =
      static_cast<int64_t>(25 * BpsPerGBps);
  tPathSelector.relax_bgp_min_aggregate_lbw_bps() = true;
  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();
  auto withdrawalMsg = popUntil([](const RibOutMessage& msg) {
    const auto* withdrawal = std::get_if<RibOutWithdrawal>(&msg);
    return withdrawal && !withdrawal->addPathEntries.empty();
  });
  ASSERT_TRUE(withdrawalMsg.has_value());
  std::set<uint32_t> withdrawnPathIds;
  for (const auto& wd :
       std::get<RibOutWithdrawal>(*withdrawalMsg).addPathEntries) {
    withdrawnPathIds.insert(wd.pathIdToSend);
  }
  EXPECT_EQ(keptPathIds, withdrawnPathIds);
  EXPECT_EQ(keptPathIds, multipathIds(false));
  EXPECT_TRUE(multipathIds(true).empty());

  /* A third 10G path satisfies the policy (30G), so the best path returns. */
  auto attrs3 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs3->setNexthop(kV4Nexthop3);
  attrs3->setNonTransitiveLbwExtCommunity(kLocalAs1, kLbw10G);
  attrs3->publish();
  sendAnnouncement(prefixBatch, eBgpPeer3_, attrs3);
  auto announcementMsg = popUntil([](const RibOutMessage& msg) {
    const auto* announcement = std::get_if<RibOutAnnouncement>(&msg);
    return announcement && !announcement->addPathEntries.empty();
  });
  ASSERT_TRUE(announcementMsg.has_value());
  std::set<uint32_t> announcedPathIds;
  for (const auto& addPathEntry :
       std::get<RibOutAnnouncement>(*announcementMsg).addPathEntries) {
    announcedPathIds.insert(addPathEntry.pathIdToSend);
  }
  const auto expectedPathIds = multipathIds(false);
  ASSERT_EQ(3, expectedPathIds.size());
  EXPECT_EQ(expectedPathIds, announcedPathIds);
  for (const auto pathId : keptPathIds) {
    EXPECT_TRUE(announcedPathIds.contains(pathId)) << "path ID " << pathId;
  }
}

/*
 * With RIB allocated path IDs, a partial drain change with unchanged multipaths
 * re-sends every advertised path with the new partial drain state.
 */
TEST_F(
    RibAllocatedPathIdFixture,
    MultipathDeltaPartialDrainChangeReannouncesAllPaths) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    // A peer without a local UCMP weight keeps the aggregate weights unchanged.
    const TinyPeerInfo peer(
        kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
    const std::vector<folly::IPAddress> nexthops{kV4Nexthop1, kV4Nexthop2};
    for (uint32_t i = 0; i < nexthops.size(); i++) {
      auto attrs = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      attrs->setNexthop(nexthops[i]);
      attrs->publish();
      entry.updatePath(peer, attrs, true, i + 1);
    }
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    // Commit as advertised.
    entry.commitMultipaths();
    entry.commitMultipathNexthops();
    entry.commitBestpath();
    std::set<uint32_t> expected;
    for (const auto& [pathId, _] : entry.getAdvertisedMultipaths()) {
      expected.insert(pathId);
    }

    for (const bool isPartialDrain : {true, false}) {
      entry.isPartialDrain_ = isPartialDrain;
      ASSERT_FALSE(entry.multipathChanged());
      RibOutAnnouncement announcement;
      RibOutAnnouncement announcementAddPath;
      RibOutWithdrawal withdrawalAddPath;

      rib_->handleAddUpdatedUnicastRoute(
          entry,
          false /* sendWithEoR */,
          announcement,
          announcementAddPath,
          withdrawalAddPath);

      std::set<uint32_t> announced;
      for (const auto& addPathEntry : announcementAddPath.addPathEntries) {
        EXPECT_EQ(isPartialDrain, addPathEntry.isPartialDrain);
        announced.insert(addPathEntry.pathIdToSend);
      }
      EXPECT_EQ(expected, announced) << "isPartialDrain " << isPartialDrain;
      EXPECT_TRUE(withdrawalAddPath.addPathEntries.empty());
      EXPECT_FALSE(entry.advertisedPrefixFieldsChanged());
    }
  });
}

/*
 * With RIB allocated path IDs, adding a path that changes the aggregate UCMP
 * weight re-sends every advertised path with the new weight, not only the new
 * path.
 */
TEST_F(
    RibAllocatedPathIdFixture,
    MultipathDeltaUcmpWeightChangeReannouncesAllPaths) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs1->setNexthop(kV4Nexthop1);
    attrs1->publish();
    auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs2->setNexthop(kV4Nexthop2);
    attrs2->publish();
    auto attrs3 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs3->setNexthop(kV4Nexthop3);
    attrs3->publish();
    // The fixture peers have local UCMP weights 10, 20 and 30.
    entry.updatePath(eBgpPeer1_, attrs1);
    entry.updatePath(eBgpPeer2_, attrs2);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    // Commit as advertised, then add a third path.
    entry.commitMultipaths();
    entry.commitMultipathNexthops();
    entry.commitBestpath();
    const auto weightBefore = entry.getAggregateLocalUcmpWeight();
    entry.updatePath(eBgpPeer3_, attrs3);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_EQ(3, entry.getMultipaths().size());
    ASSERT_NE(weightBefore, entry.getAggregateLocalUcmpWeight());
    ASSERT_TRUE(entry.advertisedPrefixFieldsChanged());
    std::set<uint32_t> expected;
    for (const auto& [pathId, _] : entry.getMultipaths()) {
      expected.insert(pathId);
    }
    RibOutAnnouncement announcement;
    RibOutAnnouncement announcementAddPath;
    RibOutWithdrawal withdrawalAddPath;

    rib_->handleAddUpdatedUnicastRoute(
        entry,
        false /* sendWithEoR */,
        announcement,
        announcementAddPath,
        withdrawalAddPath);

    std::set<uint32_t> announced;
    for (const auto& addPathEntry : announcementAddPath.addPathEntries) {
      EXPECT_EQ(
          entry.getAggregateLocalUcmpWeight(),
          addPathEntry.aggregateLocalUcmpWeight);
      announced.insert(addPathEntry.pathIdToSend);
    }
    EXPECT_EQ(expected, announced);
    EXPECT_TRUE(withdrawalAddPath.addPathEntries.empty());
  });
}

/*
 * With RIB allocated path IDs, a rib policy UCMP weight change with unchanged
 * multipaths re-sends every advertised path with the new weight.
 */
TEST_F(
    RibAllocatedPathIdFixture,
    MultipathDeltaRibPolicyWeightChangeReannouncesAllPaths) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    RibEntry entry(kV4Prefix1);
    // A peer without a local UCMP weight keeps the aggregate weights unchanged.
    const TinyPeerInfo peer(
        kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
    const std::vector<folly::IPAddress> nexthops{kV4Nexthop1, kV4Nexthop2};
    for (uint32_t i = 0; i < nexthops.size(); i++) {
      auto attrs = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      attrs->setNexthop(nexthops[i]);
      attrs->publish();
      entry.updatePath(peer, attrs, true, i + 1);
    }
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    // Commit as advertised, then set a rib policy UCMP weight.
    entry.commitMultipaths();
    entry.commitMultipathNexthops();
    entry.commitBestpath();
    std::set<uint32_t> expected;
    for (const auto& [pathId, _] : entry.getAdvertisedMultipaths()) {
      expected.insert(pathId);
    }
    entry.setRibPolicyUcmpWeight(5.0f);
    ASSERT_FALSE(entry.multipathChanged());
    RibOutAnnouncement announcement;
    RibOutAnnouncement announcementAddPath;
    RibOutWithdrawal withdrawalAddPath;

    rib_->handleAddUpdatedUnicastRoute(
        entry,
        false /* sendWithEoR */,
        announcement,
        announcementAddPath,
        withdrawalAddPath);

    std::set<uint32_t> announced;
    for (const auto& addPathEntry : announcementAddPath.addPathEntries) {
      EXPECT_EQ(std::optional<float>(5.0f), addPathEntry.ribPolicyUcmpWeight);
      announced.insert(addPathEntry.pathIdToSend);
    }
    EXPECT_EQ(expected, announced);
    EXPECT_TRUE(withdrawalAddPath.addPathEntries.empty());
  });
}

// Verify that after read only announcements are sent in chunks
TEST_F(RibFixture, IncrementalAnnoucementChunkTestAfterReadOnly) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(2);
  EXPECT_CALL(*fib_, program_(_)).Times(2);
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(2 * (kRibChunkSize + 1));
  /*
   * As RibDumpReq is served by a different fiber, make sure the fibsync updates
   * are enqueued before dump-req is sent. The expected-match sequence is coded
   * with this assumption.
   */
  folly::fibers::Baton fibSyncDone;
  // ribOutQ_ consumer
  auto listenerThread = std::thread([&]() {
    /*
     * We expect 5 messages:
     *   one message indicating start of initial dump
     *   two messages for initial dump
     *   two messages for incremental announcements
     */

    // Indicate start of initial announcements
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));
    }

    // first dump to all peers
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
      auto announcement = std::get<RibOutAnnouncement>(msg);
      EXPECT_EQ(kRibChunkSize, announcement.addPathEntries.size());
      checkRibOutEntriesAddPathIds(announcement);
      EXPECT_EQ(true, announcement.initialDump);
      EXPECT_EQ(false, announcement.sendWithEoR);
    }
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
      auto announcement = std::get<RibOutAnnouncement>(msg);
      EXPECT_EQ(kRibChunkSize, announcement.entries.size());
      for (auto& entry : announcement.entries) {
        EXPECT_EQ(kDefaultPathID, entry.pathIdToSend);
      }
      EXPECT_EQ(true, announcement.initialDump);
      EXPECT_EQ(false, announcement.sendWithEoR);
    }
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
      auto announcement = std::get<RibOutAnnouncement>(msg);
      ASSERT_EQ(1, announcement.entries.size());
      EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
      ASSERT_EQ(1, announcement.addPathEntries.size());
      EXPECT_EQ(kMinPathIDToSend, announcement.addPathEntries[0].pathIdToSend);
      EXPECT_EQ(true, announcement.initialDump);
      EXPECT_EQ(true, announcement.sendWithEoR);
    }
    fibSyncDone.post();
    // incremental announcements to kPeerAddr1
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
      auto announcement = std::get<RibOutAnnouncement>(msg);
      EXPECT_EQ(kRibChunkSize, announcement.addPathEntries.size());
      checkRibOutEntriesAddPathIds(announcement);
      EXPECT_EQ(false, announcement.initialDump);
      EXPECT_EQ(false, announcement.sendWithEoR);
    }
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
      auto announcement = std::get<RibOutAnnouncement>(msg);
      EXPECT_EQ(kRibChunkSize, announcement.entries.size());
      for (auto& entry : announcement.entries) {
        EXPECT_EQ(kDefaultPathID, entry.pathIdToSend);
      }
      EXPECT_EQ(false, announcement.initialDump);
      EXPECT_EQ(false, announcement.sendWithEoR);
    }
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
      auto announcement = std::get<RibOutAnnouncement>(msg);
      ASSERT_EQ(1, announcement.entries.size());
      EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
      ASSERT_EQ(1, announcement.addPathEntries.size());
      ASSERT_EQ(kMinPathIDToSend, announcement.addPathEntries[0].pathIdToSend);
      EXPECT_EQ(false, announcement.initialDump);
      EXPECT_EQ(false, announcement.sendWithEoR);
    }
  });

  // send bulk prefixes from iBgpPeer_(kPeerAddr2) to Rib
  sendBulkAnnouncement(kRibChunkSize + 1, iBgpPeer_, attr_);

  // send EoR
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  fibSyncDone.wait();
  /*
   * send rib dump req for kPeerAddr1 to rib
   * this will cause rib to send RibOutWithdrawal for "kRibChunkSize"
   * prefixes with sendWithEoR being true
   */
  fibFuture = fib_->getFibProgramFuture();
  // send bulk prefixes from iBgpPeer_(kPeerAddr2) to Rib
  sendBulkAnnouncement(
      kRibChunkSize + 1, iBgpPeer_, attr_, 0x02000000); // 2.0.0.0
  fibFuture.wait();

  listenerThread.join();
}

// Verify that after read only withdrawals are sent in chunks
TEST_F(RibFixture, IncrementalWithdrawnChunkTestAfterReadOnly) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(2);
  EXPECT_CALL(*fib_, program_(_)).Times(2);
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(2 * (kRibChunkSize + 1));
  /*
   * As RibDumpReq is served by a different fiber, make sure the fibsync updates
   * are enqueued before dump-req is sent. The expected-match sequence is coded
   * with this assumption.
   */
  folly::fibers::Baton fibSyncDone;
  // ribOutQ_ consumer
  auto listenerThread = std::thread([&]() {
    /*
     * We expect 5 messages:
     *   one message indicating start of initial dump
     *   two messages for initial dump
     *   two messages for incremental withdrawals
     */
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));
    }
    // first dump to all peers
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
      auto announcement = std::get<RibOutAnnouncement>(msg);
      EXPECT_EQ(kRibChunkSize, announcement.addPathEntries.size());
      checkRibOutEntriesAddPathIds(announcement);
      EXPECT_EQ(true, announcement.initialDump);
      EXPECT_EQ(false, announcement.sendWithEoR);
    }
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
      auto announcement = std::get<RibOutAnnouncement>(msg);
      EXPECT_EQ(kRibChunkSize, announcement.entries.size());
      for (auto& entry : announcement.entries) {
        EXPECT_EQ(kDefaultPathID, entry.pathIdToSend);
      }
      EXPECT_EQ(true, announcement.initialDump);
      EXPECT_EQ(false, announcement.sendWithEoR);
    }
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
      auto announcement = std::get<RibOutAnnouncement>(msg);
      ASSERT_EQ(1, announcement.entries.size());
      EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
      ASSERT_EQ(1, announcement.addPathEntries.size());
      EXPECT_EQ(kMinPathIDToSend, announcement.addPathEntries[0].pathIdToSend);
      EXPECT_EQ(true, announcement.initialDump);
      EXPECT_EQ(true, announcement.sendWithEoR);
    }
    fibSyncDone.post();
    // second withdrawal to kPeerAddr1
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
      auto withdrawal = std::get<RibOutWithdrawal>(msg);
      EXPECT_EQ(kRibChunkSize, withdrawal.entries.size());
      for (auto& entry : withdrawal.entries) {
        EXPECT_EQ(kDefaultPathID, entry.pathIdToSend);
      }
    }
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
      auto withdrawal = std::get<RibOutWithdrawal>(msg);
      EXPECT_EQ(kRibChunkSize, withdrawal.addPathEntries.size());
      checkRibOutEntriesAddPathIds(withdrawal);
    }
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
      auto withdrawal = std::get<RibOutWithdrawal>(msg);
      ASSERT_EQ(1, withdrawal.entries.size());
      EXPECT_EQ(kDefaultPathID, withdrawal.entries[0].pathIdToSend);
      ASSERT_EQ(1, withdrawal.addPathEntries.size());
      EXPECT_EQ(kMinPathIDToSend, withdrawal.addPathEntries[0].pathIdToSend);
    }
  });

  // send bulk prefixes from iBgpPeer_(kPeerAddr2) to Rib
  sendBulkAnnouncement(kRibChunkSize + 1, iBgpPeer_, attr_);

  // send EoR
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  fibSyncDone.wait();
  /*
   * send rib dump req for kPeerAddr1 to rib
   * this will cause rib to send RibOutWithdrawal for "kRibChunkSize"
   * prefixes with sendWithEoR being true
   */
  fibFuture = fib_->getFibProgramFuture();
  sendBulkWithdrawal(kRibChunkSize + 1, iBgpPeer_);
  fibFuture.wait();

  listenerThread.join();
}

// Verify that after read only withdrawals are sent in chunks
TEST_F(RibFixture, IncrementalWithdrawnChunkTestAfterReadOnlyForAddPath) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(2);
  EXPECT_CALL(*fib_, program_(_)).Times(2);
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(2 * (kRibChunkSize + 1));
  /*
   * As RibDumpReq is served by a different fiber, make sure the fibsync updates
   * are enqueued before dump-req is sent. The expected-match sequence is coded
   * with this assumption.
   */
  folly::fibers::Baton fibSyncDone;
  // ribOutQ_ consumer
  auto listenerThread = std::thread([&]() {
    /*
     * We expect 5 messages:
     *   one message to mark start of initial dump
     *   two messages for initial dump
     *   two messages for incremental withdrawals
     */

    // Expect RibInitialAnnouncementStart
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));
    }
    // first dump to all peers
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
      auto announcement = std::get<RibOutAnnouncement>(msg);
      EXPECT_EQ(kRibChunkSize, announcement.addPathEntries.size());
      checkRibOutEntriesAddPathIds(announcement);
      EXPECT_EQ(true, announcement.initialDump);
      EXPECT_EQ(false, announcement.sendWithEoR);
    }
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
      auto announcement = std::get<RibOutAnnouncement>(msg);
      EXPECT_EQ(kRibChunkSize, announcement.entries.size());
      for (auto& entry : announcement.entries) {
        EXPECT_EQ(kDefaultPathID, entry.pathIdToSend);
      }
      EXPECT_EQ(true, announcement.initialDump);
      EXPECT_EQ(false, announcement.sendWithEoR);
    }
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
      auto announcement = std::get<RibOutAnnouncement>(msg);
      ASSERT_EQ(1, announcement.entries.size());
      EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
      ASSERT_EQ(1, announcement.addPathEntries.size());
      EXPECT_EQ(kMinPathIDToSend, announcement.addPathEntries[0].pathIdToSend);
      EXPECT_EQ(true, announcement.initialDump);
      EXPECT_EQ(true, announcement.sendWithEoR);
    }
    fibSyncDone.post();
    // second withdrawal to kPeerAddr1
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
      auto withdrawal = std::get<RibOutWithdrawal>(msg);
      EXPECT_EQ(kRibChunkSize, withdrawal.entries.size());
      for (auto& entry : withdrawal.entries) {
        EXPECT_EQ(kDefaultPathID, entry.pathIdToSend);
      }
    }
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
      auto withdrawal = std::get<RibOutWithdrawal>(msg);
      EXPECT_EQ(kRibChunkSize, withdrawal.addPathEntries.size());
      checkRibOutEntriesAddPathIds(withdrawal);
    }
    {
      auto msg = folly::coro::blockingWait(ribOutQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
      auto withdrawal = std::get<RibOutWithdrawal>(msg);
      ASSERT_EQ(1, withdrawal.entries.size());
      EXPECT_EQ(kDefaultPathID, withdrawal.entries[0].pathIdToSend);
      ASSERT_EQ(1, withdrawal.addPathEntries.size());
      EXPECT_EQ(kMinPathIDToSend, withdrawal.addPathEntries[0].pathIdToSend);
    }
  });

  // send bulk prefixes from iBgpPeer_(kPeerAddr2) to Rib
  sendBulkAnnouncement(kRibChunkSize + 1, iBgpPeer_, attr_);

  // send EoR
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  fibSyncDone.wait();
  /*
   * send rib dump req for kPeerAddr1 to rib
   * this will cause rib to send RibOutWithdrawal for "kRibChunkSize"
   * prefixes with sendWithEoR being true
   */
  fibFuture = fib_->getFibProgramFuture();
  sendBulkWithdrawal(kRibChunkSize + 1, iBgpPeer_);
  fibFuture.wait();

  listenerThread.join();
}

/*
 * Verify that multiple full-sync requests will cause one-time of FIB
 * programming to avoid syncing 0 routes to FIB.
 */
TEST_F(RibFixture, MultipleFullSyncRequest) {
  EXPECT_CALL(*fib_, program_(true)).Times(1);

  // hold future for fib programming
  auto fibFuture = fib_->getFibProgramFuture();

  auto& toFibQ = rib_->toFibMessageQ_;
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    /*
     * call prepareFibProgramming() multiple times(>1) to mimic the full-sync
     * requests being piled up scenario
     */
    rib_->prepareFibProgramming(true);
    rib_->prepareFibProgramming(true);

    /*
     * ATTN: this cb will NOT yield to other fiber tasks, which can read the
     * pending requests. Thus we expect 2 pending messages inside the queue.
     */
    EXPECT_EQ(2, toFibQ.size());
  });

  // MockFib::program() will fulfill this
  fibFuture.wait();

  WITH_RETRIES({
    facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
        fmt::format("{}.avg.60", RibStats::ribFullSyncPathSelectionTimeMs));
    facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
        fmt::format(
            "{}.avg.60", RibStatsDC::ribFullSyncRouteAttributeOverwriteTimeMs));
  });
}

/*
 * Verify that initial rib dump is responded in chunks.
 * Fib programmed kRibChunkSize + 1 prefixes, we should see that two rib
 * announcements happen, one with kRibChunkSize prefixes and another with 1
 * prefix.
 */
TEST_F(RibFixture, InitialDumpChunkTest) {
  auto& inputQ = rib_->fromFibMessageQ_;

  // populate ribEntries_ with kRibChunkSize + 1 entries
  folly::
      F14NodeMap<folly::CIDRNetwork, std::shared_ptr<const WeightedNexthopMap>>
          prefixToNexthops;
  auto startAddress = 0x01000000; // 1.0.0.0

  for (auto i = 0; i < kRibChunkSize + 1; i++) {
    auto prefix =
        folly::CIDRNetwork(IPAddress::fromLongHBO(startAddress + i), 32);
    RibEntry entry(prefix);
    entry.updatePath(eBgpPeer1_, attr_, false);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    // initialise a FibProgrammedMessage with same routes in ribEntries
    prefixToNexthops.emplace(
        make_pair(prefix, entry.getMultipathWeightedNexthops()));
    rib_->ribEntries_.emplace(make_pair(prefix, std::move(entry)));
  }

  folly::F14FastMap<
      std::shared_ptr<const BgpPath>,
      folly::F14NodeMap<
          folly::CIDRNetwork,
          std::shared_ptr<const WeightedNexthopMap>>>
      FibProgrammedPfxs;
  FibProgrammedPfxs[nullptr] = prefixToNexthops;

  Fib::FibProgrammedMessage fibMsg(FibProgrammedPfxs, true);
  inputQ.push(std::move(fibMsg));

  /*
   * We expect four messages
   * All should have initial dump as true.
   * First is RibInitialAnnouncementStart.
   * Last three are addPath entries and regular entries.
   * Only third message should have send with EOR set with 1 prefix.
   * As this is initial dump, there shouldn't be any peerAddr (representing
   * notification is to all peers)
   */
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));
  }
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    EXPECT_EQ(kRibChunkSize, announcement.addPathEntries.size());
    checkRibOutEntriesAddPathIds(announcement);
    EXPECT_EQ(true, announcement.initialDump);
    EXPECT_EQ(false, announcement.sendWithEoR);
  }
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    EXPECT_EQ(kRibChunkSize, announcement.entries.size());
    EXPECT_LE(100, announcement.entries.size());
    for (auto& entry : announcement.entries) {
      EXPECT_EQ(kDefaultPathID, entry.pathIdToSend);
    }
    EXPECT_EQ(true, announcement.initialDump);
    EXPECT_EQ(false, announcement.sendWithEoR);
  }
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
    ASSERT_EQ(1, announcement.addPathEntries.size());
    ASSERT_EQ(kMinPathIDToSend, announcement.addPathEntries[0].pathIdToSend);
    EXPECT_EQ(true, announcement.initialDump);
    EXPECT_EQ(true, announcement.sendWithEoR);
  }

  // expect no more announcements
  REPEAT_N(5, { EXPECT_EQ(0, ribOutQ_.size()); });
}

// This tests Withdrawal sent after READ_ONLY period
TEST_F(RibFixture, WithdrawalAfterReadOnly) {
  auto prefixBatch1 = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto prefixBatch2 = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(2);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
      .Times(2);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV6Prefix1), _, _, _, _, _))
      .Times(1);
  EXPECT_CALL(*fib_, program_(true)).Times(1);
  EXPECT_CALL(*fib_, program_(false)).Times(1);

  // send prefixes to Rib
  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
  sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);
  sendInitialPathComputation();
  fibFuture.wait();

  // Now set fibBatchTime to 15 milliseconds
  rib_->setFibBatchTime(milliseconds(15));
  auto batch = rib_->getFibBatchTime();
  EXPECT_EQ(batch, milliseconds(15));

  /*
   * send withdrawal to rib
   * this will cause rib to send a withdrawal for kV4Prefix1 immediately
   * then trigger fib programming in 15 milliseconds
   */
  fibFuture = fib_->getFibProgramFuture();
  auto start = steady_clock::now();
  /*
   * create listener thread. This couldn't be done with fiber as
   * fibFuture.wait() blocks the thread itself.
   */
  auto listenerThread = std::thread([&]() {
    auto ribInitialAnnouncementStart =
        folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(
        std::holds_alternative<RibInitialAnnouncementStart>(
            ribInitialAnnouncementStart));

    auto msg1 = folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg1));
    auto announcement = std::get<RibOutAnnouncement>(msg1);
    ASSERT_EQ(2, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
    EXPECT_EQ(kDefaultPathID, announcement.entries[1].pathIdToSend);
    EXPECT_TRUE(announcement.sendWithEoR);

    auto msg2 = folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg2));
    auto withdrawal = std::get<RibOutWithdrawal>(msg2);
    // we should receive this before fib programming timer triggers
    auto end = steady_clock::now();
    auto elapsed = duration_cast<milliseconds>(end - start);
    XLOGF(DBG1, "elapsed:{} batch:{}", elapsed.count(), batch.count());
    EXPECT_GE(elapsed, batch);

    ASSERT_EQ(1, withdrawal.entries.size());
    EXPECT_EQ(kDefaultPathID, withdrawal.entries[0].pathIdToSend);
    EXPECT_EQ(kV4Prefix1, withdrawal.entries[0].prefix);
  });
  sendWithdrawal(prefixBatch1, iBgpPeer_);
  // wait for fib programming happens
  fibFuture.wait();
  auto end = steady_clock::now();
  auto elapsed = duration_cast<milliseconds>(end - start);
  EXPECT_GE(elapsed, batch);

  listenerThread.join();
}

// This tests Withdrawal sent during READ_ONLY period
TEST_F(RibFixture, WithdrawalDuringReadOnly) {
  auto prefixBatch1 = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto prefixBatch2 = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
      .Times(0);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV6Prefix1), _, _, _, _, _))
      .Times(1);
  EXPECT_CALL(*fib_, program_(true)).Times(1);

  // send prefixes to Rib
  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
  sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);
  /*
   * send withdrawal to rib
   * this will cause bestpath selection set nexthops to nullptr in
   * fullSync fib programming, which should be ignored
   */
  sendWithdrawal(prefixBatch1, iBgpPeer_);
  sendInitialPathComputation();
  fibFuture.wait();
}

/*
 * This checks rib announce local routes that do not need
 * minimum supporting routes upon thread starting
 */
TEST_F(RibWithLocalRouteFixture, LocalRouteWithoutMinSupportRoutes) {
  setUpRibAndFib(getDefaultLocalRoutes());
  rib_->setFibBatchTime(milliseconds(8));

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(2);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
      .Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV6Prefix1), _, _, _, _, _))
      .Times(1);
  // one announcement one withdrawal of kV4Prefix1Slash27
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1Slash27), _, _, _, _, _))
      .Times(2);
  EXPECT_CALL(*fib_, program_(true)).Times(1);
  // this is caused by withdrawal after EoR
  EXPECT_CALL(*fib_, program_(false)).Times(1);

  // wait until local routes are put in rib
  WITH_RETRIES({
    auto v4Prefix = std::make_unique<std::string>(
        folly::IPAddress::networkToString(kV4Prefix1));
    ASSERT_EVENTUALLY_FALSE(
        rib_->getRibEntryForPrefix(std::move(v4Prefix)).empty());
  });

  auto fibFuture = fib_->getFibProgramFuture();
  // send announcement with prefix fall into kV4Prefix1 subnet
  auto prefixBatch = PrefixPathIds{{kV4Prefix1Slash27, kDefaultPathID}};
  sendAnnouncement(prefixBatch, iBgpPeer_, attr_);
  // send eor to trigger bestpath calculation
  sendInitialPathComputation();
  fibFuture.wait();

  {
    // check kV4Prefix1 bestpath
    auto bestpath = rib_->getBestPath(kV4Prefix1);
    EXPECT_NE(bestpath, nullptr);
    EXPECT_EQ(kEmptyV4Nexthop, bestpath->attrs->getNexthop());
    EXPECT_EQ(kLocalPref, *bestpath->attrs->getLocalPref());
    EXPECT_TRUE(bestpath->attrs->getCommunities().nullOrEmpty());
    EXPECT_EQ(localPeer_, bestpath->peer);
  }
  {
    // check kV6Prefix1 bestpath
    auto bestpath = rib_->getBestPath(kV6Prefix1);
    EXPECT_NE(bestpath, nullptr);
    if (bestpath) {
      EXPECT_EQ(kEmptyV6Nexthop, bestpath->attrs->getNexthop());
      EXPECT_EQ(kLocalPref, *bestpath->attrs->getLocalPref());
      EXPECT_EQ(2, bestpath->attrs->getCommunities()->size());
      EXPECT_EQ(localPeer_, bestpath->peer);
    }
  }

  // this withdrawal should not cause local route withdrawal of kV4Prefix1
  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(prefixBatch, iBgpPeer_);
  fibFuture.wait();
}

/*
 * This checks rib announce local routes that need
 * minimum supporting routes only when support criteria is met.
 */
TEST_F(RibWithLocalRouteFixture, RouteAggregation) {
  setUpRibAndFib(getDefaultLocalRoutes());
  rib_->setFibBatchTime(milliseconds(8));

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(3);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
      .Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV6Prefix1), _, _, _, _, _))
      .Times(1);
  /*
   * updateUnicastRoute_ called two times for kV4Prefix2 and kV6Prefix2
   * one announcement because minimum_supporting_routes is reached
   * one withdrawal due to withdrawal of supporting routes
   */
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix2), _, _, _, _, _))
      .Times(2);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV6Prefix2), _, _, _, _, _))
      .Times(2);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix2Slash31), _, _, _, _, _))
      .Times(2);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV6Prefix2Slash127), _, _, _, _, _))
      .Times(2);
  EXPECT_CALL(*fib_, program_(true)).Times(1);
  EXPECT_CALL(*fib_, program_(false)).Times(2);

  // Use the technique in InstallToFibTest
  folly::EventBase localEvb;
  auto& localFm = folly::fibers::getFiberManager(
      localEvb, nettools::bgplib::getFiberManagerOptions());
  localFm.addTask([&] {
    auto fibFuture = fib_->getFibProgramFuture();
    // send prefixes to Rib
    auto prefixBatch1 = PrefixPathIds{{kV4Prefix2Slash31, kDefaultPathID}};
    auto prefixBatch2 = PrefixPathIds{{kV6Prefix2Slash127, kDefaultPathID}};
    sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
    sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);
    sendInitialPathComputation();
    fibFuture.wait();

    // check kV4Prefix2 bestpath
    auto bestpath = rib_->getBestPath(kV4Prefix2);
    CHECK(bestpath);
    EXPECT_EQ(kEmptyV4Nexthop, bestpath->attrs->getNexthop());
    EXPECT_EQ(kLocalPref, *bestpath->attrs->getLocalPref());
    EXPECT_EQ(1, bestpath->attrs->getCommunities()->size());
    EXPECT_EQ(localPeer_, bestpath->peer);

    // check kV6Prefix2 bestpath
    bestpath = rib_->getBestPath(kV6Prefix2);
    CHECK(bestpath);
    EXPECT_EQ(kEmptyV6Nexthop, bestpath->attrs->getNexthop());
    EXPECT_EQ(kLocalPref, *bestpath->attrs->getLocalPref());
    EXPECT_EQ(1, bestpath->attrs->getCommunities()->size());
    EXPECT_EQ(localPeer_, bestpath->peer);

    /*
     * withdrawal of kV4Prefix2Slash31 and kV6Prefix2Slash127 should cause
     * local route withdrawal of kV4Prefix2 and kV6Prefix2
     */
    fibFuture = fib_->getFibProgramFuture();
    sendWithdrawal(prefixBatch1, iBgpPeer_);
    fibFuture.wait();
    fibFuture = fib_->getFibProgramFuture();
    sendWithdrawal(prefixBatch2, iBgpPeer_);
    fibFuture.wait();
  });
  localEvb.loop();
}

// This checks route aggregation logic when a supporting route flaps
TEST_F(RibWithLocalRouteFixture, RouteAggregationStress) {
  // "2401:db00:21:7000::/59" with minimum_supporting_routes = 1
  thrift::BgpNetwork network;
  *network.prefix() = IPAddress::networkToString(kV6Prefix4_0Slash59);
  network.minimum_supporting_routes() = 1;
  setUpRibAndFib({{kV6Prefix4_0Slash59, network}});

  rib_->setFibBatchTime(milliseconds(8));

  std::vector<TinyPeerInfo> peers = {
      eBgpPeer1_, eBgpPeer2_, eBgpPeer3_, eBgpPeer4_, eBgpPeer5_, eBgpPeer6_};
  PrefixPathIds supportingPrefixes;
  for (int i = 0; i < peers.size(); i++) {
    auto pfx = folly::IPAddress::createNetwork(
        fmt::format("2401:db00:21:700{}::/64", i));
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(pfx), _, _, _, _, _)).Times(1);
    supportingPrefixes.emplace_back(pfx, kDefaultPathID);
  }

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(true)).Times(1);
  EXPECT_CALL(*fib_, program_(false)).Times(testing::AnyNumber());
  EXPECT_CALL(
      *fib_, updateUnicastRoute_(Eq(kV6Prefix4_0Slash59), _, _, _, _, _))
      .Times(1);

  auto fibFuture = fib_->getFibProgramFuture();
  // send prefixes to Rib
  for (int i = 0; i < peers.size(); i++) {
    sendAnnouncement({supportingPrefixes[i]}, peers[i], attr_);
  }
  // send eor to trigger bestpath calculation
  sendInitialPathComputation();
  fibFuture.wait();

  // check 2401:db00:21:7000::/59 bestpath
  auto bestpath = rib_->getBestPath(kV6Prefix4_0Slash59);
  CHECK(bestpath);
  EXPECT_EQ(kEmptyV6Nexthop, bestpath->attrs->getNexthop());
  EXPECT_EQ(kLocalPref, *bestpath->attrs->getLocalPref());
  EXPECT_EQ(localPeer_, bestpath->peer);

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));

  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  auto announcement = std::get<RibOutAnnouncement>(msg);
  EXPECT_EQ(7, announcement.entries.size());
  for (auto& entry : announcement.entries) {
    EXPECT_EQ(kDefaultPathID, entry.pathIdToSend);
  }
  EXPECT_EQ(true, announcement.initialDump);
  EXPECT_EQ(true, announcement.sendWithEoR);

  EXPECT_CALL(
      *fib_,
      updateUnicastRoute_(Eq(get<0>(supportingPrefixes[3])), _, _, _, _, _))
      .Times(testing::AnyNumber());

  /*
   * 1. flap 2401:db00:21:7003::/64 with withdraw followed by immediate
   * announcement: S200838
   */
  {
    fibFuture = fib_->getFibProgramFuture();
    sendWithdrawal({supportingPrefixes[3]}, peers[3]);
    sendAnnouncement({supportingPrefixes[3]}, peers[3], attr_);
    fibFuture.wait();

    EXPECT_EQ(
        supportingPrefixes.size(),
        rib_->localRoutes_.find(kV6Prefix4_0Slash59)->second.supportPfxCnt);
  }

  // 2. flap 2401:db00:21:7003::/64 with consecutive announcement and withdrawn
  {
    // first withdraw 2401:db00:21:7003::/64
    fibFuture = fib_->getFibProgramFuture();
    sendWithdrawal({supportingPrefixes[3]}, peers[3]);
    fibFuture.wait();
    EXPECT_EQ(
        supportingPrefixes.size() - 1,
        rib_->localRoutes_.find(kV6Prefix4_0Slash59)->second.supportPfxCnt);

    fibFuture = fib_->getFibProgramFuture();
    sendAnnouncement({supportingPrefixes[3]}, peers[3], attr_);
    sendWithdrawal({supportingPrefixes[3]}, peers[3]);
    sendAnnouncement({supportingPrefixes[3]}, peers[3], attr_);
    sendWithdrawal({supportingPrefixes[3]}, peers[3]);
    sendAnnouncement({supportingPrefixes[3]}, peers[3], attr_);
    fibFuture.wait();
    EXPECT_EQ(
        supportingPrefixes.size(),
        rib_->localRoutes_.find(kV6Prefix4_0Slash59)->second.supportPfxCnt);
  }

  // 3. flap 2401:db00:21:7003::/64 with duplicate announcements
  {
    // first withdraw 2401:db00:21:7003::/64
    fibFuture = fib_->getFibProgramFuture();
    sendWithdrawal({supportingPrefixes[3]}, peers[3]);
    fibFuture.wait();
    EXPECT_EQ(
        supportingPrefixes.size() - 1,
        rib_->localRoutes_.find(kV6Prefix4_0Slash59)->second.supportPfxCnt);

    fibFuture = fib_->getFibProgramFuture();
    sendAnnouncement({supportingPrefixes[3]}, peers[3], attr_);
    sendAnnouncement({supportingPrefixes[3]}, peers[3], attr_);
    fibFuture.wait();
    EXPECT_EQ(
        supportingPrefixes.size(),
        rib_->localRoutes_.find(kV6Prefix4_0Slash59)->second.supportPfxCnt);
  }

  // 4. flap 2401:db00:21:7003::/64 with duplicate withdrawals
  {
    fibFuture = fib_->getFibProgramFuture();
    sendWithdrawal({supportingPrefixes[3]}, peers[3]);
    sendWithdrawal({supportingPrefixes[3]}, peers[3]);
    fibFuture.wait();
    EXPECT_EQ(
        supportingPrefixes.size() - 1,
        rib_->localRoutes_.find(kV6Prefix4_0Slash59)->second.supportPfxCnt);
  }
}

// This checks routes announced are in the order of same attributes
TEST_F(RibFixture, RoutesAnnouncedPerAttr) {
  rib_->setFibBatchTime(milliseconds(8));

  std::vector<TinyPeerInfo> peers = {
      eBgpPeer1_, eBgpPeer2_, eBgpPeer3_, eBgpPeer4_, eBgpPeer5_, eBgpPeer6_};
  PrefixPathIds supportingPrefixes;
  for (int i = 0; i < peers.size(); i++) {
    auto pfx = folly::IPAddress::createNetwork(
        fmt::format("2401:db00:21:700{}::/64", i));
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(pfx), _, _, _, _, _)).Times(1);
    supportingPrefixes.emplace_back(pfx, kDefaultPathID);
  }

  auto attr0 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(1, 1, 1, 1));
  attr0->publish();
  auto attr1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(2, 2, 2, 2));
  attr1->publish();
  auto attr2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(3, 3, 3, 3));
  attr2->publish();

  auto fibFuture = fib_->getFibProgramFuture();
  // send prefixes to Rib
  for (int i = 0; i < peers.size(); i++) {
    if (i % 3 == 0) {
      sendAnnouncement({supportingPrefixes[i]}, peers[i], attr0);
    } else if (i % 3 == 1) {
      sendAnnouncement({supportingPrefixes[i]}, peers[i], attr1);
    } else {
      sendAnnouncement({supportingPrefixes[i]}, peers[i], attr2);
    }
  }
  // send eor to trigger bestpath calculation
  sendInitialPathComputation();
  fibFuture.wait();

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  // Expect RibInitialAnnouncementStart before initial dump.
  ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));

  msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  auto announcement = std::get<RibOutAnnouncement>(msg);
  EXPECT_EQ(6, announcement.entries.size());
  for (auto& entry : announcement.entries) {
    EXPECT_EQ(kDefaultPathID, entry.pathIdToSend);
  }
  EXPECT_EQ(true, announcement.initialDump);
  EXPECT_EQ(true, announcement.sendWithEoR);

  /*
   * Check all announced prefixes are next to each other for a given attr
   * prefixes in announcements are based on 1st hash of attrs
   */
  EXPECT_EQ(announcement.entries[0].attrs, announcement.entries[1].attrs);
  EXPECT_NE(announcement.entries[1].attrs, announcement.entries[2].attrs);
  EXPECT_EQ(announcement.entries[2].attrs, announcement.entries[3].attrs);
}

/*
 * This checks rib announce/withdrawn local routes correctly
 * when supporting routes has multiple updates on their attributes
 */
TEST_F(RibWithLocalRouteFixture, RouteAggregationWithPathUpdate) {
  thrift::BgpNetwork network;
  *network.prefix() = IPAddress::networkToString(kV4Prefix2);
  network.minimum_supporting_routes() = 1;
  network.communities() = std::vector<std::string>{kCommunity3};

  // setup rib with one v4 local route with minimum_supporting_routes = 1
  setUpRibAndFib({{kV4Prefix2, network}});
  rib_->setFibBatchTime(milliseconds(8));

  /*
   * updateUnicastRoute_ called two times for kV4Prefix2
   * one announcement because minimum_supporting_routes is reached
   * one withdrawal due to withdrawal of supporting routes
   */
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix2), _, _, _, _, _))
      .Times(1);
  EXPECT_CALL(
      *fib_, updateUnicastRoute_(Eq(kV4Prefix2), _, Eq(nullptr), _, _, _))
      .Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix2Slash31), _, _, _, _, _))
      .Times(3);
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(3);
  EXPECT_CALL(*fib_, program_(true)).Times(1);
  EXPECT_CALL(*fib_, program_(false)).Times(2);

  auto fibFuture = fib_->getFibProgramFuture();
  // send supporting prefixes to Rib
  auto prefixBatch = PrefixPathIds{{kV4Prefix2Slash31, kDefaultPathID}};
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  // send eor to trigger bestpath calculation
  sendInitialPathComputation();

  fibFuture.wait();
  // check kV4Prefix2 bestpath
  auto bestpath = rib_->getBestPath(kV4Prefix2);
  CHECK(bestpath);
  EXPECT_EQ(kEmptyV4Nexthop, bestpath->attrs->getNexthop());
  EXPECT_EQ(kLocalPref, *bestpath->attrs->getLocalPref());
  EXPECT_EQ(1, bestpath->attrs->getCommunities()->size());
  EXPECT_EQ(localPeer_, bestpath->peer);

  // send supporting prefixes with updated attributes to Rib
  fibFuture = fib_->getFibProgramFuture();
  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(3, 3, 3, 3));
  attr->publish();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr);
  fibFuture.wait();

  // withdrawal of supporting route should cause local route withdrawal
  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(prefixBatch, eBgpPeer1_);
  fibFuture.wait();
}

/*
 * This checks rib announce local routes correctly
 * when supporting routes has ecmp path
 */
TEST_F(RibWithLocalRouteFixture, RouteAggregationWithEcmp) {
  thrift::BgpNetwork network;
  *network.prefix() = IPAddress::networkToString(kV4Prefix2);
  network.minimum_supporting_routes() = 1;
  network.communities() = std::vector<std::string>{kCommunity3};

  // setup rib with one v4 local route with minimum_supporting_routes = 1
  setUpRibAndFib({{kV4Prefix2, network}});
  rib_->setFibBatchTime(milliseconds(8));

  /*
   * updateUnicastRoute_ called two times for kV4Prefix2
   * one announcement because minimum_supporting_routes is reached
   * one withdrawal due to withdrawal of supporting routes
   */
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix2), _, _, _, _, _))
      .Times(1);
  EXPECT_CALL(
      *fib_, updateUnicastRoute_(Eq(kV4Prefix2), _, Eq(nullptr), _, _, _))
      .Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix2Slash31), _, _, _, _, _))
      .Times(3);
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(3);
  EXPECT_CALL(*fib_, program_(true)).Times(1);
  EXPECT_CALL(*fib_, program_(false)).Times(2);

  auto fibFuture = fib_->getFibProgramFuture();
  // send supporting prefixes to Rib
  auto prefixBatch = PrefixPathIds{{kV4Prefix2Slash31, kDefaultPathID}};
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  sendAnnouncement(prefixBatch, eBgpPeer2_, attr_);
  // send eor to trigger bestpath calculation
  sendInitialPathComputation();

  fibFuture.wait();
  // check kV4Prefix2 bestpath
  auto bestpath = rib_->getBestPath(kV4Prefix2);
  CHECK(bestpath);
  EXPECT_EQ(kEmptyV4Nexthop, bestpath->attrs->getNexthop());
  EXPECT_EQ(kLocalPref, *bestpath->attrs->getLocalPref());
  EXPECT_EQ(1, bestpath->attrs->getCommunities()->size());
  EXPECT_EQ(localPeer_, bestpath->peer);

  // send supporting prefixes with updated attributes to Rib
  fibFuture = fib_->getFibProgramFuture();
  /*
   * withdrawal of one path of the supporting route
   * should not cause local route withdrawal
   */
  sendWithdrawal(prefixBatch, eBgpPeer1_);

  fibFuture.wait();
  // check kV4Prefix2 is not whidrawn yet
  bestpath = rib_->getBestPath(kV4Prefix2);
  CHECK(bestpath);
  EXPECT_EQ(kEmptyV4Nexthop, bestpath->attrs->getNexthop());
  EXPECT_EQ(kLocalPref, *bestpath->attrs->getLocalPref());
  EXPECT_EQ(1, bestpath->attrs->getCommunities()->size());
  EXPECT_EQ(localPeer_, bestpath->peer);

  // send supporting prefixes with updated attributes to Rib
  fibFuture = fib_->getFibProgramFuture();
  /*
   * withdrawal of one path of the supporting route
   * should not cause local route withdrawal
   */
  sendWithdrawal(prefixBatch, eBgpPeer2_);
  fibFuture.wait();
}

// This checks installToFib flag when programming to fib
TEST_F(RibWithLocalRouteFixture, InstallToFibTest) {
  /*
   * Out of 4 default local routes, set install_to_fib to true for kV6Prefix1
   * and kV4Prefix2; set it to false for kV4Prefix1 and kV6Prefix2
   */
  auto localRoutes = getDefaultLocalRoutes();
  localRoutes[kV4Prefix1].install_to_fib() = false;
  localRoutes[kV4Prefix2].install_to_fib() = true;
  localRoutes[kV6Prefix1].install_to_fib() = true;
  // default for KV6Prefix2 should be false as well

  setUpRibAndFib(localRoutes);

  rib_->setFibBatchTime(milliseconds(8));

  /*
   * Local routes land in the RIB asynchronously. The path computation below
   * programs whatever is present at that instant, so a route that has not
   * landed yet is never scheduled for FIB programming at all — and the
   * fibFuture then completes on that partial batch, hiding the omission.
   *
   * Only kV4Prefix1 and kV6Prefix1 are originated at startup; the remaining
   * defaults carry minimum_supporting_routes = 1 and cannot appear until the
   * supporting announcement below.
   */
  for (const auto& prefix : {kV4Prefix1, kV6Prefix1}) {
    WITH_RETRIES({
      auto prefixStr =
          std::make_unique<std::string>(IPAddress::networkToString(prefix));
      ASSERT_EVENTUALLY_FALSE(
          rib_->getRibEntryForPrefix(std::move(prefixStr)).empty());
    });
  }
  {
    /*
     * send update msg with prefix 9.0.0.0/28 through eBgpPeer1_. Since we
     * need to write (0/28, {eBgpPeer1_}) to FIB, we do not need to write (1/32,
     * {eBgpPeer1_}) or (2/32, {eBgpPeer1_}) to FIB because they share same
     * nexthops. In addition, kV4Prefix2, kV4Prefix1, and kV6Prefix1 are
     * configured as local routes. Therefore, (kV4Prefix2,
     * {kLocalV4RoutePeerAddr}), (kV4Prefix1, {kLocalV4RoutePeerAddr}), and
     * (kV6Prefix1, {kLocalV6RoutePeerAddr}) will also be written to FIB
     */
    WeightedNexthopMap nhWts = {{eBgpPeer1_.addr, 0}};
    EXPECT_CALL(
        *fib_,
        updateUnicastRoute_(
            Eq(kV4Prefix8_0Slash28), _, Pointee(nhWts), false, true, _))
        .Times(1);
    nhWts = {{kLocalV4RoutePeerAddr, 0}};
    EXPECT_CALL(
        *fib_,
        updateUnicastRoute_(Eq(kV4Prefix2), _, Pointee(nhWts), true, true, _))
        .Times(1);
    nhWts = {{kLocalV4RoutePeerAddr, 0}};
    EXPECT_CALL(
        *fib_,
        updateUnicastRoute_(Eq(kV4Prefix1), _, Pointee(nhWts), true, false, _))
        .Times(1);
    nhWts = {{kLocalV6RoutePeerAddr, 0}};
    EXPECT_CALL(
        *fib_,
        updateUnicastRoute_(Eq(kV6Prefix1), _, Pointee(nhWts), true, true, _))
        .Times(1);
    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(2, 1, 0, 1));
    attrs->setNexthop(eBgpPeer1_.addr);
    attrs->publish();
    auto fibFuture = fib_->getFibProgramFuture();

    // send eor to trigger bestpath calculation
    folly::EventBase localEvb;
    auto& localFm = folly::fibers::getFiberManager(
        localEvb, nettools::bgplib::getFiberManagerOptions());
    localFm.addTask([&] {
      // send prefixes to Rib
      auto prefixBatch = PrefixPathIds{{kV4Prefix8_0Slash28, kDefaultPathID}};
      sendAnnouncement(prefixBatch, eBgpPeer1_, attrs);
      sendInitialPathComputation();
    });
    localEvb.loop();

    fibFuture.wait();
  }
}

// TODO: remove test T113736668
TEST_F(LocalRouteWithPolicyFixture, LocalRouteWithOldAsField) {
  // create rib with default local routes
  rib_ = createMockRib(getLocalRoutesWithOldAsField());
  // verify the default attribute settings
  auto localRoutes = rib_->getLocalRoutes();
  EXPECT_EQ(1, localRoutes.size());
  EXPECT_EQ(
      1,
      fb303::ThreadCachedServiceData::getShared()->getCounter(
          RibStats::kTotalOriginatedRoutes));
  {
    auto it = localRoutes.find(kV4Prefix1);
    EXPECT_TRUE(it != localRoutes.end());
    std::vector<nettools::bgplib::BgpAttrCommunityC> expectedCommunitySet;
    std::vector<BgpAttrAsPathSegmentC> expectedAsPath =
        createBgpAttrAsPathSegmentCV({std::make_pair(
            2, std::vector<uint32_t>{kAsn2, kAsn3})}); // AS_SEQUENCE
    verifyAttributes(
        it->second.attrs,
        kLocalRouteV4Nexthop,
        nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_IGP,
        kDefaultLocalPref,
        expectedCommunitySet,
        expectedAsPath);
  }
}

// This checks rib generates local route correctly when policy is not used
TEST_F(LocalRouteWithPolicyFixture, LocalRouteWithoutPolicy) {
  // create rib with default local routes
  rib_ = createMockRib(getDefaultLocalRoutes());
  // verify the default attribute settings
  auto localRoutes = rib_->getLocalRoutes();
  EXPECT_EQ(5, localRoutes.size());
  EXPECT_EQ(
      5,
      fb303::ThreadCachedServiceData::getShared()->getCounter(
          RibStats::kTotalOriginatedRoutes));
  std::vector<folly::CIDRNetwork> prefixSet;
  for (const auto& kv : localRoutes) {
    prefixSet.push_back(kv.first);
  }
  std::vector<folly::CIDRNetwork> expectedPrefixSet{
      kV4Prefix1, kV6Prefix1, kV4Prefix2, kV6Prefix2, kV4Prefix5};
  EXPECT_THAT(prefixSet, UnorderedElementsAreArray(expectedPrefixSet));
  {
    auto it = localRoutes.find(kV4Prefix1);
    EXPECT_TRUE(it != localRoutes.end());
    std::vector<nettools::bgplib::BgpAttrCommunityC> expectedCommunitySet;
    verifyAttributes(
        it->second.attrs,
        kLocalRouteV4Nexthop,
        nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_IGP,
        kDefaultLocalPref,
        expectedCommunitySet,
        {});
  }
  {
    auto it = localRoutes.find(kV6Prefix1);
    EXPECT_TRUE(it != localRoutes.end());
    std::vector<nettools::bgplib::BgpAttrCommunityC> expectedCommunitySet{
        nettools::bgplib::BgpAttrCommunityC(65500, 1),
        nettools::bgplib::BgpAttrCommunityC(65500, 2)};
    verifyAttributes(
        it->second.attrs,
        kLocalRouteV6Nexthop,
        nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_IGP,
        kDefaultLocalPref,
        expectedCommunitySet,
        {});
  }
  {
    auto it = localRoutes.find(kV4Prefix2);
    EXPECT_TRUE(it != localRoutes.end());
    std::vector<nettools::bgplib::BgpAttrCommunityC> expectedCommunitySet{
        nettools::bgplib::BgpAttrCommunityC(65500, 3)};
    verifyAttributes(
        it->second.attrs,
        kLocalRouteV4Nexthop,
        nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_IGP,
        kDefaultLocalPref,
        expectedCommunitySet,
        {});
  }
  {
    auto it = localRoutes.find(kV6Prefix2);
    EXPECT_TRUE(it != localRoutes.end());
    std::vector<nettools::bgplib::BgpAttrCommunityC> expectedCommunitySet{
        nettools::bgplib::BgpAttrCommunityC(65500, 4)};
    verifyAttributes(
        it->second.attrs,
        kLocalRouteV6Nexthop,
        nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_IGP,
        kDefaultLocalPref,
        expectedCommunitySet,
        {});
  }
  {
    auto it = localRoutes.find(kV4Prefix5);
    EXPECT_TRUE(it != localRoutes.end());
    std::vector<nettools::bgplib::BgpAttrCommunityC> expectedCommunitySet{
        nettools::bgplib::BgpAttrCommunityC(65500, 3)};
    std::vector<BgpAttrAsPathSegmentC> expectedAsPath =
        createBgpAttrAsPathSegmentCV(
            {std::make_pair(
                 4,
                 std::vector<uint32_t>{kAsn1, kAsn2, kAsn3}), // AS_CONFED_SET
             std::make_pair(1, std::vector<uint32_t>{kAsn4, kAsn5})}); // AS_SET
    verifyAttributes(
        it->second.attrs,
        kV4Nexthop1,
        nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP,
        kLocalPref2,
        expectedCommunitySet,
        expectedAsPath);
  }
}

/*
 * This checks that local routes have weight set to 2^15 to ensure they win
 * in path selection when weight comparison is enabled
 */
TEST_F(LocalRouteWithPolicyFixture, LocalRouteHasMaxWeight) {
  // create rib with default local routes
  rib_ = createMockRib(getDefaultLocalRoutes());
  // verify the weight is set to 2^15 for all local routes
  auto localRoutes = rib_->getLocalRoutes();
  EXPECT_FALSE(localRoutes.empty());
  for (const auto& [prefix, localRoute] : localRoutes) {
    EXPECT_EQ(1 << 15, localRoute.attrs->getWeight())
        << "Local route " << folly::IPAddress::networkToString(prefix)
        << " should have weight 2^15";
  }
}

// This checks rib generates local route correctly when policy is used
TEST_F(LocalRouteWithPolicyFixture, LocalRouteWithPolicy) {
  {
    /*
     * create rib with local routes.
     * kV4Prefix1 and kV6Prefix1 have policy config which sets origin,
     * local-pref, and community list. kV4Prefix2 and kV6Prefix2 will continue
     * to have the default values.
     */
    auto localRouteConfig = getDefaultLocalRoutes();
    localRouteConfig[kV4Prefix1].policy_name() = "SET-ATTR";
    localRouteConfig[kV6Prefix1].policy_name() = "SET-ATTR";
    localRouteConfig[kV4Prefix5].policy_name() = "SET-ATTR";
    auto setOriginToIncompleteAction = createBgpPolicyAction(
        bgp_policy::BgpPolicyActionType::ORIGIN,
        {} /* not used */,
        "" /* not used */,
        bgp_policy::Origin::INCOMPLETE);
    auto setLocalPrefTo25Action = createActionSetLocalPreference(25);
    auto setCommunityAction = createBgpPolicyCommunityAction(
        bgp_policy::CommunityActionType::SET,
        {kCommunity1, kCommunity2, kCommunity3, kCommunity4});
    auto setAsPathAction = createPolicySetAsPathPrependAction(kAsn6, 1);
    auto policyConfig = createBgpPolicies(
        "SET-ATTR",
        {},
        {setOriginToIncompleteAction,
         setLocalPrefTo25Action,
         setCommunityAction,
         setAsPathAction});
    rib_ = createMockRib(localRouteConfig, policyConfig);

    /*
     * verify the resulting attributes
     * kV4Prefix1 and kV6Prefix1 have the new attribute settings.
     * kV4Prefix2 and kV6Prefix2 have the default attribute settings.
     */
    auto localRoutes = rib_->getLocalRoutes();
    EXPECT_EQ(5, localRoutes.size());
    EXPECT_EQ(
        5,
        fb303::ThreadCachedServiceData::getShared()->getCounter(
            RibStats::kTotalOriginatedRoutes));
    std::vector<folly::CIDRNetwork> prefixSet;
    for (const auto& kv : localRoutes) {
      prefixSet.push_back(kv.first);
    }
    std::vector<folly::CIDRNetwork> expectedPrefixSet{
        kV4Prefix1, kV6Prefix1, kV4Prefix2, kV6Prefix2, kV4Prefix5};
    EXPECT_THAT(prefixSet, UnorderedElementsAreArray(expectedPrefixSet));
    std::vector<nettools::bgplib::BgpAttrCommunityC> expectedCommunitySet{
        nettools::bgplib::BgpAttrCommunityC(65500, 1),
        nettools::bgplib::BgpAttrCommunityC(65500, 2),
        nettools::bgplib::BgpAttrCommunityC(65500, 3),
        nettools::bgplib::BgpAttrCommunityC(65500, 4),
    };
    std::vector<BgpAttrAsPathSegmentC> expectedAsPath =
        createBgpAttrAsPathSegmentCV(
            {std::make_pair(2, std::vector<uint32_t>{kAsn6})}); // AS_SEQUENCE
    {
      auto it = localRoutes.find(kV4Prefix1);
      EXPECT_TRUE(it != localRoutes.end());
      verifyAttributes(
          it->second.attrs,
          kLocalRouteV4Nexthop,
          nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE,
          25,
          expectedCommunitySet,
          expectedAsPath); // {kAsn6}
    }
    {
      auto it = localRoutes.find(kV6Prefix1);
      EXPECT_TRUE(it != localRoutes.end());
      verifyAttributes(
          it->second.attrs,
          kLocalRouteV6Nexthop,
          nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE,
          25,
          expectedCommunitySet,
          expectedAsPath); // {kAsn6}
    }
    {
      auto it = localRoutes.find(kV4Prefix2);
      EXPECT_TRUE(it != localRoutes.end());
      std::vector<nettools::bgplib::BgpAttrCommunityC> expectedCommunitySet2{
          nettools::bgplib::BgpAttrCommunityC(65500, 3)};
      verifyAttributes(
          it->second.attrs,
          kLocalRouteV4Nexthop,
          nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_IGP,
          kDefaultLocalPref,
          expectedCommunitySet2,
          {});
    }
    {
      auto it = localRoutes.find(kV6Prefix2);
      EXPECT_TRUE(it != localRoutes.end());
      std::vector<nettools::bgplib::BgpAttrCommunityC> expectedCommunitySet2{
          nettools::bgplib::BgpAttrCommunityC(65500, 4)};
      verifyAttributes(
          it->second.attrs,
          kLocalRouteV6Nexthop,
          nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_IGP,
          kDefaultLocalPref,
          expectedCommunitySet2,
          {});
    }
    {
      auto it = localRoutes.find(kV4Prefix5);
      EXPECT_TRUE(it != localRoutes.end());
      std::vector<BgpAttrAsPathSegmentC> expectedAsPath2 =
          createBgpAttrAsPathSegmentCV(
              {std::make_pair(2, std::vector<uint32_t>{kAsn6}), // AS_SEQUENCE
               std::make_pair(
                   4,
                   std::vector<uint32_t>{kAsn1, kAsn2, kAsn3}), // AS_CONFED_SET
               std::make_pair(
                   1, std::vector<uint32_t>{kAsn4, kAsn5})}); // AS_SET
      verifyAttributes(
          it->second.attrs,
          kV4Nexthop1,
          nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE,
          25,
          expectedCommunitySet,
          expectedAsPath2);
    }
  }
  {
    /*
     * create rib with local routes.
     * All 5 prefixes have policy config which denies kV4Prefix1 and kV6Prefix1.
     */
    auto localRouteConfig = getDefaultLocalRoutes();
    localRouteConfig[kV4Prefix1].policy_name() = "REJECT-SOME";
    localRouteConfig[kV6Prefix1].policy_name() = "REJECT-SOME";
    localRouteConfig[kV4Prefix2].policy_name() = "REJECT-SOME";
    localRouteConfig[kV6Prefix2].policy_name() = "REJECT-SOME";
    localRouteConfig[kV4Prefix5].policy_name() = "REJECT-SOME";
    // create policy match, action, term
    routing_policy::CompareNumericValue compareStructEQ;
    *compareStructEQ.compare_operator() =
        routing_policy::ComparisonOperator::EQ;
    *compareStructEQ.value() = kV4Prefix1.second;
    const auto& prefixListEntry1 = createPrefixListEntry(
        IPAddress::networkToString(kV4Prefix1), {compareStructEQ});
    *compareStructEQ.compare_operator() =
        routing_policy::ComparisonOperator::EQ;
    *compareStructEQ.value() = kV6Prefix1.second;
    const auto& prefixListEntry2 = createPrefixListEntry(
        IPAddress::networkToString(kV6Prefix1), {compareStructEQ});
    const auto& match =
        createPrefixListMatch({prefixListEntry1, prefixListEntry2});
    auto actionDeny =
        createBgpPolicyAction(bgp_policy::BgpPolicyActionType::DENY);
    auto term1 = createBgpPolicyTerm("Term1", "", {match}, {actionDeny});
    auto term2 = createBgpPolicyTerm("Term2", "", {}, {});
    // create policy
    const auto& policyConfig = createBgpPolicies("REJECT-SOME", {term1, term2});
    rib_ = createMockRib(localRouteConfig, policyConfig);

    /*
     * verify the resulting attributes
     * kV4Prefix1 and kV6Prefix1 are denied but rib entries are created for
     * them. kV4Prefix2, kV6Prefix2 and kV4Prefix5 have the default attribute
     * settings
     */
    auto localRoutes = rib_->getLocalRoutes();
    EXPECT_EQ(3, localRoutes.size());
    EXPECT_EQ(
        3,
        fb303::ThreadCachedServiceData::getShared()->getCounter(
            RibStats::kTotalOriginatedRoutes));
    std::vector<folly::CIDRNetwork> prefixSet;
    for (const auto& kv : localRoutes) {
      prefixSet.push_back(kv.first);
    }
    std::vector<folly::CIDRNetwork> expectedPrefixSet{
        kV4Prefix2, kV6Prefix2, kV4Prefix5};
    EXPECT_THAT(prefixSet, UnorderedElementsAreArray(expectedPrefixSet));
    {
      auto it = localRoutes.find(kV4Prefix2);
      EXPECT_TRUE(it != localRoutes.end());
      std::vector<nettools::bgplib::BgpAttrCommunityC> expectedCommunitySet{
          nettools::bgplib::BgpAttrCommunityC(65500, 3)};
      verifyAttributes(
          it->second.attrs,
          kLocalRouteV4Nexthop,
          nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_IGP,
          kDefaultLocalPref,
          expectedCommunitySet,
          {});
    }
    {
      auto it = localRoutes.find(kV6Prefix2);
      EXPECT_TRUE(it != localRoutes.end());
      std::vector<nettools::bgplib::BgpAttrCommunityC> expectedCommunitySet{
          nettools::bgplib::BgpAttrCommunityC(65500, 4)};
      verifyAttributes(
          it->second.attrs,
          kLocalRouteV6Nexthop,
          nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_IGP,
          kDefaultLocalPref,
          expectedCommunitySet,
          {});
    }
    {
      auto it = localRoutes.find(kV4Prefix5);
      EXPECT_TRUE(it != localRoutes.end());
      std::vector<nettools::bgplib::BgpAttrCommunityC> expectedCommunitySet{
          nettools::bgplib::BgpAttrCommunityC(65500, 3)};
      std::vector<BgpAttrAsPathSegmentC> expectedAsPath =
          createBgpAttrAsPathSegmentCV(
              {std::make_pair(
                   4,
                   std::vector<uint32_t>{kAsn1, kAsn2, kAsn3}), // AS_CONFED_SET
               std::make_pair(
                   1, std::vector<uint32_t>{kAsn4, kAsn5})}); // AS_SET
      verifyAttributes(
          it->second.attrs,
          kV4Nexthop1,
          nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP,
          kLocalPref2,
          expectedCommunitySet,
          expectedAsPath);
    }
  }
  {
    /*
     * create rib with local routes.
     * kV4Prefix1 has policy config, but the policy definition does not exist.
     */
    std::vector<folly::CIDRNetwork> expectedPrefixSet{
        kV4Prefix1, kV6Prefix1, kV4Prefix2, kV6Prefix2};
    auto localRouteConfig = getDefaultLocalRoutes();
    localRouteConfig[kV4Prefix1].policy_name() = "SET-ATTR";
    try {
      rib_ = createMockRib(localRouteConfig);
    } catch (const BgpError& error) {
      EXPECT_EQ(
          "Missing network policy (SET-ATTR) for prefix (8.0.0.0/24)",
          *error.message());
    }
  }
}

/*
 * When programming FIB automatically include the min-support
 * viable local (aggregate) routes in the current fib-batch.
 * Send update for the following:
 *      kV4Prefix3 8.0.1.0/24
 *      kV4Prefix2Slash31 9.0.0.0/31
 *      kV6Prefix2Slash127 2002::/27
 * Due to summerization expect to have the following
 * prefixes in updateUnicastRoute_ call:
 *      kV4Prefix3 8.0.1.0/24 : part of update
 *      kV4Prefix2Slash31 9.0.0.0/31 : part of update
 *      kV6Prefix2Slash127 2002::/127 : part of update
 *      kV4Prefix2 9.0.0.0/24 : min-support local route
 *      kV6Prefix2 2002::/64  : min-support local route
 *      kV4Prefix1 8.0.0.0/24 : no min-support local route
 *      kV6Prefix1 2001::/64  : no min-support local route
 */
TEST_F(RibWithLocalRouteFixture, RouteAggregationPromotion) {
  setUpRibAndFib(getDefaultLocalRoutes());
  rib_->setFibBatchTime(milliseconds(8));
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
      .Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV6Prefix1), _, _, _, _, _))
      .Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix2), _, _, _, _, _))
      .Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix3), _, _, _, _, _))
      .Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV6Prefix2), _, _, _, _, _))
      .Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix2Slash31), _, _, _, _, _))
      .Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV6Prefix2Slash127), _, _, _, _, _))
      .Times(1);
  EXPECT_CALL(*fib_, program_(true)).Times(1);

  // wait until local routes are put in rib
  WITH_RETRIES({
    auto v4Prefix = std::make_unique<std::string>(
        folly::IPAddress::networkToString(kV4Prefix1));
    ASSERT_EVENTUALLY_FALSE(
        rib_->getRibEntryForPrefix(std::move(v4Prefix)).empty());
  });
  auto fibFuture = fib_->getFibProgramFuture();

  // send eor to trigger bestpath calculation
  folly::EventBase localEvb;
  auto& localFm = folly::fibers::getFiberManager(
      localEvb, nettools::bgplib::getFiberManagerOptions());
  localFm.addTask([&] {
    // send prefixes to Rib
    auto prefixBatch1 = PrefixPathIds{{kV4Prefix3, kDefaultPathID}};
    auto prefixBatch2 = PrefixPathIds{{kV4Prefix2Slash31, kDefaultPathID}};
    auto prefixBatch3 = PrefixPathIds{{kV6Prefix2Slash127, kDefaultPathID}};
    sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
    sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);
    sendAnnouncement(prefixBatch3, iBgpPeer_, attr_);
    sendInitialPathComputation();
  });
  localEvb.loop();

  fibFuture.wait();
  {
    // check kV4Prefix1 bestpath
    auto bestpath = rib_->getBestPath(kV4Prefix1);
    EXPECT_NE(bestpath, nullptr);
    EXPECT_EQ(kEmptyV4Nexthop, bestpath->attrs->getNexthop());
    EXPECT_EQ(kLocalPref, *bestpath->attrs->getLocalPref());
    EXPECT_EQ(localPeer_, bestpath->peer);
  }
  {
    // check kV4Prefix2 bestpath
    auto bestpath = rib_->getBestPath(kV4Prefix2);
    EXPECT_NE(bestpath, nullptr);
    if (bestpath) {
      EXPECT_EQ(kEmptyV4Nexthop, bestpath->attrs->getNexthop());
      EXPECT_EQ(kLocalPref, *bestpath->attrs->getLocalPref());
      EXPECT_EQ(localPeer_, bestpath->peer);
    }
  }
  {
    // check kV6Prefix1 bestpath
    auto bestpath = rib_->getBestPath(kV6Prefix1);
    EXPECT_NE(bestpath, nullptr);
    if (bestpath) {
      EXPECT_EQ(kEmptyV6Nexthop, bestpath->attrs->getNexthop());
      EXPECT_EQ(kLocalPref, *bestpath->attrs->getLocalPref());
      EXPECT_EQ(localPeer_, bestpath->peer);
    }
  }
  {
    // check kV6Prefix2 bestpath
    auto bestpath = rib_->getBestPath(kV6Prefix2);
    EXPECT_NE(bestpath, nullptr);
    if (bestpath) {
      EXPECT_EQ(kEmptyV6Nexthop, bestpath->attrs->getNexthop());
      EXPECT_EQ(kLocalPref, *bestpath->attrs->getLocalPref());
      EXPECT_EQ(localPeer_, bestpath->peer);
    }
  }
  {
    // check kV4Prefix3 bestpath
    auto bestpath = rib_->getBestPath(kV4Prefix3);
    EXPECT_NE(bestpath, nullptr);
    if (bestpath) {
      EXPECT_EQ(kV4Nexthop1, bestpath->attrs->getNexthop());
      EXPECT_EQ(kLocalPref, *bestpath->attrs->getLocalPref());
      EXPECT_EQ(iBgpPeer_, bestpath->peer);
    }
  }
  {
    // check kV4Prefix2Slash31 bestpath
    auto bestpath = rib_->getBestPath(kV4Prefix2Slash31);
    EXPECT_NE(bestpath, nullptr);
    if (bestpath) {
      EXPECT_EQ(kV4Nexthop1, bestpath->attrs->getNexthop());
      EXPECT_EQ(kLocalPref, *bestpath->attrs->getLocalPref());
      EXPECT_EQ(iBgpPeer_, bestpath->peer);
    }
  }
  {
    // check kV6Prefix2Slash127 bestpath
    auto bestpath = rib_->getBestPath(kV6Prefix2Slash127);
    EXPECT_NE(bestpath, nullptr);
    if (bestpath) {
      EXPECT_EQ(kV4Nexthop1, bestpath->attrs->getNexthop());
      EXPECT_EQ(kLocalPref, *bestpath->attrs->getLocalPref());
      EXPECT_EQ(iBgpPeer_, bestpath->peer);
    }
  }
  /*
   * First message is RibInitialAnnouncementStart.
   * Second message is RibOutAnnouncement.
   */
  EXPECT_EQ(ribOutQ_.size(), 2);

  // Expect RibInitialAnnouncementStart before initial announcement.
  auto ribInitialAnnouncementStart = folly::coro::blockingWait(ribOutQ_.pop());
  EXPECT_TRUE(
      std::holds_alternative<RibInitialAnnouncementStart>(
          ribInitialAnnouncementStart));

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  auto ret = folly::variant_match(
      msg,
      [&](const ShadowRibOutAnnouncement& /* unused */) { return false; },
      [&](const ShadowRibOutWithdrawal& /* unused */) { return false; },
      [&](const RibInitialAnnouncementStart& /* unused */) { return false; },
      [&](const RibOutWithdrawal& /* unused */) { return false; },
      [&](const RibOutAnnouncement& announcement) {
        EXPECT_EQ(7, announcement.entries.size());
        EXPECT_EQ(true, announcement.sendWithEoR);
        EXPECT_EQ(true, announcement.initialDump);
        std::vector<bool> matched(7, false);
        for (int i = 0; i < announcement.entries.size(); ++i) {
          EXPECT_EQ(kDefaultPathID, announcement.entries[i].pathIdToSend);
          if (announcement.entries[i].prefix == kV4Prefix1) {
            EXPECT_EQ(
                facebook::bgp::kLocalRouteV4Nexthop,
                announcement.entries[i].attrs->getNexthop());
            matched[0] = true;
          }
          if (announcement.entries[i].prefix == kV6Prefix1) {
            EXPECT_EQ(
                facebook::bgp::kLocalRouteV6Nexthop,
                announcement.entries[i].attrs->getNexthop());
            matched[1] = true;
          }
          if (announcement.entries[i].prefix == kV4Prefix2) {
            EXPECT_EQ(
                facebook::bgp::kLocalRouteV4Nexthop,
                announcement.entries[i].attrs->getNexthop());
            matched[2] = true;
          }
          if (announcement.entries[i].prefix == kV6Prefix2) {
            EXPECT_EQ(
                facebook::bgp::kLocalRouteV6Nexthop,
                announcement.entries[i].attrs->getNexthop());
            matched[3] = true;
          }
          if (announcement.entries[i].prefix == kV6Prefix2Slash127) {
            EXPECT_EQ(kV4Nexthop1, announcement.entries[i].attrs->getNexthop());
            matched[4] = true;
          }
          if (announcement.entries[i].prefix == kV4Prefix3) {
            EXPECT_EQ(kV4Nexthop1, announcement.entries[i].attrs->getNexthop());
            matched[5] = true;
          }
          if (announcement.entries[i].prefix == kV4Prefix2Slash31) {
            EXPECT_EQ(kV4Nexthop1, announcement.entries[i].attrs->getNexthop());
            matched[6] = true;
          }
        }
        // Expect all the 7 unique prefixes to be present
        bool allMatched =
            (std::find(matched.cbegin(), matched.cend(), false) ==
             matched.cend());
        EXPECT_TRUE(allMatched);
        return true;
      });
  EXPECT_EQ(true, ret);
}

// Helper function to create a local route with require_nexthop_resolution
std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>
getConditionalLocalRoutes() {
  // v4 prefix with require_nexthop_resolution = true
  thrift::BgpNetwork network;
  *network.prefix() = folly::IPAddress::networkToString(kV4Prefix1);
  network.nexthop() = kV4Nexthop1.str();
  network.require_nexthop_resolution() = true;
  return {{kV4Prefix1, network}};
}

/*
 * Helper function to create multiple conditional local routes
 * with two prefixes sharing the same nexthop (kV4Nexthop1)
 * and one prefix with a different nexthop (kV4Nexthop2)
 */
std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>
getMultipleConditionalLocalRoutes() {
  std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork> routes;

  // kV4Prefix1 with nexthop kV4Nexthop1
  thrift::BgpNetwork network1;
  *network1.prefix() = folly::IPAddress::networkToString(kV4Prefix1);
  network1.nexthop() = kV4Nexthop1.str();
  network1.require_nexthop_resolution() = true;
  routes[kV4Prefix1] = network1;

  // kV4Prefix2 with nexthop kV4Nexthop1 (same nexthop as prefix1)
  thrift::BgpNetwork network2;
  *network2.prefix() = folly::IPAddress::networkToString(kV4Prefix2);
  network2.nexthop() = kV4Nexthop1.str();
  network2.require_nexthop_resolution() = true;
  routes[kV4Prefix2] = network2;

  // kV4Prefix3 with nexthop kV4Nexthop2 (different nexthop)
  thrift::BgpNetwork network3;
  *network3.prefix() = folly::IPAddress::networkToString(kV4Prefix3);
  network3.nexthop() = kV4Nexthop2.str();
  network3.require_nexthop_resolution() = true;
  routes[kV4Prefix3] = network3;

  return routes;
}

/*
 * hasConditionalLocalRoutes() must reflect whether any configured local route
 * requires nexthop resolution. This is the single source of truth the
 * composition root (Main.cpp) uses to arm PeerManagerBase's nexthop-resolution
 * gate, so it must be true whenever conditionalLocalRoutes_ is populated in the
 * RIB ctor and false otherwise.
 */
TEST_F(
    RibWithLocalRouteFixture,
    HasConditionalLocalRoutes_TrueWhenRoutesRequireResolution) {
  setUpRibAndFib(getMultipleConditionalLocalRoutes());
  EXPECT_TRUE(rib_->hasConditionalLocalRoutes());
}

TEST_F(
    RibWithLocalRouteFixture,
    HasConditionalLocalRoutes_FalseWhenNoRouteRequiresResolution) {
  setUpRibAndFib(getDefaultLocalRoutes());
  EXPECT_FALSE(rib_->hasConditionalLocalRoutes());
}

/*
 * Test that conditional local routes are NOT originated at startup
 * when the nexthop is not resolved
 */
TEST_F(
    RibWithLocalRouteFixture,
    ConditionalLocalRoute_NotOriginatedWithoutNexthopResolution) {
  setUpRibAndFib(getConditionalLocalRoutes());
  rib_->setFibBatchTime(milliseconds(8));

  /*
   * The route should NOT be announced because it requires nexthop resolution
   * and no resolution update has been received
   */
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
  // No updateUnicastRoute calls expected for kV4Prefix1
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
      .Times(0);
  EXPECT_CALL(*fib_, program_(true)).Times(1);

  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  // Verify the route is not in RIB yet (no best path)
  auto bestpath = rib_->getBestPath(kV4Prefix1);
  EXPECT_EQ(bestpath, nullptr);
}

/*
 * Test that conditional local routes ARE originated when nexthop becomes
 * resolved
 */
TEST_F(RibWithLocalRouteFixture, ConditionalLocalRoute_OriginatedWhenResolved) {
  setUpRibAndFib(getConditionalLocalRoutes());
  rib_->setFibBatchTime(milliseconds(8));

  // First, send EOR without any nexthop resolution
  {
    EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
        .Times(0);
    EXPECT_CALL(*fib_, program_(true)).Times(1);

    auto fibFuture = fib_->getFibProgramFuture();
    sendInitialPathComputation();
    fibFuture.wait();

    // No route yet
    auto bestpath = rib_->getBestPath(kV4Prefix1);
    EXPECT_EQ(bestpath, nullptr);
  }

  // Now send NexthopResolutionUpdate with the nexthop resolved
  {
    EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
        .Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);

    auto fibFuture = fib_->getFibProgramFuture();

    // Create NexthopResolutionUpdate with kV4Nexthop1 resolved
    std::vector<folly::IPAddress> resolved{kV4Nexthop1};
    std::vector<folly::IPAddress> unresolved{};
    sendNexthopResolutionUpdate(
        NexthopResolutionUpdate(std::move(resolved), std::move(unresolved)));
    fibFuture.wait();

    // Now the route should be in RIB
    auto bestpath = rib_->getBestPath(kV4Prefix1);
    EXPECT_NE(bestpath, nullptr);
    // Verify it's a local route
    EXPECT_EQ(kV4Nexthop1, bestpath->attrs->getNexthop());
  }
}

/*
 * Test that conditional local routes ARE withdrawn when nexthop becomes
 * unresolved
 */
TEST_F(
    RibWithLocalRouteFixture,
    ConditionalLocalRoute_WithdrawnWhenUnresolved) {
  setUpRibAndFib(getConditionalLocalRoutes());
  rib_->setFibBatchTime(milliseconds(8));

  // First, send EOR without any nexthop resolution
  {
    EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
        .Times(0);
    EXPECT_CALL(*fib_, program_(true)).Times(1);

    auto fibFuture = fib_->getFibProgramFuture();
    sendInitialPathComputation();
    fibFuture.wait();
  }

  // Now send NexthopResolutionUpdate with the nexthop resolved
  {
    EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
        .Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);

    auto fibFuture = fib_->getFibProgramFuture();

    std::vector<folly::IPAddress> resolved{kV4Nexthop1};
    std::vector<folly::IPAddress> unresolved{};
    sendNexthopResolutionUpdate(
        NexthopResolutionUpdate(std::move(resolved), std::move(unresolved)));
    fibFuture.wait();

    // Route should be in RIB
    auto bestpath = rib_->getBestPath(kV4Prefix1);
    EXPECT_NE(bestpath, nullptr);
  }

  // Now send NexthopResolutionUpdate with the nexthop unresolved
  {
    EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
        .Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);

    auto fibFuture = fib_->getFibProgramFuture();

    std::vector<folly::IPAddress> resolved{};
    std::vector<folly::IPAddress> unresolved{kV4Nexthop1};
    sendNexthopResolutionUpdate(
        NexthopResolutionUpdate(std::move(resolved), std::move(unresolved)));
    fibFuture.wait();

    // Route should be withdrawn from RIB
    auto bestpath = rib_->getBestPath(kV4Prefix1);
    EXPECT_EQ(bestpath, nullptr);
  }
}

/*
 * Test that multiple conditional local routes sharing the same nexthop are all
 * announced when that nexthop becomes resolved
 */
TEST_F(
    RibWithLocalRouteFixture,
    ConditionalLocalRoute_MultipleRoutesForSameNexthop) {
  setUpRibAndFib(getMultipleConditionalLocalRoutes());
  rib_->setFibBatchTime(milliseconds(8));

  // Send EOR first
  {
    EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _)).Times(0);
    EXPECT_CALL(*fib_, program_(true)).Times(1);

    auto fibFuture = fib_->getFibProgramFuture();
    sendInitialPathComputation();
    fibFuture.wait();

    // No routes yet
    EXPECT_EQ(rib_->getBestPath(kV4Prefix1), nullptr);
    EXPECT_EQ(rib_->getBestPath(kV4Prefix2), nullptr);
    EXPECT_EQ(rib_->getBestPath(kV4Prefix3), nullptr);
  }

  /*
   * Resolve kV4Nexthop1 — should announce kV4Prefix1 and kV4Prefix2
   * but NOT kV4Prefix3 (which uses kV4Nexthop2)
   */
  {
    EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
        .Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix2), _, _, _, _, _))
        .Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix3), _, _, _, _, _))
        .Times(0);
    EXPECT_CALL(*fib_, program_(false)).Times(1);

    auto fibFuture = fib_->getFibProgramFuture();

    std::vector<folly::IPAddress> resolved{kV4Nexthop1};
    std::vector<folly::IPAddress> unresolved{};
    sendNexthopResolutionUpdate(
        NexthopResolutionUpdate(std::move(resolved), std::move(unresolved)));
    fibFuture.wait();

    // kV4Prefix1 and kV4Prefix2 should be in RIB
    EXPECT_NE(rib_->getBestPath(kV4Prefix1), nullptr);
    EXPECT_NE(rib_->getBestPath(kV4Prefix2), nullptr);
    // kV4Prefix3 should NOT be in RIB
    EXPECT_EQ(rib_->getBestPath(kV4Prefix3), nullptr);
  }
}

/*
 * Test that a conditional local route can be re-originated after being
 * withdrawn (resolve -> unresolve -> re-resolve cycle)
 */
TEST_F(
    RibWithLocalRouteFixture,
    ConditionalLocalRoute_ReResolutionAfterWithdrawal) {
  setUpRibAndFib(getConditionalLocalRoutes());
  rib_->setFibBatchTime(milliseconds(8));

  // Send EOR
  {
    EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
        .Times(0);
    EXPECT_CALL(*fib_, program_(true)).Times(1);

    auto fibFuture = fib_->getFibProgramFuture();
    sendInitialPathComputation();
    fibFuture.wait();
  }

  // Resolve nexthop — route originated
  {
    EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
        .Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);

    auto fibFuture = fib_->getFibProgramFuture();
    std::vector<folly::IPAddress> resolved{kV4Nexthop1};
    std::vector<folly::IPAddress> unresolved{};
    sendNexthopResolutionUpdate(
        NexthopResolutionUpdate(std::move(resolved), std::move(unresolved)));
    fibFuture.wait();

    EXPECT_NE(rib_->getBestPath(kV4Prefix1), nullptr);
  }

  // Unresolve nexthop — route withdrawn
  {
    EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
        .Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);

    auto fibFuture = fib_->getFibProgramFuture();
    std::vector<folly::IPAddress> resolved{};
    std::vector<folly::IPAddress> unresolved{kV4Nexthop1};
    sendNexthopResolutionUpdate(
        NexthopResolutionUpdate(std::move(resolved), std::move(unresolved)));
    fibFuture.wait();

    EXPECT_EQ(rib_->getBestPath(kV4Prefix1), nullptr);
  }

  // Re-resolve nexthop — route re-originated
  {
    EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
        .Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);

    auto fibFuture = fib_->getFibProgramFuture();
    std::vector<folly::IPAddress> resolved{kV4Nexthop1};
    std::vector<folly::IPAddress> unresolved{};
    sendNexthopResolutionUpdate(
        NexthopResolutionUpdate(std::move(resolved), std::move(unresolved)));
    fibFuture.wait();

    auto bestpath = rib_->getBestPath(kV4Prefix1);
    EXPECT_NE(bestpath, nullptr);
    EXPECT_EQ(kV4Nexthop1, bestpath->attrs->getNexthop());
  }
}

/*
 * Test that a single NexthopResolutionUpdate with both resolved and unresolved
 * nexthops correctly announces and withdraws the corresponding routes
 */
TEST_F(
    RibWithLocalRouteFixture,
    ConditionalLocalRoute_MixedResolvedAndUnresolved) {
  setUpRibAndFib(getMultipleConditionalLocalRoutes());
  rib_->setFibBatchTime(milliseconds(8));

  // Send EOR
  {
    EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _)).Times(0);
    EXPECT_CALL(*fib_, program_(true)).Times(1);

    auto fibFuture = fib_->getFibProgramFuture();
    sendInitialPathComputation();
    fibFuture.wait();
  }

  // Resolve both nexthops first
  {
    EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
        .Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix2), _, _, _, _, _))
        .Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix3), _, _, _, _, _))
        .Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);

    auto fibFuture = fib_->getFibProgramFuture();
    std::vector<folly::IPAddress> resolved{kV4Nexthop1, kV4Nexthop2};
    std::vector<folly::IPAddress> unresolved{};
    sendNexthopResolutionUpdate(
        NexthopResolutionUpdate(std::move(resolved), std::move(unresolved)));
    fibFuture.wait();

    EXPECT_NE(rib_->getBestPath(kV4Prefix1), nullptr);
    EXPECT_NE(rib_->getBestPath(kV4Prefix2), nullptr);
    EXPECT_NE(rib_->getBestPath(kV4Prefix3), nullptr);
  }

  /*
   * Now send mixed update: resolve kV4Nexthop2, unresolve kV4Nexthop1
   * This should withdraw kV4Prefix1 and kV4Prefix2, keep kV4Prefix3
   */
  {
    EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
    /*
     * kV4Prefix1 and kV4Prefix2 should be withdrawn (updateUnicastRoute for
     * withdrawal)
     */
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
        .Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix2), _, _, _, _, _))
        .Times(1);
    /*
     * kV4Prefix3 should NOT be updated (nexthop2 re-resolved is a no-op since
     * already resolved)
     */
    EXPECT_CALL(*fib_, program_(false)).Times(1);

    auto fibFuture = fib_->getFibProgramFuture();
    std::vector<folly::IPAddress> resolved{kV4Nexthop2};
    std::vector<folly::IPAddress> unresolved{kV4Nexthop1};
    sendNexthopResolutionUpdate(
        NexthopResolutionUpdate(std::move(resolved), std::move(unresolved)));
    fibFuture.wait();

    // kV4Prefix1 and kV4Prefix2 should be withdrawn
    EXPECT_EQ(rib_->getBestPath(kV4Prefix1), nullptr);
    EXPECT_EQ(rib_->getBestPath(kV4Prefix2), nullptr);
    // kV4Prefix3 should still be present
    EXPECT_NE(rib_->getBestPath(kV4Prefix3), nullptr);
  }
}

/*
 * Test that sending a NexthopResolutionUpdate with a nexthop that has no
 * conditional routes is a no-op
 */
TEST_F(RibWithLocalRouteFixture, ConditionalLocalRoute_UnknownNexthopNoOp) {
  setUpRibAndFib(getConditionalLocalRoutes());
  rib_->setFibBatchTime(milliseconds(8));

  // Send EOR
  {
    EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _)).Times(0);
    EXPECT_CALL(*fib_, program_(true)).Times(1);

    auto fibFuture = fib_->getFibProgramFuture();
    sendInitialPathComputation();
    fibFuture.wait();
  }

  /*
   * Resolve a nexthop (kV4Nexthop2) that has no conditional routes
   * This should be a no-op — no routes announced
   */
  {
    // No FIB programming expected since no routes change
    EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _)).Times(0);

    std::vector<folly::IPAddress> resolved{kV4Nexthop2};
    std::vector<folly::IPAddress> unresolved{};
    sendNexthopResolutionUpdate(
        NexthopResolutionUpdate(std::move(resolved), std::move(unresolved)));

    // No routes should be in RIB
    EXPECT_EQ(rib_->getBestPath(kV4Prefix1), nullptr);
  }
}

// This checks we set EBGP/Local flag correctly for incoming EBGP announcements
TEST_F(RibFixture, EbgpRoute) {
  rib_->setFibBatchTime(milliseconds(2));

  auto prefix1 = folly::IPAddress::createNetwork("1::/64");
  auto prefixBatch1 = PrefixPathIds{{prefix1, kDefaultPathID}};

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(prefix1), _, _, _, _, _)).Times(1);
  EXPECT_CALL(*fib_, program_(true)).Times(1);

  auto fibFuture = fib_->getFibProgramFuture();
  // send route from eBGP Peer
  sendAnnouncement(prefixBatch1, eBgpPeer1_, attr_);
  sendInitialPathComputation();
  fibFuture.wait();

  // eBGP flag should be true
  auto eBgpPath = rib_->getBestPath(prefix1);
  EXPECT_NE(eBgpPath, nullptr);
  if (eBgpPath) {
    EXPECT_TRUE(eBgpPath->getIsRouteExternal());
    EXPECT_FALSE(eBgpPath->getIsRouteConfedExternal());
    EXPECT_FALSE(eBgpPath->getIsRouteLocal());
  }
}

TEST_F(RibFixture, RibInAddPath) {
  rib_->setFibBatchTime(milliseconds(2));

  auto prefix1 = folly::IPAddress::createNetwork("1::/64");
  auto prefixBatch1 = PrefixPathIds{{prefix1, 0}};

  /*
   * send first announcement
   * prefix: prefixBatch1
   * nexthop: kV4Nexthop1
   * peer: eBgpPeer1_
   */
  auto fibFuture = fib_->getFibProgramFuture();
  // send route from eBGP Peer
  sendAnnouncement(prefixBatch1, eBgpPeer1_, attr_);
  sendInitialPathComputation();
  fibFuture.wait();

  // eBGP flag should be true
  auto multiPath = rib_->getMultipath(prefix1);
  EXPECT_EQ(multiPath.size(), 1);
  EXPECT_EQ(multiPath.begin()->second->attrs->getNexthop(), kV4Nexthop1);

  /*
   * send second announcement, just changed nexthop
   * prefix: prefixBatch1
   * nexthop: kV4Nexthop2
   * peer: eBgpPeer1_
   * verify new path has been added into multipaths.
   */
  auto attrFields = buildBgpPathFields(4, 4, 4, 4);
  attrFields->nexthop = kV4Nexthop2;
  // change clustlist length to do the tie breaking for best path

  auto mutableAttrs = attrFields->attrs.get();
  auto newClusterList = mutableAttrs.clusterList.get();
  newClusterList.emplace_back(0x0a0a0a0b);
  mutableAttrs.clusterList = std::move(newClusterList);
  attrFields->attrs = std::move(mutableAttrs);

  auto newAttr = std::make_shared<facebook::bgp::BgpPath>(*attrFields);
  newAttr->publish();

  fibFuture = fib_->getFibProgramFuture();
  // announce with different path ID
  auto prefixBatch2 = PrefixPathIds{{prefix1, 1}};
  sendAnnouncement(prefixBatch2, eBgpPeer1_, newAttr);
  fibFuture.wait();

  multiPath = rib_->getMultipath(prefix1);
  EXPECT_EQ(multiPath.size(), 2);
  std::unordered_set<folly::IPAddress> nhsetActual;
  for (const auto& it : multiPath) {
    nhsetActual.insert(it.second->attrs->getNexthop());
  }
  std::unordered_set<folly::IPAddress> nhsetExpect({kV4Nexthop1, kV4Nexthop2});
  EXPECT_EQ(nhsetActual, nhsetExpect);
  auto routeInfos = rib_->ribEntries_.find(prefix1)->second.getRouteInfos(
      BgpPeerId{eBgpPeer1_.addr, eBgpPeer1_.routerId});
  EXPECT_NE(routeInfos.find(0), routeInfos.end());
  EXPECT_NE(routeInfos.find(1), routeInfos.end());

  /*
   * send withdraw announcement
   * prefix: prefixBatch1
   * nexthop: kV4Nexthop1
   * peer: eBgpPeer1_
   * verify only path with kV4Nexthop1 exist.
   */
  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(prefixBatch1, eBgpPeer1_);
  fibFuture.wait();
  multiPath = rib_->getMultipath(prefix1);
  EXPECT_EQ(multiPath.size(), 1);
  EXPECT_EQ(multiPath.begin()->second->attrs->getNexthop(), kV4Nexthop2);
  auto routeInfos2 = rib_->ribEntries_.find(prefix1)->second.getRouteInfos(
      BgpPeerId{eBgpPeer1_.addr, eBgpPeer1_.routerId});
  EXPECT_EQ(routeInfos2.find(0), routeInfos.end());
  EXPECT_NE(routeInfos2.find(1), routeInfos.end());
}

/*
 * This checks we set EBGP/Local flag correctly for incoming Confed EBGP
 * announcements
 */
TEST_F(RibFixture, ConfedEbgpRoute) {
  rib_->setFibBatchTime(milliseconds(2));

  auto prefix1 = folly::IPAddress::createNetwork("1::/64");
  auto prefixBatch1 = PrefixPathIds{{prefix1, kDefaultPathID}};

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(prefix1), _, _, _, _, _)).Times(1);
  EXPECT_CALL(*fib_, program_(true)).Times(1);

  auto fibFuture = fib_->getFibProgramFuture();
  // send route from confed eBGP Peer
  sendAnnouncement(prefixBatch1, confedEBgpPeer_, attr_);
  sendInitialPathComputation();
  fibFuture.wait();

  // eBGP flag should be true
  auto path = rib_->getBestPath(prefix1);
  EXPECT_NE(path, nullptr);
  if (path) {
    EXPECT_FALSE(path->getIsRouteExternal());
    EXPECT_TRUE(path->getIsRouteConfedExternal());
    EXPECT_FALSE(path->getIsRouteLocal());
  }
}

// This checks we set EBGP/Local flag correctly for incoming IBGP announcements
TEST_F(RibFixture, IbgpRoute) {
  rib_->setFibBatchTime(milliseconds(2));

  auto prefix1 = folly::IPAddress::createNetwork("1::/64");
  auto prefixBatch1 = PrefixPathIds{{prefix1, kDefaultPathID}};

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(prefix1), _, _, _, _, _)).Times(1);
  EXPECT_CALL(*fib_, program_(true)).Times(1);

  auto fibFuture = fib_->getFibProgramFuture();
  // send route from iBgpPeer_
  sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
  sendInitialPathComputation();
  fibFuture.wait();

  // both EBGP and Local flag should be false
  auto iBgpPath = rib_->getBestPath(prefix1);
  EXPECT_NE(iBgpPath, nullptr);
  if (iBgpPath) {
    EXPECT_FALSE(iBgpPath->getIsRouteExternal());
    EXPECT_FALSE(iBgpPath->getIsRouteConfedExternal());
    EXPECT_FALSE(iBgpPath->getIsRouteLocal());
  }
}

// This checks we set EBGP/Local flag correctly for local route announcements
TEST_F(RibFixture, LocalRoute) {
  rib_->setFibBatchTime(milliseconds(2));

  auto prefix1 = folly::IPAddress::createNetwork("1::/64");
  auto prefixBatch1 = PrefixPathIds{{prefix1, kDefaultPathID}};

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(prefix1), _, _, _, _, _)).Times(1);
  EXPECT_CALL(*fib_, program_(true)).Times(1);

  auto fibFuture = fib_->getFibProgramFuture();
  // send route from localPeer_
  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr->setNexthop(kLocalRouteV4Nexthop);
  attr->publish();
  sendAnnouncement(prefixBatch1, localPeer_, attr);
  sendInitialPathComputation();
  fibFuture.wait();

  // Local flag should be true
  auto localPath = rib_->getBestPath(prefix1);
  EXPECT_NE(localPath, nullptr);
  if (localPath) {
    EXPECT_FALSE(localPath->getIsRouteExternal());
    EXPECT_FALSE(localPath->getIsRouteConfedExternal());
    EXPECT_TRUE(localPath->getIsRouteLocal());
  }
}

TEST_F(RibFixture, GetRibEntries) {
  // make 2 attrs with different nexthops, attrs2 has higher local preferece
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs2->setNexthop(kV4Nexthop2);
  attrs2->setLocalPref(kLocalPref2);
  attrs1->publish();
  attrs2->publish();

  /*
   * add 2 v4 entry and 1 v6 entry to rib
   * entry1 has 2 peers, p1
   * entry2 has 2 peers, p1 & p2
   * entry3 has 2 peers, p1 & p2
   */
  RibEntry entry1(kV4Prefix1);
  EXPECT_EQ(entry1.getMultipathNexthopsStr(), "[]");
  entry1.updatePath(eBgpPeer1_, attrs1, false); // p1 has nexthop kV4Nexthop1
  RibBase::selectBestPath(
      entry1, multipathSelector, bestpathSelector, false, 0);
  EXPECT_EQ(entry1.getMultipathNexthopsStr(), "[(11.0.0.1:0)]");

  RibEntry entry2(kV4Prefix2);
  entry2.updatePath(
      eBgpPeer1_, attrs1, false); // p1 in this entry has nexthop kV4Nexthop1
  entry2.updatePath(
      eBgpPeer2_, attrs2, false); // p2 in this entry has nexthop kV4Nexthop2
  RibBase::selectBestPath(
      entry2, multipathSelector, bestpathSelector, false, 0);

  RibEntry entry3(kV6Prefix1);
  entry3.updatePath(
      eBgpPeer1_, attrs1, false); // p1 in this entry has nexthop kV4Nexthop1
  entry3.updatePath(
      eBgpPeer2_, attrs2, false); // p2 in this entry has nexthop kV4Nexthop2
  RibBase::selectBestPath(
      entry3, multipathSelector, bestpathSelector, false, 0);

  // not putting v6 entry in entries for now
  rib_->ribEntries_.emplace(make_pair(kV4Prefix1, std::move(entry1)));
  rib_->ribEntries_.emplace(make_pair(kV4Prefix2, std::move(entry2)));

  auto output1 = rib_->getRibEntries(TBgpAfi::AFI_IPV4);
  auto output2 = rib_->getRibEntries(TBgpAfi::AFI_IPV6);

  EXPECT_THAT(
      output1,
      UnorderedElementsAre(
          rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix1)),
          rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix2))));
  EXPECT_EQ(output2.size(), 0);

  /*
   * verify the content of the returned TRibEntry objects
   * compare 1st element in output1
   */
  bool firstIsPrefix1 = false;
  if ((*output1[0].prefix()->prefix_bin() ==
       network::toBinaryAddress(kV4Prefix1.first).addr()->toStdString()) &&
      (*output1[0].prefix()->num_bits() == kV4Prefix1.second)) {
    firstIsPrefix1 = true;
  }

  {
    // get entry for kV4Prefix1
    auto entry = firstIsPrefix1 ? output1[0] : output1[1];
    EXPECT_EQ(TBgpAfi::AFI_IPV4, *entry.prefix()->afi());

    // paths
    auto p1Nexthop = network::toBinaryAddress(kV4Nexthop1);
    auto paths = entry.paths()->find(kBestPathGroup)->second;
    EXPECT_EQ(1, paths.size());
    EXPECT_EQ(
        p1Nexthop.addr()->toStdString(), *paths[0].next_hop()->prefix_bin());

    // local preference
    EXPECT_EQ(kLocalPref, *paths[0].local_pref());

    // best_next_hop
    EXPECT_EQ(
        p1Nexthop.addr()->toStdString(), *entry.best_next_hop()->prefix_bin());

    // is_best_path
    EXPECT_TRUE(paths[0].is_best_path().has_value());
    EXPECT_TRUE(*paths[0].is_best_path());

    // best_path: top-level convenience copy of the selected bestpath
    ASSERT_TRUE(entry.best_path().has_value());
    EXPECT_EQ(
        p1Nexthop.addr()->toStdString(),
        *entry.best_path()->next_hop()->prefix_bin());
    EXPECT_EQ(kLocalPref, *entry.best_path()->local_pref());
    ASSERT_TRUE(entry.best_path()->is_best_path().has_value());
    EXPECT_TRUE(*entry.best_path()->is_best_path());
    EXPECT_EQ(paths[0], *entry.best_path());
  }
  {
    // get entry for kV4Prefix2
    auto entry = firstIsPrefix1 ? output1[1] : output1[0];
    EXPECT_EQ(TBgpAfi::AFI_IPV4, *entry.prefix()->afi());

    // paths
    auto p1Nexthop = network::toBinaryAddress(kV4Nexthop1);
    auto p2Nexthop = network::toBinaryAddress(kV4Nexthop2);

    /*
     * As path pointing to p2Nexthop has a better local-pref
     * this will be selected in the bestPathGroup and other entry
     * will be part of the default group.
     */
    auto paths = entry.paths()->find(kBestPathGroup)->second;
    EXPECT_EQ(1, paths.size());
    EXPECT_THAT(
        *paths[0].next_hop()->prefix_bin(), p2Nexthop.addr()->toStdString());
    EXPECT_THAT(*paths[0].local_pref(), kLocalPref2);
    // is_best_path
    EXPECT_TRUE(paths[0].is_best_path().has_value());
    EXPECT_TRUE(*paths[0].is_best_path());

    // best_path: top-level convenience copy of the selected bestpath
    ASSERT_TRUE(entry.best_path().has_value());
    EXPECT_EQ(
        p2Nexthop.addr()->toStdString(),
        *entry.best_path()->next_hop()->prefix_bin());
    EXPECT_EQ(kLocalPref2, *entry.best_path()->local_pref());
    EXPECT_EQ(paths[0], *entry.best_path());

    paths = entry.paths()->find(kDefaultPathGroup)->second;
    EXPECT_EQ(1, paths.size());
    EXPECT_THAT(
        *paths[0].next_hop()->prefix_bin(), p1Nexthop.addr()->toStdString());
    EXPECT_THAT(*paths[0].local_pref(), kLocalPref);
    // best_next_hop
    EXPECT_EQ(
        p2Nexthop.addr()->toStdString(), *entry.best_next_hop()->prefix_bin());
  }

  // putting v6 entry to RIB
  rib_->ribEntries_.emplace(make_pair(kV6Prefix1, std::move(entry3)));
  output2 = rib_->getRibEntries(TBgpAfi::AFI_IPV6);
  EXPECT_THAT(
      output2,
      UnorderedElementsAre(
          rib_->createTRibEntry(*rib_->ribEntries_.find(kV6Prefix1))));
  {
    // get entry for kV6Prefix1
    auto entry = output2[0];
    EXPECT_EQ(TBgpAfi::AFI_IPV6, *entry.prefix()->afi());
    auto binAddr = network::toBinaryAddress(kV6Prefix1.first);
    EXPECT_EQ(binAddr.addr()->toStdString(), *entry.prefix()->prefix_bin());
    auto len = kV6Prefix1.second;
    EXPECT_EQ(len, *entry.prefix()->num_bits());

    // paths
    auto p1Nexthop = network::toBinaryAddress(kV4Nexthop1);
    auto p2Nexthop = network::toBinaryAddress(kV4Nexthop2);
    auto paths = entry.paths()->find(kBestPathGroup)->second;
    /*
     * As path pointing to p2Nexthop has a better local-pref
     * this will be selected in the bestPathGroup and other entry
     * will be part of the default group.
     */
    EXPECT_EQ(1, paths.size());
    EXPECT_THAT(
        *paths[0].next_hop()->prefix_bin(), p2Nexthop.addr()->toStdString());
    EXPECT_THAT(*paths[0].local_pref(), kLocalPref2);
    paths = entry.paths()->find(kDefaultPathGroup)->second;
    EXPECT_EQ(1, paths.size());
    EXPECT_THAT(
        *paths[0].next_hop()->prefix_bin(), p1Nexthop.addr()->toStdString());
    EXPECT_THAT(*paths[0].local_pref(), kLocalPref);
  }
}

TEST_F(RibFixture, LocalRouteIgpCostSerialization) {
  auto attrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs->publish();

  RibEntry entry(kV4Prefix1);
  entry.updatePath(kV4LocalPeerInfo, attrs, false);
  RibBase::selectBestPath(entry, multipathSelector, bestpathSelector, false, 0);
  rib_->ribEntries_.emplace(kV4Prefix1, std::move(entry));

  const auto expectedIgpCost =
      static_cast<int64_t>(std::numeric_limits<uint32_t>::max());
  const auto legacy = rib_->getRibEntries(TBgpAfi::AFI_IPV4);
  ASSERT_EQ(legacy.size(), 1);
  const auto& legacyPaths = legacy[0].paths()->at(kBestPathGroup);
  ASSERT_EQ(legacyPaths.size(), 1);
  ASSERT_TRUE(legacyPaths[0].igp_cost().has_value());
  EXPECT_EQ(*legacyPaths[0].igp_cost(), expectedIgpCost);

  const auto canonical = rib_->getRibEntriesCanonical(TBgpAfi::AFI_IPV4);
  ASSERT_EQ(canonical.rib_entries()->size(), 1);
  const auto& canonicalEntry = canonical.rib_entries()->at(
      folly::IPAddress::networkToString(kV4Prefix1));
  const auto& canonicalPaths =
      canonicalEntry.paths()->at(std::string(kBestPathGroup));
  ASSERT_EQ(canonicalPaths.size(), 1);
  ASSERT_TRUE(canonicalPaths[0].igp_cost().has_value());
  EXPECT_EQ(*canonicalPaths[0].igp_cost(), expectedIgpCost);
}

/*
 * getRibEntriesCanonical returns the same logical RIB state as getRibEntries,
 * but in deduplicated form: paths are interned by address, peers by (addr,
 * routerId), and list-valued sub-attrs (AS_PATH, communities, etc.) are
 * factored into a shared dict.
 */
TEST_F(RibFixture, GetRibEntriesCanonical) {
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs2->setNexthop(kV4Nexthop2);
  attrs2->setLocalPref(kLocalPref2);
  attrs1->publish();
  attrs2->publish();

  /* Build the same RIB as the legacy GetRibEntries test */
  RibEntry entry1(kV4Prefix1);
  entry1.updatePath(eBgpPeer1_, attrs1, false);
  RibBase::selectBestPath(
      entry1, multipathSelector, bestpathSelector, false, 0);

  RibEntry entry2(kV4Prefix2);
  entry2.updatePath(eBgpPeer1_, attrs1, false);
  entry2.updatePath(eBgpPeer2_, attrs2, false);
  RibBase::selectBestPath(
      entry2, multipathSelector, bestpathSelector, false, 0);

  RibEntry entry3(kV6Prefix1);
  entry3.updatePath(eBgpPeer1_, attrs1, false);
  entry3.updatePath(eBgpPeer2_, attrs2, false);
  RibBase::selectBestPath(
      entry3, multipathSelector, bestpathSelector, false, 0);

  rib_->ribEntries_.emplace(make_pair(kV4Prefix1, std::move(entry1)));
  rib_->ribEntries_.emplace(make_pair(kV4Prefix2, std::move(entry2)));
  rib_->ribEntries_.emplace(make_pair(kV6Prefix1, std::move(entry3)));

  /* Fetch both legacy and canonical */
  auto legacy = rib_->getRibEntries(TBgpAfi::AFI_IPV4);
  auto canonical = rib_->getRibEntriesCanonical(TBgpAfi::AFI_IPV4);

  /* Same prefix set */
  EXPECT_EQ(legacy.size(), 2);
  EXPECT_EQ(canonical.rib_entries()->size(), 2);
  EXPECT_TRUE(canonical.rib_entries()->contains(
      folly::IPAddress::networkToString(kV4Prefix1)));
  EXPECT_TRUE(canonical.rib_entries()->contains(
      folly::IPAddress::networkToString(kV4Prefix2)));

  /*
   * Deduplication: attrs1 is shared across both prefixes and appears under
   * two different peers in entry2, so we expect 2 distinct whole-path entries
   * (different next_hop: attrs1 has kV4Nexthop1, attrs2 has kV4Nexthop2) but
   * shared AS_PATH / communities.
   */
  EXPECT_EQ(canonical.deduped_paths()->size(), 2);
  EXPECT_EQ(canonical.attr_dict()->as_path_lists()->size(), 1);
  EXPECT_EQ(canonical.attr_dict()->community_lists()->size(), 1);
  EXPECT_EQ(canonical.peers()->size(), 2);

  /*
   * Verify entry1: single best path with kV4Nexthop1, local_pref kLocalPref
   */
  const auto& e1 = canonical.rib_entries()->at(
      folly::IPAddress::networkToString(kV4Prefix1));
  EXPECT_EQ(
      *e1.prefix()->prefix_bin(),
      *network::toBinaryAddress(kV4Prefix1.first).addr());
  EXPECT_EQ(e1.prefix()->num_bits().value(), kV4Prefix1.second);
  /*
   * The get-rib converter leaves the FSDB-only top-level best_path unset; the
   * selected path is the is_best_path-marked entry in the best-path group.
   */
  EXPECT_FALSE(e1.best_path().has_value());
  const auto& e1Best = e1.paths()->at(std::string(kBestPathGroup));
  ASSERT_EQ(e1Best.size(), 1);
  EXPECT_TRUE(e1Best[0].is_best_path().value());
  auto pathIdx1 = e1Best[0].path_idx().value();
  const auto& dedupPath1 = canonical.deduped_paths()->at(pathIdx1);
  EXPECT_EQ(
      *dedupPath1.next_hop()->prefix_bin(),
      *network::toBinaryAddress(kV4Nexthop1).addr());
  EXPECT_EQ(dedupPath1.local_pref().value(), kLocalPref);

  /*
   * Verify entry2: best path has kV4Nexthop2 (higher local_pref), one default
   * path with kV4Nexthop1
   */
  const auto& e2 = canonical.rib_entries()->at(
      folly::IPAddress::networkToString(kV4Prefix2));
  EXPECT_FALSE(e2.best_path().has_value());
  const auto& bestGroup = e2.paths()->at(std::string(kBestPathGroup));
  const auto& defaultGroup = e2.paths()->at(std::string(kDefaultPathGroup));
  EXPECT_EQ(bestGroup.size(), 1);
  EXPECT_EQ(defaultGroup.size(), 1);
  EXPECT_TRUE(bestGroup[0].is_best_path().value());
  auto pathIdx2Best = bestGroup[0].path_idx().value();
  const auto& dedupPath2Best = canonical.deduped_paths()->at(pathIdx2Best);
  EXPECT_EQ(
      *dedupPath2Best.next_hop()->prefix_bin(),
      *network::toBinaryAddress(kV4Nexthop2).addr());
  EXPECT_EQ(dedupPath2Best.local_pref().value(), kLocalPref2);

  auto pathIdxDefault = defaultGroup[0].path_idx().value();
  const auto& dedupPathDefault = canonical.deduped_paths()->at(pathIdxDefault);
  EXPECT_EQ(
      *dedupPathDefault.next_hop()->prefix_bin(),
      *network::toBinaryAddress(kV4Nexthop1).addr());
  EXPECT_EQ(dedupPathDefault.local_pref().value(), kLocalPref);

  /* Shared sub-attr dedup: both paths point to the same AS_PATH / community idx
   */
  EXPECT_EQ(
      dedupPath1.as_path_idx().value(), dedupPath2Best.as_path_idx().value());
  EXPECT_EQ(
      dedupPath1.as_path_idx().value(), dedupPathDefault.as_path_idx().value());
  EXPECT_EQ(
      dedupPath1.communities_idx().value(),
      dedupPath2Best.communities_idx().value());

  /* AFI_IPV6 returns only the v6 entry */
  auto canonicalV6 = rib_->getRibEntriesCanonical(TBgpAfi::AFI_IPV6);
  EXPECT_EQ(canonicalV6.rib_entries()->size(), 1);
  EXPECT_TRUE(canonicalV6.rib_entries()->contains(
      folly::IPAddress::networkToString(kV6Prefix1)));
}

/*
 * The full builder labels best_group only when a best path was actually
 * selected: a resolved entry gets best_group == "best" (with a "best" group in
 * paths), while an entry with no best path leaves best_group empty and no
 * "best" group -- so best_group reads as a presence signal, not a constant.
 */
TEST_F(RibFixture, CreateTRibEntryBestGroupReflectsBestPathPresence) {
  // Entry WITH a selected best path -> best_group == "best".
  auto attrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs->publish();
  RibEntry withBest(kV4Prefix1);
  withBest.updatePath(eBgpPeer1_, attrs, false);
  RibBase::selectBestPath(
      withBest, multipathSelector, bestpathSelector, false, 0);
  rib_->ribEntries_.emplace(make_pair(kV4Prefix1, std::move(withBest)));

  auto resolved = rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix1));
  EXPECT_EQ(kBestPathGroup, *resolved.best_group());
  EXPECT_NE(resolved.paths()->end(), resolved.paths()->find(kBestPathGroup));

  // Entry with NO best path -> best_group empty, no "best" group in paths.
  RibEntry noBest(kV4Prefix2);
  rib_->ribEntries_.emplace(make_pair(kV4Prefix2, std::move(noBest)));

  auto unresolved = rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix2));
  EXPECT_TRUE(unresolved.best_group()->empty());
  EXPECT_EQ(
      unresolved.paths()->end(), unresolved.paths()->find(kBestPathGroup));
}

TEST_F(RibFixture, UpdateEntryStatsTest) {
  RibEntry entry1(kV4Prefix1);
  RibEntry entry2(kV4Prefix2);
  RibEntry entry3(kV6Prefix1);

  rib_->ribEntries_.emplace(make_pair(kV4Prefix1, std::move(entry1)));
  rib_->ribEntries_.emplace(make_pair(kV4Prefix2, std::move(entry2)));
  rib_->ribEntries_.emplace(make_pair(kV6Prefix1, std::move(entry3)));

  uint32_t expectedPathId = 135;
  PrefixPathIds prefixBatch{{kV4Prefix1, expectedPathId}};
  sendAnnouncement(prefixBatch, iBgpPeer_, attr_);

  TEntryStats stats;
  rib_->updateEntryStats(stats);

  uint64_t totalRibPaths = 0;
  for (const auto& [_, ribEntry] : rib_->ribEntries_) {
    totalRibPaths += ribEntry.getAllPathsCnt();
  }

  EXPECT_EQ(*stats.total_ucast_routes(), rib_->ribEntries_.size());
  EXPECT_EQ(*stats.total_originated_routes(), rib_->localRoutes_.size());
  EXPECT_EQ(*stats.total_rib_paths(), totalRibPaths);
}

TEST_F(RibFixture, GetRibEntriesWithCommunityFilter) {
  /*
   * make 2 attrs with different nexthops, attrs2 has higher local preferece
   * attr1 has 4 communities 65530:15800 to 65530:15803 where-as
   * attr2 has 3 communities 65530:15800 to 65530:15802
   * attr2 wins best-path and attr1 is not selected in ecmp.
   */
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 3, 4, 4));
  attrs2->setNexthop(kV4Nexthop2);
  attrs2->setLocalPref(kLocalPref2);
  attrs1->publish();
  attrs2->publish();

  /*
   * add 2 v4 entry and 1 v6 entry to rib
   * entry1 has 1 peer, p2
   * entry2 has 2 peers, p1 & p2
   * entry3 has 2 peers, p1 & p2
   */
  RibEntry entry1(kV4Prefix1);
  entry1.updatePath(eBgpPeer1_, attrs2, false); // p1 has nexthop kV4Nexthop1
  RibBase::selectBestPath(
      entry1, multipathSelector, bestpathSelector, false, 0);

  RibEntry entry2(kV4Prefix2);
  entry2.updatePath(
      eBgpPeer1_, attrs1, false); // p1 in this entry has nexthop kV4Nexthop1
  entry2.updatePath(
      eBgpPeer2_, attrs2, false); // p2 in this entry has nexthop kV4Nexthop2
  RibBase::selectBestPath(
      entry2, multipathSelector, bestpathSelector, false, 0);

  RibEntry entry3(kV6Prefix1);
  entry3.updatePath(
      eBgpPeer1_, attrs1, false); // p1 in this entry has nexthop kV4Nexthop1
  entry3.updatePath(
      eBgpPeer2_, attrs2, false); // p2 in this entry has nexthop kV4Nexthop2
  RibBase::selectBestPath(
      entry3, multipathSelector, bestpathSelector, false, 0);

  // not putting v6 entry (entry3) in entries for now
  rib_->ribEntries_.emplace(make_pair(kV4Prefix1, std::move(entry1)));
  rib_->ribEntries_.emplace(make_pair(kV4Prefix2, std::move(entry2)));

  /*
   * With a matched filter for community 65530:15800
   * we should see both rib entries returned.
   */
  auto output1 = rib_->getRibEntriesForCommunities(
      TBgpAfi::AFI_IPV4, {nettools::bgplib::BgpAttrCommunityC(65530, 15800)});
  EXPECT_THAT(
      output1,
      UnorderedElementsAre(
          rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix1)),
          rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix2))));

  // With thrift API it should give the same output as direct api call
  auto communitiesStr = std::make_unique<std::vector<std::string>>();
  communitiesStr->emplace_back("65530:15800");
  auto thriftBatchOutput =
      folly::coro::blockingWait(service_->co_getRibEntriesForCommunities(
          TBgpAfi::AFI_IPV4, std::move(communitiesStr)));
  EXPECT_EQ(*thriftBatchOutput, output1);

  // Thrift batch and singleton API should return the same output
  auto communityStr = std::make_unique<std::string>("65530:15800");
  auto thriftSingletonOutput =
      folly::coro::blockingWait(service_->co_getRibEntriesForCommunity(
          TBgpAfi::AFI_IPV4, std::move(communityStr)));
  EXPECT_EQ(*thriftSingletonOutput, *thriftBatchOutput);

  // With an unmatched community filter we should not see any returned entries.
  auto output2 = rib_->getRibEntriesForCommunities(
      TBgpAfi::AFI_IPV4, {nettools::bgplib::BgpAttrCommunityC(0xF004, 0xABCD)});
  EXPECT_EQ(output2.size(), 0);

  /*
   * With an unmatched community and a matched one, we should see non-empty
   * result.
   */
  auto output3 = rib_->getRibEntriesForCommunities(
      TBgpAfi::AFI_IPV4,
      {nettools::bgplib::BgpAttrCommunityC(0xF004, 0xABCD),
       nettools::bgplib::BgpAttrCommunityC(65530, 15803)});
  EXPECT_EQ(output3.size(), 1);

  /*
   * With a matched filter for communities [65530:15802, 65530:15803]
   * we should see both rib entries returned
   */
  auto output4 = rib_->getRibEntriesForCommunities(
      TBgpAfi::AFI_IPV4,
      {nettools::bgplib::BgpAttrCommunityC(65530, 15802),
       nettools::bgplib::BgpAttrCommunityC(65530, 15803)});
  EXPECT_THAT(
      output4,
      UnorderedElementsAre(
          rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix1)),
          rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix2))));

  // With an emtpy communities list, no entry should return
  auto output5 = rib_->getRibEntriesForCommunities(TBgpAfi::AFI_IPV4, {});
  EXPECT_EQ(output5.size(), 0);

  // verify the content of the returned TRibEntry objects
  bool firstIsPrefix1 = false;
  if ((*output1[0].prefix()->prefix_bin() ==
       network::toBinaryAddress(kV4Prefix1.first).addr()->toStdString()) &&
      (*output1[0].prefix()->num_bits() == kV4Prefix1.second)) {
    firstIsPrefix1 = true;
  }

  // verify the communities
  auto verifyCommunities =
      [](std::vector<TBgpCommunity> thriftComms,
         std::initializer_list<std::pair<uint16_t, uint16_t>>
             expectedCommList) {
        EXPECT_EQ(thriftComms.size(), expectedCommList.size());
        std::vector<nettools::bgplib::BgpAttrCommunityC> returnedComms;
        for (auto thriftComm : thriftComms) {
          returnedComms.emplace_back(
              static_cast<uint16_t>(*thriftComm.asn()),
              static_cast<uint16_t>(*thriftComm.value()));
        }
        std::vector<nettools::bgplib::BgpAttrCommunityC> expectedComms;
        for (auto expectedComm : expectedCommList) {
          expectedComms.emplace_back(expectedComm.first, expectedComm.second);
        }
        EXPECT_EQ(returnedComms, expectedComms);
      };

  auto verifyV4Entry1 = [verifyCommunities](const TRibEntry& entry) {
    EXPECT_EQ(TBgpAfi::AFI_IPV4, *entry.prefix()->afi());
    // paths
    auto p2Nexthop = network::toBinaryAddress(kV4Nexthop2);
    auto paths = entry.paths()->find(kBestPathGroup)->second;
    EXPECT_EQ(1, paths.size());
    EXPECT_EQ(
        p2Nexthop.addr()->toStdString(), *paths[0].next_hop()->prefix_bin());
    // local preference
    EXPECT_EQ(kLocalPref2, *paths[0].local_pref());
    // best_next_hop
    EXPECT_EQ(
        p2Nexthop.addr()->toStdString(), *entry.best_next_hop()->prefix_bin());
    verifyCommunities(
        paths[0].communities().value_or({}),
        {{65530, 15800}, {65530, 15801}, {65530, 15802}});
    // best_path mirrors paths[0] in the bestPath group
    ASSERT_TRUE(entry.best_path().has_value());
    EXPECT_EQ(paths[0], *entry.best_path());
  };
  /*
   * Lambda to verify a ribentry and associated attributes.
   * param bestPathPresent controls whether the rib-entry is expected
   * to have bestPath selected as part of community filtered rib-entries
   * or not.
   */
  auto verifyV4Entry2 = [verifyCommunities](
                            const TRibEntry& entry,
                            bool bestPathPresent = true) {
    EXPECT_EQ(TBgpAfi::AFI_IPV4, *entry.prefix()->afi());

    // paths
    auto p1Nexthop = network::toBinaryAddress(kV4Nexthop1);
    auto p2Nexthop = network::toBinaryAddress(kV4Nexthop2);

    /*
     * As path pointing to p2Nexthop has a better local-pref
     * this will be selected in the bestPathGroup and other entry
     * will be part of the default group.
     */
    std::vector<TBgpPath> paths;
    if (bestPathPresent) {
      paths = entry.paths()->find(kBestPathGroup)->second;
      EXPECT_EQ(1, paths.size());
      EXPECT_THAT(
          *paths[0].next_hop()->prefix_bin(), p2Nexthop.addr()->toStdString());
      EXPECT_THAT(*paths[0].local_pref(), kLocalPref2);
      verifyCommunities(
          paths[0].communities().value_or({}),
          {{65530, 15800}, {65530, 15801}, {65530, 15802}});
      // is_best_path
      EXPECT_TRUE(paths[0].is_best_path().has_value());
      EXPECT_TRUE(*paths[0].is_best_path());
      // best_path mirrors paths[0] in the bestPath group
      ASSERT_TRUE(entry.best_path().has_value());
      EXPECT_EQ(paths[0], *entry.best_path());
    } else {
      EXPECT_EQ(entry.paths()->find(kBestPathGroup), entry.paths()->end());
      /*
       * When no path is in the bestPath group (e.g. filtered out by
       * community filter), best_path must also be unset.
       */
      EXPECT_FALSE(entry.best_path().has_value());
    }
    paths = entry.paths()->find(kDefaultPathGroup)->second;
    EXPECT_EQ(1, paths.size());
    EXPECT_THAT(
        *paths[0].next_hop()->prefix_bin(), p1Nexthop.addr()->toStdString());
    EXPECT_THAT(*paths[0].local_pref(), kLocalPref);
    verifyCommunities(
        paths[0].communities().value_or({}),
        {{65530, 15800}, {65530, 15801}, {65530, 15802}, {65530, 15803}});
    // best_next_hop
    EXPECT_EQ(
        p2Nexthop.addr()->toStdString(), *entry.best_next_hop()->prefix_bin());
  };

  // match entry for kV4Prefix1
  verifyV4Entry1(firstIsPrefix1 ? output1[0] : output1[1]);
  // match entry for kV4Prefix2
  verifyV4Entry2(firstIsPrefix1 ? output1[1] : output1[0]);

  // putting v6 entry to RIB
  rib_->ribEntries_.emplace(make_pair(kV6Prefix1, std::move(entry3)));
  output2 = rib_->getRibEntriesForCommunities(
      TBgpAfi::AFI_IPV6, {nettools::bgplib::BgpAttrCommunityC(65530, 15800)});
  EXPECT_THAT(
      output2,
      UnorderedElementsAre(
          rib_->createTRibEntry(*rib_->ribEntries_.find(kV6Prefix1))));
  /*
   * Lambda to verify a ribentry and associated attributes.
   * param bestPathPresent controls whether the rib-entry is expected
   * to have bestPath selected as part of community filtered rib-entries
   * or not.
   */
  auto verifyV6Entry = [verifyCommunities](
                           const TRibEntry& entry,
                           bool bestPathPresent = true) {
    // get entry for kV6Prefix1
    EXPECT_EQ(TBgpAfi::AFI_IPV6, *entry.prefix()->afi());
    auto binAddr = network::toBinaryAddress(kV6Prefix1.first);
    EXPECT_EQ(binAddr.addr()->toStdString(), *entry.prefix()->prefix_bin());
    auto len = kV6Prefix1.second;
    EXPECT_EQ(len, *entry.prefix()->num_bits());

    // paths
    auto p1Nexthop = network::toBinaryAddress(kV4Nexthop1);
    auto p2Nexthop = network::toBinaryAddress(kV4Nexthop2);
    std::vector<TBgpPath> paths;
    /*
     * As path pointing to p2Nexthop has a better local-pref
     * this will be selected in the bestPathGroup and other entry
     * will be part of the default group.
     */
    if (bestPathPresent) {
      paths = entry.paths()->find(kBestPathGroup)->second;
      EXPECT_EQ(1, paths.size());
      EXPECT_THAT(
          *paths[0].next_hop()->prefix_bin(), p2Nexthop.addr()->toStdString());
      EXPECT_THAT(*paths[0].local_pref(), kLocalPref2);
      verifyCommunities(
          paths[0].communities().value_or({}),
          {{65530, 15800}, {65530, 15801}, {65530, 15802}});
    } else {
      EXPECT_EQ(entry.paths()->find(kBestPathGroup), entry.paths()->end());
    }
    paths = entry.paths()->find(kDefaultPathGroup)->second;
    EXPECT_EQ(1, paths.size());
    EXPECT_THAT(
        *paths[0].next_hop()->prefix_bin(), p1Nexthop.addr()->toStdString());
    EXPECT_THAT(*paths[0].local_pref(), kLocalPref);
    verifyCommunities(
        paths[0].communities().value_or({}),
        {{65530, 15800}, {65530, 15801}, {65530, 15802}, {65530, 15803}});
  };
  EXPECT_EQ(output2.size(), 1);
  verifyV6Entry(output2[0]);

  // With a unmatched community filter we should not get any entries returned.
  output2 = rib_->getRibEntriesForCommunities(
      TBgpAfi::AFI_IPV6, {nettools::bgplib::BgpAttrCommunityC(0xFFFF, 0xFFFF)});
  EXPECT_THAT(output2.size(), 0);

  /*
   * AFI agnostic filtering for a matched community case:
   * We should see all the three entries with their paths
   */
  output2 = rib_->getRibEntriesForCommunities(
      TBgpAfi::AFI_ALL, {nettools::bgplib::BgpAttrCommunityC(65530, 15800)});
  EXPECT_THAT(output2.size(), 3);
  bool matched_v4_1 = false, matched_v4_2 = false, matched_v6 = false;
  for (const auto& output_entry : output2) {
    if (checkTRibEntryIsForPrefix(output_entry, kV4Prefix1)) {
      verifyV4Entry1(output_entry);
      matched_v4_1 = true;
    }
    if (checkTRibEntryIsForPrefix(output_entry, kV4Prefix2)) {
      verifyV4Entry2(output_entry);
      matched_v4_2 = true;
    }
    if (checkTRibEntryIsForPrefix(output_entry, kV6Prefix1)) {
      verifyV6Entry(output_entry);
      matched_v6 = true;
    }
  }
  EXPECT_EQ((matched_v4_1 && matched_v4_2 && matched_v6), true);

  /*
   * Perform a partial-match fetch. Note that only attrs1 has 65530:15803
   * community. As prefix1 has only 1 path with attrs2 -- we don't expect to
   * see this in result. Other two prefixes namely kV4prefix2 and kV6Prefix1
   * should be returned but both should only return the path corresponding to
   * attrs2 and it should not be marked as best-path.
   */
  output2 = rib_->getRibEntriesForCommunities(
      TBgpAfi::AFI_ALL, {nettools::bgplib::BgpAttrCommunityC(65530, 15803)});
  EXPECT_THAT(output2.size(), 2);
  matched_v4_1 = matched_v4_2 = matched_v6 = false;
  for (const auto& output_entry : output2) {
    if (checkTRibEntryIsForPrefix(output_entry, kV4Prefix1)) {
      verifyV4Entry1(output_entry);
      matched_v4_1 = true;
    }
    if (checkTRibEntryIsForPrefix(output_entry, kV4Prefix2)) {
      verifyV4Entry2(output_entry, false);
      matched_v4_2 = true;
    }
    if (checkTRibEntryIsForPrefix(output_entry, kV6Prefix1)) {
      verifyV6Entry(output_entry, false);
      matched_v6 = true;
    }
  }
  EXPECT_EQ(matched_v4_1, false);
  EXPECT_EQ((matched_v4_2 && matched_v6), true);
}

TEST_F(RibFixture, GetRibEntriesForSubprefixes) {
  // make 2 attrs with different nexthops, attrs2 has higher local preferece
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs2->setNexthop(kV4Nexthop2);
  attrs2->setLocalPref(kLocalPref2);
  attrs1->publish();
  attrs2->publish();

  /*
   * add 4 v4 entry and 1 v6 entry to rib
   * entry1 has 1 peer, p1
   * entry2 has 2 peers, p1 & p2
   * entry3 has 2 peers, p1 & p2
   * entry4 has 1 peer, p1
   * entry5 has 1 peer, p2
   */
  RibEntry entry1(kV4Prefix1);
  EXPECT_EQ(entry1.getMultipathNexthopsStr(), "[]");
  entry1.updatePath(eBgpPeer1_, attrs1, false); // p1 has nexthop kV4Nexthop1
  RibBase::selectBestPath(
      entry1, multipathSelector, bestpathSelector, false, 0);
  EXPECT_EQ(entry1.getMultipathNexthopsStr(), "[(11.0.0.1:0)]");

  RibEntry entry2(kV4Prefix2);
  entry2.updatePath(
      eBgpPeer1_, attrs1, false); // p1 in this entry has nexthop kV4Nexthop1
  entry2.updatePath(
      eBgpPeer2_, attrs2, false); // p2 in this entry has nexthop kV4Nexthop2
  RibBase::selectBestPath(
      entry2, multipathSelector, bestpathSelector, false, 0);

  RibEntry entry3(kV6Prefix1);
  entry3.updatePath(
      eBgpPeer1_, attrs1, false); // p1 in this entry has nexthop kV4Nexthop1
  entry3.updatePath(
      eBgpPeer2_, attrs2, false); // p2 in this entry has nexthop kV4Nexthop2
  RibBase::selectBestPath(
      entry3, multipathSelector, bestpathSelector, false, 0);

  RibEntry entry4(kV4Prefix1Slash23);
  entry4.updatePath(eBgpPeer1_, attrs1, false); // p1 has nexthop kV4Nexthop1
  RibBase::selectBestPath(
      entry4, multipathSelector, bestpathSelector, false, 0);

  RibEntry entry5(kV4Prefix1Slash25);
  entry5.updatePath(eBgpPeer2_, attrs2, false); // p2 has nexthop kV4Nexthop2
  RibBase::selectBestPath(
      entry5, multipathSelector, bestpathSelector, false, 0);

  // put all entries into the rib
  rib_->ribEntries_.emplace(make_pair(kV4Prefix1, std::move(entry1)));
  rib_->ribEntries_.emplace(make_pair(kV4Prefix2, std::move(entry2)));
  rib_->ribEntries_.emplace(make_pair(kV6Prefix1, std::move(entry3)));
  rib_->ribEntries_.emplace(make_pair(kV4Prefix1Slash23, std::move(entry4)));
  rib_->ribEntries_.emplace(make_pair(kV4Prefix1Slash25, std::move(entry5)));

  // with null prefixes
  auto output1 = rib_->getRibEntriesForSubprefixes(nullptr);
  EXPECT_EQ(output1.size(), 0);

  {
    // with valid prefix that exists
    auto prefix = std::make_unique<std::string>(
        folly::IPAddress::networkToString(kV4Prefix2));
    auto output = rib_->getRibEntriesForSubprefixes(std::move(prefix));
    EXPECT_THAT(
        output,
        UnorderedElementsAre(
            rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix2))));
  }

  {
    // with valid prefix that exists
    auto prefix = std::make_unique<std::string>(
        folly::IPAddress::networkToString(kV6Prefix1));
    auto output = rib_->getRibEntriesForSubprefixes(std::move(prefix));
    EXPECT_THAT(
        output,
        UnorderedElementsAre(
            rib_->createTRibEntry(*rib_->ribEntries_.find(kV6Prefix1))));
  }

  {
    /*
     * with valid prefix that exists
     * kV4Prefix1, kV4Prefix2, kV4Prefix1Slash23,
     * and kV4Prefix1Slash25 are within 8.0.0.0/7
     */
    auto prefix = std::make_unique<std::string>("8.0.0.0/7");
    auto output = rib_->getRibEntriesForSubprefixes(std::move(prefix));
    EXPECT_THAT(
        output,
        UnorderedElementsAre(
            rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix1)),
            rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix2)),
            rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix1Slash23)),
            rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix1Slash25))));
  }

  {
    /*
     * with valid prefix that exists
     * kV4Prefix1Slash23 and kV4Prefix1Slash25 are within kV4Prefix1Base
     */
    auto prefix = std::make_unique<std::string>(
        folly::IPAddress::networkToString(kV4Prefix1Base));
    auto output = rib_->getRibEntriesForSubprefixes(std::move(prefix));
    EXPECT_THAT(
        output,
        UnorderedElementsAre(
            rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix1)),
            rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix1Slash23)),
            rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix1Slash25))));
  }

  {
    /*
     * with valid prefix that exists
     * kV4Prefix1 and kV4Prefix1Slash25 are within kV4Prefix1
     */
    auto prefix = std::make_unique<std::string>(
        folly::IPAddress::networkToString(kV4Prefix1));
    auto output = rib_->getRibEntriesForSubprefixes(std::move(prefix));
    EXPECT_THAT(
        output,
        UnorderedElementsAre(
            rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix1)),
            rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix1Slash25))));
  }

  {
    /*
     * with valid prefix that exists
     * kV6Prefix1 is within 2001::/60
     */
    auto prefix = std::make_unique<std::string>("2001::/60");
    auto output = rib_->getRibEntriesForSubprefixes(std::move(prefix));
    EXPECT_THAT(
        output,
        UnorderedElementsAre(
            rib_->createTRibEntry(*rib_->ribEntries_.find(kV6Prefix1))));
  }

  {
    // with valid prefix that does not exists
    auto prefix = std::make_unique<std::string>(
        folly::IPAddress::networkToString(kV4Prefix3));
    auto output = rib_->getRibEntriesForSubprefixes(std::move(prefix));
    EXPECT_EQ(output.size(), 0);
  }
}

TEST_F(RibFixture, GetRibEntryForPrefix) {
  // make 2 attrs with different nexthops, attrs2 has higher local preferece
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs2->setNexthop(kV4Nexthop2);
  attrs2->setLocalPref(kLocalPref2);
  attrs1->publish();
  attrs2->publish();

  /*
   * add 2 v4 entry and 1 v6 entry to rib
   * entry1 has 2 peers, p1
   * entry2 has 2 peers, p1 & p2
   * entry3 has 2 peers, p1 & p2
   */
  RibEntry entry1(kV4Prefix1);
  entry1.updatePath(eBgpPeer1_, attrs1, false); // p1 has nexthop kV4Nexthop1
  RibBase::selectBestPath(
      entry1, multipathSelector, bestpathSelector, false, 0);

  RibEntry entry2(kV4Prefix2);
  entry2.updatePath(
      eBgpPeer1_, attrs1, false); // p1 in this entry has nexthop kV4Nexthop1
  entry2.updatePath(
      eBgpPeer2_, attrs2, false); // p2 in this entry has nexthop kV4Nexthop2
  RibBase::selectBestPath(
      entry2, multipathSelector, bestpathSelector, false, 0);

  RibEntry entry3(kV6Prefix1);
  entry3.updatePath(
      eBgpPeer1_, attrs1, false); // p1 in this entry has nexthop kV4Nexthop1
  entry3.updatePath(
      eBgpPeer2_, attrs2, false); // p2 in this entry has nexthop kV4Nexthop2
  RibBase::selectBestPath(
      entry3, multipathSelector, bestpathSelector, false, 0);

  rib_->ribEntries_.emplace(make_pair(kV4Prefix1, std::move(entry1)));
  rib_->ribEntries_.emplace(make_pair(kV4Prefix2, std::move(entry2)));
  rib_->ribEntries_.emplace(make_pair(kV6Prefix1, std::move(entry3)));

  // with null prefixes
  auto output1 = rib_->getRibEntryForPrefix(nullptr);
  EXPECT_EQ(output1.size(), 0);

  {
    // with empty prefix
    auto prefix = std::make_unique<std::string>("");
    auto output = rib_->getRibEntryForPrefix(std::move(prefix));
    EXPECT_EQ(output.size(), 0);
  }

  {
    // with invalid prefix, eg. 10.2.3
    auto prefix = std::make_unique<std::string>("10.2.3");
    auto output = rib_->getRibEntryForPrefix(std::move(prefix));
    EXPECT_EQ(output.size(), 0);
  }

  {
    // with valid prefix that exists
    auto prefix = std::make_unique<std::string>(
        folly::IPAddress::networkToString(kV4Prefix1));
    auto output = rib_->getRibEntryForPrefix(std::move(prefix));
    EXPECT_THAT(
        output,
        UnorderedElementsAre(
            rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix1))));
  }

  {
    // with valid prefix that exists
    auto prefix = std::make_unique<std::string>(
        folly::IPAddress::networkToString(kV4Prefix2));
    auto output = rib_->getRibEntryForPrefix(std::move(prefix));
    EXPECT_THAT(
        output,
        UnorderedElementsAre(
            rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix2))));
  }

  {
    // with valid prefix that exists
    auto prefix = std::make_unique<std::string>(
        folly::IPAddress::networkToString(kV6Prefix1));
    auto output = rib_->getRibEntryForPrefix(std::move(prefix));
    EXPECT_THAT(
        output,
        UnorderedElementsAre(
            rib_->createTRibEntry(*rib_->ribEntries_.find(kV6Prefix1))));
  }

  {
    // with valid prefix that does not exists
    auto prefix = std::make_unique<std::string>(
        folly::IPAddress::networkToString(kV4Prefix3));
    auto output = rib_->getRibEntryForPrefix(std::move(prefix));
    EXPECT_EQ(output.size(), 0);
  }
}

TEST_F(RibFixture, GetRibEntryWeightedNexthopsTest) {
  const auto attrs = *buildBgpPathFields(4, 4, 4, 4);
  auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  auto peer2 = TinyPeerInfo(
      kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);
  RibEntry ribEntry(kV4Prefix1);

  {
    auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(attrs);
    attrs1->setNonTransitiveLbwExtCommunity(kLocalAs1, 1021 * BpsPerGBps / 8);
    attrs1->setNexthop(kPeerAddr1);
    attrs1->publish();
    EXPECT_TRUE(ribEntry.updatePath(peer1, attrs1, false));

    auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(attrs);
    attrs2->setNonTransitiveLbwExtCommunity(kLocalAs1, 1024 * BpsPerGBps / 8);
    attrs2->setNexthop(kPeerAddr2);
    attrs2->publish();
    EXPECT_TRUE(ribEntry.updatePath(peer2, attrs2, false));

    RibBase::selectBestPath(
        ribEntry,
        multipathSelector,
        bestpathSelector,
        true,
        1024 /* ucmp-width */);
    WeightedNexthopMap nhWts = {{kPeerAddr1, 511}, {kPeerAddr2, 513}};
    EXPECT_EQ(nhWts, *ribEntry.getMultipathWeightedNexthops());
    rib_->ribEntries_.emplace(kV4Prefix1, std::move(ribEntry));
    auto tRibEntry = rib_->createTRibEntry(*rib_->ribEntries_.find(kV4Prefix1));
    for (const auto& [_, pathGrps] : *tRibEntry.paths()) {
      for (const auto& path : pathGrps) {
        if (*path.peer_id() == createTIpPrefix(kPeerAddr1)) {
          EXPECT_EQ(511, *path.next_hop_weight());
        } else if (*path.peer_id() == createTIpPrefix(kPeerAddr2)) {
          EXPECT_EQ(513, *path.next_hop_weight());
        } else {
          ADD_FAILURE();
        }
      }
    }
  }
}

TEST_F(RibFixture, LbwCommunityBestPath) {
  runLbwCommunityBestPathTest(true);
}

TEST_F(RibNoUcmpComputeFixture, LbwCommunityBestPathNoUcmpCompute) {
  runLbwCommunityBestPathTest(false);
}

TEST_F(RibFixtureCountConfedsInAsPathLen, NativeBestPathCompute) {
  /*
   * Build 3 paths:
   *  1st: 2 asns in AsSequence, 1 confed in ConfedAsSequence
   *  2nd: 1 asn, 2 Confeds
   *  3rd: 0 asn, 4 confeds
   * With native BGP path selection, verify that :
   *  * first 2 paths are selected as multipaths
   *  * 3th should not be rejected due to longer as path len.
   */
  auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(
      *buildBgpPathFields(2, 1, 0, 0, 1));
  auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(
      *buildBgpPathFields(1, 1, 0, 0, 2));
  auto attrs3 = std::make_shared<facebook::bgp::BgpPath>(
      *buildBgpPathFields(0, 1, 0, 0, 4));

  attrs1->setNexthop(kV4Nexthop1);
  attrs2->setNexthop(kV4Nexthop2);
  attrs3->setNexthop(kV4Nexthop3);

  attrs1->publish();
  attrs2->publish();
  attrs3->publish();

  // build peer2 and peer3 as confed peer
  TinyPeerInfo confedEBgpPeer1{
      kPeerAddr1,
      kPeerAsn1,
      kPeerRouterId1,
      BgpSessionType::ConfedEBGP,
      false /*isRrClient*/,
      false /*isRedistributePeer*/};
  TinyPeerInfo confedEBgpPeer2{
      kPeerAddr2,
      kPeerAsn2,
      kPeerRouterId2,
      BgpSessionType::ConfedEBGP,
      false /*isRrClient*/,
      false /*isRedistributePeer*/};
  TinyPeerInfo confedEBgpPeer3{
      kPeerAddr3,
      kPeerAsn3,
      kPeerRouterId3,
      BgpSessionType::ConfedEBGP,
      false /*isRrClient*/,
      false /*isRedistributePeer*/};

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(2);
  {
    InSequence dummy;
    EXPECT_CALL(*fib_, program_(true)).Times(1);
    WeightedNexthopMap nhWts = {{kV4Nexthop1, 0}, {kV4Nexthop2, 0}};
    EXPECT_CALL(
        *fib_,
        updateUnicastRoute_(Eq(kV4Prefix1), _, Pointee(nhWts), false, true, _))
        .Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);
  }

  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  // first trigger EoR
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  // send 4 updates and wait for best path calculation
  rib_->setFibBatchTime(milliseconds(200));
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, confedEBgpPeer1, attrs1);
  sendAnnouncement(prefixBatch, confedEBgpPeer2, attrs2);
  sendAnnouncement(prefixBatch, confedEBgpPeer3, attrs3);
  fibFuture.wait();

  auto prefix = std::make_unique<std::string>(
      folly::IPAddress::networkToString(kV4Prefix1));
  auto output = rib_->getRibEntryForPrefix(std::move(prefix));
  auto paths = output[0].paths()->find(kBestPathGroup)->second;
  EXPECT_EQ(2, paths.size());
  auto ecmp_nh_set = {
      *paths[0].next_hop()->prefix_bin(), *paths[1].next_hop()->prefix_bin()};
  EXPECT_THAT(
      ecmp_nh_set,
      UnorderedElementsAre(
          network::toBinaryAddress(kV4Nexthop1).addr()->toStdString(),
          network::toBinaryAddress(kV4Nexthop2).addr()->toStdString()));
  auto rejPaths = output[0].paths()->find(kDefaultPathGroup)->second;
  EXPECT_EQ(1, rejPaths.size());
  EXPECT_EQ(
      *rejPaths[0].next_hop()->prefix_bin(),
      network::toBinaryAddress(kV4Nexthop3).addr()->toStdString());
}

/**
 * Verifies aggregation of local and received UCMP weights. Here we only intend
 * to ensure that Rib glues the AggregateLocalWeight from RibEntry to
 * RibOutAnnouncementEntry. All corner cases related to aggregation of local
 * weights is carried out in RibEntryTest.AggregateLocalUcmpWeight
 */
TEST_F(RibFixture, AggregateUcmpWeight) {
  /*
   * Build 4 paths with first 3 of localPref=200 and 4th one with
   * localPref=100.  All the attrs have LBW community except for attr4.
   * call selectBestPath() on above path-set.  Because all paths in ECMP of
   * bestpath have LBW community, ribEntry should have honorUcmpWeight set,
   * even though the non-best path does not have LBW community
   */
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(2, 1, 0, 2));
  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(2, 1, 0, 2));
  auto attrs3 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(2, 1, 0, 2));
  auto attrs4 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(2, 1, 0, 2));

  attrs1->setNexthop(kV4Nexthop1);
  attrs2->setNexthop(kV4Nexthop2);
  attrs3->setNexthop(kV4Nexthop3);
  attrs4->setNexthop(kV4Nexthop4);

  attrs1->setLocalPref(kLocalPref2);
  attrs2->setLocalPref(kLocalPref2);
  attrs3->setLocalPref(kLocalPref2);
  attrs4->setLocalPref(kLocalPref);

  attrs1->setNonTransitiveLbwExtCommunity(kLocalAs1, kLbw10G);
  attrs2->setNonTransitiveLbwExtCommunity(kLocalAs1, kLbw5G);
  attrs3->setNonTransitiveLbwExtCommunity(kLocalAs1, kLbw2G);
  // Set extended community attributes of attr4
  {
    nettools::bgplib::BgpAttrExtCommunitiesC extCommunities;
    // AS-specific AS# 1237
    extCommunities.emplace_back(0x00011237, 0x600DCAFE);
    // IPv4-specific IP: 3.3.3.3
    extCommunities.emplace_back(0x01010303, 0x0303CAFE);
    attrs4->setExtCommunities(extCommunities);
  }

  attrs1->publish();
  attrs2->publish();
  attrs3->publish();
  attrs4->publish();

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(5);
  {
    InSequence dummy;
    EXPECT_CALL(*fib_, program_(true)).Times(1);
    // As LBW community is present in all paths, UCMP will be effective

    // After sendAnnouncement(prefixBatch, eBgpPeer1_, attrs1);
    WeightedNexthopMap nhWts1 = {{kV4Nexthop1, 1}};
    EXPECT_CALL(
        *fib_,
        updateUnicastRoute_(Eq(kV4Prefix1), _, Pointee(nhWts1), false, true, _))
        .Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);

    // After sendAnnouncement(prefixBatch, eBgpPeer2_, attrs2);
    WeightedNexthopMap nhWts2 = {{kV4Nexthop1, 2}, {kV4Nexthop2, 1}};
    EXPECT_CALL(
        *fib_,
        updateUnicastRoute_(Eq(kV4Prefix1), _, Pointee(nhWts2), false, true, _))
        .Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);

    // After sendAnnouncement(prefixBatch, eBgpPeer3_, attrs3);
    WeightedNexthopMap nhWts3 = {
        {kV4Nexthop1, 10}, {kV4Nexthop2, 5}, {kV4Nexthop3, 2}};
    EXPECT_CALL(
        *fib_,
        updateUnicastRoute_(Eq(kV4Prefix1), _, Pointee(nhWts3), false, true, _))
        .Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);

    // eBgpPeer4_ is not selected as a next hop due to lower local preference
  }
  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendInitialPathComputation();
  ribFuture.wait();
  rib_->setFibBatchTime(milliseconds(2));

  // Verify LBWs in attrs before best path calculation
  EXPECT_EQ(attrs1->getNonTransitiveLbwValue().value(), kLbw10G);
  EXPECT_EQ(attrs2->getNonTransitiveLbwValue().value(), kLbw5G);
  EXPECT_EQ(attrs3->getNonTransitiveLbwValue().value(), kLbw2G);

  /*
   * For this test assert that kPeerAddr1 is the smallest value so its
   * advertisement is chosen to be best path
   */
  ASSERT_LT(kPeerAddr1, kPeerAddr2);
  ASSERT_LT(kPeerAddr1, kPeerAddr3);
  ASSERT_LT(kPeerAddr1, kPeerAddr4);

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attrs1);
  ribFuture.wait();

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendAnnouncement(prefixBatch, eBgpPeer2_, attrs2);
  ribFuture.wait();

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendAnnouncement(prefixBatch, eBgpPeer3_, attrs3);
  ribFuture.wait();

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendAnnouncement(prefixBatch, eBgpPeer4_, attrs4);
  ribFuture.wait();

  // Ensure LBWs in attrs are unchanged after best path calculation
  EXPECT_EQ(attrs1->getNonTransitiveLbwValue().value(), kLbw10G);
  EXPECT_EQ(attrs2->getNonTransitiveLbwValue().value(), kLbw5G);
  EXPECT_EQ(attrs3->getNonTransitiveLbwValue().value(), kLbw2G);

  auto prefix = std::make_unique<std::string>(
      folly::IPAddress::networkToString(kV4Prefix1));
  auto output = rib_->getRibEntryForPrefix(std::move(prefix));
  const auto& ribEntry = (rib_->ribEntries_.find(kV4Prefix1))->second;
  // EXPECT_TRUE(ribEntry.getHonorUcmpWeights());
  // Verify aggregated received UCMP weight
  EXPECT_EQ(
      static_cast<uint32_t>((kLbw10G + kLbw5G + kLbw2G) / LbwToUcmpWt),
      static_cast<uint32_t>(
          ribEntry.getAggregateReceivedUcmpWeight().value() / LbwToUcmpWt));
  // Verify aggregated local UCMP weight
  ASSERT_TRUE(eBgpPeer1_.ucmpWeight.has_value());
  ASSERT_TRUE(eBgpPeer2_.ucmpWeight.has_value());
  ASSERT_TRUE(eBgpPeer3_.ucmpWeight.has_value());
  EXPECT_EQ(
      *eBgpPeer1_.ucmpWeight + *eBgpPeer2_.ucmpWeight + *eBgpPeer3_.ucmpWeight,
      ribEntry.getAggregateLocalUcmpWeight());
  // Ensure best path has LBW Value of 10G
  const auto bestpath = ribEntry.getAdvertisedBestPath();
  EXPECT_EQ(bestpath->attrs->getNonTransitiveLbwValue().value(), kLbw10G);
  // Ensure getAttrsToBeAdvertised() also has LBW same as best path
  EXPECT_EQ(bestpath->attrs->getNonTransitiveLbwValue().value(), kLbw10G);
}

TEST_F(RibFixture, ProgramTopoInfoTest) {
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(1, 1, 0, 0));

  std::unordered_map<std::string, int64_t> topoInfo1{
      {"rack_id", 1},
      {"plane_id", 8},
      {"remote_rack_capacity", 36},
      {"spine_capacity", 36}};
  attrs1->setTopologyInfo(topoInfo1);

  std::unordered_map<std::string, int64_t> topoInfo2{
      {"rack_id", 2},
      {"plane_id", 8},
      {"remote_rack_capacity", 36},
      {"spine_capacity", 36}};
  attrs2->setNexthop(kV4Nexthop2);
  attrs2->setTopologyInfo(topoInfo2);

  attrs1->publish();
  attrs2->publish();

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(2);
  {
    InSequence dummy;
    EXPECT_CALL(*fib_, program_(true)).Times(1);
    WeightedNexthopMap nhWts = {{kV4Nexthop1, 0}, {kV4Nexthop2, 0}};
    NexthopTopoInfoMap topoInfoMap = {
        {kV4Nexthop1, topoInfo1}, {kV4Nexthop2, topoInfo2}};
    EXPECT_CALL(
        *fib_,
        updateUnicastRoute_(
            Eq(kV4Prefix1),
            _,
            Pointee(nhWts),
            false,
            true,
            _,
            std::optional<uint32_t>(std::nullopt),
            Pointee(topoInfoMap)))
        .Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);
  }
  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  rib_->setFibBatchTime(std::chrono::seconds(1));
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attrs1);
  sendAnnouncement(prefixBatch, eBgpPeer2_, attrs2);
  fibFuture.wait();
  auto prefix = std::make_unique<std::string>(
      folly::IPAddress::networkToString(kV4Prefix1));
  auto output = rib_->getRibEntryForPrefix(std::move(prefix));
  auto paths = output[0].paths()->find(kBestPathGroup)->second;
  EXPECT_EQ(2, paths.size());
  auto ecmp_nh_set = {
      *paths[0].next_hop()->prefix_bin(), *paths[1].next_hop()->prefix_bin()};
  EXPECT_THAT(
      ecmp_nh_set,
      UnorderedElementsAre(
          network::toBinaryAddress(kV4Nexthop1).addr()->toStdString(),
          network::toBinaryAddress(kV4Nexthop2).addr()->toStdString()));
  {
    const auto& ribEntry = (rib_->ribEntries_.find(kV4Prefix1))->second;
    auto topoInfoMap = ribEntry.getNexthopTopoInfoMap();
    EXPECT_EQ(topoInfoMap->size(), 2);
    for (const auto& [nh, topoInfo] : *topoInfoMap) {
      if (nh == kV4Nexthop1) {
        EXPECT_EQ(topoInfo, topoInfo1);
      } else if (nh == kV4Nexthop2) {
        EXPECT_EQ(topoInfo, topoInfo2);
      }
    }
  }
}

TEST_F(RibFixture, NoLbwECMP) {
  /*
   * Build 4 paths with first 3 of localPref=200 and 4th one with
   * localPref=100.  One of the ECMP paths does not have lbw community, the
   * others do.  Call selectBestPath() on above path-set.  Because one of
   * the ECMP paths does not have LBW community, ribEntry should have not
   * have honorUcmpWeight set, and we should resort to doing ECMP, not UCMP.
   */
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(2, 1, 0, 2));
  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(2, 1, 0, 2));
  auto attrs3 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(2, 1, 0, 2));
  auto attrs4 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(2, 1, 0, 2));

  attrs1->setNexthop(kV4Nexthop1);
  attrs2->setNexthop(kV4Nexthop2);
  attrs3->setNexthop(kV4Nexthop3);
  attrs4->setNexthop(kV4Nexthop4);

  attrs1->setLocalPref(kLocalPref2);
  attrs2->setLocalPref(kLocalPref2);
  attrs3->setLocalPref(kLocalPref2);
  attrs4->setLocalPref(kLocalPref);

  // Set LBW extended community attributes for attr1
  {
    nettools::bgplib::BgpAttrExtCommunitiesC extCommunities;
    // LBW Community attr with b/w = 10G
    extCommunities.emplace_back(0x40041111, 0x501502f9);
    attrs1->setExtCommunities(extCommunities);
  }
  // Set non-LBW extended community attributes for attr2
  {
    nettools::bgplib::BgpAttrExtCommunitiesC extCommunities;
    // AS-specific AS# 1237
    extCommunities.emplace_back(0x00011237, 0x600DCAFE);
    // IPv4-specific IP: 3.3.3.3
    extCommunities.emplace_back(0x01010303, 0x0303CAFE);
    attrs2->setExtCommunities(extCommunities);
  }

  // Set LBW extended community attributes for attr3
  {
    nettools::bgplib::BgpAttrExtCommunitiesC extCommunities;
    // LBW Community attr with b/w = 5G
    extCommunities.emplace_back(0x40041111, 0x4f9502f9);
    attrs3->setExtCommunities(extCommunities);
  }

  // Set LBW extended community attributes for attr4
  {
    nettools::bgplib::BgpAttrExtCommunitiesC extCommunities;
    // LBW Community attr with b/w = 2G
    extCommunities.emplace_back(0x40041111, 0x4eee6b28);
    attrs4->setExtCommunities(extCommunities);
  }

  attrs1->publish();
  attrs2->publish();
  attrs3->publish();
  attrs4->publish();

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(2);
  {
    InSequence dummy;
    EXPECT_CALL(*fib_, program_(true)).Times(1);
    /*
     * As LBW community is not present in all ECMP paths, UCMP will be
     * effectively disabled. Hence we should expect weight params of each nh
     * to be 0 i.e. ucmp degenrates to ecmp.
     */
    WeightedNexthopMap nhWts = {
        {kV4Nexthop1, 0}, {kV4Nexthop2, 0}, {kV4Nexthop3, 0}};
    EXPECT_CALL(
        *fib_,
        updateUnicastRoute_(Eq(kV4Prefix1), _, Pointee(nhWts), false, true, _))
        .Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);
  }
  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  rib_->setFibBatchTime(seconds(1));
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attrs1);
  sendAnnouncement(prefixBatch, eBgpPeer2_, attrs2);
  sendAnnouncement(prefixBatch, eBgpPeer3_, attrs3);
  sendAnnouncement(prefixBatch, eBgpPeer4_, attrs4);
  fibFuture.wait();

  auto prefix = std::make_unique<std::string>(
      folly::IPAddress::networkToString(kV4Prefix1));
  auto output = rib_->getRibEntryForPrefix(std::move(prefix));
  const auto& ribEntry = (rib_->ribEntries_.find(kV4Prefix1))->second;
  // EXPECT_FALSE(ribEntry.getHonorUcmpWeights());
  EXPECT_FALSE(ribEntry.getAggregateReceivedUcmpWeight().has_value());
}

TEST_F(RibFixture, LbwCommunityForward) {
  auto installToFib{true};
  auto& inputQ = rib_->fromFibMessageQ_;
  /*
   * Two peers are advertising a given prefix with lbw community set.
   * We should see lbwCommunity in the RibOutAnnouncement attrs.
   */
  RibEntry entry(kV4Prefix1);
  {
    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    // Add a LBW Community attr with b/w = 10G
    attrs->setNonTransitiveLbwExtCommunity(kLocalAs1, kLbw10G);
    attrs->publish();
    entry.updatePath(eBgpPeer1_, attrs, false, installToFib);
  }
  {
    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    // Add a LBW Community attr with b/w = 10G
    attrs->setNonTransitiveLbwExtCommunity(kLocalAs1, kLbw10G);
    attrs->publish();
    auto peer2 = TinyPeerInfo(
        kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);
    entry.updatePath(peer2, attrs, false, installToFib);
  }
  RibBase::selectBestPath(entry, multipathSelector, bestpathSelector, false, 0);
  // EXPECT_EQ(entry.getHonorUcmpWeights(), true);
  EXPECT_EQ(
      entry.getAggregateReceivedUcmpWeight().value(), (kLbw10G + kLbw10G));
  {
    // initialise a FibProgrammedMessage with same route in ribEntries
    folly::F14NodeMap<
        folly::CIDRNetwork,
        std::shared_ptr<const WeightedNexthopMap>>
        prefixToNexthops = {{kV4Prefix1, entry.getMultipathWeightedNexthops()}};
    folly::F14FastMap<
        std::shared_ptr<const BgpPath>,
        folly::F14NodeMap<
            folly::CIDRNetwork,
            std::shared_ptr<const WeightedNexthopMap>>>
        FibProgrammedPfxs;
    FibProgrammedPfxs[nullptr] = prefixToNexthops;

    Fib::FibProgrammedMessage msg(FibProgrammedPfxs, false);
    rib_->ribEntries_.emplace(make_pair(kV4Prefix1, std::move(entry)));
    inputQ.push(std::move(msg));

    auto result = folly::coro::blockingWait(ribOutQ_.pop());
    auto ret = folly::variant_match(
        result,
        [&](const ShadowRibOutAnnouncement& /* unused */) { return false; },
        [&](const ShadowRibOutWithdrawal& /* unused */) { return false; },
        [&](const RibInitialAnnouncementStart& /* unused */) { return false; },
        [&](const RibOutWithdrawal& /* unused */) { return false; },
        [&](const RibOutAnnouncement& announcement) {
          EXPECT_EQ(1, announcement.entries.size());
          EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
          EXPECT_EQ(kV4Prefix1, announcement.entries[0].prefix);
          EXPECT_TRUE(
              announcement.entries[0].attrs->hasNonTransitiveLbwExtCommunity());
          EXPECT_EQ(
              announcement.entries[0].attrs->getNonTransitiveLbwValue(),
              kLbw10G);
          EXPECT_EQ(
              announcement.entries[0].aggregateReceivedUcmpWeight,
              (kLbw10G + kLbw10G));
          return true;
        });
    EXPECT_EQ(ret, true);

    // expect no more announcement
    REPEAT_N(5, { EXPECT_EQ(0, ribOutQ_.size()); });
  }
}

TEST_F(RibFixture, LbwCommunitySuppression1) {
  auto installToFib{true};
  auto& inputQ = rib_->fromFibMessageQ_;
  /*
   *  2 peers advertise same prefix but only 1 of them advertise
   *  lbw community also. we would expect bestpath to mark
   *  rib entry's honorUcmpWeight as false and in the RibOutAnnoucmnent
   *  we should not see any lbw community attribute.
   */
  RibEntry entry(kV4Prefix2);
  {
    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    // Add a LBW community attribute
    attrs->setNonTransitiveLbwExtCommunity(kLocalAs1, kLbw10G);
    // nettools::bgplib::BgpAttrExtCommunitiesC extCommunities;
    // extCommunities.emplace_back(0x40041111, 0x501502f9);
    // attrs->setExtCommunities(extCommunities);
    EXPECT_EQ(attrs->getExtCommunities()->size(), 5);
    EXPECT_TRUE(attrs->hasNonTransitiveLbwExtCommunity());
    attrs->publish();
    entry.updatePath(eBgpPeer1_, attrs, false, installToFib);
  }
  {
    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    EXPECT_EQ(attrs->getExtCommunities()->size(), 4);
    EXPECT_FALSE(attrs->hasNonTransitiveLbwExtCommunity());
    attrs->publish();
    auto peer2 = TinyPeerInfo(
        kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);
    entry.updatePath(peer2, attrs, false, installToFib);
  }
  /*
   * For this test, we assert that peer1 < peer2, so as to ensure that
   * the route advertised by peer1 is the best path
   */
  ASSERT_LT(kPeerAddr1, kPeerAddr2);
  RibBase::selectBestPath(entry, multipathSelector, bestpathSelector, false, 0);
  EXPECT_EQ(entry.getBestPath()->attrs->getExtCommunities()->size(), 5);
  // Best Path should have LBW, but honorUCMPWeights should be false
  EXPECT_TRUE(entry.getBestPath()->attrs->hasNonTransitiveLbwExtCommunity());
  // EXPECT_FALSE(entry.getHonorUcmpWeights());
  {
    // initialise a FibProgrammedMessage with same route in ribEntries
    folly::F14NodeMap<
        folly::CIDRNetwork,
        std::shared_ptr<const WeightedNexthopMap>>
        prefixToNexthops = {{kV4Prefix2, entry.getMultipathWeightedNexthops()}};
    folly::F14FastMap<
        std::shared_ptr<const BgpPath>,
        folly::F14NodeMap<
            folly::CIDRNetwork,
            std::shared_ptr<const WeightedNexthopMap>>>
        FibProgrammedPfxs;
    FibProgrammedPfxs[nullptr] = prefixToNexthops;

    Fib::FibProgrammedMessage msg(FibProgrammedPfxs, false);
    rib_->ribEntries_.emplace(make_pair(kV4Prefix2, std::move(entry)));
    inputQ.push(std::move(msg));

    auto result = folly::coro::blockingWait(ribOutQ_.pop());
    auto ret = folly::variant_match(
        result,
        [&](const ShadowRibOutAnnouncement& /* unused */) { return false; },
        [&](const ShadowRibOutWithdrawal& /* unused */) { return false; },
        [&](const RibInitialAnnouncementStart& /* unused */) { return false; },
        [&](const RibOutWithdrawal& /* unused */) { return false; },
        [&](const RibOutAnnouncement& announcement) {
          EXPECT_EQ(1, announcement.entries.size());
          EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
          EXPECT_EQ(kV4Prefix2, announcement.entries[0].prefix);
          EXPECT_TRUE(
              announcement.entries[0].attrs->hasNonTransitiveLbwExtCommunity());
          // Best path's LBW should be unchanged
          EXPECT_EQ(
              announcement.entries[0].attrs->getNonTransitiveLbwValue().value(),
              kLbw10G);
          // However, aggregateReceivedUcmpWeight must be set to zero
          EXPECT_FALSE(
              announcement.entries[0].aggregateReceivedUcmpWeight.has_value());
          return true;
        });
    EXPECT_EQ(ret, true);

    // expect no more announcement
    REPEAT_N(5, { EXPECT_EQ(0, ribOutQ_.size()); });
  }
}

TEST_F(RibFixture, LbwCommunitySuppression2) {
  auto installToFib{true};
  auto& inputQ = rib_->fromFibMessageQ_;
  /*
   *  2 peers advertise same prefix but only 1 of them advertise
   *  lbw community also. we would expect bestpath to mark
   *  rib entry's honorUcmpWeight as false and in the RibOutAnnoucmnent
   *  we should not see any lbw community attribute.
   */
  RibEntry entry(kV4Prefix2);
  {
    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    EXPECT_EQ(attrs->getExtCommunities()->size(), 4);
    EXPECT_FALSE(attrs->hasNonTransitiveLbwExtCommunity());
    attrs->publish();
    entry.updatePath(eBgpPeer1_, attrs, false, installToFib);
  }
  {
    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    // Add a LBW community attribute
    nettools::bgplib::BgpAttrExtCommunitiesC extCommunities;
    extCommunities.emplace_back(0x40041111, 0x501502f9);
    attrs->setExtCommunities(extCommunities);
    EXPECT_EQ(attrs->getExtCommunities()->size(), 1);
    EXPECT_TRUE(attrs->hasNonTransitiveLbwExtCommunity());
    attrs->publish();
    auto peer2 = TinyPeerInfo(
        kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);
    entry.updatePath(peer2, attrs, false, installToFib);
  }
  /*
   * For this test, we assert that peer1 < peer2, so as to ensure that
   * the route advertised by peer1 is the best path
   */
  ASSERT_LT(kPeerAddr1, kPeerAddr2);
  RibBase::selectBestPath(entry, multipathSelector, bestpathSelector, false, 0);
  EXPECT_EQ(entry.getBestPath()->attrs->getExtCommunities()->size(), 4);
  // Best Path should not have LBW, and honorUcmpWeights should also be false
  EXPECT_FALSE(entry.getBestPath()->attrs->hasNonTransitiveLbwExtCommunity());
  // EXPECT_FALSE(entry.getHonorUcmpWeights());
  {
    // initialise a FibProgrammedMessage with same route in ribEntries
    folly::F14NodeMap<
        folly::CIDRNetwork,
        std::shared_ptr<const WeightedNexthopMap>>
        prefixToNexthops = {{kV4Prefix2, entry.getMultipathWeightedNexthops()}};
    folly::F14FastMap<
        std::shared_ptr<const BgpPath>,
        folly::F14NodeMap<
            folly::CIDRNetwork,
            std::shared_ptr<const WeightedNexthopMap>>>
        FibProgrammedPfxs;
    FibProgrammedPfxs[nullptr] = prefixToNexthops;

    Fib::FibProgrammedMessage msg(FibProgrammedPfxs, false);
    rib_->ribEntries_.emplace(make_pair(kV4Prefix2, std::move(entry)));
    inputQ.push(std::move(msg));

    auto result = folly::coro::blockingWait(ribOutQ_.pop());
    auto ret = folly::variant_match(
        result,
        [&](const ShadowRibOutAnnouncement& /* unused */) { return false; },
        [&](const ShadowRibOutWithdrawal& /* unused */) { return false; },
        [&](const RibInitialAnnouncementStart& /* unused */) { return false; },
        [&](const RibOutWithdrawal& /* unused */) { return false; },
        [&](const RibOutAnnouncement& announcement) {
          EXPECT_EQ(1, announcement.entries.size());
          EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
          EXPECT_EQ(kV4Prefix2, announcement.entries[0].prefix);
          auto extCommunities =
              announcement.entries[0].attrs->getExtCommunities();
          EXPECT_EQ(extCommunities->size(), 4);
          EXPECT_FALSE(
              announcement.entries[0].attrs->hasNonTransitiveLbwExtCommunity());
          EXPECT_FALSE(
              announcement.entries[0].aggregateReceivedUcmpWeight.has_value());
          return true;
        });
    EXPECT_EQ(ret, true);

    // expect no more announcement
    REPEAT_N(5, { EXPECT_EQ(0, ribOutQ_.size()); });
  }
}

TEST_F(RibFixture, EorSentInitializationEvent) {
  facebook::fb303::ThreadCachedServiceData::getShared()->setCounter(
      fmt::format(
          kInitEventCounterFormat,
          apache::thrift::util::enumNameSafe(BgpInitializationEvent::EOR_SENT)),
      0);

  auto& inputQ = rib_->fromFibMessageQ_;
  EXPECT_EQ(-1, rib_->getLastProgrammedRoutesTimeStamp());

  // get a time delta from now to last time programmed routes
  const auto timestamp1 = service_->getTimeElapsedSinceLastFibUpdate();
  EXPECT_EQ(-1, timestamp1);

  EXPECT_EQ(
      0,
      facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
          fmt::format(
              kInitEventCounterFormat,
              apache::thrift::util::enumNameSafe(
                  BgpInitializationEvent::EOR_SENT))));

  RibEntry entry(kV4Prefix1);
  folly::
      F14NodeMap<folly::CIDRNetwork, std::shared_ptr<const WeightedNexthopMap>>
          prefixToNexthops = {
              {kV4Prefix1, entry.getMultipathWeightedNexthops()}};
  folly::F14FastMap<
      std::shared_ptr<const BgpPath>,
      folly::F14NodeMap<
          folly::CIDRNetwork,
          std::shared_ptr<const WeightedNexthopMap>>>
      FibProgrammedPfxs;
  FibProgrammedPfxs[nullptr] = prefixToNexthops;

  Fib::FibProgrammedMessage fibMsg(FibProgrammedPfxs, true /* fullSync */);
  inputQ.push(std::move(fibMsg));

  auto msg0 = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg0));

  auto msg1 = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg1));
  auto announcement = std::get<RibOutAnnouncement>(msg1);
  EXPECT_EQ(true, announcement.sendWithEoR);
  EXPECT_LE(0, rib_->getLastProgrammedRoutesTimeStamp());

  // get a time delta from now to last time programmed routes
  const auto timestamp2 = service_->getTimeElapsedSinceLastFibUpdate();
  EXPECT_LE(0, timestamp2);
  ASSERT_NE(timestamp1, timestamp2);

  EXPECT_LT(
      0,
      facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
          fmt::format(
              kInitEventCounterFormat,
              apache::thrift::util::enumNameSafe(
                  BgpInitializationEvent::EOR_SENT))));
}

/*
 * This test create a RibEntry of a route - kV4Prefix1
 * This route has 2 paths (via kV4Nexthop1 and kV4Nexthop2)
 * We simulate that 1 path is removed. Rib has computed the new best path/or new
 * additional path(s). But, before the FIB has been programmed, there is a
 * RibDumpReq requested to the Rib. Then, bgp++ must not crash.
 */
TEST_P(RibFixtureAddPathTestSuite, PathRemovedAndRibDumpReqTest) {
  /*
   * send prefix from eBgpPeer1_(kPeerAddr1), nh is kV4Nexthop1 to Rib
   * attr_ has kV4Nexthop1 as next hop
   */
  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  // send prefix from eBgpPeer2_(kPeerAddr2), nh is kV4Nexthop2 to Rib
  auto newAttr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  newAttr->setNexthop(kV4Nexthop2);
  newAttr->publish();
  sendAnnouncement(prefixBatch, eBgpPeer2_, newAttr);

  /*
   * rib_ learns that this kV4Prefix1 route has 2 paths (via kV4Nexthop1 and
   * kV4Nexthop2)
   */

  EXPECT_EQ(0, ribOutQ_.size());

  // send EoR
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  uint32_t peer1PathIdToSend =
      kDefaultPathID; // placeholder val. Actual val will be assigned
                      // non-deterministically by best path selection, and
                      // then fetched below
  {
    // Expect RibInitialAnnouncementStart before initial dump.
    auto msg0 = folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg0));

    // initial dump to all peers after syncFib
    auto msg1 = folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg1));
    auto announcement1 = std::get<RibOutAnnouncement>(msg1);
    /*
     * entries will be 1 since this announcement of kV4Prefix1 will advertise
     * only best path
     */
    ASSERT_EQ(1, announcement1.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement1.entries[0].pathIdToSend);
    EXPECT_EQ(true, announcement1.sendWithEoR);
    EXPECT_EQ(true, announcement1.initialDump);
    /*
     * If the rib_->sendAddPath is true, the addPathEntries will be 2, since
     * there are 2 paths for this kV4Prefix1 route, otherwise 0. Each AdjRib
     * will decide whether entires or addPathEntries will be sent to the remote
     * peer
     */
    ASSERT_EQ(2, announcement1.addPathEntries.size());
    checkRibOutEntriesAddPathIds(announcement1);
    /*
     * store pathIdToSend from peer 1's path, as we'll remove this path from Rib
     * later and we want to make sure the subsequent RibOutAnnouncement does not
     * have the same pathIdToSend value
     */
    bool entry1FromPeer1 = announcement1.addPathEntries[0].peer == eBgpPeer1_;
    bool entry2FromPeer1 = announcement1.addPathEntries[1].peer == eBgpPeer1_;
    EXPECT_NE(
        entry1FromPeer1,
        entry2FromPeer1); // exactly one announced entry for a path from peer 1.
                          // Could be either the first or second in
                          // addPathEntries
    peer1PathIdToSend = entry1FromPeer1
        ? announcement1.addPathEntries[0].pathIdToSend
        : announcement1.addPathEntries[1].pathIdToSend;
  }

  fibFuture.wait();
  {
    auto& kV4Prefix1Entry = rib_->ribEntries_.find(kV4Prefix1)->second;
    ASSERT_NE(nullptr, kV4Prefix1Entry.getMultipathWeightedNexthops());
    ASSERT_NE(
        nullptr, kV4Prefix1Entry.getAdvertisedMultipathWeightedNexthops());
    // Remove a path from kV4Prefix1 route
    EXPECT_EQ(true, kV4Prefix1Entry.updatePath(eBgpPeer1_, nullptr, false));
    RibBase::selectBestPath(
        kV4Prefix1Entry, multipathSelector, bestpathSelector, false, 0);
    // Verify that the path has been removed by RIB's computation
    ASSERT_NE(nullptr, kV4Prefix1Entry.getMultipathWeightedNexthops());
    // Verify that FIB has not been programmed with new next hop set yet
    ASSERT_NE(
        nullptr, kV4Prefix1Entry.getAdvertisedMultipathWeightedNexthops());
    EXPECT_EQ(1, kV4Prefix1Entry.getMultipathWeightedNexthops()->size());
    EXPECT_EQ(
        2, kV4Prefix1Entry.getAdvertisedMultipathWeightedNexthops()->size());
  }
  {
    /*
     * Simulate FIB has been programmed and acks back to Rib
     */
    auto& kV4Prefix1Entry = rib_->ribEntries_.find(kV4Prefix1)->second;
    auto& fromFibToRibQ = rib_->fromFibMessageQ_;

    /*
     * Route is update because a path is removed, but there is another path
     * left. The kV4Prefix1Entry.getMultipathWeightedNexthops() is computed by
     * RIB and will be programmed to FIB
     */
    EXPECT_NE(nullptr, kV4Prefix1Entry.getMultipathWeightedNexthops());
    folly::F14NodeMap<
        folly::CIDRNetwork,
        std::shared_ptr<const WeightedNexthopMap>>
        prefixToNexthops = {
            {kV4Prefix1, kV4Prefix1Entry.getMultipathWeightedNexthops()}};
    folly::F14FastMap<
        std::shared_ptr<const BgpPath>,
        folly::F14NodeMap<
            folly::CIDRNetwork,
            std::shared_ptr<const WeightedNexthopMap>>>
        FibProgrammedPfxs;
    FibProgrammedPfxs[nullptr] = prefixToNexthops;

    Fib::FibProgrammedMessage msg(FibProgrammedPfxs, false /* fullSync */);
    fromFibToRibQ.push(std::move(msg));

    /*
     * expect 2 elements in the ribOutQ_, 1 RibOutWithdrawal followed by
     * 1 RibOutAnnouncement since rib_->sendAddPath is true
     */
    auto msg1 = folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg1));
    auto msg2 = folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg2));
    auto announcement = std::get<RibOutAnnouncement>(msg2);
    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
    ASSERT_EQ(1, announcement.addPathEntries.size());
    // peer 1's path was removed. Don't announce its pathIdToSend out of Rib
    EXPECT_NE(peer1PathIdToSend, announcement.addPathEntries[0].pathIdToSend);

    // Verify that there are no more msg in the ribOutQ_
    REPEAT_N(5, { ASSERT_EQ(0, ribOutQ_.size()); });
  }
  {
    /*
     * Verify that kV4Prefix1 entry is still in the ribEntries_, but with only 1
     * path left
     */
    ASSERT_NE(rib_->ribEntries_.find(kV4Prefix1), rib_->ribEntries_.end());
    auto& kV4Prefix1Entry = rib_->ribEntries_.find(kV4Prefix1)->second;
    /*
     * Verify that kV4Prefix1 entry advertisedWeightedNexthops_ is updated to
     * multipathWeightedNexthops
     */
    ASSERT_EQ(
        kV4Prefix1Entry.getAdvertisedMultipathWeightedNexthops(),
        kV4Prefix1Entry.getMultipathWeightedNexthops());
    std::shared_ptr<const WeightedNexthopMap> nextHopMap =
        rib_->ribEntries_.find(kV4Prefix1)
            ->second.getAdvertisedMultipathWeightedNexthops();
    /*
     * Verify that nextHopMap is not nullptr since there is still a path through
     * kV4Nexthop2
     */
    ASSERT_NE(nullptr, nextHopMap);
    EXPECT_EQ(1, nextHopMap->size());
    EXPECT_NE(nextHopMap->find(kV4Nexthop2), nextHopMap->end());
  }
}

/*
 * This test create a RibEntry a route - kV4Prefix1
 * We simulate that a route is withdrawn. Rib has computed the new best path/or
 * new additional path(s), which will be null. But, before the FIB has been
 * programmed, there is a RibDumpReq requested to the Rib. Then, bgp++ must not
 * crash.
 */
TEST_P(RibFixtureAddPathTestSuite, RouteWithdrawnAndRibDumpReqTest) {
  /*
   * send prefix from eBgpPeer1_(kPeerAddr1), nh is kV4Nexthop1 to Rib
   * attr_ has kV4Nexthop1 as next hop
   */
  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  EXPECT_EQ(0, ribOutQ_.size());

  // send EoR
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  {
    // Expect RibInitialAnnouncementStart before announcement.
    auto msg = folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));

    // initial dump to all peers after syncFib
    msg = folly::coro::blockingWait(ribOutQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    /*
     * entries will be 1 since this announcement of kV4Prefix1 will advertise
     * only best path
     */
    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
    EXPECT_EQ(true, announcement.sendWithEoR);
    EXPECT_EQ(true, announcement.initialDump);
    /*
     * If the rib_->sendAddPath is true, the addPathEntries will be 1 (1 path),
     * otherwise 0 Each AdjRib will decide whether entires or addPathEntries
     * will be sent to the remote peer
     */
    ASSERT_EQ(1, announcement.addPathEntries.size());
    EXPECT_EQ(kMinPathIDToSend, announcement.addPathEntries[0].pathIdToSend);
  }

  fibFuture.wait();
  {
    auto& kV4Prefix1Entry = rib_->ribEntries_.find(kV4Prefix1)->second;
    ASSERT_NE(nullptr, kV4Prefix1Entry.getMultipathWeightedNexthops());
    ASSERT_NE(
        nullptr, kV4Prefix1Entry.getAdvertisedMultipathWeightedNexthops());
    kV4Prefix1Entry.updatePath(eBgpPeer1_, nullptr, false);
    RibBase::selectBestPath(
        kV4Prefix1Entry, multipathSelector, bestpathSelector, false, 0);
    ASSERT_NE(
        nullptr, kV4Prefix1Entry.getAdvertisedMultipathWeightedNexthops());
    /*
     * Verify that a route has been withdrawn. The RIB's computed
     * multipathWeightNexthops must be nullptr
     */
    ASSERT_EQ(nullptr, kV4Prefix1Entry.getMultipathWeightedNexthops());
    /*
     * Verify that advertisedMultipathWeightedNexthops is not nullptr since FIB
     * has not programmed the withdrawn route yet.
     */
    EXPECT_EQ(
        1, kV4Prefix1Entry.getAdvertisedMultipathWeightedNexthops()->size());
  }
}

TEST_F(RibFixture, GetSelectionFilterCriteriaTest) {
  /*
   * make 5 attrs with different attributes
   * attrs1: { LocalPref = kLocalPref2, AsPathCount = 4, Origin = IGP }
   * attrs2: { LocalPref = kLocalPref2, AsPathCount = 4, Origin = IGP }
   * attrs3: { LocalPref = kLocalPref2, AsPathCount = 4, Origin = EGP }
   * attrs4: { LocalPref = kLocalPref2, AsPathCount = 5, Origin = IGP }
   * attrs5: { LocalPref = kLocalPref, AsPathCount = 4, Origin = IGP }
   * BGP Selection Algo of relevance for this test (in the following order)
   *   LocalPref2 > LocalPref  wins.
   *   Shorter AS Path wins and IGP wins over EGP (origin).
   *   Lowest peer-id wins.
   * As such the preference order of the above attr set:
   *  attrs1 > attrs2 > attrs3 > attrs4 > attrs5
   */

  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  auto attrs3 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  auto attrs4 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(5, 4, 4, 4));
  auto attrs5 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));

  attrs1->setNexthop(kV4Nexthop3);
  attrs2->setNexthop(kV4Nexthop5);
  attrs3->setNexthop(kV4Nexthop4);
  attrs4->setNexthop(kV4Nexthop2);
  attrs5->setNexthop(kV4Nexthop1);

  attrs1->setLocalPref(kLocalPref2);
  attrs2->setLocalPref(kLocalPref2);
  attrs3->setLocalPref(kLocalPref2);
  attrs4->setLocalPref(kLocalPref2);
  attrs5->setLocalPref(kLocalPref);

  attrs1->setOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
  attrs2->setOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
  attrs3->setOrigin(BgpAttrOrigin::BGP_ORIGIN_EGP);
  attrs4->setOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
  attrs5->setOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);

  attrs1->publish();
  attrs2->publish();
  attrs3->publish();
  attrs4->publish();
  attrs5->publish();

  // Case:1 add rib entry entry0  :: single path atttr (attrs5).
  RibEntry entry0(kV4Prefix1);
  entry0.updatePath(eBgpPeer1_, attrs5, false);
  RibBase::selectBestPath(
      entry0, multipathSelector, bestpathSelector, false, 0);
  rib_->ribEntries_.emplace(make_pair(kV4Prefix1, std::move(entry0)));
  auto prefix0 = std::make_unique<std::string>(
      folly::IPAddress::networkToString(kV4Prefix1));
  auto output0 = rib_->getRibEntryForPrefix(std::move(prefix0));
  {
    EXPECT_EQ(output0.size(), 1);
    auto paths = output0[0].paths()->find(kBestPathGroup)->second;
    EXPECT_EQ(1, paths.size());
    EXPECT_THAT(
        *paths[0].next_hop()->prefix_bin(),
        network::toBinaryAddress(kV4Nexthop1).addr()->toStdString());
    EXPECT_EQ(*paths[0].bestpath_filter_descr(), "");
  }

  // Case:2 add rib entry entry1  :: just pure ecmp groups <attrs1 and attrs2>.
  RibEntry entry1(kV4Prefix2);
  entry1.updatePath(eBgpPeer1_, attrs1, false);
  entry1.updatePath(eBgpPeer2_, attrs2, false);
  RibBase::selectBestPath(
      entry1, multipathSelector, bestpathSelector, false, 0);
  rib_->ribEntries_.emplace(make_pair(kV4Prefix2, std::move(entry1)));
  auto prefix1 = std::make_unique<std::string>(
      folly::IPAddress::networkToString(kV4Prefix2));
  {
    auto output1 = rib_->getRibEntryForPrefix(std::move(prefix1));

    EXPECT_EQ(output1.size(), 1);
    auto paths = output1[0].paths()->find(kBestPathGroup)->second;
    /*
     * Check the filter Criteria are set properly
     * Both are selected as ECMP paths however nextHop3 will win in best-path
     * due to Lowest peer-ip.
     */
    EXPECT_THAT(
        paths,
        UnorderedElementsAre(
            AllOf(
                ResultOf(
                    [](const auto& p) { return *p.next_hop()->prefix_bin(); },
                    Eq(network::toBinaryAddress(kV4Nexthop5)
                           .addr()
                           ->toStdString())),
                ResultOf(
                    [](const auto& p) { return *p.bestpath_filter_descr(); },
                    Eq("Router-Id, Filter Criterion: Choose Lowest Value"))),
            AllOf(
                ResultOf(
                    [](const auto& p) { return *p.next_hop()->prefix_bin(); },
                    Eq(network::toBinaryAddress(kV4Nexthop3)
                           .addr()
                           ->toStdString())),
                ResultOf(
                    [](const auto& p) { return *p.bestpath_filter_descr(); },
                    Eq("")))));
  }

  /*
   * Case-3:
   *      add rib-entry2 :: all 5 attrs -- winner are <attrs1 and attrs2>
   *      nexthop1 is best path and both nexthop1 & 2 are ecmp.
   */
  RibEntry entry2(kV4Prefix3);
  entry2.updatePath(eBgpPeer1_, attrs5, false);
  entry2.updatePath(eBgpPeer2_, attrs4, false);
  entry2.updatePath(eBgpPeer3_, attrs1, false);
  entry2.updatePath(eBgpPeer4_, attrs3, false);
  entry2.updatePath(eBgpPeer5_, attrs2, false);

  RibBase::selectBestPath(
      entry2, multipathSelector, bestpathSelector, false, 0);
  rib_->ribEntries_.emplace(make_pair(kV4Prefix3, std::move(entry2)));
  auto prefix2 = std::make_unique<std::string>(
      folly::IPAddress::networkToString(kV4Prefix3));
  auto output2 = rib_->getRibEntryForPrefix(std::move(prefix2));
  {
    EXPECT_EQ(output2.size(), 1);
    auto paths = output2[0].paths()->find(kBestPathGroup)->second;
    EXPECT_EQ(2, paths.size());

    /*
     * the paths' order here doesn't matter as long as the filter description
     * matched the record
     * Also check the filter Criteria are set properly
     * Both attrs (attrs1 & attrs2) are selected as ECMP paths however nextHop3
     * will win in best-path due to Lowest router-id; 6 vs. 127.3.0.1.
     */
    std::vector<std::pair<std::string, std::string>> path_prefixes(
        paths.size());
    std::transform(
        paths.cbegin(), paths.cend(), path_prefixes.begin(), [](auto p) {
          return std::pair<std::string, std::string>(
              *p.next_hop()->prefix_bin(), *p.bestpath_filter_descr());
        });
    EXPECT_THAT(
        path_prefixes,
        UnorderedElementsAre(
            Pair(
                network::toBinaryAddress(kV4Nexthop3).addr()->toStdString(),
                "Router-Id, Filter Criterion: Choose Lowest Value"),
            Pair(
                network::toBinaryAddress(kV4Nexthop5).addr()->toStdString(),
                "")));

    auto nonecmpPaths = output2[0].paths()->find(kDefaultPathGroup)->second;
    EXPECT_EQ(3, nonecmpPaths.size());

    /*
     * the paths'order here doesn't matter as long as the filter description
     * matched the record
     */
    std::unordered_set<std::string> nonecmpPathNhs{
        *nonecmpPaths[0].next_hop()->prefix_bin(),
        *nonecmpPaths[1].next_hop()->prefix_bin(),
        *nonecmpPaths[2].next_hop()->prefix_bin()};
    EXPECT_THAT(
        nonecmpPathNhs,
        UnorderedElementsAre(
            network::toBinaryAddress(kV4Nexthop1).addr()->toStdString(),
            network::toBinaryAddress(kV4Nexthop2).addr()->toStdString(),
            network::toBinaryAddress(kV4Nexthop4).addr()->toStdString()));

    // Check non-selected (non-ecmp) attrs
    for (const auto& path : nonecmpPaths) {
      const auto& prefix = *path.next_hop()->prefix_bin();
      const auto& filter = *path.bestpath_filter_descr();
      if (prefix ==
          network::toBinaryAddress(kV4Nexthop1).addr()->toStdString()) {
        // attrs5 : looses due to local-pref
        EXPECT_EQ(
            filter, "Local Preference, Filter Criterion: Choose Highest Value");
      }
      if (prefix ==
          network::toBinaryAddress(kV4Nexthop2).addr()->toStdString()) {
        // attrs4 : looses due to as-path
        EXPECT_EQ(filter, "AS-Path Len, Filter Criterion: Choose Lowest Value");
      }
      if (prefix ==
          network::toBinaryAddress(kV4Nexthop4).addr()->toStdString()) {
        // attrs3: looses due to Origin
        EXPECT_EQ(filter, "Origin Code, Filter Criterion: Choose Lowest Value");
      }
    }
  }

  /*
   * Case-4:
   *      add rib-entry3 :: 2 attrs -- with same router ID but different peer IP
   */
  RibEntry entry3(kV4Prefix4);
  entry3.updatePath(eBgpPeer1_, attrs1, false);
  entry3.updatePath(eBgpPeer6_, attrs1, false);

  RibBase::selectBestPath(
      entry3, multipathSelector, bestpathSelector, false, 0);
  rib_->ribEntries_.emplace(make_pair(kV4Prefix4, std::move(entry3)));
  auto prefix3 = std::make_unique<std::string>(
      folly::IPAddress::networkToString(kV4Prefix4));
  auto output3 = rib_->getRibEntryForPrefix(std::move(prefix3));
  {
    EXPECT_EQ(output3.size(), 1);
    auto paths = output3[0].paths()->find(kBestPathGroup)->second;
    EXPECT_THAT(
        paths,
        UnorderedElementsAre(
            AllOf(
                ResultOf(
                    [](const auto& p) { return *p.peer_id()->prefix_bin(); },
                    Eq(network::toBinaryAddress(kPeerAddr6)
                           .addr()
                           ->toStdString())),
                ResultOf(
                    [](const auto& p) { return *p.bestpath_filter_descr(); },
                    Eq("Peer IP, Filter Criterion: Choose Lowest Value"))),
            AllOf(
                ResultOf(
                    [](const auto& p) { return *p.peer_id()->prefix_bin(); },
                    Eq(network::toBinaryAddress(kPeerAddr1)
                           .addr()
                           ->toStdString())),
                ResultOf(
                    [](const auto& p) { return *p.bestpath_filter_descr(); },
                    Eq("")))));
  }
}

TEST_F(RibFixture, InjectRemoveLocalRouteTest) {
  rib_->setFibBatchTime(milliseconds(8));

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(4);
  // when EoR is called
  EXPECT_CALL(*fib_, program_(true)).Times(1);
  // two for announcement and one for withdrawal
  EXPECT_CALL(*fib_, program_(false)).Times(3);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
      .Times(3);

  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  // inject
  folly::EventBase localEvb;
  auto& localFm = folly::fibers::getFiberManager(
      localEvb, nettools::bgplib::getFiberManagerOptions());

  // with limited attributes, see if we set default correctly
  localFm.addTask([&] {
    // inject local route kV4Prefix1
    auto tBgpCommunities = std::vector<TBgpCommunity>(
        createTBgpCommunities({kCommunity1, kCommunity2}));
    fibFuture = fib_->getFibProgramFuture();

    std::map<TIpPrefix, TBgpAttributes> routesToInject;
    TBgpAttributes attributes;
    attributes.communities() = std::move(tBgpCommunities);
    routesToInject[createTIpPrefix(kV4Prefix1)] = std::move(attributes);

    rib_->injectLocalRoutes(routesToInject);
  });
  localEvb.loop();

  {
    // verify injection
    fibFuture.wait();

    auto bestpath = rib_->getBestPath(kV4Prefix1);
    EXPECT_NE(bestpath, nullptr);
    if (bestpath) {
      EXPECT_EQ(kLocalRouteV4Nexthop, bestpath->attrs->getNexthop());
      EXPECT_EQ(kDefaultLocalPref, *bestpath->attrs->getLocalPref());
      EXPECT_EQ(
          nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_IGP,
          bestpath->attrs->getOrigin());
      EXPECT_EQ(0, bestpath->attrs->getBgpAsPathLen());
      EXPECT_EQ(0, bestpath->attrs->getBgpAsPathLenWithConfed());
      EXPECT_EQ(0, bestpath->attrs->getOriginatorId());
      EXPECT_EQ(kV4LocalPeerInfo, bestpath->peer);
      EXPECT_EQ(
          facebook::bgp::createBgpAttrCommunitiesC({kCommunity1, kCommunity2}),
          bestpath->attrs->getCommunities().get());
      EXPECT_TRUE(bestpath->attrs->getExtCommunities().nullOrEmpty());
      EXPECT_TRUE(bestpath->attrs->getClusterList().nullOrEmpty());
    }
  }

  // with most of the attributes, see if we set corresponding fields correctly
  localFm.addTask([&] {
    // inject local route kV4Prefix1
    auto tBgpCommunities = std::vector<TBgpCommunity>(
        createTBgpCommunities({kCommunity1, kCommunity2}));
    fibFuture = fib_->getFibProgramFuture();

    std::map<TIpPrefix, TBgpAttributes> routesToInject;
    TBgpAttributes attributes;
    TIpPrefix tPrefix;
    auto binAddr = network::toBinaryAddress(kPrefix1.first);
    tPrefix.afi() =
        kPrefix1.first.isV4() ? TBgpAfi::AFI_IPV4 : TBgpAfi::AFI_IPV6;
    tPrefix.num_bits() = kPrefix1.second;
    tPrefix.prefix_bin() = binAddr.addr()->toStdString();
    attributes.nexthop() = tPrefix;
    attributes.local_pref() = kLocalPref2;
    attributes.origin() = 1; // BGP_ORIGIN_EGP
    attributes.communities() = std::move(tBgpCommunities);

    // asPath int64 == uint32
    auto tAsPath = createTAsPath(
        {std::make_pair(
             4, std::vector<int64_t>{kAsn1, kAsn2, kAsn3}), // AS_CONFED_SET
         std::make_pair(1, std::vector<int64_t>{kAsn4, kAsn5})}); // AS_SET

    attributes.as_path() = tAsPath;
    routesToInject[createTIpPrefix(kV4Prefix1)] = std::move(attributes);

    rib_->injectLocalRoutes(routesToInject);
  });
  localEvb.loop();

  {
    // verify injection
    fibFuture.wait();

    auto bestpath = rib_->getBestPath(kV4Prefix1);
    EXPECT_NE(bestpath, nullptr);
    if (bestpath) {
      EXPECT_EQ(kPrefix1.first, bestpath->attrs->getNexthop());
      EXPECT_EQ(kLocalPref2, *bestpath->attrs->getLocalPref());
      EXPECT_EQ(
          nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP,
          bestpath->attrs->getOrigin());
      EXPECT_EQ(kV4LocalPeerInfo, bestpath->peer);
      EXPECT_EQ(
          facebook::bgp::createBgpAttrCommunitiesC({kCommunity1, kCommunity2}),
          bestpath->attrs->getCommunities().get());
      std::vector<BgpAttrAsPathSegmentC> expectedAsPath =
          createBgpAttrAsPathSegmentCV(
              {std::make_pair(
                   4,
                   std::vector<uint32_t>{kAsn1, kAsn2, kAsn3}), // AS_CONFED_SET
               std::make_pair(
                   1, std::vector<uint32_t>{kAsn4, kAsn5})}); // AS_SET
      EXPECT_THAT(
          expectedAsPath,
          UnorderedElementsAreArray(*bestpath->attrs->getAsPath()));
      EXPECT_TRUE(bestpath->attrs->getClusterList().nullOrEmpty());
    }
  }

  // remove
  localFm.addTask([&] {
    // remove local route kV4Prefix1
    fibFuture = fib_->getFibProgramFuture();
    std::set<TIpPrefix> prefixSet = {createTIpPrefix(kV4Prefix1)};
    rib_->removeLocalRoutes(prefixSet);
  });
  localEvb.loop();

  {
    // verify removal
    fibFuture.wait();

    auto bestpath = rib_->getBestPath(kV4Prefix1);
    EXPECT_FALSE(bestpath);
  }
}

TEST_F(RibFixture, ScaleInjectRemoveLocalRouteTest) {
  const u_int16_t numOfRoutes = 50;
  const auto prefixes = getv4Prefixes(numOfRoutes);

  rib_->setFibBatchTime(milliseconds(16));
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(2 * numOfRoutes);

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendInitialPathComputation();
  ribFuture.wait();

  // inject
  folly::EventBase localEvb;
  auto& localFm = folly::fibers::getFiberManager(
      localEvb, nettools::bgplib::getFiberManagerOptions());
  localFm.addTask([&] {
    // inject 50 local routes
    auto tBgpCommunities = std::vector<TBgpCommunity>(
        createTBgpCommunities({kCommunity1, kCommunity2}));

    auto routesToInject =
        std::make_unique<std::map<TIpPrefix, TBgpAttributes>>();

    for (const auto& prefix : prefixes) {
      TBgpAttributes attributes;
      attributes.communities() = tBgpCommunities;
      (*routesToInject)[createTIpPrefix(prefix)] = std::move(attributes);
    }

    ribFuture = rib_->getRibPrepareFibProgrammingFuture(routesToInject->size());

    service_->addNetworks(std::move(routesToInject));
  });
  localEvb.loop();

  {
    // verify injection
    ribFuture.wait();

    for (const auto& prefix : prefixes) {
      auto bestpath = rib_->getBestPath(prefix);
      EXPECT_NE(bestpath, nullptr);
      if (bestpath) {
        EXPECT_EQ(kLocalRouteV4Nexthop, bestpath->attrs->getNexthop());
        EXPECT_EQ(kDefaultLocalPref, *bestpath->attrs->getLocalPref());
        EXPECT_EQ(
            nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_IGP,
            bestpath->attrs->getOrigin());
        EXPECT_EQ(kV4LocalPeerInfo, bestpath->peer);
        EXPECT_EQ(
            facebook::bgp::createBgpAttrCommunitiesC(
                {kCommunity1, kCommunity2}),
            bestpath->attrs->getCommunities().get());
      }
    }
  }

  // remove
  localFm.addTask([&] {
    // remove 50 local routes
    auto prefixSetPtr = std::make_unique<std::set<TIpPrefix>>();
    for (const auto& prefix : prefixes) {
      prefixSetPtr->insert(createTIpPrefix(prefix));
    }

    ribFuture = rib_->getRibPrepareFibProgrammingFuture(prefixSetPtr->size());

    service_->delNetworks(std::move(prefixSetPtr));
  });
  localEvb.loop();

  {
    // verify removal
    ribFuture.wait();

    for (const auto& prefix : prefixes) {
      auto bestpath = rib_->getBestPath(prefix);
      EXPECT_FALSE(bestpath);
    }
  }
}

TEST_F(RibFixture, GetOriginatedRoutes) {
  std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork> localRoutes;
  // Create config with v4(no communities), v6(with communities) local routes
  thrift::BgpNetwork network4_1;
  network4_1.prefix() = IPAddress::networkToString(kV4Prefix1);
  localRoutes[kV4Prefix1] = network4_1;

  thrift::BgpNetwork network6_1;
  auto commStr = std::to_string(kCommAsNum) + ":" + std::to_string(kCommAsVal);
  network6_1.prefix() = IPAddress::networkToString(kV6Prefix1);
  std::optional<std::vector<std::string>> communities({commStr});
  network6_1.communities().from_optional(communities);
  localRoutes[kV6Prefix1] = network6_1;

  RibDC rib(
      localRoutes,
      *bgpGlobalConfig1_,
      std::nullopt, /* policyConfig */
      ribInQ_,
      ribOutQ_,
      kDevPlatform,
      nullptr /* fsdbSyncer */);

  auto& fm = folly::fibers::getFiberManager(rib.evb_);

  folly::fibers::Baton syncBaton;
  // create a dummy task to make event loop not exit.
  fm.addTask(
      [&]() { facebook::bgp::test::boundedBatonWait(syncBaton, "syncBaton"); });

  std::thread thd([&]() { rib.evb_.loop(); });
  auto routes = rib.getOriginatedRoutes();
  syncBaton.post();

  ASSERT_EQ(2, routes.size());

  for (auto i = 0; i < routes.size(); i++) {
    auto tPrefix = *routes[i].prefix();
    if (*tPrefix.afi() == TBgpAfi::AFI_IPV4) {
      EXPECT_EQ(TBgpAfi::AFI_IPV4, *tPrefix.afi());
      EXPECT_EQ(kV4Prefix1.second, *tPrefix.num_bits());
      auto binAddr = facebook::network::toBinaryAddress(kV4Prefix1.first);
      EXPECT_EQ(binAddr.addr()->toStdString(), *tPrefix.prefix_bin());

      /*
       * Verify that communities was not passed in configuration(optional),
       * but returned empty vector for display
       */
      EXPECT_EQ(0, routes[i].communities()->size());

    } else {
      EXPECT_EQ(TBgpAfi::AFI_IPV6, *tPrefix.afi());
      EXPECT_EQ(kV6Prefix1.second, *tPrefix.num_bits());
      auto binAddr = facebook::network::toBinaryAddress(kV6Prefix1.first);
      EXPECT_EQ(binAddr.addr()->toStdString(), *tPrefix.prefix_bin());

      // Verify that communities is returned properly
      auto& tComms = *routes[i].communities();
      ASSERT_EQ(1, tComms.size());
      EXPECT_EQ(kCommAsNum, *tComms[0].asn());
      EXPECT_EQ(kCommAsVal, *tComms[0].value());
      EXPECT_EQ(
          ((int64_t)kCommAsNum << 16) + kCommAsVal, *tComms[0].community());
    }
  }

  thd.join();
}

/*
 * check if unicastRouteLogging is enabled for default route with associated
 * flag
 */
TEST_F(RibFixture, EnableUnicastRouteLoggingTest) {
  FLAGS_enable_default_route_logging = false;

  EXPECT_FALSE(rib_->enableUnicastRouteLogging(kDefaultRouteV4));

  EXPECT_FALSE(rib_->enableUnicastRouteLogging(kDefaultRouteV6));

  FLAGS_enable_default_route_logging = true;

  EXPECT_TRUE(rib_->enableUnicastRouteLogging(kDefaultRouteV4));

  EXPECT_TRUE(rib_->enableUnicastRouteLogging(kDefaultRouteV6));

  EXPECT_FALSE(rib_->enableUnicastRouteLogging(
      folly::CIDRNetwork(
          folly::IPAddress("::"), 32))); // wrong subnet of default route

  EXPECT_FALSE(rib_->enableUnicastRouteLogging(
      folly::CIDRNetwork(folly::IPAddress("1.2.3.4"), 1))); // not default route
}

TEST_F(RibFixture, ScubaLoggingTest) {
  // Ensure ribPolicyLogger is not created
  EXPECT_EQ(nullptr, rib_->ribPolicyLogger_);
}

/**
 * This test verifies that when PauseBestPathAndFibProgramming message is
 * sent to Rib and ResumeBestPathAndFibProgramming message is not sent within
 * the set timeout, Rib resumes best path computation and Fib programming at
 * the timeout.
 */
TEST_F(RibFixture, RibPauseTimeOutTest) {
  auto fibFuture = fib_->getFibProgramFuture();
  // Step 1: Set ribPauseTime to 1 second
  rib_->setRibPauseTime(seconds(1));

  // Step 2: Send EOR to simulate RIB in steady state
  sendInitialPathComputation();
  fibFuture.wait();

  /*
   * Step 3: Send PauseBestPathAndFibProgramming message to rib and verify
   * best path and Fib programming is paused
   */
  sendPauseBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE);
  WITH_RETRIES_N_TIMED(3000, milliseconds(10), {
    EXPECT_EVENTUALLY_TRUE(isBestPathAndFibProgrammingPaused());
    EXPECT_EVENTUALLY_EQ(
        1, rib_->bestPathAndFibProgrammingPausedBy_.rlock()->size());
  });

  // Step 4: Verify RIB is unpaused after timeout
  WITH_RETRIES(
      { ASSERT_EVENTUALLY_FALSE(isBestPathAndFibProgrammingPaused()); });

  WITH_RETRIES({
    facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
        fmt::format(
            "{}.avg.60", RibStats::ribBestPathAndFibProgrammingPauseTimeMs));
  });
  EXPECT_EQ(0, rib_->bestPathAndFibProgrammingPausedBy_.rlock()->size());
}

/**
 * This test verifies that when PauseBestPathAndFibProgramming message is
 * sent to Rib from multiple operations and ResumeBestPathAndFibProgramming
 * message is not sent within the set timeout, Rib resumes best path computation
 * and Fib programming at the timeout.
 */
TEST_F(RibFixture, RibPauseTimeOutMultipleTasksTest) {
  auto fibFuture = fib_->getFibProgramFuture();
  // Step 1: Set ribPauseTime to 100 milliseconds
  rib_->setRibPauseTime(milliseconds(100));

  // Step 2: Send EOR to simulate RIB in steady state
  sendInitialPathComputation();
  fibFuture.wait();

  /*
   * Step 3: Send PauseBestPathAndFibProgramming message to rib from
   * BACKPRESSURE and wait for the non-expiring pause to take effect
   */
  sendPauseBestPathAndFibProgramming(RibPauseResumeCause::BACKPRESSURE);
  WITH_RETRIES({
    EXPECT_EVENTUALLY_TRUE(isBestPathAndFibProgrammingPaused());
    EXPECT_EVENTUALLY_TRUE(
        rib_->bestPathAndFibProgrammingPausedBy_.rlock()->contains(
            RibPauseResumeCause::BACKPRESSURE));
  });

  // Step 4: Send PauseBestPathAndFibProgramming message to rib from SAFE_MODE
  sendPauseBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE);

  /*
   * Step 5: Verify best path and Fib programming is paused and size of
   * bestPathAndFibProgrammingPausedBy_ set is 2
   */
  WITH_RETRIES_N_TIMED(200, milliseconds(5), {
    EXPECT_EVENTUALLY_TRUE(isBestPathAndFibProgrammingPaused());
    EXPECT_EVENTUALLY_EQ(
        2, rib_->bestPathAndFibProgrammingPausedBy_.rlock()->size());
  });

  folly::EventBase testEvb;
  testEvb.scheduleAt(
      [&]() noexcept {
        // Step 6: Verify RIB is still paused after timeout
        EXPECT_TRUE(isBestPathAndFibProgrammingPaused());
        EXPECT_EQ(1, rib_->bestPathAndFibProgrammingPausedBy_.rlock()->size());

        /*
         * Step 7: Verify bestPathAndFibProgrammingPausedBy_ is cleared after
         * timeout
         */
        sendResumeBestPathAndFibProgramming(RibPauseResumeCause::BACKPRESSURE);

        WITH_RETRIES(
            { ASSERT_EVENTUALLY_FALSE(isBestPathAndFibProgrammingPaused()); });
        EXPECT_EQ(0, rib_->bestPathAndFibProgrammingPausedBy_.rlock()->size());

        WITH_RETRIES({
          facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
              fmt::format(
                  "{}.avg.60",
                  RibStats::ribBestPathAndFibProgrammingPauseTimeMs));
        });
      },
      steady_clock::now() + milliseconds(200));

  // let eventbase run
  testEvb.loop();
}

TEST_F(RibFixture, GetSwitchIdTest) {
  // std::nullopt is returned if switchId is not set
  EXPECT_FALSE(getSwitchId(std::nullopt));
  // std::nullopt is returned if switchId is empty
  EXPECT_FALSE(getSwitchId(""));
  // std::nullopt is returned if switchId has no '.' in it
  EXPECT_FALSE(getSwitchId("foo"));
  // std::nullopt is returned if first segment does not end with digits
  EXPECT_FALSE(getSwitchId("foo.bar"));

  // FE
  EXPECT_EQ(1, getSwitchId("foo001.bar"));
  EXPECT_EQ(2, getSwitchId("rsw002.p001.f01.abc1"));
  EXPECT_EQ(3, getSwitchId("fsw003.p001.f01.abc1"));
  EXPECT_EQ(4, getSwitchId("ssw004.s001.f01.abc1"));
  EXPECT_EQ(5, getSwitchId("fa001-du005.abc1"));
  EXPECT_EQ(6, getSwitchId("fa001-uu006.abc1"));
  EXPECT_EQ(2, getSwitchId("fa001-uu002.abc1"));
  EXPECT_EQ(1, getSwitchId("fa002-uu001.abc1"));
  EXPECT_EQ(2, getSwitchId("fa001-uu2.abc1"));

  // BE
  EXPECT_EQ(1, getSwitchId("rtsw1.u001.c001.abc1"));
  EXPECT_EQ(10, getSwitchId("rtsw10.u001.c001.abc1"));
  EXPECT_EQ(7, getSwitchId("rtsw007.u001.c001.abc1"));
  EXPECT_EQ(8, getSwitchId("ftsw008.r001.c001.abc1"));
  EXPECT_EQ(9, getSwitchId("stsw009.r001.c001.abc1"));
  EXPECT_EQ(1024, getSwitchId("rtsw1024.u001.c001.abc1"));
  EXPECT_EQ(1024, getSwitchId("1024.u001.c001.abc1"));
}

TEST_F(RibFixture, RouteChurnDetectionTest) {
  RibStats::initCounters();
  // set route churn detection thresholds
  rib_->setRouteChurnDetectionThresholds(5, 2, 2s);

  PrefixPathIds prefixBatch1;
  for (uint16_t i = 1; i <= 10; ++i) {
    auto prefix =
        folly::IPAddress::createNetwork(fmt::format("3:{}::/64", i), 64);
    prefixBatch1.emplace_back(prefix, kDefaultPathID);
  }

  auto fibFuture = fib_->getFibProgramFuture();
  // Step 1: Send EOR to simulate RIB in steady state
  sendInitialPathComputation();

  // Step 2: Send 10 announcements to rib
  sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
  fibFuture.wait();

  // Step 3: Expect Route churn detection to be triggered and detect a churn
  WITH_RETRIES_N(4, {
    EXPECT_EVENTUALLY_TRUE(rib_->routeChurnDetected_);
    EXPECT_EVENTUALLY_TRUE(isBestPathAndFibProgrammingPaused());
  });
  facebook::fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_GT(
      facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
          RibStats::kTotalRouteChurnDetected),
      0);

  // Step 4: Send 1 prefix batch (Lower than the lower watermark)
  auto prefixBatch2 = PrefixPathIds{
      {folly::IPAddress::createNetwork("2::/64"), kDefaultPathID}};
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);
  fibFuture.wait();

  // Step 5: Ensure Route churn is stable with checks at constant intervals
  folly::EventBase testEvb;
  testEvb.scheduleAt(
      [&]() noexcept {
        EXPECT_FALSE(rib_->routeChurnDetected_);
        EXPECT_FALSE(isBestPathAndFibProgrammingPaused());
      },
      steady_clock::now() + seconds(2));

  testEvb.loop();
}

/**
 * Verifies that route churn detection is not triggered when the Rib is not
 * initialized.
 *
 * Route churn detection should only be triggered after Rib initialization, as
 * it optimizes the number of best path computation and FIB programming cycles,
 * which are not performed until Rib initialization.
 */
TEST_F(RibFixture, RouteChurnDetectionTestBeforeRibInitialization) {
  RibStats::initCounters();
  // set route churn detection thresholds
  rib_->setRouteChurnDetectionThresholds(5, 2, 2s);

  PrefixPathIds prefixBatch1;
  for (uint16_t i = 1; i <= 10; ++i) {
    auto prefix =
        folly::IPAddress::createNetwork(fmt::format("3:{}::/64", i), 64);
    prefixBatch1.emplace_back(prefix, kDefaultPathID);
  }

  // Step 1: Send 10 announcements to rib before Rib is initialized
  sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);

  // Step 2: Route churn should not be detected
  WITH_RETRIES({ ASSERT_FALSE(rib_->routeChurnDetected_); });
  EXPECT_FALSE(isBestPathAndFibProgrammingPaused());

  facebook::fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(
      facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
          RibStats::kTotalRouteChurnDetected),
      0);

  // Step 3: Initialize Rib and send 10 more announcements to rib
  PrefixPathIds prefixBatch2;
  for (uint16_t i = 1; i <= 10; ++i) {
    auto prefix =
        folly::IPAddress::createNetwork(fmt::format("4:{}::/64", i), 64);
    prefixBatch2.emplace_back(prefix, kDefaultPathID);
  }
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);
  fibFuture.wait();

  // Step 4: Expect Route churn detection to be triggered and detect a churn
  WITH_RETRIES_N(4, {
    EXPECT_EVENTUALLY_TRUE(rib_->routeChurnDetected_);
    EXPECT_EVENTUALLY_TRUE(isBestPathAndFibProgrammingPaused());
  });
  facebook::fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_GT(
      facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
          RibStats::kTotalRouteChurnDetected),
      0);
}

TEST_F(RibFixture, RouteChurnDetectionTestWithAnnouncementsAndWithdraws) {
  RibStats::initCounters();
  // set route churn detection thresholds
  rib_->setRouteChurnDetectionThresholds(5, 2, 2s);

  PrefixPathIds prefixBatch1;
  for (uint16_t i = 1; i <= 10; ++i) {
    auto prefix =
        folly::IPAddress::createNetwork(fmt::format("1:{}::/64", i), 64);
    prefixBatch1.emplace_back(prefix, kDefaultPathID);
  }
  auto prefixBatch2 = PrefixPathIds{
      {folly::IPAddress::createNetwork("2::/64"), kDefaultPathID}};

  // Step 1: Send RibInEOR to simulate RIB in steady state
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  // Step 2: Now, in a loop, announce, withdraw and re-advertise the routes
  for (int i = 0; i < 5; i++) {
    fibFuture = fib_->getFibProgramFuture();
    sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
    sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);
    sendWithdrawal(prefixBatch1, iBgpPeer_);
    sendWithdrawal(prefixBatch2, iBgpPeer_);
    sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
    sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);
    fibFuture.wait();
  }

  // Step 3: Expect Route churn detection to be triggered and detect a churn
  WITH_RETRIES({ ASSERT_EVENTUALLY_TRUE(rib_->routeChurnDetected_); });
  EXPECT_TRUE(isBestPathAndFibProgrammingPaused());
  facebook::fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_GT(
      facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
          RibStats::kTotalRouteChurnDetected),
      0);

  /*
   * Step 4: No more announcements sent, Ensure Route churn is stable with
   * checks at constant intervals
   */
  folly::EventBase testEvb;
  testEvb.scheduleAt(
      [&]() noexcept {
        EXPECT_FALSE(rib_->routeChurnDetected_);
        EXPECT_FALSE(isBestPathAndFibProgrammingPaused());
      },
      steady_clock::now() + seconds(2));

  testEvb.loop();
}

TEST_F(RibFixture, AnnounceIncludesPathIdOnRouteInfo) {
  uint32_t expectedPathId = 135;
  PrefixPathIds prefixBatch{{kV4Prefix1, expectedPathId}};
  sendAnnouncement(prefixBatch, iBgpPeer_, attr_);
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto& ribEntries = rib_->ribEntries_;
    EXPECT_EQ(1, ribEntries.size());
    auto& ribEntry = ribEntries.at(kV4Prefix1);
    EXPECT_EQ(1, ribEntry.getAllPathsCnt());
    auto routeInfos = ribEntry.getRouteInfos(
        nettools::bgplib::BgpPeerId(iBgpPeer_.addr, iBgpPeer_.routerId));
    EXPECT_EQ(1, routeInfos.size());
    EXPECT_TRUE(routeInfos.contains(expectedPathId));
    EXPECT_EQ(routeInfos.begin()->second->receivedPathId, expectedPathId);
  });
}

TEST_F(RibFixture, PathsAreUpdatedByPathId) {
  /*
   * updatePath(1) creates routeInfo w/ pathID 1
   * updatePath(2) (with same attrs) creates different routeInfo w/ pathID 2
   * 2nd updatePath(1) updates route w/ pathID 1, even if nh is different
   * updatePath(1) with null attrs (withdraw) removes routeinfo w/ path 1
   */
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs2->setNexthop(kV4Nexthop2);
  attrs2->setLocalPref(kLocalPref2);
  attrs1->publish();
  attrs2->publish();

  RibEntry entry1(kV4Prefix1);
  uint32_t path1Id = 1;
  uint32_t path2Id = 2;
  entry1.updatePath(eBgpPeer1_, attrs1, true, path1Id);

  // verify routeinfo with ID 1. One RouteInfo
  EXPECT_EQ(1, entry1.getAllPathsCnt());
  auto routeInfos = entry1.getRouteInfos(
      nettools::bgplib::BgpPeerId(eBgpPeer1_.addr, eBgpPeer1_.routerId));
  EXPECT_EQ(routeInfos[path1Id]->attrs, attrs1);

  entry1.updatePath(eBgpPeer1_, attrs1, true, path2Id);

  // verify routeinfo with ID 2. Two RouteInfos
  EXPECT_EQ(2, entry1.getAllPathsCnt());
  routeInfos = entry1.getRouteInfos(
      nettools::bgplib::BgpPeerId(eBgpPeer1_.addr, eBgpPeer1_.routerId));
  EXPECT_EQ(routeInfos[path2Id]->attrs, attrs1);

  entry1.updatePath(eBgpPeer1_, attrs2, true, path1Id);

  // verify routeinfo with ID 1 is updated. Still two RouteInfos
  EXPECT_EQ(2, entry1.getAllPathsCnt());
  routeInfos = entry1.getRouteInfos(
      nettools::bgplib::BgpPeerId(eBgpPeer1_.addr, eBgpPeer1_.routerId));
  EXPECT_EQ(routeInfos[path1Id]->attrs, attrs2);

  entry1.updatePath(eBgpPeer1_, nullptr, true, path1Id);

  // verify routeinfo with ID 1 is gone. ID 2 is still there
  EXPECT_EQ(1, entry1.getAllPathsCnt());
  routeInfos = entry1.getRouteInfos(
      nettools::bgplib::BgpPeerId(eBgpPeer1_.addr, eBgpPeer1_.routerId));
  EXPECT_FALSE(routeInfos.contains(path1Id));
  EXPECT_TRUE(routeInfos.contains(path2Id));

  // announce from a second peer: new outer-map entry bumps the count
  entry1.updatePath(eBgpPeer2_, attrs1, true, path1Id);
  EXPECT_EQ(2, entry1.getAllPathsCnt());

  // no-op re-announcement (same attrs, same peer) leaves the count alone
  EXPECT_FALSE(entry1.updatePath(eBgpPeer2_, attrs1, true, path1Id));
  EXPECT_EQ(2, entry1.getAllPathsCnt());

  // withdrawing an unknown path ID is a no-op for the count
  EXPECT_FALSE(entry1.updatePath(eBgpPeer2_, nullptr, true, path2Id));
  EXPECT_EQ(2, entry1.getAllPathsCnt());

  // withdrawing all remaining paths drains the count to zero
  entry1.updatePath(eBgpPeer1_, nullptr, true, path2Id);
  entry1.updatePath(eBgpPeer2_, nullptr, true, path1Id);
  EXPECT_EQ(0, entry1.getAllPathsCnt());
  EXPECT_EQ(0, entry1.getAllPaths().size());
}

namespace {
void setNexthopTrackingFeatureFlag(bool enabled);
int64_t inactivePathCount();
int64_t timeseriesCount(const std::string& name);
} // namespace

TEST_F(RibNexthopTrackingFixture, RibInAnnouncementWithNexthopTracking) {
  auto tcData = fb303::ThreadCachedServiceData::get();
  RibStats::initCounters();

  // Create a NexthopStatus with IGP cost
  NexthopStatus status(kPeerAddr1, true, 100);

  // Add the status to the cache
  std::vector<NexthopStatus> nexthopStatusList;
  nexthopStatusList.push_back(status);
  updateCacheAndNotifyRib(nexthopStatusList);

  auto prefix1 = folly::IPAddress::createNetwork("1::/64");
  auto prefixBatch1 = PrefixPathIds{{prefix1, kDefaultPathID}};

  const auto attrs = *buildBgpPathFields(4, 4, 4, 4);
  auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(attrs);
  attrs1->setNexthop(kPeerAddr1);
  attrs1->publish();
  auto fibFuture = fib_->getFibProgramFuture();
  // send route from iBGP Peer
  sendAnnouncement(prefixBatch1, iBgpPeer_, attrs1);
  sendInitialPathComputation();
  fibFuture.wait();

  // Verify that the route was added to the Rib
  auto entry = rib_->ribEntries_.find(prefix1);
  ASSERT_NE(entry, rib_->ribEntries_.end());

  // Verify that the NexthopInfo was created in the nexthopInfoMap_
  auto nexthopInfoIt = rib_->nexthopInfoMap_.find(kPeerAddr1);
  ASSERT_NE(nexthopInfoIt, rib_->nexthopInfoMap_.end());

  // Verify that the NexthopInfo has the correct IGP cost
  EXPECT_EQ(nexthopInfoIt->second.getIgpCost(), std::optional<uint32_t>(100));

  // Verify that the RouteInfo is linked to the NexthopInfo
  EXPECT_EQ(nexthopInfoIt->second.getRouteInfoListSize(), 1);

  // Verify that the RouteInfo's getIgpCostValue returns the correct cost
  auto routeInfos = entry->second.getAllPaths();
  ASSERT_EQ(routeInfos.size(), 1);
  EXPECT_EQ(routeInfos[0]->getIgpCostValue(), 100);

  // Reachable nexthop should not increment the unresolvable counter
  tcData->publishStats();
  EXPECT_EQ(0, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));
}

TEST_F(RibNexthopTrackingFixture, PathUpdatesMaintainNexthopAssociations) {
  RibStats::initCounters();
  updateCacheAndNotifyRib(
      {NexthopStatus(kPeerAddr1, false), NexthopStatus(kPeerAddr2, false)});
  auto tcData = fb303::ThreadCachedServiceData::get();

  const auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  const auto peerId = BgpPeerId{iBgpPeer_.addr, iBgpPeer_.routerId};
  auto makeAttrs = [](int asCount, const folly::IPAddress& nexthop) {
    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(asCount, 4, 4, 4));
    attrs->setNexthop(nexthop);
    attrs->publish();
    return attrs;
  };

  auto initialAttrs = makeAttrs(4, kPeerAddr1);
  std::shared_ptr<RouteInfo> initialRouteInfo;
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->processSingleRibInUpdate(
        iBgpPeer_, initialAttrs, prefixBatch.front());
    auto entry = rib_->ribEntries_.find(kV4Prefix1);
    ASSERT_NE(entry, rib_->ribEntries_.end());
    initialRouteInfo = entry->second.getRouteInfo(peerId, kDefaultPathID);
    ASSERT_NE(initialRouteInfo, nullptr);
    auto nexthopInfo = rib_->nexthopInfoMap_.find(kPeerAddr1);
    ASSERT_NE(nexthopInfo, rib_->nexthopInfoMap_.end());
    EXPECT_EQ(1, nexthopInfo->second.getRouteInfoListSize());
  });
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->processSingleRibInUpdate(
        iBgpPeer_, initialAttrs, prefixBatch.front());
    const auto& entry = rib_->ribEntries_.at(kV4Prefix1);
    EXPECT_EQ(initialRouteInfo, entry.getRouteInfo(peerId, kDefaultPathID));
    EXPECT_EQ(1, rib_->nexthopInfoMap_.at(kPeerAddr1).getRouteInfoListSize());
  });

  auto refreshedAttrs = makeAttrs(5, kPeerAddr1);
  std::shared_ptr<RouteInfo> refreshedRouteInfo;
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->processSingleRibInUpdate(
        iBgpPeer_, refreshedAttrs, prefixBatch.front());
    const auto& entry = rib_->ribEntries_.at(kV4Prefix1);
    refreshedRouteInfo = entry.getRouteInfo(peerId, kDefaultPathID);
    ASSERT_NE(refreshedRouteInfo, nullptr);
    EXPECT_NE(initialRouteInfo, refreshedRouteInfo);
    EXPECT_FALSE(initialRouteInfo->isOnNextHopList());
    EXPECT_TRUE(refreshedRouteInfo->isOnNextHopList());
    EXPECT_EQ(1, rib_->nexthopInfoMap_.at(kPeerAddr1).getRouteInfoListSize());
  });

  auto changedNexthopAttrs = makeAttrs(6, kPeerAddr2);
  std::shared_ptr<RouteInfo> changedNexthopRouteInfo;
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->processSingleRibInUpdate(
        iBgpPeer_, changedNexthopAttrs, prefixBatch.front());
    const auto& entry = rib_->ribEntries_.at(kV4Prefix1);
    changedNexthopRouteInfo = entry.getRouteInfo(peerId, kDefaultPathID);
    ASSERT_NE(changedNexthopRouteInfo, nullptr);
    EXPECT_FALSE(refreshedRouteInfo->isOnNextHopList());
    EXPECT_TRUE(changedNexthopRouteInfo->isOnNextHopList());
    EXPECT_EQ(
        rib_->nexthopInfoMap_.end(), rib_->nexthopInfoMap_.find(kPeerAddr1));
    EXPECT_EQ(1, rib_->nexthopInfoMap_.at(kPeerAddr2).getRouteInfoListSize());
    EXPECT_FALSE(rib_->nexthopCache_->isRegistered(kPeerAddr1));
  });
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->processSingleRibInUpdate(iBgpPeer_, nullptr, prefixBatch.front());
    EXPECT_FALSE(changedNexthopRouteInfo->isOnNextHopList());
    EXPECT_EQ(
        rib_->nexthopInfoMap_.end(), rib_->nexthopInfoMap_.find(kPeerAddr2));
    EXPECT_FALSE(rib_->nexthopCache_->isRegistered(kPeerAddr2));
  });
  tcData->publishStats();
  EXPECT_EQ(0, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));

  const PrefixPathId firstPath{kV6Prefix1, 1};
  const PrefixPathId secondPath{kV6Prefix1, 2};
  auto sharedNexthopAttrs = makeAttrs(4, kPeerAddr1);
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->processSingleRibInUpdate(iBgpPeer_, sharedNexthopAttrs, firstPath);
    rib_->processSingleRibInUpdate(iBgpPeer_, sharedNexthopAttrs, secondPath);
    EXPECT_EQ(2, rib_->nexthopInfoMap_.at(kPeerAddr1).getRouteInfoListSize());
  });
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->processSingleRibInUpdate(iBgpPeer_, nullptr, firstPath);
    EXPECT_EQ(1, rib_->nexthopInfoMap_.at(kPeerAddr1).getRouteInfoListSize());
    EXPECT_TRUE(rib_->nexthopCache_->isRegistered(kPeerAddr1));
  });
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->processSingleRibInUpdate(iBgpPeer_, nullptr, secondPath);
    EXPECT_EQ(
        rib_->nexthopInfoMap_.end(), rib_->nexthopInfoMap_.find(kPeerAddr1));
    EXPECT_FALSE(rib_->nexthopCache_->isRegistered(kPeerAddr1));
  });
  tcData->publishStats();
  EXPECT_EQ(0, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));
}

/**
 * Verify that nexthops learned from RIB-IN announcements are handed to the
 * nexthop-subscribe requester exactly once each (de-duped), at the RIB-IN
 * batch boundary. This is the RIB-side of the RIB-IN-driven FSDB tracking.
 */
TEST_F(
    RibNexthopTrackingFixture,
    RibInAnnouncementRequestsNexthopSubscription) {
  /*
   * All access to `requested` happens on the rib evb thread (the requester is
   * invoked there, and we read it there too), so no lock is needed.
   */
  std::vector<folly::IPAddress> requested;
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->setNexthopSubscribeRequester(
        [&requested](std::vector<folly::IPAddress> nexthops) {
          requested.insert(requested.end(), nexthops.begin(), nexthops.end());
        });
  });

  auto announce = [&](const std::string& cidr, const folly::IPAddress& nh) {
    auto batch =
        PrefixPathIds{{folly::IPAddress::createNetwork(cidr), kDefaultPathID}};
    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs->setNexthop(nh);
    attrs->publish();
    sendAnnouncement(batch, iBgpPeer_, attrs);
    /*
     * Draining the rib evb ensures processRibInAnnouncement (and its flush)
     * has run before we inspect `requested`.
     */
    rib_->evb_.runInEventBaseThreadAndWait([]() {});
  };
  auto requestedCount = [&]() {
    size_t n = 0;
    rib_->evb_.runInEventBaseThreadAndWait([&]() { n = requested.size(); });
    return n;
  };
  auto requestedAt = [&](size_t i) {
    folly::IPAddress ip;
    rib_->evb_.runInEventBaseThreadAndWait([&]() { ip = requested.at(i); });
    return ip;
  };

  // First announcement: kPeerAddr1 is requested once.
  announce("1::/64", kPeerAddr1);
  ASSERT_EQ(requestedCount(), 1);
  EXPECT_EQ(requestedAt(0), kPeerAddr1);

  // Different prefix, same nexthop: de-duped, no new request.
  announce("2::/64", kPeerAddr1);
  EXPECT_EQ(requestedCount(), 1);

  // New nexthop: requested.
  announce("3::/64", kPeerAddr2);
  ASSERT_EQ(requestedCount(), 2);
  EXPECT_EQ(requestedAt(1), kPeerAddr2);

  // Unspecified nexthop (e.g. locally-originated/aggregate routes) is skipped.
  announce("4::/64", folly::IPAddress("::"));
  EXPECT_EQ(requestedCount(), 2);

  /*
   * Drop the requester on the rib evb so the captured `requested` reference is
   * not used after this stack frame returns.
   */
  rib_->evb_.runInEventBaseThreadAndWait(
      [&]() { rib_->setNexthopSubscribeRequester(nullptr); });
}

/**
 * Tests that RouteInfo correctly stores and retrieves the parent RibEntry
 * pointer. This test verifies the back-reference mechanism that allows
 * RouteInfo objects to directly access their parent RibEntry without needing to
 * perform lookups.
 */
TEST_F(RibNexthopTrackingFixture, RouteInfoStoresRibEntryPointer) {
  // Create attributes for kPeerAddr1
  const auto attrs = *buildBgpPathFields(4, 4, 4, 4);
  auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(attrs);
  attrs1->setNexthop(kPeerAddr1);
  attrs1->publish();

  // Send an announcement to create a RibEntry and RouteInfo
  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, iBgpPeer_, attrs1);
  sendInitialPathComputation();
  fibFuture.wait();

  // Get the RibEntry from the Rib
  auto ribEntryIt = rib_->ribEntries_.find(kV4Prefix1);
  ASSERT_NE(ribEntryIt, rib_->ribEntries_.end());

  // Get the RouteInfo from the RibEntry
  auto routeInfo = ribEntryIt->second.getRouteInfo(
      nettools::bgplib::BgpPeerId{iBgpPeer_.addr, iBgpPeer_.routerId},
      kDefaultPathID);

  // Verify that the parent RibEntry reference is set correctly
  ASSERT_NE(routeInfo, nullptr);
  EXPECT_EQ(&(routeInfo->getRibEntry()), &(ribEntryIt->second));
}

/**
 * The test verifies that on receiving a RibInNexthopUpdate,
 * Rib triggers best path computation and FIB programming for the RibEntry
 * prefixes from the associated routeInfo.
 */
TEST_F(RibNexthopTrackingFixture, RibInNexthopUpdate) {
  auto tcData = fb303::ThreadCachedServiceData::get();
  RibStats::initCounters();

  auto loadBgpBestpathFeatures = [](bool enableNextHopTracking) {
    thrift::BgpConfig thriftConfig;
    thrift::BgpSettingConfig tBgpSettingConfig;
    tBgpSettingConfig.enable_med_comparison() = false;
    tBgpSettingConfig.enable_med_missing_as_worst() = false;
    tBgpSettingConfig.enable_weight_comparison() = false;
    tBgpSettingConfig.enable_next_hop_tracking() = enableNextHopTracking;
    thriftConfig.bgp_setting_config() = std::move(tBgpSettingConfig);
    FeatureFlags::LoadFromThriftConfig(thriftConfig);
  };
  loadBgpBestpathFeatures(true); // Enable next hop tracking

  // Update the batch time to 2ms for test purposes
  rib_->setFibBatchTime(milliseconds(2));
  // Create NexthopStatus objects with nexthop IPs
  NexthopStatus unreachableStatus(kPeerAddr1, false);
  // Create a NexthopStatus with reachable nexthop
  NexthopStatus reachableStatus(kPeerAddr2, true, 100);

  // Add the statuses to the cache
  std::vector<NexthopStatus> nexthopStatusList;
  nexthopStatusList.push_back(unreachableStatus);
  nexthopStatusList.push_back(reachableStatus);
  updateCacheAndNotifyRib(nexthopStatusList);

  auto prefixBatch1 = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto prefixBatch2 = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};

  const auto attrs = *buildBgpPathFields(4, 4, 4, 4);

  // Create attributes for kPeerAddr1 (unreachable)
  auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(attrs);
  attrs1->setNexthop(kPeerAddr1);
  attrs1->publish();

  // Create attributes for kPeerAddr2 (reachable)
  auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(attrs);
  attrs2->setNexthop(kPeerAddr2);
  attrs2->publish();

  // Send announcements for both nexthops
  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch1, iBgpPeer_, attrs1);
  sendAnnouncement(prefixBatch2, eBgpPeer1_, attrs2);
  sendInitialPathComputation();
  fibFuture.wait();

  // Verify both nexthops are in the nexthopInfoMap_
  auto nexthopInfoIt1 = rib_->nexthopInfoMap_.find(kPeerAddr1);
  auto nexthopInfoIt2 = rib_->nexthopInfoMap_.find(kPeerAddr2);
  ASSERT_NE(nexthopInfoIt1, rib_->nexthopInfoMap_.end());
  ASSERT_NE(nexthopInfoIt2, rib_->nexthopInfoMap_.end());
  EXPECT_EQ(nexthopInfoIt1->second.getRouteInfoListSize(), 1);
  EXPECT_EQ(nexthopInfoIt2->second.getRouteInfoListSize(), 1);

  // After announcements: 1 unreachable (kPeerAddr1), 1 reachable (kPeerAddr2)
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));

  /*
   * Update the nexthop status for kPeerAddr1 to reachable
   * Create a vector of NexthopStatus objects
   */
  nexthopStatusList.clear();

  // Create a NexthopStatus with lower IGP cost
  NexthopStatus lowerCostReachableStatus(kPeerAddr1, true, 50);
  nexthopStatusList.push_back(lowerCostReachableStatus);

  /*
   * Expect best path computation and FIB programming to be triggered on the
   * nexthop update
   */
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
  auto fibFuture2 = fib_->getFibProgramFuture();
  updateCacheAndNotifyRib(nexthopStatusList);
  fibFuture2.wait();

  // After update: kPeerAddr1 is now reachable, counter should be 0
  tcData->publishStats();
  EXPECT_EQ(0, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));
}

TEST_F(RibNexthopTrackingFixture, ReachabilityAndResolutionAreIndependent) {
  RibStats::initCounters();
  setNexthopTrackingFeatureFlag(true);
  rib_->setFibBatchTime(milliseconds(2));

  const NexthopStatus dcStatus(
      kPeerAddr1,
      /*isReachable=*/true,
      /*igpCost=*/std::nullopt,
      /*isConnected=*/std::nullopt,
      /*excludeNexthopWithoutCost=*/false);
  const NexthopStatus bbStatus(
      kPeerAddr2,
      /*isReachable=*/true,
      /*igpCost=*/std::nullopt,
      /*isConnected=*/std::nullopt,
      /*excludeNexthopWithoutCost=*/true);
  updateCacheAndNotifyRib({dcStatus, bbStatus});

  const auto attrs = *buildBgpPathFields(4, 4, 4, 4);
  auto dcAttrs = std::make_shared<facebook::bgp::BgpPath>(attrs);
  dcAttrs->setNexthop(kPeerAddr1);
  dcAttrs->publish();
  auto bbAttrs = std::make_shared<facebook::bgp::BgpPath>(attrs);
  bbAttrs->setNexthop(kPeerAddr2);
  bbAttrs->publish();

  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(
      PrefixPathIds{{kV4Prefix1, kDefaultPathID}}, iBgpPeer_, dcAttrs);
  sendAnnouncement(
      PrefixPathIds{{kV6Prefix1, kDefaultPathID}}, eBgpPeer1_, bbAttrs);
  sendInitialPathComputation(std::chrono::milliseconds(0));
  fibFuture.wait();

  const auto reachableEventsBefore =
      timeseriesCount(RibStats::kNhtRibNexthopReachable);
  const auto unreachableEventsBefore =
      timeseriesCount(RibStats::kNhtRibNexthopUnreachable);
  const auto reevaluationsBefore =
      timeseriesCount(RibStats::kNhtRibResolvabilityChangePrefixReevaluation);

  EXPECT_NE(nullptr, rib_->getBestPath(kV4Prefix1));
  EXPECT_EQ(nullptr, rib_->getBestPath(kV6Prefix1));
  EXPECT_EQ(1, inactivePathCount());

  const auto dcInfo = rib_->getNexthopInfoForNexthop(kPeerAddr1);
  ASSERT_TRUE(dcInfo.has_value());
  EXPECT_TRUE(*dcInfo->is_reachable());
  EXPECT_FALSE(dcInfo->igp_cost().has_value());
  EXPECT_TRUE(*dcInfo->is_resolved_for_selection());
  EXPECT_TRUE(dcInfo->last_reachability_change_age_s().has_value());
  EXPECT_FALSE(dcInfo->last_igp_cost_change_age_s().has_value());

  const auto bbInfo = rib_->getNexthopInfoForNexthop(kPeerAddr2);
  ASSERT_TRUE(bbInfo.has_value());
  EXPECT_TRUE(*bbInfo->is_reachable());
  EXPECT_FALSE(bbInfo->igp_cost().has_value());
  EXPECT_FALSE(*bbInfo->is_resolved_for_selection());

  auto checkLegacyPaths = [](const auto& entries, bool inactive) {
    ASSERT_EQ(entries.size(), 1);
    for (const auto& [_, paths] : entries[0].paths().value()) {
      for (const auto& path : paths) {
        EXPECT_FALSE(path.igp_cost().has_value());
        EXPECT_EQ(inactive, path.is_inactive().value_or(false));
      }
    }
  };
  checkLegacyPaths(rib_->getRibEntries(TBgpAfi::AFI_IPV4), false);
  checkLegacyPaths(rib_->getRibEntries(TBgpAfi::AFI_IPV6), true);

  auto checkCanonicalPaths = [](const auto& state, bool inactive) {
    ASSERT_EQ(state.rib_entries()->size(), 1);
    for (const auto& [_, entry] : state.rib_entries().value()) {
      for (const auto& [_, paths] : entry.paths().value()) {
        for (const auto& path : paths) {
          EXPECT_FALSE(path.igp_cost().has_value());
          EXPECT_EQ(inactive, path.is_inactive().value_or(false));
        }
      }
    }
  };
  checkCanonicalPaths(rib_->getRibEntriesCanonical(TBgpAfi::AFI_IPV4), false);
  checkCanonicalPaths(rib_->getRibEntriesCanonical(TBgpAfi::AFI_IPV6), true);

  auto tcData = fb303::ThreadCachedServiceData::get();
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));

  fibFuture = fib_->getFibProgramFuture();
  updateCacheAndNotifyRib({NexthopStatus(
      kPeerAddr1,
      /*isReachable=*/false,
      /*igpCost=*/std::nullopt,
      /*isConnected=*/std::nullopt,
      /*excludeNexthopWithoutCost=*/false)});
  fibFuture.wait();
  EXPECT_EQ(nullptr, rib_->getBestPath(kV4Prefix1));
  EXPECT_EQ(2, inactivePathCount());
  tcData->publishStats();
  EXPECT_EQ(2, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));
  EXPECT_EQ(
      unreachableEventsBefore + 1,
      timeseriesCount(RibStats::kNhtRibNexthopUnreachable));
  EXPECT_EQ(
      reevaluationsBefore + 1,
      timeseriesCount(RibStats::kNhtRibResolvabilityChangePrefixReevaluation));

  fibFuture = fib_->getFibProgramFuture();
  updateCacheAndNotifyRib({dcStatus});
  fibFuture.wait();
  EXPECT_NE(nullptr, rib_->getBestPath(kV4Prefix1));
  EXPECT_EQ(1, inactivePathCount());
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));
  EXPECT_EQ(
      reachableEventsBefore + 1,
      timeseriesCount(RibStats::kNhtRibNexthopReachable));
  EXPECT_EQ(
      reevaluationsBefore + 2,
      timeseriesCount(RibStats::kNhtRibResolvabilityChangePrefixReevaluation));
}

TEST_F(RibNexthopTrackingFixture, SourceOnlyReachabilityChangeDoesNotReselect) {
  RibStats::initCounters();
  setNexthopTrackingFeatureFlag(true);
  rib_->setFibBatchTime(milliseconds(2));
  updateCacheAndNotifyRib({NexthopStatus(kPeerAddr1, false)});

  auto attrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs->setNexthop(kPeerAddr1);
  attrs->publish();

  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(
      PrefixPathIds{{kV4Prefix1, kDefaultPathID}}, iBgpPeer_, attrs);
  sendInitialPathComputation(std::chrono::milliseconds(0));
  fibFuture.wait();

  const auto reachableEventsBefore =
      timeseriesCount(RibStats::kNhtRibNexthopReachable);
  const auto reevaluationsBefore =
      timeseriesCount(RibStats::kNhtRibResolvabilityChangePrefixReevaluation);

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(0);
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto updates = rib_->nexthopCache_->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, true, std::nullopt)});
    ASSERT_EQ(updates.size(), 1);
    rib_->processRibInNexthopUpdate(RibInNexthopUpdate(std::move(updates)));
    EXPECT_FALSE(rib_->fibBatchTimer_->isScheduled());
  });
  testing::Mock::VerifyAndClearExpectations(rib_.get());

  auto info = rib_->getNexthopInfoForNexthop(kPeerAddr1);
  ASSERT_TRUE(info.has_value());
  EXPECT_TRUE(*info->is_reachable());
  EXPECT_FALSE(info->igp_cost().has_value());
  EXPECT_FALSE(*info->is_resolved_for_selection());
  EXPECT_TRUE(info->last_reachability_change_age_s().has_value());
  EXPECT_FALSE(info->last_igp_cost_change_age_s().has_value());
  EXPECT_EQ(nullptr, rib_->getBestPath(kV4Prefix1));

  auto tcData = fb303::ThreadCachedServiceData::get();
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));
  EXPECT_EQ(
      reachableEventsBefore + 1,
      timeseriesCount(RibStats::kNhtRibNexthopReachable));
  EXPECT_EQ(
      reevaluationsBefore,
      timeseriesCount(RibStats::kNhtRibResolvabilityChangePrefixReevaluation));

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
  fibFuture = fib_->getFibProgramFuture();
  updateCacheAndNotifyRib({NexthopStatus(kPeerAddr1, true, 50)});
  fibFuture.wait();
  testing::Mock::VerifyAndClearExpectations(rib_.get());

  info = rib_->getNexthopInfoForNexthop(kPeerAddr1);
  ASSERT_TRUE(info.has_value());
  EXPECT_TRUE(*info->is_resolved_for_selection());
  EXPECT_EQ(info->igp_cost(), 50);
  tcData->publishStats();
  EXPECT_EQ(0, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));
  EXPECT_EQ(
      reevaluationsBefore + 1,
      timeseriesCount(RibStats::kNhtRibResolvabilityChangePrefixReevaluation));

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  updateCacheAndNotifyRib({NexthopStatus(kPeerAddr1, true, 100)});
  ribFuture.wait();
  testing::Mock::VerifyAndClearExpectations(rib_.get());

  info = rib_->getNexthopInfoForNexthop(kPeerAddr1);
  ASSERT_TRUE(info.has_value());
  EXPECT_TRUE(*info->is_resolved_for_selection());
  EXPECT_EQ(info->igp_cost(), 100);
  tcData->publishStats();
  EXPECT_EQ(0, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
  fibFuture = fib_->getFibProgramFuture();
  updateCacheAndNotifyRib({NexthopStatus(kPeerAddr1, true, std::nullopt)});
  fibFuture.wait();
  testing::Mock::VerifyAndClearExpectations(rib_.get());

  info = rib_->getNexthopInfoForNexthop(kPeerAddr1);
  ASSERT_TRUE(info.has_value());
  EXPECT_TRUE(*info->is_reachable());
  EXPECT_FALSE(*info->is_resolved_for_selection());
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));
  EXPECT_EQ(
      reevaluationsBefore + 2,
      timeseriesCount(RibStats::kNhtRibResolvabilityChangePrefixReevaluation));

  const PrefixPathId prefixPathId{kV4Prefix1, kDefaultPathID};
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->processSingleRibInUpdate(iBgpPeer_, nullptr, prefixPathId);
  });
  EXPECT_FALSE(rib_->getNexthopInfoForNexthop(kPeerAddr1).has_value());
  EXPECT_FALSE(rib_->nexthopCache_->isRegistered(kPeerAddr1));
  const auto retainedStatus =
      rib_->nexthopCache_->registerAndGetNexthopStatus(kPeerAddr1);
  EXPECT_TRUE(retainedStatus.isReachable());
  EXPECT_FALSE(retainedStatus.getIgpCost().has_value());
  tcData->publishStats();
  EXPECT_EQ(0, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));
}

TEST_F(
    RibNexthopTrackingFixture,
    ResolvabilityChangeCountsOneReevaluationPerPrefix) {
  RibStats::initCounters();
  setNexthopTrackingFeatureFlag(true);
  rib_->setFibBatchTime(milliseconds(2));
  updateCacheAndNotifyRib({NexthopStatus(kPeerAddr1, false)});

  auto attrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs->setNexthop(kPeerAddr1);
  attrs->publish();

  const auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, iBgpPeer_, attrs);
  sendAnnouncement(prefixBatch, eBgpPeer1_, attrs);
  sendInitialPathComputation(std::chrono::milliseconds(0));
  fibFuture.wait();
  EXPECT_EQ(nullptr, rib_->getBestPath(kV4Prefix1));

  const auto reevaluationsBefore =
      timeseriesCount(RibStats::kNhtRibResolvabilityChangePrefixReevaluation);
  fibFuture = fib_->getFibProgramFuture();
  updateCacheAndNotifyRib({NexthopStatus(kPeerAddr1, true, 50)});
  fibFuture.wait();

  EXPECT_NE(nullptr, rib_->getBestPath(kV4Prefix1));
  EXPECT_EQ(
      reevaluationsBefore + 1,
      timeseriesCount(RibStats::kNhtRibResolvabilityChangePrefixReevaluation));
}

TEST_F(
    RibNexthopTrackingFixture,
    StaleQueuedNexthopUpdateDoesNotRecreateUntrackedState) {
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto* nexthopCache = rib_->nexthopCache_.get();
    nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/true, /*igpCost=*/100)});
    nexthopCache->registerAndGetNexthopStatus(kPeerAddr1);

    auto queuedStatuses = nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/false)});
    ASSERT_EQ(1, queuedStatuses.size());
    EXPECT_TRUE(nexthopCache->unregisterAndRemoveNexthopStatus(kPeerAddr1));

    rib_->processRibInNexthopUpdate(
        RibInNexthopUpdate(std::move(queuedStatuses)));
    EXPECT_EQ(
        rib_->nexthopInfoMap_.end(), rib_->nexthopInfoMap_.find(kPeerAddr1));

    EXPECT_TRUE(nexthopCache
                    ->addOrUpdateNextHopStatus({NexthopStatus(
                        kPeerAddr1, /*isReachable=*/true, /*igpCost=*/100)})
                    .empty());
  });

  auto attrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs->setNexthop(kPeerAddr1);
  attrs->publish();
  sendAnnouncement(
      PrefixPathIds{{kV4Prefix1, kDefaultPathID}}, iBgpPeer_, attrs);

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto nexthopInfoIt = rib_->nexthopInfoMap_.find(kPeerAddr1);
    ASSERT_NE(rib_->nexthopInfoMap_.end(), nexthopInfoIt);
    EXPECT_TRUE(nexthopInfoIt->second.isReachable());
    EXPECT_EQ(std::optional<uint32_t>(100), nexthopInfoIt->second.getIgpCost());
    EXPECT_TRUE(rib_->nexthopCache_->isRegistered(kPeerAddr1));
  });
}

namespace {
std::shared_ptr<facebook::bgp::BgpPath> pathVia(
    const folly::IPAddress& nexthop) {
  auto attrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs->setNexthop(nexthop);
  attrs->publish();
  return attrs;
}

int64_t staleNexthopUpdateSkippedCount() {
  auto tcData = fb303::ThreadCachedServiceData::get();
  tcData->publishStats();
  return tcData->getCounter(RibStats::kNhtRibStaleUpdateSkipped + ".count");
}
} // namespace

/*
 * The last route via a nexthop is withdrawn and announced again while an update
 * that marks the nexthop unreachable is still queued. When the route returns,
 * the RIB reads the reachable status from NexthopCache. After the RIB processes
 * the queued update, the NexthopInfo must stay reachable.
 */
TEST_F(
    RibNexthopTrackingFixture,
    QueuedUnreachableUpdateDoesNotOverwriteReregisteredStatus) {
  auto* nexthopCache = rib_->nexthopCache_.get();
  const NexthopStatus reachable(
      kPeerAddr1, /*isReachable=*/true, /*igpCost=*/100);
  const PrefixPathId prefixPathId{kV4Prefix1, kDefaultPathID};
  const auto attrs = pathVia(kPeerAddr1);

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    nexthopCache->addOrUpdateNextHopStatus({reachable});
    rib_->processSingleRibInUpdate(iBgpPeer_, attrs, prefixPathId);
    auto queuedStatuses = nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/false)});
    ASSERT_EQ(1, queuedStatuses.size());

    rib_->processSingleRibInUpdate(iBgpPeer_, nullptr, prefixPathId);
    ASSERT_TRUE(nexthopCache->addOrUpdateNextHopStatus({reachable}).empty());
    rib_->processSingleRibInUpdate(iBgpPeer_, attrs, prefixPathId);
    rib_->processRibInNexthopUpdate(
        RibInNexthopUpdate(std::move(queuedStatuses)));

    const auto& nexthopInfo = rib_->nexthopInfoMap_.at(kPeerAddr1);
    EXPECT_TRUE(nexthopInfo.isReachable());
    EXPECT_EQ(std::optional<uint32_t>(100), nexthopInfo.getIgpCost());
  });
}

/*
 * The last route via a nexthop is withdrawn and announced again while an update
 * that marks the nexthop reachable is still queued. When the route returns, the
 * RIB reads the unreachable status from NexthopCache. After the RIB processes
 * the queued update, the NexthopInfo must stay unreachable.
 */
TEST_F(
    RibNexthopTrackingFixture,
    QueuedReachableUpdateDoesNotOverwriteReregisteredStatus) {
  auto* nexthopCache = rib_->nexthopCache_.get();
  const NexthopStatus unreachable(kPeerAddr1, /*isReachable=*/false);
  const PrefixPathId prefixPathId{kV4Prefix1, kDefaultPathID};
  const auto attrs = pathVia(kPeerAddr1);

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    nexthopCache->addOrUpdateNextHopStatus({unreachable});
    rib_->processSingleRibInUpdate(iBgpPeer_, attrs, prefixPathId);
    auto queuedStatuses = nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/true, /*igpCost=*/100)});
    ASSERT_EQ(1, queuedStatuses.size());

    rib_->processSingleRibInUpdate(iBgpPeer_, nullptr, prefixPathId);
    ASSERT_TRUE(nexthopCache->addOrUpdateNextHopStatus({unreachable}).empty());
    rib_->processSingleRibInUpdate(iBgpPeer_, attrs, prefixPathId);
    rib_->processRibInNexthopUpdate(
        RibInNexthopUpdate(std::move(queuedStatuses)));

    const auto& nexthopInfo = rib_->nexthopInfoMap_.at(kPeerAddr1);
    EXPECT_FALSE(nexthopInfo.isReachable());
    EXPECT_FALSE(nexthopInfo.getIgpCost().has_value());
  });
}

/*
 * The RIB can receive two nexthop updates in the reverse order of the
 * NexthopCache writes. After the RIB processes both, the NexthopInfo must match
 * the latest status in NexthopCache.
 */
TEST_F(
    RibNexthopTrackingFixture,
    OutOfOrderNexthopUpdatesUseLatestCachedStatus) {
  auto* nexthopCache = rib_->nexthopCache_.get();
  const PrefixPathId prefixPathId{kV4Prefix1, kDefaultPathID};
  const auto attrs = pathVia(kPeerAddr1);

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/true, /*igpCost=*/10)});
    rib_->processSingleRibInUpdate(iBgpPeer_, attrs, prefixPathId);

    auto firstStatuses = nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/false)});
    auto secondStatuses = nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/true, /*igpCost=*/20)});
    ASSERT_EQ(1, firstStatuses.size());
    ASSERT_EQ(1, secondStatuses.size());

    rib_->processRibInNexthopUpdate(
        RibInNexthopUpdate(std::move(secondStatuses)));
    rib_->processRibInNexthopUpdate(
        RibInNexthopUpdate(std::move(firstStatuses)));

    const auto& nexthopInfo = rib_->nexthopInfoMap_.at(kPeerAddr1);
    EXPECT_TRUE(nexthopInfo.isReachable());
    EXPECT_EQ(std::optional<uint32_t>(20), nexthopInfo.getIgpCost());
  });
}

/*
 * The RIB skips a queued update that is older than the status it already read,
 * and counts the skip. The newer status is applied when its own update arrives.
 */
TEST_F(RibNexthopTrackingFixture, StaleNexthopUpdateIsSkippedAndCounted) {
  RibStats::initCounters();
  auto* nexthopCache = rib_->nexthopCache_.get();
  const PrefixPathId prefixPathId{kV4Prefix1, kDefaultPathID};

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/true, /*igpCost=*/10)});
    rib_->processSingleRibInUpdate(
        iBgpPeer_, pathVia(kPeerAddr1), prefixPathId);
    auto staleStatuses = nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/false)});
    auto newerStatuses = nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/true, /*igpCost=*/20)});
    rib_->processRibInNexthopUpdate(
        RibInNexthopUpdate(std::move(newerStatuses)));
    auto latestStatuses = nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/true, /*igpCost=*/30)});
    const auto skippedBefore = staleNexthopUpdateSkippedCount();

    rib_->processRibInNexthopUpdate(
        RibInNexthopUpdate(std::move(staleStatuses)));

    EXPECT_EQ(
        std::optional<uint32_t>(20),
        rib_->nexthopInfoMap_.at(kPeerAddr1).getIgpCost());
    EXPECT_EQ(skippedBefore + 1, staleNexthopUpdateSkippedCount());

    rib_->processRibInNexthopUpdate(
        RibInNexthopUpdate(std::move(latestStatuses)));
    EXPECT_EQ(
        std::optional<uint32_t>(30),
        rib_->nexthopInfoMap_.at(kPeerAddr1).getIgpCost());
  });
}

/*
 * Epochs are compared per nexthop. An update with a larger epoch for one
 * nexthop must not make the RIB skip an older update for another nexthop.
 */
TEST_F(RibNexthopTrackingFixture, NexthopUpdatesForTwoNexthopsApplyOutOfOrder) {
  auto* nexthopCache = rib_->nexthopCache_.get();

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/true, /*igpCost=*/10),
         NexthopStatus(kPeerAddr2, /*isReachable=*/true, /*igpCost=*/10)});
    rib_->processSingleRibInUpdate(
        iBgpPeer_, pathVia(kPeerAddr1), {kV4Prefix1, kDefaultPathID});
    rib_->processSingleRibInUpdate(
        iBgpPeer_, pathVia(kPeerAddr2), {kV4Prefix2, kDefaultPathID});
    auto statusesB = nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr2, /*isReachable=*/true, /*igpCost=*/20)});
    auto statusesA = nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/true, /*igpCost=*/20)});

    rib_->processRibInNexthopUpdate(RibInNexthopUpdate(std::move(statusesA)));
    rib_->processRibInNexthopUpdate(RibInNexthopUpdate(std::move(statusesB)));

    EXPECT_EQ(
        std::optional<uint32_t>(20),
        rib_->nexthopInfoMap_.at(kPeerAddr2).getIgpCost());
  });
}

/*
 * An update with epoch 0 was not stored by NexthopCache, for example a status
 * built by hand. The RIB always re-reads NexthopCache for it.
 */
TEST_F(RibNexthopTrackingFixture, UnstampedNexthopUpdateAlwaysRereads) {
  auto* nexthopCache = rib_->nexthopCache_.get();
  const PrefixPathId prefixPathId{kV4Prefix1, kDefaultPathID};

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/true, /*igpCost=*/10)});
    rib_->processSingleRibInUpdate(
        iBgpPeer_, pathVia(kPeerAddr1), prefixPathId);
    nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/true, /*igpCost=*/20)});

    rib_->processRibInNexthopUpdate(
        RibInNexthopUpdate({NexthopStatus(kPeerAddr1, /*isReachable=*/false)}));

    EXPECT_EQ(
        std::optional<uint32_t>(20),
        rib_->nexthopInfoMap_.at(kPeerAddr1).getIgpCost());
  });
}

/*
 * An update whose epoch equals the epoch the RIB holds is re-read, not
 * skipped. A cache write that kept an old epoch therefore still reaches the
 * RIB.
 */
TEST_F(RibNexthopTrackingFixture, NexthopUpdateWithEqualEpochRereads) {
  auto* nexthopCache = rib_->nexthopCache_.get();
  const PrefixPathId prefixPathId{kV4Prefix1, kDefaultPathID};

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/true, /*igpCost=*/10)});
    rib_->processSingleRibInUpdate(
        iBgpPeer_, pathVia(kPeerAddr1), prefixPathId);
    auto statuses = nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/true, /*igpCost=*/20)});
    rib_->processRibInNexthopUpdate(RibInNexthopUpdate(statuses));
    nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/true, /*igpCost=*/30)});

    rib_->processRibInNexthopUpdate(RibInNexthopUpdate(std::move(statuses)));

    EXPECT_EQ(
        std::optional<uint32_t>(30),
        rib_->nexthopInfoMap_.at(kPeerAddr1).getIgpCost());
  });
}

/*
 * A skipped entry must not stop the RIB from processing the rest of the
 * update, or from re-checking the initial path computation gate.
 */
TEST_F(
    RibNexthopTrackingFixture,
    SkippedNexthopUpdateStillRunsInitialPathComputation) {
  auto* nexthopCache = rib_->nexthopCache_.get();

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->processSingleRibInUpdate(
        iBgpPeer_, pathVia(kPeerAddr1), {kV4Prefix1, kDefaultPathID});
    rib_->processSingleRibInUpdate(
        iBgpPeer_, pathVia(kPeerAddr2), {kV4Prefix2, kDefaultPathID});
    rib_->processRibInInitialPathComputation(std::chrono::seconds(60));
    ASSERT_FALSE(rib_->ribEoRReceived_);
    auto staleStatuses = nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/true, /*igpCost=*/10)});
    auto newerStatuses = nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/true, /*igpCost=*/20)});
    rib_->processRibInNexthopUpdate(
        RibInNexthopUpdate(std::move(newerStatuses)));
    auto statusesB = nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr2, /*isReachable=*/true, /*igpCost=*/10)});
    ASSERT_EQ(1, staleStatuses.size());
    ASSERT_EQ(1, statusesB.size());

    rib_->processRibInNexthopUpdate(
        RibInNexthopUpdate({staleStatuses.front(), statusesB.front()}));

    EXPECT_TRUE(rib_->ribEoRReceived_);
    EXPECT_TRUE(rib_->nexthopInfoMap_.at(kPeerAddr2).isResolvedForSelection());
  });
}

/*
 * A route replacement moves the last route off nexthop N1 while a FIB batch
 * that names N1 is still waiting for the FIB loop. The FIB reads N1's
 * NexthopInfo to fill in the interface, so N1 must still be in nexthopInfoMap_
 * when the FIB loop runs. After the FIB loop takes the batch, N1 is erased and
 * unregistered.
 */
TEST_F(
    RibNexthopTrackingFixture,
    ReplacedNexthopStaysInMapUntilPendingBatchIsTaken) {
  struct FibNexthopView {
    std::optional<std::string> ifName;
    std::optional<bool> isConnected;
  };
  auto* nexthopCache = rib_->nexthopCache_.get();
  const auto prefix = folly::IPAddress::createNetwork("1::/64");
  const PrefixPathId prefixPathId{prefix, kDefaultPathID};
  bool programmed{false};
  std::optional<FibNexthopView> programmedN1;
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(prefix), _, _, _, _, _))
      .WillOnce([&](const folly::CIDRNetwork&,
                    std::shared_ptr<const BgpPath>,
                    std::shared_ptr<const WeightedNexthopMap>,
                    const bool,
                    const bool,
                    const MockFib::NexthopInfoMap& nexthopInfoMap) {
        programmed = true;
        const auto it = nexthopInfoMap.find(kPeerAddr1);
        if (it != nexthopInfoMap.end()) {
          programmedN1 =
              FibNexthopView{it->second.getIfName(), it->second.isConnected()};
        }
      })
      .WillRepeatedly(::testing::Return());

  auto fibFuture = fib_->getFibProgramFuture();
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(
             kPeerAddr1,
             /*isReachable=*/true,
             /*igpCost=*/1,
             /*isConnected=*/true,
             /*excludeNexthopWithoutCost=*/true,
             /*ifName=*/"eth1/1/1"),
         NexthopStatus(kPeerAddr2, /*isReachable=*/true, /*igpCost=*/10)});
    rib_->processSingleRibInUpdate(
        iBgpPeer_, pathVia(kPeerAddr1), prefixPathId);
    rib_->processRibInInitialPathComputation(std::chrono::milliseconds(0));
    rib_->processSingleRibInUpdate(
        iBgpPeer_, pathVia(kPeerAddr2), prefixPathId);
  });
  fibFuture.wait();

  ASSERT_TRUE(programmed);
  ASSERT_TRUE(programmedN1.has_value());
  EXPECT_EQ(std::optional<std::string>("eth1/1/1"), programmedN1->ifName);
  EXPECT_EQ(std::optional<bool>(true), programmedN1->isConnected);
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    EXPECT_EQ(
        rib_->nexthopInfoMap_.end(), rib_->nexthopInfoMap_.find(kPeerAddr1));
    EXPECT_FALSE(nexthopCache->isRegistered(kPeerAddr1));
  });
}

/*
 * The last route via nexthop N1 is withdrawn while a FIB batch that names N1
 * is still waiting for the FIB loop. The prefix keeps a path via N2, so its
 * RibEntry stays in the batch. N1 must still be in nexthopInfoMap_ when the
 * FIB loop builds the request. After the FIB loop takes the batch, N1 is erased
 * and unregistered.
 */
TEST_F(
    RibNexthopTrackingFixture,
    WithdrawnNexthopStaysInMapUntilPendingBatchIsTaken) {
  auto* nexthopCache = rib_->nexthopCache_.get();
  const auto prefix = folly::IPAddress::createNetwork("1::/64");
  const PrefixPathId prefixPathId{prefix, kDefaultPathID};
  bool requestNamesN1{false};
  std::optional<std::string> programmedN1IfName;
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(prefix), _, _, _, _, _))
      .WillOnce([&](const folly::CIDRNetwork&,
                    std::shared_ptr<const BgpPath>,
                    std::shared_ptr<const WeightedNexthopMap> weightedNexthops,
                    const bool,
                    const bool,
                    const MockFib::NexthopInfoMap& nexthopInfoMap) {
        requestNamesN1 =
            weightedNexthops && weightedNexthops->contains(kPeerAddr1);
        const auto it = nexthopInfoMap.find(kPeerAddr1);
        if (it != nexthopInfoMap.end()) {
          programmedN1IfName = it->second.getIfName();
        }
      })
      .WillRepeatedly(::testing::Return());

  auto fibFuture = fib_->getFibProgramFuture();
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(
             kPeerAddr1,
             /*isReachable=*/true,
             /*igpCost=*/1,
             /*isConnected=*/true,
             /*excludeNexthopWithoutCost=*/true,
             /*ifName=*/"eth1/1/1"),
         NexthopStatus(kPeerAddr2, /*isReachable=*/true, /*igpCost=*/10)});
    // The eBGP path via N1 is the best path. The iBGP path via N2 keeps the
    // RibEntry after the withdrawal.
    rib_->processSingleRibInUpdate(
        eBgpPeer1_, pathVia(kPeerAddr1), prefixPathId);
    rib_->processSingleRibInUpdate(
        iBgpPeer_, pathVia(kPeerAddr2), prefixPathId);
    rib_->processRibInInitialPathComputation(std::chrono::milliseconds(0));
    rib_->processSingleRibInUpdate(eBgpPeer1_, nullptr, prefixPathId);
  });
  fibFuture.wait();

  EXPECT_TRUE(requestNamesN1);
  EXPECT_EQ(std::optional<std::string>("eth1/1/1"), programmedN1IfName);
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    EXPECT_EQ(
        rib_->nexthopInfoMap_.end(), rib_->nexthopInfoMap_.find(kPeerAddr1));
    EXPECT_FALSE(nexthopCache->isRegistered(kPeerAddr1));
  });
}

/*
 * Nexthop N1 loses its last route while a FIB batch is pending. A route for
 * another prefix then links to N1 before the FIB loop runs. After the FIB loop
 * takes the batch, N1 must stay in nexthopInfoMap_ and stay registered.
 */
TEST_F(
    RibNexthopTrackingFixture,
    RelinkedNexthopIsKeptAfterPendingBatchIsTaken) {
  auto* nexthopCache = rib_->nexthopCache_.get();
  const PrefixPathId prefixPathId{
      folly::IPAddress::createNetwork("1::/64"), kDefaultPathID};
  const PrefixPathId otherPrefixPathId{
      folly::IPAddress::createNetwork("2::/64"), kDefaultPathID};

  auto fibFuture = fib_->getFibProgramFuture();
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    nexthopCache->addOrUpdateNextHopStatus(
        {NexthopStatus(kPeerAddr1, /*isReachable=*/true, /*igpCost=*/1),
         NexthopStatus(kPeerAddr2, /*isReachable=*/true, /*igpCost=*/10)});
    rib_->processSingleRibInUpdate(
        iBgpPeer_, pathVia(kPeerAddr1), prefixPathId);
    rib_->processRibInInitialPathComputation(std::chrono::milliseconds(0));
    rib_->processSingleRibInUpdate(
        iBgpPeer_, pathVia(kPeerAddr2), prefixPathId);
    ASSERT_TRUE(rib_->nexthopInfoMap_.contains(kPeerAddr1));
    ASSERT_TRUE(rib_->nexthopInfoMap_.at(kPeerAddr1).isRouteInfoListEmpty());
    rib_->processSingleRibInUpdate(
        iBgpPeer_, pathVia(kPeerAddr1), otherPrefixPathId);
  });
  fibFuture.wait();

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    const auto it = rib_->nexthopInfoMap_.find(kPeerAddr1);
    ASSERT_NE(rib_->nexthopInfoMap_.end(), it);
    EXPECT_FALSE(it->second.isRouteInfoListEmpty());
    EXPECT_TRUE(nexthopCache->isRegistered(kPeerAddr1));
  });
}

namespace {
/*
 * prePathSelectionFiltering consults the FeatureFlags global (not the RIB's own
 * enableNexthopTracking_), so the inactive-path tests must set it explicitly.
 */
void setNexthopTrackingFeatureFlag(bool enabled) {
  thrift::BgpConfig thriftConfig;
  thrift::BgpSettingConfig tBgpSettingConfig;
  tBgpSettingConfig.enable_next_hop_tracking() = enabled;
  thriftConfig.bgp_setting_config() = std::move(tBgpSettingConfig);
  FeatureFlags::LoadFromThriftConfig(thriftConfig);
}

int64_t inactivePathCount() {
  auto tcData = fb303::ThreadCachedServiceData::get();
  tcData->publishStats();
  return tcData->getCounter(RibStats::kInactivePathsUnresolvedNexthopsCount);
}

int64_t timeseriesCount(const std::string& name) {
  auto tcData = fb303::ThreadCachedServiceData::get();
  tcData->publishStats();
  return tcData->getCounter(name + ".count");
}
} // namespace

/*
 * The inactive-path counter reports the paths best-path selection actually
 * excluded, recomputed from the RIB on every selection pass. A path whose
 * nexthop is unresolved is inactive; resolving that nexthop makes it active
 * again, and a path with a resolved nexthop is never counted.
 */
TEST_F(RibNexthopTrackingFixture, InactivePathCountTracksNexthopResolution) {
  RibStats::initCounters();
  setNexthopTrackingFeatureFlag(true);
  rib_->setFibBatchTime(milliseconds(2));

  NexthopStatus unreachableStatus(kPeerAddr1, false);
  NexthopStatus reachableStatus(kPeerAddr2, true, 100);
  updateCacheAndNotifyRib({unreachableStatus, reachableStatus});

  const auto attrs = *buildBgpPathFields(4, 4, 4, 4);
  auto unresolvedAttrs = std::make_shared<facebook::bgp::BgpPath>(attrs);
  unresolvedAttrs->setNexthop(kPeerAddr1);
  unresolvedAttrs->publish();
  auto resolvedAttrs = std::make_shared<facebook::bgp::BgpPath>(attrs);
  resolvedAttrs->setNexthop(kPeerAddr2);
  resolvedAttrs->publish();

  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(
      PrefixPathIds{{kV4Prefix1, kDefaultPathID}}, iBgpPeer_, unresolvedAttrs);
  sendAnnouncement(
      PrefixPathIds{{kV6Prefix1, kDefaultPathID}}, eBgpPeer1_, resolvedAttrs);
  sendInitialPathComputation();
  fibFuture.wait();

  // Only the path via the unresolved nexthop is excluded from selection.
  EXPECT_EQ(1, inactivePathCount());
  /*
   * getRibSummary reports the same split per address family: the v4 prefix
   * holds the single inactive path, the v6 one is fully active.
   */
  auto v4Summary = rib_->getRibSummary(TBgpAfi::AFI_IPV4);
  EXPECT_EQ(1, v4Summary.total_paths().value());
  EXPECT_EQ(1, v4Summary.inactive_paths().value());
  auto v6Summary = rib_->getRibSummary(TBgpAfi::AFI_IPV6);
  EXPECT_EQ(1, v6Summary.total_paths().value());
  EXPECT_EQ(0, v6Summary.inactive_paths().value());

  // Resolving that nexthop returns its path to the candidate set.
  auto fibFuture2 = fib_->getFibProgramFuture();
  updateCacheAndNotifyRib({NexthopStatus(kPeerAddr1, true, 50)});
  fibFuture2.wait();

  EXPECT_EQ(0, inactivePathCount());
}

/*
 * Exercise both surviving-prefix withdrawal directions. Removing the inactive
 * path must decrement the aggregate even though the selected path (and thus
 * the prefix) stays installed. Removing the active path instead must retain
 * the inactive contribution while the prefix survives with no best path.
 */
TEST_F(RibNexthopTrackingFixture, InactivePathCountTracksPartialWithdrawals) {
  RibStats::initCounters();
  setNexthopTrackingFeatureFlag(true);
  rib_->setFibBatchTime(milliseconds(2));

  updateCacheAndNotifyRib(
      {NexthopStatus(kPeerAddr1, false), NexthopStatus(kPeerAddr2, true, 100)});

  const auto attrs = *buildBgpPathFields(4, 4, 4, 4);
  auto unresolvedAttrs = std::make_shared<facebook::bgp::BgpPath>(attrs);
  unresolvedAttrs->setNexthop(kPeerAddr1);
  unresolvedAttrs->publish();
  auto resolvedAttrs = std::make_shared<facebook::bgp::BgpPath>(attrs);
  resolvedAttrs->setNexthop(kPeerAddr2);
  resolvedAttrs->publish();

  const auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, iBgpPeer_, unresolvedAttrs);
  sendAnnouncement(prefixBatch, eBgpPeer1_, resolvedAttrs);
  sendInitialPathComputation();
  fibFuture.wait();

  EXPECT_EQ(1, inactivePathCount());
  EXPECT_TRUE(rib_->getBestPath(kV4Prefix1));

  /*
   * The prefix and its selected path survive, but its inactive contribution
   * disappears on the next selection pass.
   */
  sendWithdrawal(prefixBatch, iBgpPeer_);
  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(0, inactivePathCount()); });
  EXPECT_TRUE(rib_->getBestPath(kV4Prefix1));

  /*
   * Restore the mixed entry, then remove only the active path. The unresolved
   * path keeps the RibEntry alive and remains counted, despite there being no
   * best path to program.
   */
  sendAnnouncement(prefixBatch, iBgpPeer_, unresolvedAttrs);
  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(1, inactivePathCount()); });

  auto withdrawalFuture = fib_->getFibProgramFuture();
  sendWithdrawal(prefixBatch, eBgpPeer1_);
  withdrawalFuture.wait();
  EXPECT_FALSE(rib_->getBestPath(kV4Prefix1));
  EXPECT_EQ(1, inactivePathCount());

  /*
   * Removing the final inactive path erases the entry and releases its cached
   * contribution through RibCounters::onPrefixRemoved.
   */
  sendWithdrawal(prefixBatch, iBgpPeer_);
  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(0, inactivePathCount()); });
}

/*
 * A path counted inactive is flagged on the wire, while an active one is not.
 * Check both legacy and canonical encodings, since the CLI prefers canonical.
 */
TEST_F(RibNexthopTrackingFixture, InactiveFlagSurfacedOnThriftPaths) {
  RibStats::initCounters();
  setNexthopTrackingFeatureFlag(true);
  rib_->setFibBatchTime(milliseconds(2));

  updateCacheAndNotifyRib(
      {NexthopStatus(kPeerAddr1, false), NexthopStatus(kPeerAddr2, true, 100)});

  const auto attrs = *buildBgpPathFields(4, 4, 4, 4);
  auto unresolvedAttrs = std::make_shared<facebook::bgp::BgpPath>(attrs);
  unresolvedAttrs->setNexthop(kPeerAddr1);
  unresolvedAttrs->publish();
  auto resolvedAttrs = std::make_shared<facebook::bgp::BgpPath>(attrs);
  resolvedAttrs->setNexthop(kPeerAddr2);
  resolvedAttrs->publish();

  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(
      PrefixPathIds{{kV4Prefix1, kDefaultPathID}}, iBgpPeer_, unresolvedAttrs);
  sendAnnouncement(
      PrefixPathIds{{kV6Prefix1, kDefaultPathID}}, eBgpPeer1_, resolvedAttrs);
  sendInitialPathComputation();
  fibFuture.wait();

  auto v4Entries = rib_->getRibEntries(TBgpAfi::AFI_IPV4);
  ASSERT_EQ(1, v4Entries.size());
  for (const auto& [group, paths] : v4Entries[0].paths().value()) {
    for (const auto& path : paths) {
      EXPECT_TRUE(path.is_inactive().value_or(false));
    }
  }

  auto v6Entries = rib_->getRibEntries(TBgpAfi::AFI_IPV6);
  ASSERT_EQ(1, v6Entries.size());
  for (const auto& [group, paths] : v6Entries[0].paths().value()) {
    for (const auto& path : paths) {
      EXPECT_FALSE(path.is_inactive().value_or(false));
    }
  }

  /*
   * The flag is per (prefix, path) instance, so it lives on TBgpPathCanonical
   * rather than in the shared deduped-path pool.
   */
  auto countCanonicalInactivePaths = [](const auto& canonicalState) {
    int inactive = 0, total = 0;
    for (const auto& [prefix, canonicalEntry] :
         canonicalState.rib_entries().value()) {
      for (const auto& [group, paths] : canonicalEntry.paths().value()) {
        for (const auto& path : paths) {
          ++total;
          if (path.is_inactive().value_or(false)) {
            ++inactive;
          }
        }
      }
    }
    return std::make_pair(inactive, total);
  };

  EXPECT_EQ(
      std::make_pair(1, 1),
      countCanonicalInactivePaths(
          rib_->getRibEntriesCanonical(TBgpAfi::AFI_IPV4)));
  EXPECT_EQ(
      std::make_pair(0, 1),
      countCanonicalInactivePaths(
          rib_->getRibEntriesCanonical(TBgpAfi::AFI_IPV6)));
}

/*
 * Regression guard for the drift that made the previous counter untrustworthy.
 * That counter incremented once per RouteInfo linked to an unresolved nexthop,
 * but re-advertising a prefix creates a new RouteInfo and displaces the old one
 * without unlinking it -- the intrusive list hook auto-unlinks on destruction,
 * so no decrement ever ran and the counter leaked +1 per re-advertisement.
 * Recomputing from the RIB each pass makes the count depend on RIB contents
 * alone, so it must stay at 1 no matter how many times the path is refreshed.
 */
TEST_F(
    RibNexthopTrackingFixture,
    InactivePathCountStableAcrossReadvertisement) {
  RibStats::initCounters();
  setNexthopTrackingFeatureFlag(true);
  rib_->setFibBatchTime(milliseconds(2));

  updateCacheAndNotifyRib({NexthopStatus(kPeerAddr1, false)});

  constexpr int kNumReadvertisements = 10;
  for (int i = 0; i < kNumReadvertisements; i++) {
    /*
     * Vary the AS path so each refresh is a genuine attribute change rather
     * than an update that updatePath() short-circuits.
     */
    auto refreshedAttrs = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4 + i, 4, 4, 4));
    refreshedAttrs->setNexthop(kPeerAddr1);
    refreshedAttrs->publish();

    sendAnnouncement(
        PrefixPathIds{{kV4Prefix1, kDefaultPathID}}, iBgpPeer_, refreshedAttrs);
    sendInitialPathComputation();

    /*
     * One prefix, one path, still unresolved -- exactly one inactive path, no
     * matter how many refreshes preceded this one. Polled rather than waiting
     * on a FIB-program future: this prefix never has a best path, so it is
     * never programmed and no such event is produced. Under the old counter
     * this grew by one per refresh and the assertion would never come true.
     */
    WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(1, inactivePathCount()); });
  }
}

/*
 * A prefix whose every path is inactive has no best path, so it reaches the
 * RibEntry erase path still carrying a cached inactive count. Withdrawing it
 * must return the aggregate to zero rather than stranding that count.
 */
TEST_F(RibNexthopTrackingFixture, InactivePathCountReleasedOnWithdrawal) {
  RibStats::initCounters();
  setNexthopTrackingFeatureFlag(true);
  rib_->setFibBatchTime(milliseconds(2));

  updateCacheAndNotifyRib({NexthopStatus(kPeerAddr1, false)});

  auto unresolvedAttrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  unresolvedAttrs->setNexthop(kPeerAddr1);
  unresolvedAttrs->publish();

  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch, iBgpPeer_, unresolvedAttrs);
  sendInitialPathComputation();
  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(1, inactivePathCount()); });

  /*
   * The entry is erased inline on the withdrawal (no best path and no paths
   * left), before any further selection pass can recompute its count -- so
   * this only returns to zero because the erase path releases the cached
   * count.
   */
  sendWithdrawal(prefixBatch, iBgpPeer_);
  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(0, inactivePathCount()); });
}

/**
 * Test that repeatedly announces and withdraws a route with a unique nexthop
 * to stress test the register/unregister functionality. This verifies that
 * NexthopInfo objects are properly created and cleaned up during route
 * flapping.
 */
TEST_F(RibNexthopTrackingFixture, RouteFlappingWithUniqueNexthop) {
  auto tcData = fb303::ThreadCachedServiceData::get();
  RibStats::initCounters();

  // Create NexthopStatus objects with nexthop IPs
  NexthopStatus unreachableStatus(kPeerAddr1, false);
  // Create a NexthopStatus with reachable nexthop
  NexthopStatus reachableStatus(kPeerAddr2, true, 100);

  // Add the statuses to the cache
  std::vector<NexthopStatus> nexthopStatusList;
  nexthopStatusList.push_back(unreachableStatus);
  nexthopStatusList.push_back(reachableStatus);
  updateCacheAndNotifyRib(nexthopStatusList);

  auto prefixBatch1 = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto prefixBatch2 = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};

  const auto attrs = *buildBgpPathFields(4, 4, 4, 4);

  // Create attributes for kPeerAddr1 (unreachable)
  auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(attrs);
  attrs1->setNexthop(kPeerAddr1);
  attrs1->publish();

  // Create attributes for kPeerAddr2 (reachable)
  auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(attrs);
  attrs2->setNexthop(kPeerAddr2);
  attrs2->publish();

  constexpr int kNumFlaps = 10;
  for (int i = 0; i < kNumFlaps; i++) {
    // Send announcements for both nexthops
    auto fibFuture = fib_->getFibProgramFuture();
    sendAnnouncement(prefixBatch1, iBgpPeer_, attrs1);
    sendAnnouncement(prefixBatch2, eBgpPeer1_, attrs2);
    sendInitialPathComputation();
    fibFuture.wait();

    // Verify both nexthops are in the nexthopInfoMap_
    auto nexthopInfoIt1 = rib_->nexthopInfoMap_.find(kPeerAddr1);
    auto nexthopInfoIt2 = rib_->nexthopInfoMap_.find(kPeerAddr2);
    ASSERT_NE(nexthopInfoIt1, rib_->nexthopInfoMap_.end());
    ASSERT_NE(nexthopInfoIt2, rib_->nexthopInfoMap_.end());
    EXPECT_EQ(nexthopInfoIt1->second.getRouteInfoListSize(), 1);
    EXPECT_EQ(nexthopInfoIt2->second.getRouteInfoListSize(), 1);

    // After announcement: 1 unreachable nexthop (kPeerAddr1)
    tcData->publishStats();
    EXPECT_EQ(1, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));

    // Send withdrawal for both nexthops
    fibFuture = fib_->getFibProgramFuture();
    sendWithdrawal(prefixBatch1, iBgpPeer_);
    sendWithdrawal(prefixBatch2, eBgpPeer1_);
    fibFuture.wait();

    /*
     * Verify both reachable and unreachable nexthops are removed from the
     * nexthopInfoMap_ since routeInfos are removed
     */
    nexthopInfoIt1 = rib_->nexthopInfoMap_.find(kPeerAddr1);
    nexthopInfoIt2 = rib_->nexthopInfoMap_.find(kPeerAddr2);
    EXPECT_EQ(nexthopInfoIt1, rib_->nexthopInfoMap_.end());
    EXPECT_EQ(nexthopInfoIt2, rib_->nexthopInfoMap_.end());

    // After withdrawal: unreachable nexthop deleted, counter back to 0
    tcData->publishStats();
    EXPECT_EQ(0, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));
  }
}

/**
 * Test the getNexthopInfoForNexthop API to verify it correctly returns
 * NexthopInfo for registered nexthops and std::nullopt for unregistered ones.
 */
TEST_F(RibNexthopTrackingFixture, GetNexthopInfoForNexthop) {
  auto tcData = fb303::ThreadCachedServiceData::get();
  RibStats::initCounters();

  // Create NexthopStatus objects with nexthop IPs
  NexthopStatus reachableStatus(kPeerAddr1, true, 100);
  NexthopStatus unreachableStatus(kPeerAddr2, false);

  // Add the statuses to the cache
  std::vector<NexthopStatus> nexthopStatusList;
  nexthopStatusList.push_back(reachableStatus);
  nexthopStatusList.push_back(unreachableStatus);
  updateCacheAndNotifyRib(nexthopStatusList);

  // Announce routes with these nexthops
  auto prefixBatch1 = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto prefixBatch2 = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};

  const auto attrs = *buildBgpPathFields(4, 4, 4, 4);

  // Create attributes for kPeerAddr1 (reachable)
  auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(attrs);
  attrs1->setNexthop(kPeerAddr1);
  attrs1->publish();

  // Create attributes for kPeerAddr2 (unreachable)
  auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(attrs);
  attrs2->setNexthop(kPeerAddr2);
  attrs2->publish();

  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch1, iBgpPeer_, attrs1);
  sendAnnouncement(prefixBatch2, iBgpPeer_, attrs2);
  sendInitialPathComputation();
  fibFuture.wait();

  // 1 reachable + 1 unreachable nexthop => counter should be 1
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));

  // 1: Query for nexthop that exists in nexthopInfoMap_ (kPeerAddr1)
  auto nexthopInfo1 = rib_->getNexthopInfoForNexthop(kPeerAddr1);
  ASSERT_TRUE(nexthopInfo1.has_value());
  EXPECT_TRUE(*nexthopInfo1->is_reachable());
  EXPECT_EQ(nexthopInfo1->igp_cost(), 100);
  EXPECT_EQ(
      folly::IPAddress::fromBinary(
          folly::ByteRange(
              folly::StringPiece(*nexthopInfo1->next_hop()->prefix_bin()))),
      kPeerAddr1);

  // 2: Query for nexthop that exists but is unreachable (kPeerAddr2)
  auto nexthopInfo2 = rib_->getNexthopInfoForNexthop(kPeerAddr2);
  ASSERT_TRUE(nexthopInfo2.has_value());
  EXPECT_FALSE(*nexthopInfo2->is_reachable());
  EXPECT_FALSE(nexthopInfo2->igp_cost().has_value());
  EXPECT_EQ(
      folly::IPAddress::fromBinary(
          folly::ByteRange(
              folly::StringPiece(*nexthopInfo2->next_hop()->prefix_bin()))),
      kPeerAddr2);

  // 3: Query for nexthop that doesn't exist in nexthopInfoMap_
  auto nonExistentNexthop = folly::IPAddress("10.99.99.99");
  auto nexthopInfo3 = rib_->getNexthopInfoForNexthop(nonExistentNexthop);
  EXPECT_FALSE(nexthopInfo3.has_value());
}

/**
 * Verifies getNexthopInfos returns all cache entries when the filter is empty,
 * a subset when specific nexthops are requested, and skips unknown nexthops.
 */
TEST_F(RibNexthopTrackingFixture, GetNexthopInfos) {
  RibStats::initCounters();

  NexthopStatus reachableStatus(kPeerAddr1, true, 100);
  NexthopStatus unreachableStatus(kPeerAddr2, false);

  std::vector<NexthopStatus> nexthopStatusList;
  nexthopStatusList.push_back(reachableStatus);
  nexthopStatusList.push_back(unreachableStatus);
  updateCacheAndNotifyRib(nexthopStatusList);

  auto prefixBatch1 = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto prefixBatch2 = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};

  const auto attrs = *buildBgpPathFields(4, 4, 4, 4);

  auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(attrs);
  attrs1->setNexthop(kPeerAddr1);
  attrs1->publish();

  auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(attrs);
  attrs2->setNexthop(kPeerAddr2);
  attrs2->publish();

  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch1, iBgpPeer_, attrs1);
  sendAnnouncement(prefixBatch2, iBgpPeer_, attrs2);
  sendInitialPathComputation();
  fibFuture.wait();

  // Empty filter => all cache entries (both nexthops).
  auto all = rib_->getNexthopInfos({});
  ASSERT_EQ(all.size(), 2);
  std::map<folly::IPAddress, TNexthopInfo> byAddr;
  for (const auto& info : all) {
    byAddr.emplace(
        folly::IPAddress::fromBinary(
            folly::ByteRange(
                folly::StringPiece(*info.next_hop()->prefix_bin()))),
        info);
  }
  ASSERT_TRUE(byAddr.count(kPeerAddr1));
  ASSERT_TRUE(byAddr.count(kPeerAddr2));

  /*
   * Reachable nexthop: resolved, and has change timestamps (resolved at
   * creation).
   */
  const auto& reachable = byAddr.at(kPeerAddr1);
  EXPECT_TRUE(*reachable.is_reachable());
  EXPECT_TRUE(*reachable.is_resolved_for_selection());
  EXPECT_TRUE(reachable.last_reachability_change_age_s().has_value());
  EXPECT_TRUE(reachable.last_igp_cost_change_age_s().has_value());

  // Unreachable nexthop: never resolved => no change timestamps ("-").
  const auto& unreachable = byAddr.at(kPeerAddr2);
  EXPECT_FALSE(*unreachable.is_reachable());
  EXPECT_FALSE(unreachable.last_reachability_change_age_s().has_value());
  EXPECT_FALSE(unreachable.last_igp_cost_change_age_s().has_value());

  // Specific filter => only requested (and existing) nexthops.
  auto filtered = rib_->getNexthopInfos({kPeerAddr1});
  ASSERT_EQ(filtered.size(), 1);
  EXPECT_TRUE(*filtered[0].is_reachable());
  EXPECT_EQ(filtered[0].igp_cost(), 100);
  EXPECT_GE(*filtered[0].route_count(), 1);

  // Unknown nexthop => skipped (empty result).
  auto none = rib_->getNexthopInfos({folly::IPAddress("10.99.99.99")});
  EXPECT_TRUE(none.empty());
}

// Test that setRouteFilterPolicy logs the correct policy versions
TEST_F(RibFixture, ReplaceRouteFilterPolicyLoggingTest) {
  // Replace the real scuba logger with mock
  auto mockScuba = std::make_shared<MockScubaData>();
  rib_->ribPolicyLogger_ =
      std::make_unique<ScubaRibPolicyLogger>("test_device", mockScuba);

  rib_policy::TRouteFilterPolicy tRouteFilterPolicy;
  tRouteFilterPolicy.version() = 12345;
  rib_policy::TPathSelectionPolicy tPathSelectionPolicy;
  tPathSelectionPolicy.version() = 67890;

  // Set initial path selection policy using public API
  auto future = rib_->getRibPolicyReplaceFuture();

  auto result = rib_->setPathSelectionPolicy(
      std::make_unique<rib_policy::TPathSelectionPolicy>(tPathSelectionPolicy));
  EXPECT_TRUE(*result.success());

  // ensure that the path selection policy has been replaced
  future.wait();

  EXPECT_CALL(*mockScuba, addSample(_, _, _, _))
      .Times(1)
      .WillOnce(
          ::testing::Invoke(
              [&](const auto& sample,
                  auto /* unused */,
                  auto /* unused */,
                  const auto& /* unused */) -> size_t {
                EXPECT_EQ("test_device", sample.getNormalValue("device"));
                EXPECT_EQ("67890", sample.getNormalValue("ps_policy_version"));
                EXPECT_EQ("12345", sample.getNormalValue("rf_policy_version"));
                return 1;
              }));

  future = rib_->getRibPolicyReplaceFuture();

  rib_->setRouteFilterPolicy(
      std::make_unique<rib_policy::TRouteFilterPolicy>(tRouteFilterPolicy));

  future.wait();
}

// Test that setPathSelectionPolicy logs the correct policy versions
TEST_F(RibFixture, ReplacePathSelectionPolicyLoggingTest) {
  auto mockScuba = std::make_shared<MockScubaData>();
  rib_->ribPolicyLogger_ =
      std::make_unique<ScubaRibPolicyLogger>("test_device", mockScuba);

  // Create test policies with specific versions
  rib_policy::TPathSelectionPolicy tPathSelectionPolicy;
  tPathSelectionPolicy.version() = 98765;
  rib_policy::TRouteFilterPolicy tRouteFilterPolicy;
  tRouteFilterPolicy.version() = 54321;

  auto future = rib_->getRibPolicyReplaceFuture();

  rib_->setRouteFilterPolicy(
      std::make_unique<rib_policy::TRouteFilterPolicy>(tRouteFilterPolicy));

  // ensure that the route filter policy has been replaced
  future.wait();

  EXPECT_CALL(*mockScuba, addSample(_, _, _, _))
      .Times(1)
      .WillOnce(
          ::testing::Invoke(
              [&](const auto& sample,
                  auto /* unused */,
                  auto /* unused */,
                  const auto& /* unused */) -> size_t {
                EXPECT_EQ("test_device", sample.getNormalValue("device"));
                EXPECT_EQ("98765", sample.getNormalValue("ps_policy_version"));
                EXPECT_EQ("54321", sample.getNormalValue("rf_policy_version"));
                return 1;
              }));

  future = rib_->getRibPolicyReplaceFuture();

  auto result = rib_->setPathSelectionPolicy(
      std::make_unique<rib_policy::TPathSelectionPolicy>(tPathSelectionPolicy));
  EXPECT_TRUE(*result.success());

  // ensure all requests are served before ending
  future.wait();
}

/*
 * With nexthop tracking, the initial full-sync is deferred until all registered
 * nexthops resolve (bounded by nexthopResolutionTimeout), so it does not wipe
 * FIB routes whose nexthops FSDB has not resolved yet. Resolving the nexthop
 * runs the deferred computation.
 */
TEST_F(
    RibNexthopTrackingFixture,
    InitialPathComputationDefersUntilNexthopResolved) {
  // Register a route whose nexthop (kPeerAddr1) is not yet resolved.
  auto prefix1 = folly::IPAddress::createNetwork("1::/64");
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs1->setNexthop(kPeerAddr1);
  attrs1->publish();
  sendAnnouncement(PrefixPathIds{{prefix1, kDefaultPathID}}, iBgpPeer_, attrs1);

  /*
   * Trigger with a long resolution timeout while kPeerAddr1 is unresolved: the
   * initial full-sync must be deferred, not run.
   */
  sendInitialPathComputation(std::chrono::seconds(60));
  WITH_RETRIES_N_TIMED(100, std::chrono::milliseconds(10), {
    ASSERT_EVENTUALLY_TRUE(isInitialPathComputationPending());
  });
  EXPECT_FALSE(isRibEoRReceived());

  // Resolving the nexthop runs the deferred computation.
  auto fibFuture = fib_->getFibProgramFuture();
  updateCacheAndNotifyRib({NexthopStatus(kPeerAddr1, true, 100)});
  fibFuture.wait();
  WITH_RETRIES_N_TIMED(100, std::chrono::milliseconds(10), {
    ASSERT_EVENTUALLY_TRUE(isRibEoRReceived());
    ASSERT_EVENTUALLY_FALSE(isInitialPathComputationPending());
  });
  EXPECT_NE(nullptr, rib_->getBestPath(prefix1));
}

/*
 * A zero nexthopResolutionTimeout runs the initial full-sync immediately even
 * with unresolved nexthops (nexthop tracking off, or PeerMgr's EoR timer
 * already expired and forcing computation as the max-cap fallback).
 */
TEST_F(
    RibNexthopTrackingFixture,
    InitialPathComputationRunsImmediatelyWithZeroTimeout) {
  auto prefix1 = folly::IPAddress::createNetwork("1::/64");
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs1->setNexthop(kPeerAddr1);
  attrs1->publish();
  sendAnnouncement(PrefixPathIds{{prefix1, kDefaultPathID}}, iBgpPeer_, attrs1);

  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation(std::chrono::milliseconds(0));
  fibFuture.wait();
  EXPECT_TRUE(isRibEoRReceived());
  EXPECT_FALSE(isInitialPathComputationPending());
}

/*
 * A route with an unspecified (::) nexthop -- e.g. locally-originated /
 * next-hop-self -- must not be registered for nexthop tracking, so it cannot
 * hold the initial-path-computation resolution gate (it never resolves via
 * FSDB). Resolving the one real nexthop must complete the gate.
 */
TEST_F(
    RibNexthopTrackingFixture,
    UnspecifiedNexthopDoesNotStallInitialPathComputation) {
  // Route with an unspecified (::) nexthop -- must not be tracked.
  auto prefixSelf = folly::IPAddress::createNetwork("3::/64");
  auto attrsSelf =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrsSelf->setNexthop(folly::IPAddress("::"));
  attrsSelf->publish();
  sendAnnouncement(
      PrefixPathIds{{prefixSelf, kDefaultPathID}}, iBgpPeer_, attrsSelf);

  // Route with a real nexthop that needs FSDB resolution.
  auto prefix1 = folly::IPAddress::createNetwork("1::/64");
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs1->setNexthop(kPeerAddr1);
  attrs1->publish();
  sendAnnouncement(PrefixPathIds{{prefix1, kDefaultPathID}}, iBgpPeer_, attrs1);

  // Gate with a long timeout while kPeerAddr1 is unresolved -> deferred.
  sendInitialPathComputation(std::chrono::seconds(60));
  WITH_RETRIES_N_TIMED(100, std::chrono::milliseconds(10), {
    ASSERT_EVENTUALLY_TRUE(isInitialPathComputationPending());
  });

  /*
   * Resolving ONLY the real nexthop completes the gate: the unspecified ::
   * nexthop is not tracked, so it does not hold the computation.
   */
  auto fibFuture = fib_->getFibProgramFuture();
  updateCacheAndNotifyRib({NexthopStatus(kPeerAddr1, true, 100)});
  fibFuture.wait();
  WITH_RETRIES_N_TIMED(100, std::chrono::milliseconds(10), {
    ASSERT_EVENTUALLY_TRUE(isRibEoRReceived());
    ASSERT_EVENTUALLY_FALSE(isInitialPathComputationPending());
  });
}

/*
 * Conditional local routes are advertised when their nexthop resolves via a
 * NexthopResolutionUpdate (after the initial full-sync has run).
 */
TEST_F(
    RibWithLocalRouteFixture,
    ConditionalRouteAdvertisedOnNexthopResolution) {
  setUpRibAndFib(getConditionalLocalRoutes());
  rib_->setFibBatchTime(milliseconds(8));

  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  // Conditional route is absent until its nexthop resolves.
  EXPECT_EQ(nullptr, rib_->getBestPath(kV4Prefix1));

  // NDP resolves the conditional route's nexthop -> route advertised.
  fibFuture = fib_->getFibProgramFuture();
  sendNexthopResolutionUpdate(
      NexthopResolutionUpdate{{kV4Nexthop1}, {} /* unresolved */});
  fibFuture.wait();
  WITH_RETRIES_N_TIMED(100, std::chrono::milliseconds(10), {
    ASSERT_EVENTUALLY_NE(nullptr, rib_->getBestPath(kV4Prefix1));
  });
}

/*
 * Regression guard: with conditional local routes but nexthop tracking OFF, the
 * initial full-sync must still be deferred until the first
 * NexthopResolutionUpdate advertises the conditional routes -- otherwise the
 * sync runs first and wipes GR-retained conditional routes. (nexthopInfoMap_ is
 * empty here, so the deferral is driven purely by hasConditionalLocalRoutes(),
 * independent of tracking.)
 */
TEST_F(
    RibWithLocalRouteFixture,
    InitialPathComputationDefersForConditionalRoutesWithoutTracking) {
  setUpRibAndFib(getConditionalLocalRoutes());
  rib_->setFibBatchTime(milliseconds(8));

  /*
   * Trigger with a resolution budget; the conditional routes must hold the
   * initial full-sync until they are advertised.
   */
  sendInitialPathComputation(std::chrono::seconds(60));
  WITH_RETRIES_N_TIMED(100, std::chrono::milliseconds(10), {
    ASSERT_EVENTUALLY_TRUE(isInitialPathComputationPending());
  });
  EXPECT_FALSE(isRibEoRReceived());
  // Conditional route not yet advertised, and the sync has not run.
  EXPECT_EQ(nullptr, rib_->getBestPath(kV4Prefix1));

  /*
   * First NexthopResolutionUpdate advertises the conditional route -> the
   * deferred initial full-sync runs and includes it.
   */
  auto fibFuture = fib_->getFibProgramFuture();
  sendNexthopResolutionUpdate(
      NexthopResolutionUpdate{{kV4Nexthop1}, {} /* unresolved */});
  fibFuture.wait();
  WITH_RETRIES_N_TIMED(100, std::chrono::milliseconds(10), {
    ASSERT_EVENTUALLY_TRUE(isRibEoRReceived());
    ASSERT_EVENTUALLY_FALSE(isInitialPathComputationPending());
    ASSERT_EVENTUALLY_NE(nullptr, rib_->getBestPath(kV4Prefix1));
  });
}

/*
 * Test getRibPrefixCanonical: fetch a single prefix, test empty RIB,
 * invalid prefix, and AFI matching.
 */
TEST_F(RibFixture, GetRibPrefixCanonical) {
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs1->publish();

  /* Test 1: Invalid prefix returns empty */
  auto result = rib_->getRibPrefixCanonical(
      std::make_unique<std::string>("invalid_prefix"));
  EXPECT_EQ(result.rib_entries()->size(), 0);

  /* Test 2: Null prefix returns empty */
  result = rib_->getRibPrefixCanonical(nullptr);
  EXPECT_EQ(result.rib_entries()->size(), 0);

  /* Test 3: Missing prefix returns empty */
  result = rib_->getRibPrefixCanonical(
      std::make_unique<std::string>(
          folly::IPAddress::networkToString(kV4Prefix1)));
  EXPECT_EQ(result.rib_entries()->size(), 0);

  /* Test 4: Existing prefix returns canonical entry */
  RibEntry entry1(kV4Prefix1);
  entry1.updatePath(eBgpPeer1_, attrs1, false);
  RibBase::selectBestPath(
      entry1, multipathSelector, bestpathSelector, false, 0);
  rib_->ribEntries_.emplace(kV4Prefix1, std::move(entry1));

  result = rib_->getRibPrefixCanonical(
      std::make_unique<std::string>(
          folly::IPAddress::networkToString(kV4Prefix1)));
  EXPECT_EQ(result.rib_entries()->size(), 1);
  EXPECT_TRUE(result.rib_entries()->contains(
      folly::IPAddress::networkToString(kV4Prefix1)));
  EXPECT_EQ(result.deduped_paths()->size(), 1);
}

/*
 * Test getRibEntriesForCommunityCanonical and
 * getRibEntriesForCommunitiesCanonical: community filtering, empty communities,
 * invalid community, and AFI matching.
 */
TEST_F(RibFixture, GetRibEntriesForCommunitiesCanonical) {
  /* Build attrs with community 100:200 */
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  nettools::bgplib::BgpAttrCommunitiesC comms1;
  comms1.push_back(nettools::bgplib::BgpAttrCommunityC(100, 200));
  attrs1->setCommunities(comms1);
  attrs1->publish();

  /* Build attrs with community 300:400 */
  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  nettools::bgplib::BgpAttrCommunitiesC comms2;
  comms2.push_back(nettools::bgplib::BgpAttrCommunityC(300, 400));
  attrs2->setCommunities(comms2);
  attrs2->setNexthop(kV4Nexthop2);
  attrs2->publish();

  /* Install three entries: two IPv4 with different communities, one IPv6 */
  RibEntry entry1(kV4Prefix1);
  entry1.updatePath(eBgpPeer1_, attrs1, false);
  RibBase::selectBestPath(
      entry1, multipathSelector, bestpathSelector, false, 0);

  RibEntry entry2(kV4Prefix2);
  entry2.updatePath(eBgpPeer2_, attrs2, false);
  RibBase::selectBestPath(
      entry2, multipathSelector, bestpathSelector, false, 0);

  RibEntry entry3(kV6Prefix1);
  entry3.updatePath(eBgpPeer1_, attrs1, false);
  RibBase::selectBestPath(
      entry3, multipathSelector, bestpathSelector, false, 0);

  rib_->ribEntries_.emplace(kV4Prefix1, std::move(entry1));
  rib_->ribEntries_.emplace(kV4Prefix2, std::move(entry2));
  rib_->ribEntries_.emplace(kV6Prefix1, std::move(entry3));

  /* Test 1: Filter by community 100:200 on IPv4 (matches kV4Prefix1 only) */
  std::vector<nettools::bgplib::BgpAttrCommunityC> filterComms1{
      nettools::bgplib::BgpAttrCommunityC(100, 200)};
  auto result = rib_->getRibEntriesForCommunitiesCanonical(
      TBgpAfi::AFI_IPV4, filterComms1);
  EXPECT_EQ(result.rib_entries()->size(), 1);
  EXPECT_TRUE(result.rib_entries()->contains(
      folly::IPAddress::networkToString(kV4Prefix1)));

  /* Test 2: Filter by community 300:400 on IPv4 (matches kV4Prefix2 only) */
  std::vector<nettools::bgplib::BgpAttrCommunityC> filterComms2{
      nettools::bgplib::BgpAttrCommunityC(300, 400)};
  result = rib_->getRibEntriesForCommunitiesCanonical(
      TBgpAfi::AFI_IPV4, filterComms2);
  EXPECT_EQ(result.rib_entries()->size(), 1);
  EXPECT_TRUE(result.rib_entries()->contains(
      folly::IPAddress::networkToString(kV4Prefix2)));

  /* Test 3: Filter by community 100:200 on IPv6 (matches kV6Prefix1) */
  result = rib_->getRibEntriesForCommunitiesCanonical(
      TBgpAfi::AFI_IPV6, filterComms1);
  EXPECT_EQ(result.rib_entries()->size(), 1);
  EXPECT_TRUE(result.rib_entries()->contains(
      folly::IPAddress::networkToString(kV6Prefix1)));

  /* Test 4: Empty communities list returns empty result */
  std::vector<nettools::bgplib::BgpAttrCommunityC> emptyComms;
  result =
      rib_->getRibEntriesForCommunitiesCanonical(TBgpAfi::AFI_IPV4, emptyComms);
  EXPECT_EQ(result.rib_entries()->size(), 0);

  /* Test 5: Non-existent community returns empty */
  std::vector<nettools::bgplib::BgpAttrCommunityC> filterComms3{
      nettools::bgplib::BgpAttrCommunityC(999, 999)};
  result = rib_->getRibEntriesForCommunitiesCanonical(
      TBgpAfi::AFI_IPV4, filterComms3);
  EXPECT_EQ(result.rib_entries()->size(), 0);

  /* Test 6: AFI mismatch returns empty (IPv6 filter on IPv4-only community) */
  result = rib_->getRibEntriesForCommunitiesCanonical(
      TBgpAfi::AFI_IPV6, filterComms2);
  EXPECT_EQ(result.rib_entries()->size(), 0);

  /* Test 7: Single-community wrapper (invalid community) throws, matching the
   * legacy co_getRibEntriesForCommunity sibling. */
  EXPECT_THROW(
      rib_->getRibEntriesForCommunityCanonical(
          TBgpAfi::AFI_IPV4,
          std::make_unique<std::string>("invalid_community")),
      std::invalid_argument);

  /* Test 8: Single-community wrapper (null community) */
  result = rib_->getRibEntriesForCommunityCanonical(TBgpAfi::AFI_IPV4, nullptr);
  EXPECT_EQ(result.rib_entries()->size(), 0);

  /* Test 9: Single-community wrapper (valid community) */
  result = rib_->getRibEntriesForCommunityCanonical(
      TBgpAfi::AFI_IPV4, std::make_unique<std::string>("100:200"));
  EXPECT_EQ(result.rib_entries()->size(), 1);
  EXPECT_TRUE(result.rib_entries()->contains(
      folly::IPAddress::networkToString(kV4Prefix1)));
}

/*
 * Test getRibSubprefixesCanonical: subprefix matching, invalid prefix,
 * null prefix, and AFI family filtering.
 */
TEST_F(RibFixture, GetRibSubprefixesCanonical) {
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs1->publish();

  /*
   * Install three prefixes:
   * - kV4Prefix1 (10.0.0.0/24)
   * - kV4Prefix1Slash25 (10.0.0.0/25, subprefix of kV4Prefix1)
   * - kV6Prefix1 (different AFI)
   */
  RibEntry entry1(kV4Prefix1);
  entry1.updatePath(eBgpPeer1_, attrs1, false);
  RibBase::selectBestPath(
      entry1, multipathSelector, bestpathSelector, false, 0);

  RibEntry entry2(kV4Prefix1Slash25);
  entry2.updatePath(eBgpPeer1_, attrs1, false);
  RibBase::selectBestPath(
      entry2, multipathSelector, bestpathSelector, false, 0);

  RibEntry entry3(kV6Prefix1);
  entry3.updatePath(eBgpPeer1_, attrs1, false);
  RibBase::selectBestPath(
      entry3, multipathSelector, bestpathSelector, false, 0);

  rib_->ribEntries_.emplace(kV4Prefix1, std::move(entry1));
  rib_->ribEntries_.emplace(kV4Prefix1Slash25, std::move(entry2));
  rib_->ribEntries_.emplace(kV6Prefix1, std::move(entry3));

  /*
   * Test 1: Fetch subprefixes of kV4Prefix1
   * (returns both kV4Prefix1 itself and kV4Prefix1Slash25 per isSubnet
   * behavior)
   */
  auto result = rib_->getRibSubprefixesCanonical(
      std::make_unique<std::string>(
          folly::IPAddress::networkToString(kV4Prefix1)));
  EXPECT_EQ(result.rib_entries()->size(), 2);
  EXPECT_TRUE(result.rib_entries()->contains(
      folly::IPAddress::networkToString(kV4Prefix1)));
  EXPECT_TRUE(result.rib_entries()->contains(
      folly::IPAddress::networkToString(kV4Prefix1Slash25)));

  /* Test 2: Fetch subprefixes of a prefix with no subprefixes */
  result = rib_->getRibSubprefixesCanonical(
      std::make_unique<std::string>(
          folly::IPAddress::networkToString(kV4Prefix2)));
  EXPECT_EQ(result.rib_entries()->size(), 0);

  /* Test 3: Invalid prefix returns empty */
  result = rib_->getRibSubprefixesCanonical(
      std::make_unique<std::string>("invalid_prefix"));
  EXPECT_EQ(result.rib_entries()->size(), 0);

  /* Test 4: Null prefix returns empty */
  result = rib_->getRibSubprefixesCanonical(nullptr);
  EXPECT_EQ(result.rib_entries()->size(), 0);
}

TEST_F(RibFixture, FsdbSyncerStartsOnlyAfterInitialFullRibComputation) {
  setUpFsdb();

  completeFibProgrammingPass(/*fullSync=*/false);
  EXPECT_FALSE(isFsdbSyncerStarted());

  completeFibProgrammingPass(/*fullSync=*/true);
  EXPECT_TRUE(isFsdbSyncerStarted());
}

/*
 * With RIB allocated path IDs, a full add-path withdrawal clears the advertised
 * multipath snapshot. The RibEntry itself is kept until the FIB ack.
 */
TEST_F(
    RibAllocatedPathIdFixture,
    FullAddPathWithdrawalClearsAdvertisedMultipaths) {
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, program_(_)).Times(testing::AnyNumber());
  EXPECT_CALL(*fib_, updateUnicastRoute_(_, _, _, _, _, _))
      .Times(testing::AnyNumber());

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->ribEntries_.emplace(kV4Prefix1, RibEntry(kV4Prefix1));
    auto& entry = rib_->ribEntries_.at(kV4Prefix1);
    const std::vector<folly::IPAddress> nexthops{kV4Nexthop1, kV4Nexthop2};
    for (uint32_t i = 0; i < nexthops.size(); i++) {
      auto attrs = std::make_shared<facebook::bgp::BgpPath>(
          *buildBgpPathFields(4, 4, 4, 4));
      attrs->setNexthop(nexthops[i]);
      attrs->publish();
      entry.updatePath(eBgpPeer1_, attrs, true, i + 1);
    }
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    // Commit as advertised, then fully withdraw every path.
    entry.commitMultipaths();
    entry.commitMultipathNexthops();
    entry.commitBestpath();
    ASSERT_EQ(nexthops.size(), entry.getAdvertisedMultipaths().size());
    for (uint32_t i = 0; i < nexthops.size(); i++) {
      entry.updatePath(eBgpPeer1_, nullptr, true, i + 1);
    }
    entry.requirePathSelection();

    rib_->prepareFibProgramming();

    ASSERT_TRUE(rib_->ribEntries_.contains(kV4Prefix1));
    EXPECT_TRUE(
        rib_->ribEntries_.at(kV4Prefix1).getAdvertisedMultipaths().empty());
  });

  auto msg = folly::coro::blockingWait(ribOutQ_.pop());
  ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
  EXPECT_EQ(2, std::get<RibOutWithdrawal>(msg).addPathEntries.size());
}

} // namespace bgp
} // namespace facebook
