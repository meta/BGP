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
 * E2E test for RIB Nexthop Tracking
 * Tests complete BGP flows with nexthop tracking enabled
 * Converted from RibTest.cpp::RibInWithdrawalWithNexthopTracking
 */

#include <gtest/gtest.h>

#include <thrift/lib/cpp2/FieldRef.h>

#include <folly/logging/xlog.h>
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopStatus.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestFixture.h"
#include "neteng/fboss/bgp/if/gen-cpp2/BgpStructs_types.h"

using namespace facebook::bgp;
using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

class E2ENexthopTrackingTest : public E2ETestFixture {
 protected:
  void setupComponents(bool enableNexthopTracking = true) {
    addPeer(kDefaultPeerSpec3);
    addPeer(kDefaultPeerSpec4);
    addPeer(kDefaultPeerSpec5);

    createRib(enableNexthopTracking);
    createPeerManager(false /* enableUpdateGroup */);
  }
};

/*
 * The test ensures that both reachable and unreachable nexthops are properly
 * cleaned up when no routes reference them, and that peer5 only receives
 * announcements for routes with reachable nexthops.
 * 1. Inject nexthop statuses (1 unreachable, 1 reachable)
 * 2. Announce routes via two peers using those nexthops
 * 3. Verify nexthops are tracked in RIB's nexthopInfoMap_
 * 4. Monitor what peer5 (receiver) sees - only reachable routes are advertised
 * 5. Withdraw both routes
 * 6. Monitor what peer5 sees after withdrawals
 * 7. Verify nexthops are removed from nexthopInfoMap_ (cleanup)
 */
TEST_F(E2ENexthopTrackingTest, RibInWithdrawalWithNexthopTrackingE2E) {
  setupComponents(true /* enableNexthopTracking */);

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  bringUpPeer(kPeerAddr5);

  std::vector<NexthopStatus> nexthopStatusList;
  nexthopStatusList.emplace_back(
      NexthopKey{.ip = kPeerAddr3}, false /* unreachable */);
  nexthopStatusList.emplace_back(
      NexthopKey{.ip = kPeerAddr4}, true /* reachable */, 100 /* igpCost */);

  // Inject nexthop statuses into RIB
  injectNexthopStatuses(nexthopStatusList);

  // Announce 10.0.1.0/24 from peer3 with nexthop 127.3.0.1 (unreachable)
  addRoute(
      "v4" /* protocol */,
      "10.0.1.0" /* prefix */,
      24 /* prefixLen */,
      kPeerAddr3 /* peer */,
      kPeerAddr3.str() /* nexthop - same as peer */,
      "" /* asPath */,
      "" /* community */);

  // Announce 2401:db8::/64 from peer4 with nexthop 127.4.0.1 (reachable)
  addRoute(
      "v6" /* protocol */,
      "2401:db8::" /* prefix */,
      64 /* prefixLen */,
      kPeerAddr4 /* peer */,
      kPeerAddr4.str() /* nexthop - same as peer */,
      "" /* asPath */,
      "" /* community */);

  // Verify peer5 receives announcement only for reachable route
  EXPECT_TRUE(verifyRouteAdd(
      "v6",
      "2401:db8::",
      64,
      kPeerAddr5,
      kNextHopV6_5.str() /* expectedNexthop */));

  // Verify nexthops are tracked in nexthopInfoMap_
  EXPECT_TRUE(verifyNexthopRouteCount(kPeerAddr3, 1));
  EXPECT_TRUE(verifyNexthopRouteCount(kPeerAddr4, 1));

  // Verify ODS counter after nexthop insertions (2 nexthops tracked)
  auto tcData = fb303::ThreadCachedServiceData::get();
  tcData->publishStats();
  EXPECT_EQ(2, tcData->getCounter(RibStats::kNexthopInfoCount));

  // Verify unreachable route is NOT selected as bestpath
  auto v4RibEntries =
      rib_->getRibEntryForPrefix(std::make_unique<std::string>("10.0.1.0/24"));
  ASSERT_EQ(v4RibEntries.size(), 1);
  EXPECT_FALSE(
      apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          v4RibEntries[0].best_next_hop()));

  // Verify reachable route is selected as bestpath
  auto v6RibEntries = rib_->getRibEntryForPrefix(
      std::make_unique<std::string>("2401:db8::/64"));
  ASSERT_EQ(v6RibEntries.size(), 1);
  EXPECT_TRUE(
      apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          v6RibEntries[0].best_next_hop()));

  // Withdraw 10.0.1.0/24 from peer3
  deleteRoute(
      "v4" /* protocol */,
      "10.0.1.0" /* prefix */,
      24 /* prefixLen */,
      kPeerAddr3 /* peer */);

  // Withdraw 2401:db8::/64 from peer4
  deleteRoute(
      "v6" /* protocol */,
      "2401:db8::" /* prefix */,
      64 /* prefixLen */,
      kPeerAddr4 /* peer */);

  // Verify peer5 receives withdrawal for the reachable route
  EXPECT_TRUE(verifyRouteWithdraw("v6", "2401:db8::", 64, kPeerAddr5));

  // Verify nexthops are cleaned up from nexthopInfoMap_
  EXPECT_TRUE(verifyNexthopRouteCount(kPeerAddr3, std::nullopt));
  EXPECT_TRUE(verifyNexthopRouteCount(kPeerAddr4, std::nullopt));

  // Verify ODS counter decremented to 0 after nexthop cleanup
  tcData->publishStats();
  EXPECT_EQ(0, tcData->getCounter(RibStats::kNexthopInfoCount));
}

} // namespace bgp
} // namespace facebook
