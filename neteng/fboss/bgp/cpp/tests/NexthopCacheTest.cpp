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

#define NexthopCache_TEST_FRIENDS

#include <type_traits>

#include <folly/IPAddress.h>
#include <folly/coro/BlockingWait.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fb303/ServiceData.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/lib/coro/BackPressuredQueue.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopCache.h"

#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopKey.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopStatus.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/Utils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"
#include "neteng/fboss/bgp/cpp/watchdog/MonitoredQueue.h"

using namespace facebook::bgp;
using namespace ::testing;

namespace facebook::bgp {

static_assert(
    !std::is_constructible_v<NexthopStatus, const folly::IPAddress&, bool>);
static_assert(!std::is_constructible_v<
              NexthopStatus,
              const folly::IPAddress&,
              const openr::thrift::NextHopStatus&>);

TEST(NexthopKeyTest, HashMapKeepsAddressesAndVrfsIndependent) {
  const NexthopKey underlay{.ip = folly::IPAddress("2001:db8::1")};
  const NexthopKey otherVrf{.ip = underlay.ip, .nextHopVrfId = 7};
  const NexthopKey otherAddress{.ip = folly::IPAddress("2001:db8::2")};
  folly::F14FastMap<NexthopKey, int, NexthopKey::Hash> values;
  values.emplace(underlay, 10);
  values.emplace(otherVrf, 20);
  values.emplace(otherAddress, 30);
  EXPECT_EQ(3, values.size());
  EXPECT_EQ(10, values.at(NexthopKey{.ip = underlay.ip}));
  EXPECT_EQ(20, values.at(otherVrf));
  EXPECT_EQ(30, values.at(otherAddress));
  values.erase(otherVrf);
  EXPECT_TRUE(values.contains(underlay));
  EXPECT_FALSE(values.contains(otherVrf));
}

class NexthopCacheTestFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    cache_ = std::make_unique<NexthopCache>();
    routeInfo_ = createRouteInfo(
        kV4Prefix1,
        kPeerAddr2,
        kPeerAddr1,
        kLocalPref,
        {},
        kPeerAsn2,
        kPeerRouterId2);
  }

  void updateCacheAndNotifyRib(const std::vector<NexthopStatus>& updates) {
    auto statuses = cache_->addOrUpdateNextHopStatus(updates);
    if (!statuses.empty()) {
      ribInQ_.fiberPush(RibInNexthopUpdate(std::move(statuses)));
    }
  }

  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ_{
      nettools::bgplib::kMaxIngressQueueSize};
  std::unique_ptr<NexthopCache> cache_;
  std::shared_ptr<RouteInfo> routeInfo_;
};

TEST_F(NexthopCacheTestFixture, DefaultParameterValues) {
  // Create a nexthop IP
  folly::IPAddress nexthopIp("2620:0:1cff:dead:bef1:ffff:ffff:1");

  // Create a map with the nexthop status using FibAgent format
  std::map<std::string, openr::thrift::NextHopStatus> fibAgentStatusMap;
  // Convert IP address to binary string format
  auto binaryAddr = openr::toBinaryAddress(nexthopIp);
  std::string binaryIp(binaryAddr.addr()->data(), binaryAddr.addr()->size());

  // Create thrift NextHopStatus for unreachable nexthop
  openr::thrift::NextHopStatus thriftStatus;
  thriftStatus.isReachable() = false;

  // Add to the map
  fibAgentStatusMap[binaryIp] = thriftStatus;

  // Add the nexthop to the cache with default parameter values
  auto nexthopStatusList =
      convertFibAgentStatusToNexthopStatus(fibAgentStatusMap);
  cache_->addOrUpdateNextHopStatus(nexthopStatusList);

  /*
   * Verify the nexthop was added with default values (igpCost = std::nullopt)
   * Since the nexthop is unreachable, igpCost should be std::nullopt
   */
  auto status = cache_->registerAndGetNexthopStatus({nexthopIp});
  EXPECT_FALSE(status.isReachable());
  EXPECT_FALSE(status.getIgpCost().has_value());
  // Verify isConnected is false for FIB agent nexthops
  EXPECT_THAT(status.isConnected(), Eq(false));

  /*
   * Test with a non-existent nexthop - it will create a new entry with default
   * values
   */
  folly::IPAddress nonExistentNexhop("2620:0:1cff:dead:bef1:ffff:ffff:2");
  auto nonExistentStatus =
      cache_->registerAndGetNexthopStatus({nonExistentNexhop});
  EXPECT_FALSE(nonExistentStatus.isReachable());
  EXPECT_FALSE(nonExistentStatus.getIgpCost().has_value());
  // Verify default isConnected is UNKNOWN for new nexthops
  EXPECT_THAT(nonExistentStatus.isConnected(), Eq(std::nullopt));
}

TEST_F(NexthopCacheTestFixture, AddOrUpdateNexthopStatus) {
  // Create a nexthop IP
  folly::IPAddress nexthopIp("2620:0:1cff:dead:bef1:ffff:ffff:99");
  uint32_t igpCost = 10;

  /**
   * Case 1: NexthopCache gets update from FibAgent, then
   * registerAndGetNexthopStatus is called. No more changes/updates to the
   * nexthop status, RibInQ does not get any updates
   */

  // Step 1: Create a map with the nexthop status using FibAgent format
  std::map<std::string, openr::thrift::NextHopStatus> fibAgentStatusMap;
  // Convert IP address to binary string format
  auto binaryAddr = openr::toBinaryAddress(nexthopIp);
  std::string binaryIp(binaryAddr.addr()->data(), binaryAddr.addr()->size());

  // Create thrift NextHopStatus for reachable nexthop with IGP cost
  openr::thrift::NextHopStatus thriftStatus;
  thriftStatus.isReachable() = true;
  thriftStatus.igpCost() = igpCost;

  // Add to the map
  fibAgentStatusMap[binaryIp] = thriftStatus;

  // Step 2: Add the nexthop to the cache
  cache_->addOrUpdateNextHopStatus(
      convertFibAgentStatusToNexthopStatus(fibAgentStatusMap));

  // Step 3: Register and get the nexthop status
  auto status = cache_->registerAndGetNexthopStatus({nexthopIp});

  // Verify the status values
  EXPECT_TRUE(status.isReachable());
  EXPECT_EQ(status.getIgpCost(), igpCost);
  EXPECT_THAT(status.isConnected(), Eq(false));

  // Step 4: Verify that no message was pushed to RibInQ
  EXPECT_TRUE(ribInQ_.empty());

  /**
   *  Case 2: Now, the nexthop is registered from Rib
   * Nexthop status is updated from FibAgent, RibInQ gets an update
   */

  // Step 5: Update the nexthop's reachability status
  fibAgentStatusMap.clear();

  // Create thrift NextHopStatus for unreachable nexthop
  openr::thrift::NextHopStatus updatedThriftStatus;
  updatedThriftStatus.isReachable() = false;
  fibAgentStatusMap[binaryIp] = updatedThriftStatus;

  // Step 6: Now add the nexthop status again to trigger a message
  auto nexthopStatusList2 =
      convertFibAgentStatusToNexthopStatus(fibAgentStatusMap);
  updateCacheAndNotifyRib(nexthopStatusList2);

  // Step 7: Now check if a message was pushed to RibInQ
  EXPECT_FALSE(ribInQ_.empty());

  // Use blockingWait to get the actual message from the Task
  auto msg = folly::coro::blockingWait(ribInQ_.pop());
  EXPECT_TRUE(std::holds_alternative<RibInNexthopUpdate>(msg));

  auto& update = std::get<RibInNexthopUpdate>(msg);
  EXPECT_EQ(update.nexthopStatuses.size(), 1);
  EXPECT_EQ(update.nexthopStatuses[0].getNexthop(), nexthopIp);
  EXPECT_FALSE(update.nexthopStatuses[0].isReachable());
  EXPECT_FALSE(update.nexthopStatuses[0].getIgpCost().has_value());
  EXPECT_THAT(update.nexthopStatuses[0].isConnected(), Eq(false));

  /**
   * Case 3: registerAndGetNexthopStatus on a new nexthopIp,
   * NexthopCache gets update from FibAgent, ensure RibInQ gets the update on
   * the change
   */
  // Step 8: Create a new nexthop IP
  folly::IPAddress newNexthopIp("2620:0:1cff:dead:bef1:ffff:ffff:100");

  /*
   * Step 9: Register the new nexthop from RIB (this will mark it as registered
   * from RIB) Since it doesn't exist in the cache yet, it will create a new
   * entry
   */
  auto newStatus = cache_->registerAndGetNexthopStatus({newNexthopIp});
  EXPECT_FALSE(newStatus.isReachable());

  // Step 10: Create thrift NextHopStatus for reachable nexthop with IGP cost
  auto newBinaryAddr = openr::toBinaryAddress(newNexthopIp);
  std::string newBinaryIp(
      newBinaryAddr.addr()->data(), newBinaryAddr.addr()->size());
  openr::thrift::NextHopStatus newThriftStatus;
  newThriftStatus.isReachable() = true;
  newThriftStatus.igpCost() = 20; // Different IGP cost from the first nexthop

  // Add to the map
  fibAgentStatusMap[newBinaryIp] = newThriftStatus;

  // Step 11: Clear the RibInQ before adding the new nexthop status
  while (!ribInQ_.empty()) {
    folly::coro::blockingWait(ribInQ_.pop());
  }

  // Step 12: Add the new nexthop to the cache
  updateCacheAndNotifyRib(
      convertFibAgentStatusToNexthopStatus(fibAgentStatusMap));

  /*
   * Step 13: Check if a message was pushed to RibInQ
   * Since the nexthop was registered from RIB before the update, RibInQ should
   * get an update
   */
  EXPECT_FALSE(ribInQ_.empty());

  // Use blockingWait to get the actual message from the Task
  auto newMsg = folly::coro::blockingWait(ribInQ_.pop());
  EXPECT_TRUE(std::holds_alternative<RibInNexthopUpdate>(newMsg));

  auto& newUpdate = std::get<RibInNexthopUpdate>(newMsg);
  EXPECT_EQ(newUpdate.nexthopStatuses.size(), 1);
  EXPECT_EQ(newUpdate.nexthopStatuses[0].getNexthop(), newNexthopIp);
  EXPECT_TRUE(newUpdate.nexthopStatuses[0].isReachable());
  EXPECT_EQ(newUpdate.nexthopStatuses[0].getIgpCost().value(), 20);
  EXPECT_THAT(update.nexthopStatuses[0].isConnected(), Eq(false));
}

TEST_F(NexthopCacheTestFixture, ReachableWithoutCostUpdatesCacheAndRib) {
  const folly::IPAddress nexthopIp("2620:0:1cff:dead:bef1:ffff:ffff:101");
  cache_->registerAndGetNexthopStatus({nexthopIp});

  const NexthopStatus ebbStatus(
      {.ip = nexthopIp},
      /*isReachable=*/true,
      /*igpCost=*/std::nullopt,
      /*isConnected=*/std::nullopt,
      /*excludeNexthopWithoutCost=*/true);
  auto updates = cache_->addOrUpdateNextHopStatus({ebbStatus});
  ASSERT_EQ(updates.size(), 1);
  EXPECT_TRUE(updates[0].isReachable());
  EXPECT_FALSE(updates[0].isResolvedForSelection());

  EXPECT_TRUE(cache_->addOrUpdateNextHopStatus({ebbStatus}).empty());

  const NexthopStatus fbossStatus(
      {.ip = nexthopIp},
      /*isReachable=*/true,
      /*igpCost=*/std::nullopt,
      /*isConnected=*/std::nullopt,
      /*excludeNexthopWithoutCost=*/false);
  updates = cache_->addOrUpdateNextHopStatus({fbossStatus});
  ASSERT_EQ(updates.size(), 1);
  EXPECT_TRUE(updates[0].isResolvedForSelection());
}

TEST_F(NexthopCacheTestFixture, RetainsReachableStatusWithoutCost) {
  const folly::IPAddress nexthopIp("2620:0:1cff:dead:bef1:ffff:ffff:102");
  cache_->addOrUpdateNextHopStatus(
      {NexthopStatus({.ip = nexthopIp}, true, std::nullopt)});
  cache_->registerAndGetNexthopStatus({nexthopIp});

  EXPECT_FALSE(cache_->unregisterAndRemoveNexthopStatus({nexthopIp}));
  EXPECT_FALSE(cache_->isRegistered({nexthopIp}));

  const auto status = cache_->registerAndGetNexthopStatus({nexthopIp});
  EXPECT_TRUE(status.isReachable());
  EXPECT_FALSE(status.getIgpCost().has_value());
}

TEST_F(NexthopCacheTestFixture, AddOrUpdateMultipleNexthopsWithMap) {
  // Create multiple nexthop IPs and their statuses
  folly::IPAddress nexthopIp1("2620:0:1cff:dead:bef1:ffff:ffff:99");
  folly::IPAddress nexthopIp2("2001:db8::1");

  // Create a map of nexthops to their statuses using FibAgent format
  std::map<std::string, openr::thrift::NextHopStatus> fibAgentStatusMap;
  // Convert IP addresses to binary string format
  auto binaryAddr1 = openr::toBinaryAddress(nexthopIp1);
  auto binaryAddr2 = openr::toBinaryAddress(nexthopIp2);
  std::string binaryIp1(binaryAddr1.addr()->data(), binaryAddr1.addr()->size());
  std::string binaryIp2(binaryAddr2.addr()->data(), binaryAddr2.addr()->size());

  // Create thrift NextHopStatus objects
  openr::thrift::NextHopStatus thriftStatus1;
  thriftStatus1.isReachable() = true;
  thriftStatus1.igpCost() = 10;

  openr::thrift::NextHopStatus thriftStatus2;
  thriftStatus2.isReachable() = false;

  // Add to the map
  fibAgentStatusMap[binaryIp1] = thriftStatus1;
  fibAgentStatusMap[binaryIp2] = thriftStatus2;

  /*
   * Add the nexthops to the cache using the
   * convertFibAgentStatusToNexthopStatus method
   */
  auto nexthopStatusList3 =
      convertFibAgentStatusToNexthopStatus(fibAgentStatusMap);
  cache_->addOrUpdateNextHopStatus(nexthopStatusList3);

  // Verify the current state of the nexthops
  auto status1 = cache_->registerAndGetNexthopStatus({nexthopIp1});
  EXPECT_TRUE(status1.isReachable());
  EXPECT_EQ(status1.getIgpCost().value(), 10);
  EXPECT_THAT(status1.isConnected(), Eq(false));

  auto status2 = cache_->registerAndGetNexthopStatus({nexthopIp2});
  EXPECT_FALSE(status2.isReachable());
  EXPECT_FALSE(status2.getIgpCost().has_value());
  EXPECT_THAT(status2.isConnected(), Eq(false));

  // Update the nexthops
  std::map<std::string, openr::thrift::NextHopStatus> updateMap1;
  openr::thrift::NextHopStatus updateStatus1;
  updateStatus1.isReachable() = false;
  updateMap1[binaryIp1] = updateStatus1;
  auto updateStatusList1 = convertFibAgentStatusToNexthopStatus(updateMap1);
  cache_->addOrUpdateNextHopStatus(updateStatusList1);

  std::map<std::string, openr::thrift::NextHopStatus> updateMap2;
  openr::thrift::NextHopStatus updateStatus2;
  updateStatus2.isReachable() = true;
  updateStatus2.igpCost() = 25;
  updateMap2[binaryIp2] = updateStatus2;
  auto updateStatusList2 = convertFibAgentStatusToNexthopStatus(updateMap2);
  cache_->addOrUpdateNextHopStatus(updateStatusList2);

  // Verify the updated state of the nexthops
  auto status1Update = cache_->registerAndGetNexthopStatus({nexthopIp1});
  EXPECT_FALSE(status1Update.isReachable());
  EXPECT_FALSE(status1Update.getIgpCost().has_value());
  EXPECT_THAT(status1Update.isConnected(), Eq(false));

  auto status2Update = cache_->registerAndGetNexthopStatus({nexthopIp2});
  EXPECT_TRUE(status2Update.isReachable());
  EXPECT_EQ(status2Update.getIgpCost().value(), 25);
  EXPECT_THAT(status2Update.isConnected(), Eq(false));
}

TEST_F(NexthopCacheTestFixture, UnregisterAndRemoveNexthopStatus) {
  // Create nexthop IPs for testing
  folly::IPAddress reachableNexhop("2620:0:1cff:dead:bef1:ffff:ffff:1");
  folly::IPAddress unreachableNexhop("2620:0:1cff:dead:bef1:ffff:ffff:2");
  folly::IPAddress nonExistentNexhop("2620:0:1cff:dead:bef1:ffff:ffff:3");

  // Create a map with the nexthop statuses using FibAgent format
  std::map<std::string, openr::thrift::NextHopStatus> fibAgentStatusMap;

  // Convert IP addresses to binary string format
  auto binaryAddr1 = openr::toBinaryAddress(reachableNexhop);
  auto binaryAddr2 = openr::toBinaryAddress(unreachableNexhop);
  std::string binaryIp1(binaryAddr1.addr()->data(), binaryAddr1.addr()->size());
  std::string binaryIp2(binaryAddr2.addr()->data(), binaryAddr2.addr()->size());

  // Create thrift NextHopStatus for reachable nexthop
  openr::thrift::NextHopStatus reachableStatus;
  reachableStatus.isReachable() = true;
  reachableStatus.igpCost() = 10;

  // Create thrift NextHopStatus for unreachable nexthop
  openr::thrift::NextHopStatus unreachableStatus;
  unreachableStatus.isReachable() = false;

  // Add to the map
  fibAgentStatusMap[binaryIp1] = reachableStatus;
  fibAgentStatusMap[binaryIp2] = unreachableStatus;

  // Add the nexthops to the cache
  cache_->addOrUpdateNextHopStatus(
      convertFibAgentStatusToNexthopStatus(fibAgentStatusMap));

  // Register the nexthops
  auto reachableNexhopStatus =
      cache_->registerAndGetNexthopStatus({reachableNexhop});
  auto unreachableNexhopStatus =
      cache_->registerAndGetNexthopStatus({unreachableNexhop});

  // Verify the nexthops were added correctly
  EXPECT_TRUE(reachableNexhopStatus.isReachable());
  EXPECT_FALSE(unreachableNexhopStatus.isReachable());

  /*
   * Test 1: Try to remove a reachable nexthop - should not remove it but
   * unregister it
   */
  EXPECT_FALSE(cache_->unregisterAndRemoveNexthopStatus({reachableNexhop}));

  // Verify the reachable nexthop is still in the cache
  auto reachableStatus1 =
      cache_->registerAndGetNexthopStatus({reachableNexhop});
  EXPECT_TRUE(reachableStatus1.isReachable());
  EXPECT_EQ(reachableStatus1.getIgpCost().value(), 10);
  EXPECT_THAT(reachableStatus1.isConnected(), Eq(false));

  // Test 2: Remove an unreachable nexthop - should succeed
  EXPECT_TRUE(cache_->unregisterAndRemoveNexthopStatus({unreachableNexhop}));

  /*
   * Verify the unreachable nexthop was removed from the cache by creating a new
   * entry with default values
   */
  auto newUnreachableStatus =
      cache_->registerAndGetNexthopStatus({unreachableNexhop});
  EXPECT_FALSE(newUnreachableStatus.isReachable());
  EXPECT_FALSE(newUnreachableStatus.getIgpCost().has_value());
  EXPECT_THAT(newUnreachableStatus.isConnected(), Eq(std::nullopt));

  // Test 3: Try to remove a non-existent nexthop - should fail
  EXPECT_FALSE(cache_->unregisterAndRemoveNexthopStatus({nonExistentNexhop}));

  // Test 4: Update a reachable nexthop to unreachable, then remove it
  std::map<std::string, openr::thrift::NextHopStatus> updateMap;
  openr::thrift::NextHopStatus updatedStatus;
  updatedStatus.isReachable() = false;
  updateMap[binaryIp1] = updatedStatus;

  // Update the nexthop in the cache
  cache_->addOrUpdateNextHopStatus(
      convertFibAgentStatusToNexthopStatus(updateMap));

  // Verify the nexthop was updated to unreachable
  auto updatedStatus1 = cache_->registerAndGetNexthopStatus({reachableNexhop});
  EXPECT_FALSE(updatedStatus1.isReachable());

  // Now remove the nexthop - should succeed since it's unreachable
  EXPECT_TRUE(cache_->unregisterAndRemoveNexthopStatus({reachableNexhop}));

  /*
   * Verify the nexthop was removed from the cache and a new entry is created
   * with default values
   */
  auto newStatus = cache_->registerAndGetNexthopStatus({reachableNexhop});
  EXPECT_FALSE(newStatus.isReachable());
  EXPECT_FALSE(newStatus.getIgpCost().has_value());
  EXPECT_THAT(newStatus.isConnected(), Eq(std::nullopt));
}

TEST_F(NexthopCacheTestFixture, NhtCacheReachabilityCounters) {
  RibStats::initCounters();
  auto tcData = fb303::ThreadCachedServiceData::get();

  folly::IPAddress nexthopIp("2620:0:1cff:dead:bef1:ffff:ffff:50");

  // Zero out counters from any prior tests
  tcData->setCounter(RibStats::kNhtCacheNexthopReachable + ".count", 0);
  tcData->setCounter(RibStats::kNhtCacheNexthopUnreachable + ".count", 0);
  tcData->publishStats();

  // Add unreachable nexthop, then register from RIB
  cache_->addOrUpdateNextHopStatus(
      {NexthopStatus({.ip = nexthopIp}, /*isReachable=*/false)});
  cache_->registerAndGetNexthopStatus({nexthopIp});

  // Source transition: unreachable -> reachable without a cost.
  cache_->addOrUpdateNextHopStatus(
      {NexthopStatus({.ip = nexthopIp}, /*isReachable=*/true)});
  tcData->publishStats();

  EXPECT_EQ(
      1, tcData->getCounter(RibStats::kNhtCacheNexthopReachable + ".count"));
  EXPECT_EQ(
      0, tcData->getCounter(RibStats::kNhtCacheNexthopUnreachable + ".count"));

  // The reverse source transition is also counted without a cost.
  cache_->addOrUpdateNextHopStatus(
      {NexthopStatus({.ip = nexthopIp}, /*isReachable=*/false)});
  tcData->publishStats();

  EXPECT_EQ(
      1, tcData->getCounter(RibStats::kNhtCacheNexthopReachable + ".count"));
  EXPECT_EQ(
      1, tcData->getCounter(RibStats::kNhtCacheNexthopUnreachable + ".count"));

  cache_->addOrUpdateNextHopStatus(
      {NexthopStatus({.ip = nexthopIp}, /*isReachable=*/true)});
  tcData->publishStats();

  EXPECT_EQ(
      2, tcData->getCounter(RibStats::kNhtCacheNexthopReachable + ".count"));
  EXPECT_EQ(
      1, tcData->getCounter(RibStats::kNhtCacheNexthopUnreachable + ".count"));

  // Adding a cost does not change source reachability.
  cache_->addOrUpdateNextHopStatus(
      {NexthopStatus({.ip = nexthopIp}, /*isReachable=*/true, 10)});
  tcData->publishStats();

  EXPECT_EQ(
      2, tcData->getCounter(RibStats::kNhtCacheNexthopReachable + ".count"));
  EXPECT_EQ(
      1, tcData->getCounter(RibStats::kNhtCacheNexthopUnreachable + ".count"));

  // Transition: reachable -> unreachable
  cache_->addOrUpdateNextHopStatus(
      {NexthopStatus({.ip = nexthopIp}, /*isReachable=*/false)});
  tcData->publishStats();

  EXPECT_EQ(
      2, tcData->getCounter(RibStats::kNhtCacheNexthopReachable + ".count"));
  EXPECT_EQ(
      2, tcData->getCounter(RibStats::kNhtCacheNexthopUnreachable + ".count"));

  // No-op: unreachable -> unreachable (no counter bump)
  cache_->addOrUpdateNextHopStatus(
      {NexthopStatus({.ip = nexthopIp}, /*isReachable=*/false)});
  tcData->publishStats();

  EXPECT_EQ(
      2, tcData->getCounter(RibStats::kNhtCacheNexthopReachable + ".count"));
  EXPECT_EQ(
      2, tcData->getCounter(RibStats::kNhtCacheNexthopUnreachable + ".count"));
}

/*
 * --- Neighbor-event resolution support (used by NetlinkWrapper on backbone)
 * ---
 */

TEST_F(NexthopCacheTestFixture, IsRegistered) {
  folly::IPAddress ip("10.0.0.1");
  // Unknown nexthop is not registered.
  EXPECT_FALSE(cache_->isRegistered({ip}));

  // Present (pushed by a watcher) but not yet referenced by the RIB.
  cache_->addOrUpdateNextHopStatus({NexthopStatus(
      {.ip = ip}, /*isReachable*/ true, 1, /*isConnected*/ true)});
  EXPECT_FALSE(cache_->isRegistered({ip}));

  // Registered by the RIB.
  cache_->registerAndGetNexthopStatus({ip});
  EXPECT_TRUE(cache_->isRegistered({ip}));
}

TEST_F(NexthopCacheTestFixture, GetRegisteredNexthopsInSubnet) {
  folly::IPAddress inA("10.10.0.5");
  folly::IPAddress inB("10.10.1.9");
  folly::IPAddress outOfSubnet("10.20.0.1");
  folly::IPAddress v6("2401:db00:10::5");
  folly::IPAddress unregisteredInSubnet("10.10.0.99");

  cache_->registerAndGetNexthopStatus({inA});
  cache_->registerAndGetNexthopStatus({inB});
  cache_->registerAndGetNexthopStatus({outOfSubnet});
  cache_->registerAndGetNexthopStatus({v6});
  // Present in the subnet but never registered from RIB.
  cache_->addOrUpdateNextHopStatus(
      {NexthopStatus({.ip = unregisteredInSubnet}, true, 1, true)});

  auto result = cache_->getRegisteredNexthopsInSubnet(
      folly::IPAddress::createNetwork("10.10.0.0/16"));

  /*
   * Only registered nexthops within the v4 /16 are returned; out-of-subnet, v6
   * (family mismatch), and unregistered nexthops are excluded.
   */
  EXPECT_THAT(result, UnorderedElementsAre(inA, inB));
}

TEST_F(NexthopCacheTestFixture, ClearConnectedStatus) {
  // Unknown nexthop -> nullopt.
  EXPECT_FALSE(
      cache_->clearConnectedStatus(folly::IPAddress("10.0.0.99")).has_value());

  // Non-connected (FIB, isConnected=nullopt) entry is left untouched.
  folly::IPAddress fibIp("10.0.0.1");
  cache_->addOrUpdateNextHopStatus(
      {NexthopStatus({.ip = fibIp}, true, 5, std::nullopt)});
  EXPECT_FALSE(cache_->clearConnectedStatus({fibIp}).has_value());
  EXPECT_TRUE(cache_->registerAndGetNexthopStatus({fibIp}).isReachable());

  /*
   * Connected + registered -> reset to unreachable with isConnected unset, and
   * the cleared status is returned for the caller to notify the RIB.
   */
  folly::IPAddress connIp("10.0.0.2");
  cache_->addOrUpdateNextHopStatus(
      {NexthopStatus({.ip = connIp}, true, 1, true)});
  cache_->registerAndGetNexthopStatus({connIp});
  auto cleared = cache_->clearConnectedStatus({connIp});
  ASSERT_TRUE(cleared.has_value());
  EXPECT_FALSE(cleared->isReachable());
  EXPECT_THAT(cleared->isConnected(), Eq(std::nullopt));

  /*
   * After clearing, a non-connected (FIB) source can take over again — the
   * source-priority rule no longer blocks it.
   */
  cache_->addOrUpdateNextHopStatus(
      {NexthopStatus({.ip = connIp}, true, 7, std::nullopt)});
  auto after = cache_->registerAndGetNexthopStatus({connIp});
  EXPECT_TRUE(after.isReachable());
  EXPECT_EQ(after.getIgpCost().value(), 7);

  /*
   * Connected but NOT registered -> still cleared, but nothing returned (no RIB
   * to notify).
   */
  folly::IPAddress connUnreg("10.0.0.3");
  cache_->addOrUpdateNextHopStatus(
      {NexthopStatus({.ip = connUnreg}, true, 1, true)});
  EXPECT_FALSE(cache_->clearConnectedStatus({connUnreg}).has_value());
  cache_->addOrUpdateNextHopStatus(
      {NexthopStatus({.ip = connUnreg}, true, 9, std::nullopt)});
  EXPECT_EQ(
      cache_->registerAndGetNexthopStatus({connUnreg}).getIgpCost().value(), 9);
}

TEST_F(NexthopCacheTestFixture, OnNexthopRegisteredHook) {
  std::vector<folly::IPAddress> fired;
  cache_->setOnNexthopRegistered(
      [&](folly::IPAddress ip) { fired.push_back(ip); });

  // Registering an unknown nexthop (default unreachable) fires the hook.
  folly::IPAddress unknownIp("10.0.0.1");
  cache_->registerAndGetNexthopStatus({unknownIp});
  ASSERT_EQ(fired.size(), 1);
  EXPECT_EQ(fired[0], unknownIp);

  /*
   * Registering an already-reachable nexthop does NOT fire the hook (BGP has
   * its answer; no on-demand resolution needed).
   */
  folly::IPAddress reachableIp("10.0.0.2");
  cache_->addOrUpdateNextHopStatus(
      {NexthopStatus({.ip = reachableIp}, true, 1, true)});
  cache_->registerAndGetNexthopStatus({reachableIp});
  EXPECT_EQ(fired.size(), 1);

  // EBB still needs a cost, so registration requests resolution.
  folly::IPAddress ebbNoCostIp("10.0.0.3");
  cache_->addOrUpdateNextHopStatus(
      {NexthopStatus({.ip = ebbNoCostIp}, true, std::nullopt)});
  cache_->registerAndGetNexthopStatus({ebbNoCostIp});
  ASSERT_EQ(fired.size(), 2);
  EXPECT_EQ(fired[1], ebbNoCostIp);

  // FBOSS allows selection without a cost, but still requests the metric.
  folly::IPAddress fbossNoCostIp("10.0.0.4");
  cache_->addOrUpdateNextHopStatus({NexthopStatus(
      {.ip = fbossNoCostIp},
      true,
      std::nullopt,
      std::nullopt,
      /*excludeNexthopWithoutCost=*/false)});
  cache_->registerAndGetNexthopStatus({fbossNoCostIp});
  ASSERT_EQ(fired.size(), 3);
  EXPECT_EQ(fired[2], fbossNoCostIp);

  // Registering an existing-but-unreachable nexthop fires the hook.
  folly::IPAddress unreachableIp("10.0.0.5");
  cache_->addOrUpdateNextHopStatus(
      {NexthopStatus({.ip = unreachableIp}, false)});
  cache_->registerAndGetNexthopStatus({unreachableIp});
  ASSERT_EQ(fired.size(), 4);
  EXPECT_EQ(fired[3], unreachableIp);
}

namespace {
constexpr uint64_t kNoEpoch{0};
} // namespace

/*
 * Every write gets a larger epoch than the write before it, across nexthops.
 * The status returned for the RIB carries the epoch of its own write.
 */
TEST_F(NexthopCacheTestFixture, EpochIncreasesOnEveryWrite) {
  const folly::IPAddress a("10.0.0.1");
  const folly::IPAddress b("10.0.0.2");

  cache_->addOrUpdateNextHopStatus({NexthopStatus({.ip = a}, true, 10)});
  const auto epochA = cache_->registerAndGetNexthopStatus({a}).getEpoch();
  cache_->addOrUpdateNextHopStatus({NexthopStatus({.ip = b}, true, 10)});
  const auto epochB = cache_->getNexthopStatus({b}).value().getEpoch();
  const auto pushed =
      cache_->addOrUpdateNextHopStatus({NexthopStatus({.ip = a}, true, 20)});

  ASSERT_EQ(1, pushed.size());
  EXPECT_NE(kNoEpoch, epochA);
  EXPECT_GT(epochB, epochA);
  EXPECT_GT(pushed.front().getEpoch(), epochB);
  EXPECT_EQ(
      pushed.front().getEpoch(),
      cache_->getNexthopStatus({a}).value().getEpoch());
}

/*
 * Calls that leave the stored status unchanged keep its epoch: a duplicate
 * status, an update that source priority blocks, and registration.
 */
TEST_F(NexthopCacheTestFixture, EpochUnchangedWithoutWrite) {
  const folly::IPAddress connected("10.0.0.3");
  cache_->addOrUpdateNextHopStatus(
      {NexthopStatus({.ip = connected}, true, 1, true)});
  const auto epoch =
      cache_->registerAndGetNexthopStatus({connected}).getEpoch();

  EXPECT_TRUE(cache_
                  ->addOrUpdateNextHopStatus(
                      {NexthopStatus({.ip = connected}, true, 1, true)})
                  .empty());
  EXPECT_TRUE(
      cache_
          ->addOrUpdateNextHopStatus({NexthopStatus({.ip = connected}, false)})
          .empty());
  cache_->registerAndGetNexthopStatus({connected});

  EXPECT_NE(kNoEpoch, epoch);
  EXPECT_EQ(epoch, cache_->getNexthopStatus({connected}).value().getEpoch());
}

/*
 * Removing an unreachable nexthop and creating it again gives a larger epoch,
 * so an update queued before the removal is never newer than the new entry.
 */
TEST_F(NexthopCacheTestFixture, RecreatedEntryGetsLargerEpoch) {
  const folly::IPAddress ip("10.0.0.4");
  const auto oldEpoch = cache_->registerAndGetNexthopStatus({ip}).getEpoch();
  ASSERT_TRUE(cache_->unregisterAndRemoveNexthopStatus({ip}));

  const auto newEpoch = cache_->registerAndGetNexthopStatus({ip}).getEpoch();

  EXPECT_NE(kNoEpoch, oldEpoch);
  EXPECT_GT(newEpoch, oldEpoch);
}

/*
 * clearConnectedStatus is a write. The stored status and the returned status
 * both carry its new epoch.
 */
TEST_F(NexthopCacheTestFixture, ClearConnectedStatusAdvancesEpoch) {
  const folly::IPAddress ip("10.0.0.5");
  cache_->addOrUpdateNextHopStatus({NexthopStatus({.ip = ip}, true, 1, true)});
  const auto oldEpoch = cache_->registerAndGetNexthopStatus({ip}).getEpoch();

  const auto cleared = cache_->clearConnectedStatus({ip});

  ASSERT_TRUE(cleared.has_value());
  EXPECT_GT(cleared->getEpoch(), oldEpoch);
  EXPECT_EQ(
      cleared->getEpoch(), cache_->getNexthopStatus({ip}).value().getEpoch());
}

/*
 * registerAndGetNexthopStatus returns the stored epoch, both when it creates
 * the entry and when the entry already exists.
 */
TEST_F(NexthopCacheTestFixture, RegisterAndGetReturnsStoredEpoch) {
  const folly::IPAddress created("10.0.0.6");
  const folly::IPAddress existing("10.0.0.7");
  cache_->addOrUpdateNextHopStatus({NexthopStatus({.ip = existing}, true, 10)});

  const auto createdStatus = cache_->registerAndGetNexthopStatus({created});
  const auto existingStatus = cache_->registerAndGetNexthopStatus({existing});

  EXPECT_NE(kNoEpoch, createdStatus.getEpoch());
  EXPECT_EQ(
      createdStatus.getEpoch(),
      cache_->getNexthopStatus({created}).value().getEpoch());
  EXPECT_NE(kNoEpoch, existingStatus.getEpoch());
  EXPECT_EQ(
      existingStatus.getEpoch(),
      cache_->getNexthopStatus({existing}).value().getEpoch());
}

} // namespace facebook::bgp
