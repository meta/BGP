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

#include "neteng/fboss/bgp/cpp/tests/RibUtils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/logging/xlog.h>
#include <gflags/gflags.h>

DECLARE_bool(disable_rib_policy_scuba_logging);

#include "neteng/fboss/bgp/cpp/BgpServiceUtil.h"
#include "neteng/fboss/bgp/cpp/common/Utils.h"
#include "neteng/fboss/bgp/cpp/config/ConfigManager.h"
#include "neteng/fboss/bgp/cpp/fsdb/FsdbSyncer.h"
#include "neteng/fboss/bgp/cpp/stats/StatsDC.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"

#include "fboss/lib/thrift_service_client/ConnectionOptions.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"

using namespace facebook::neteng::fboss::bgp_attr;
using namespace facebook::nettools::bgplib;
using namespace facebook::neteng::fboss::bgp::thrift;
using namespace facebook::bgp::rib_policy;
using facebook::fboss::fsdb::test::FsdbTestServer;
using facebook::fboss::fsdb::test::FsdbTestSubscriber;

using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Pointee;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;

namespace {
const auto kUcmpWidth{1024};
}

namespace facebook {
namespace bgp {

/*
 * UTIL FUNCTIONS
 */
std::vector<TBgpCommunity> createTBgpCommunities(
    const std::vector<std::string>& communitiesStr) {
  std::vector<TBgpCommunity> result;
  for (const auto& commStr : communitiesStr) {
    auto maybeCommParts = parseCommunityStr(commStr);
    if (maybeCommParts.hasError()) {
      XLOGF(ERR, "{}", maybeCommParts.error());
      continue;
    }
    TBgpCommunity bgpCommunity;
    bgpCommunity.asn() = maybeCommParts.value().first;
    bgpCommunity.value() = maybeCommParts.value().second;
    bgpCommunity.community() =
        ((int64_t)(*bgpCommunity.asn()) << 16) + *bgpCommunity.value();
    result.emplace_back(std::move(bgpCommunity));
  }
  return result;
}

std::vector<TAsPathSeg> createTAsPath(
    const std::vector<std::pair<int, std::vector<int64_t>>>& segments) {
  std::vector<TAsPathSeg> result;
  for (const auto& seg : segments) {
    TAsPathSegType type;
    switch (seg.first) {
      case 1: // AS_SET
        type = TAsPathSegType::AS_SET;
        break;
      case 2: // AS_SEQUENCE
        type = TAsPathSegType::AS_SEQUENCE;
        break;
      case 3: // AS_CONFED_SEQUENCE
        type = TAsPathSegType::AS_CONFED_SEQUENCE;
        break;
      case 4: // AS_CONFED_SET
        type = TAsPathSegType::AS_CONFED_SET;
        break;
    }
    TAsPathSeg asPath;
    asPath.seg_type() = type;
    asPath.asns_4_byte() = seg.second;
    result.push_back(std::move(asPath));
  }
  return result;
}

std::vector<BgpAttrAsPathSegmentC> createBgpAttrAsPathSegmentCV(
    const std::vector<std::pair<int, std::vector<uint32_t>>>& segments) {
  std::vector<BgpAttrAsPathSegmentC> result;
  for (const auto& seg : segments) {
    BgpAttrAsPathSegmentC newSeg;
    switch (seg.first) {
      case 1: // AS_SET
        newSeg.asSet = std::set<uint32_t>(seg.second.begin(), seg.second.end());
        break;
      case 2: // AS_SEQUENCE
        newSeg.asSequence = seg.second;
        break;
      case 3: // AS_CONFED_SEQUENCE
        newSeg.asConfedSequence = seg.second;
        break;
      case 4: // AS_CONFED_SET
        newSeg.asConfedSet =
            std::set<uint32_t>(seg.second.begin(), seg.second.end());
        break;
    }
    result.push_back(newSeg);
  }

  return result;
}

// TODO: remove test T113736668
std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>
getLocalRoutesWithOldAsField() {
  thrift::BgpNetwork network1;
  network1.prefix() = folly::IPAddress::networkToString(kV4Prefix1);
  std::vector<TAsPathSeg> asPaths;
  TAsPathSeg asPath1;
  asPath1.asns() = {kAsn2, kAsn3};
  asPath1.seg_type() = TAsPathSegType::AS_SEQUENCE;
  asPaths.emplace_back(asPath1);
  network1.as_path() = asPaths;
  return {{kV4Prefix1, network1}};
}

std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>
getDefaultLocalRoutes() {
  // v4 prefix with minimum_supporting_routes not set
  thrift::BgpNetwork network1;
  *network1.prefix() = folly::IPAddress::networkToString(kV4Prefix1);

  // v6 prefix with minimum_supporting_routes = 0
  thrift::BgpNetwork network2;
  *network2.prefix() = folly::IPAddress::networkToString(kV6Prefix1);
  network2.minimum_supporting_routes() = 0;
  network2.communities() = std::vector<std::string>{kCommunity1, kCommunity2};

  // v4 prefix with minimum_supporting_routes = 1
  thrift::BgpNetwork network3;
  *network3.prefix() = folly::IPAddress::networkToString(kV4Prefix2);
  network3.minimum_supporting_routes() = 1;
  network3.communities() = std::vector<std::string>{kCommunity3};

  // v6 prefix with minimum_supporting_routes = 1
  thrift::BgpNetwork network4;
  *network4.prefix() = folly::IPAddress::networkToString(kV6Prefix2);
  network4.minimum_supporting_routes() = 1;
  network4.communities() = std::vector<std::string>{kCommunity4};

  // v4 prefix with all attributes
  thrift::BgpNetwork network5;
  *network5.prefix() = folly::IPAddress::networkToString(kV4Prefix5);
  network5.minimum_supporting_routes() = 1;
  network5.communities() = std::vector<std::string>{kCommunity3};
  network5.local_pref() = kLocalPref2;
  network5.origin() = 1;
  network5.nexthop() = "11.0.0.1"; // kV4Nexthop1
  // int64 == uint32
  network5.as_path() = createTAsPath(
      {std::make_pair(
           4, std::vector<int64_t>{kAsn1, kAsn2, kAsn3}), // AS_CONFED_SET
       std::make_pair(1, std::vector<int64_t>{kAsn4, kAsn5})}); // AS_SET

  return {
      {kV4Prefix1, network1},
      {kV6Prefix1, network2},
      {kV4Prefix2, network3},
      {kV6Prefix2, network4},
      {kV4Prefix5, network5}};
}

void verifyAttributesHelper(
    std::shared_ptr<const facebook::bgp::BgpPath> attrs,
    const folly::IPAddress& expectedNexthop,
    const facebook::nettools::bgplib::BgpAttrOrigin& expectedOrigin,
    const uint32_t expectedLocalPref,
    const std::vector<facebook::nettools::bgplib::BgpAttrCommunityC>&
        expectedCommunitySet,
    const std::vector<facebook::nettools::bgplib::BgpAttrAsPathSegmentC>&
        expectedAsPath) {
  EXPECT_EQ(expectedNexthop, attrs->getNexthop());
  EXPECT_EQ(expectedOrigin, attrs->getOrigin());
  EXPECT_TRUE(attrs->getLocalPref().has_value());
  EXPECT_EQ(expectedLocalPref, attrs->getLocalPref().value());
  EXPECT_THAT(
      attrs->getCommunities().get(),
      UnorderedElementsAreArray(expectedCommunitySet));
  EXPECT_THAT(expectedAsPath, ElementsAreArray(attrs->getAsPath().get()));
}

/*
 * MOCK FIB
 */

bool MockFib::isConnected() const {
  return isConnected_;
}

bool MockFib::isFullSynced() const {
  return fullSynced_;
}

std::optional<FibOutRoute> MockFib::updateUnicastRoute(
    const folly::CIDRNetwork& prefix,
    std::shared_ptr<const BgpPath> attrsToBeAdvertised,
    std::shared_ptr<const WeightedNexthopMap> weightedNexthops,
    const bool isLocalRouteBest,
    const bool installToFib,
    const folly::F14NodeMap<folly::IPAddress, facebook::bgp::NexthopInfo>&
        nexthopInfoMap,
    const std::optional<uint32_t>& classId,
    std::shared_ptr<const NexthopTopoInfoMap> nexthopTopoInfoMap,
    const BgpRouteType,
    bool enableFibOutTracking) {
  if (!classId && !nexthopTopoInfoMap) {
    updateUnicastRoute_(
        prefix,
        attrsToBeAdvertised,
        weightedNexthops,
        isLocalRouteBest,
        installToFib,
        nexthopInfoMap);
  } else if (!nexthopTopoInfoMap) {
    updateUnicastRoute_(
        prefix,
        attrsToBeAdvertised,
        weightedNexthops,
        isLocalRouteBest,
        installToFib,
        nexthopInfoMap,
        classId);
  } else {
    updateUnicastRoute_(
        prefix,
        attrsToBeAdvertised,
        weightedNexthops,
        isLocalRouteBest,
        installToFib,
        nexthopInfoMap,
        classId,
        nexthopTopoInfoMap);
  }

  waitForAck_[attrsToBeAdvertised][prefix] = weightedNexthops;
  if (!weightedNexthops || weightedNexthops->empty() || !installToFib) {
    return enableFibOutTracking ? std::optional<FibOutRoute>{FibOutRoute{}}
                                : std::nullopt;
  }

  FibNexthopSet nexthops;
  if (enableFibOutTracking && !isLocalRouteBest) {
    nexthops.reserve(weightedNexthops->size());
    for (const auto& [nexthop, weight] : *weightedNexthops) {
      nexthops.push_back(
          FibOutNexthop{
              .address = nexthop,
              .weight = weight,
              .role = FibOutNexthopRole::PRIMARY,
          });
    }
  }
  return enableFibOutTracking ? std::optional<FibOutRoute>{FibOutRoute{
                                    .nexthops = std::move(nexthops),
                                    .metadata = FibOutRouteMetadata::program(),
                                }}
                              : std::nullopt;
}

folly::coro::Task<void> MockFib::program(bool isSync) {
  program_(isSync);
  fullSynced_ |= isSync;
  toRibQ_.push(FibProgrammedMessage(waitForAck_, isSync));
  waitForAck_.clear();
  // notify that program() has finished to MockRib
  fulfillFibProgramPromise();
  co_return;
}

folly::Future<folly::Unit> MockFib::getFibProgramFuture() {
  std::lock_guard lock(fibProgramPromiseMutex_);
  if (fibProgramPromise_) {
    XLOG(
        FATAL, "fibProgramPromise_ needs to be reset before creating another.");
  }
  fibProgramPromise_ = std::make_unique<folly::Promise<folly::Unit>>();
  return fibProgramPromise_->getFuture();
}

void MockFib::disconnect() {
  isConnected_ = false;
}

void MockFib::connect() {
  isConnected_ = true;
}

void MockFib::fulfillFibProgramPromise() {
  std::unique_lock lock(fibProgramPromiseMutex_);
  if (fibProgramPromise_) {
    /*
     * not calling fibProgramPromise_->setValue() directly as fiber context
     * switch happens before updating fibProgramPromise_ to nullptr
     */
    auto promise = std::move(fibProgramPromise_);
    lock.unlock();
    promise->setValue();
  }
}

/*
 * MOCK RIB
 */

void MockRib::prepareFibProgramming(bool fullSync) noexcept {
  prepareFibProgramming_();
  RibDC::prepareFibProgramming(fullSync);
  fibItems.clear();
  for (auto it = fibBatchList_.begin(); it != fibBatchList_.end(); ++it) {
    auto [snapshot, inserted] = fibItems.emplace(it->getPrefix(), *it);
    CHECK(inserted);
    /*
     * fibItems is a test-only copy of the batch input, not another live route.
     * Do not let it contribute an owner to the canonical FIB-out set count.
     */
    eraseFibOut(snapshot->second);
  }
  fulfillRibPrepareFibProgrammingPromise(fibItems.size());
}

void MockRib::createFib() {
  if (!fib_) {
    fib_ = std::make_unique<MockFib>(
        nullptr, this->getRibAsyncScope(), fromFibMessageQ_);
  }
}

void MockRib::clearRibPolicy() {
  RibBase::clearRibPolicy();
}

TResult MockRib::setRouteAttributePolicy(
    std::unique_ptr<TRouteAttributePolicy> policy) {
  auto res = RibBase::setRouteAttributePolicy(std::move(policy));

  std::map<std::string, int64_t> counters;
  fb303::ThreadCachedServiceData::get()->getCounters(counters);
  static auto rcvdCounter = fmt::format("{}.count", RibStatsDC::kRaPolicyRcvd);
  static auto updateCounter =
      fmt::format("{}.count", RibStatsDC::kRaPolicyUpdate);
  EXPECT_TRUE(counters.contains(rcvdCounter));
  EXPECT_TRUE(counters.contains(updateCounter));
  EXPECT_GE(counters.at(rcvdCounter), 0);
  EXPECT_GE(counters.at(updateCounter), 0);

  return res;
}

void MockRib::clearRouteAttributePolicy() {
  RibBase::clearRouteAttributePolicy();

  std::map<std::string, int64_t> counters;
  fb303::ThreadCachedServiceData::get()->getCounters(counters);
  static auto rcvdCounter = fmt::format("{}.count", RibStatsDC::kRaPolicyRcvd);
  static auto updateCounter =
      fmt::format("{}.count", RibStatsDC::kRaPolicyUpdate);
  EXPECT_TRUE(counters.contains(rcvdCounter));
  EXPECT_TRUE(counters.contains(updateCounter));
  EXPECT_GE(counters.at(rcvdCounter), 0);
  EXPECT_GE(counters.at(updateCounter), 0);
}

TResult MockRib::setPathSelectionPolicy(
    std::unique_ptr<TPathSelectionPolicy> policy,
    bool forceUpdate) {
  auto res = RibDC::setPathSelectionPolicy(std::move(policy), forceUpdate);

  std::map<std::string, int64_t> counters;
  fb303::ThreadCachedServiceData::get()->getCounters(counters);
  static auto rcvdCounter = fmt::format("{}.count", RibStatsDC::kPsPolicyRcvd);
  static auto updateCounter =
      fmt::format("{}.count", RibStatsDC::kPsPolicyUpdate);
  EXPECT_TRUE(counters.contains(rcvdCounter));
  EXPECT_TRUE(counters.contains(updateCounter));
  EXPECT_GE(counters.at(rcvdCounter), 0);
  EXPECT_GE(counters.at(updateCounter), 0);
  return res;
}

void MockRib::clearPathSelectionPolicy() {
  RibDC::clearPathSelectionPolicy();

  std::map<std::string, int64_t> counters;
  fb303::ThreadCachedServiceData::get()->getCounters(counters);
  static auto rcvdCounter = fmt::format("{}.count", RibStatsDC::kPsPolicyRcvd);
  static auto updateCounter =
      fmt::format("{}.count", RibStatsDC::kPsPolicyUpdate);
  EXPECT_TRUE(counters.contains(rcvdCounter));
  EXPECT_TRUE(counters.contains(updateCounter));
  EXPECT_GE(counters.at(rcvdCounter), 0);
  EXPECT_GE(counters.at(updateCounter), 0);
}

void MockRib::configureCanonicalRibProducerForTest(
    std::shared_ptr<CanonicalRibUpdateQueue> updateQueue) {
  canonicalRibUpdateQueue_ = std::move(updateQueue);
  /* These producer-only tests begin after the initial RIB computation. */
  fsdbSyncerStarted_ = true;
}

void MockRib::processSingleRibInUpdateForTest(
    const TinyPeerInfo& peer,
    std::shared_ptr<const BgpPath> attrs,
    const PrefixPathId& prefixPathId) {
  processSingleRibInUpdate(peer, std::move(attrs), prefixPathId);
}

void MockRib::enqueueCanonicalRibFullSnapshotForTest() {
  enqueueCanonicalRibFullSnapshot("test");
}

void MockRib::setRouteFilterPolicy(
    std::unique_ptr<TRouteFilterPolicy> policy,
    bool forceUpdate) {
  RibBase::setRouteFilterPolicy(std::move(policy), forceUpdate);

  std::map<std::string, int64_t> counters;
  fb303::ThreadCachedServiceData::get()->getCounters(counters);
  static auto rcvdCounter = fmt::format("{}.count", RibStats::kRfPolicyRcvd);
  static auto updateCounter =
      fmt::format("{}.count", RibStats::kRfPolicyUpdate);
  EXPECT_TRUE(counters.contains(rcvdCounter));
  EXPECT_TRUE(counters.contains(updateCounter));
  EXPECT_GE(counters.at(rcvdCounter), 0);
  EXPECT_GE(counters.at(updateCounter), 0);
}

void MockRib::clearRouteFilterPolicy() {
  RibBase::clearRouteFilterPolicy();

  std::map<std::string, int64_t> counters;
  fb303::ThreadCachedServiceData::get()->getCounters(counters);
  static auto rcvdCounter = fmt::format("{}.count", RibStats::kRfPolicyRcvd);
  static auto updateCounter =
      fmt::format("{}.count", RibStats::kRfPolicyUpdate);
  EXPECT_TRUE(counters.contains(rcvdCounter));
  EXPECT_TRUE(counters.contains(updateCounter));
  EXPECT_GE(counters.at(rcvdCounter), 0);
  EXPECT_GE(counters.at(updateCounter), 0);
}

folly::coro::Task<bool> MockRib::co_waitForPredicate(
    const std::function<bool(void)>& pred) {
  constexpr int maxTries = 3;
  for (int tries = 0; tries < maxTries; tries++) {
    if (pred()) {
      co_return true;
    }
    co_await folly::coro::sleep(std::chrono::milliseconds(100));
  }
  co_return pred();
}

bool MockRib::waitForPredicate(const std::function<bool(void)>& pred) {
  return folly::coro::blockingWait(
      co_withExecutor(evb_.getEventBase(), co_waitForPredicate(pred)));
}

TPathSelectionPolicy MockRib::waitForPathSelectionPolicyUpdate() {
  EXPECT_TRUE(
      waitForPredicate([this]() { return pathSelectionPolicy_ != nullptr; }));
  return RibDC::getPathSelectionPolicy();
}

TRouteAttributePolicy MockRib::waitForRouteAttributePolicyUpdate() {
  EXPECT_TRUE(
      waitForPredicate([this]() { return routeAttributePolicy_ != nullptr; }));
  return RibBase::getRouteAttributePolicy();
}

TRouteFilterPolicy MockRib::waitForRouteFilterPolicyUpdate() {
  EXPECT_TRUE(
      waitForPredicate([this]() { return routeFilterPolicy_ != nullptr; }));
  return RibBase::getRouteFilterPolicy();
}

TPathSelectionPolicy MockRib::waitForPathSelectionPolicyClear() {
  EXPECT_TRUE(
      waitForPredicate([this]() { return pathSelectionPolicy_ == nullptr; }));
  return RibDC::getPathSelectionPolicy();
}

TRouteAttributePolicy MockRib::waitForRouteAttributePolicyClear() {
  EXPECT_TRUE(
      waitForPredicate([this]() { return routeAttributePolicy_ == nullptr; }));
  return RibBase::getRouteAttributePolicy();
}

TRouteFilterPolicy MockRib::waitForRouteFilterPolicyClear() {
  EXPECT_TRUE(
      waitForPredicate([this]() { return routeFilterPolicy_ == nullptr; }));
  return RibBase::getRouteFilterPolicy();
}

void MockRib::waitForRibPolicyClear() {
  EXPECT_TRUE(waitForPredicate([this]() {
    return pathSelectionPolicy_ == nullptr && routeAttributePolicy_ == nullptr;
  }));
}

folly::Future<folly::Unit> MockRib::getRibPolicyReplaceFuture() {
  folly::Future<folly::Unit> future;

  ribPolicyReplacePromise_.withWLock([this,
                                      &future](auto& ribPolicyReplacePromise) {
    if (ribPolicyReplacePromise) {
      XLOG(
          FATAL,
          "ribPolicyReplacePromise_ needs to be reset before creating another.");
    }

    ribPolicyReplacePromise = std::make_unique<folly::Promise<folly::Unit>>();

    future = ribPolicyReplacePromise->getFuture();
  });

  return future;
}

void MockRib::replaceRibPolicy(
    std::unique_ptr<RibPolicy> newRibPolicy,
    bool isBootstrap) {
  RibDC::replaceRibPolicy(std::move(newRibPolicy), isBootstrap);
  fulfillRibPolicyReplacePromise();
}
bool MockRib::replaceRouteAttributePolicy(
    std::unique_ptr<RouteAttributePolicy> newPolicy) {
  auto ret = RibDC::replaceRouteAttributePolicy(std::move(newPolicy));
  fulfillRibPolicyReplacePromise();
  return ret;
}
bool MockRib::replacePathSelectionPolicy(
    std::unique_ptr<PathSelectionPolicy> newPolicy,
    bool isBootstrap,
    bool forceUpdate) {
  auto ret = RibDC::replacePathSelectionPolicy(
      std::move(newPolicy), isBootstrap, forceUpdate);
  fulfillRibPolicyReplacePromise();
  return ret;
}
bool MockRib::replaceRouteFilterPolicy(
    std::unique_ptr<RouteFilterPolicy> newPolicy,
    bool isBootstrap,
    bool forceUpdate) {
  auto ret = RibBase::replaceRouteFilterPolicy(
      std::move(newPolicy), isBootstrap, forceUpdate);
  fulfillRibPolicyReplacePromise();
  return ret;
}

void MockRib::fulfillRibPolicyReplacePromise() {
  ribPolicyReplacePromise_.withWLock([this](auto& ribPolicyReplacePromise) {
    if (ribPolicyReplacePromise) {
      /*
       * not calling ribPolicyReplacePromise_->setValue() directly as fiber
       * context switch happens before updating ribPolicyReplacePromise_ to
       * nullptr
       */
      auto promise = std::move(ribPolicyReplacePromise);
      promise->setValue();
    }
  });
}

MockFib* MockRib::getMockFib() {
  return dynamic_cast<MockFib*>(fib_.get());
}

std::shared_ptr<RouteInfo> MockRib::getBestPath(
    const folly::CIDRNetwork& prefix) {
  std::shared_ptr<RouteInfo> bestPath = nullptr;
  evb_.runInEventBaseThreadAndWait([&]() {
    auto ribEntry = ribEntries_.find(prefix);
    if (ribEntry != ribEntries_.end()) {
      bestPath = ribEntry->second.getBestPath();
    }
  });
  return bestPath;
}

folly::F14NodeMap<uint32_t, std::shared_ptr<RouteInfo>> MockRib::getMultipath(
    const folly::CIDRNetwork& prefix) {
  folly::F14NodeMap<uint32_t, std::shared_ptr<RouteInfo>> multipath;
  evb_.runInEventBaseThreadAndWait([&]() {
    auto ribEntry = ribEntries_.find(prefix);
    if (ribEntry != ribEntries_.end()) {
      multipath = ribEntry->second.getMultipaths();
    }
  });
  return multipath;
}

folly::Future<folly::Unit> MockRib::getRibPrepareFibProgrammingFuture(
    int numRibEntriesToProgram) {
  folly::Future<folly::Unit> future;
  ribPrepareFibProgrammingPromise_.withWLock(
      [this, &numRibEntriesToProgram, &future](
          auto& ribPrepareFibProgrammingPromise) {
        if (ribPrepareFibProgrammingPromise) {
          XLOG(
              FATAL,
              "ribPrepareFibProgrammingPromise_ needs to be reset before creating another.");
        }
        ribPrepareFibProgrammingPromise =
            std::make_unique<folly::Promise<folly::Unit>>();
        // set the number of entries to program before setting the future
        this->ribEntriesToProgram_ = numRibEntriesToProgram;

        future = ribPrepareFibProgrammingPromise->getFuture();
      });
  return future;
}

void MockRib::setRibPauseTime(std::chrono::milliseconds ribPauseTime) {
  evb_.runInEventBaseThreadAndWait(
      [&, this]() { ribPauseTime_ = ribPauseTime; });
}

void MockRib::setRouteChurnDetectionThresholds(
    uint64_t highWatermarkForRouteChurn,
    uint64_t lowWatermarkForRouteChurn,
    std::chrono::seconds routeChurncheckInterval) {
  evb_.runInEventBaseThreadAndWait([&, this]() {
    highWatermarkForRouteChurn_ = highWatermarkForRouteChurn;
    lowWatermarkForRouteChurn_ = lowWatermarkForRouteChurn;
    routeChurnCheckInterval_ = routeChurncheckInterval;
  });
}

void MockRib::fulfillRibPrepareFibProgrammingPromise(
    int numRibEntriesProgrammed) {
  ribPrepareFibProgrammingPromise_.withWLock(
      [this, &numRibEntriesProgrammed](auto& ribPrepareFibProgrammingPromise) {
        if (ribPrepareFibProgrammingPromise) {
          this->ribEntriesToProgram_ -= numRibEntriesProgrammed;
          if (this->ribEntriesToProgram_ <= 0) {
            /*
             * not calling fibProgramPromise_->setValue() directly as fiber
             * context switch happens before updating fibProgramPromise_ to
             * nullptr
             */
            auto promise = std::move(ribPrepareFibProgrammingPromise);
            promise->setValue();
          }
        }
      });
}

/*
 * TEST FIXTURES
 */

void RibFixture::createGlobalConfig(
    ComputeUcmpFromLbwComm computeUcmpFromLbwComm,
    CountConfedsInAsPathLen countConfedsInAsPathLen,
    EnableNexthopTracking enableNexthopTracking,
    bool enableFibOutTracking,
    bool enableRibAllocatedPathId) {
  bgpGlobalConfig1_ = std::make_shared<facebook::bgp::BgpGlobalConfig>(
      kPeerAsn3, // localAsn
      kPeerAddr3, // routerId
      kPeerAddr3, // clusterId
      kHoldTime, // holdTime
      std::nullopt, // listenAddr
      kGrRestartTime, // grRestartTime
      std::unordered_map<
          folly::CIDRNetwork,
          thrift::BgpNetwork>(), // networksV4
      std::unordered_map<
          folly::CIDRNetwork,
          thrift::BgpNetwork>(), // networksV6
      std::nullopt, // localConfedAsn
      computeUcmpFromLbwComm, // computeUcmpFromLbwComm
      kUcmpWidth, // ucmpWidth
      std::nullopt, // ucmpQuantizer
      ValidateRemoteAs{true},
      SupportStatefulGr{true},
      EnableServerSocket{true},
      AllowLoopbackReflection{false},
      countConfedsInAsPathLen,
      std::nullopt, // deviceName
      std::nullopt, // switchLimitConfig
      std::nullopt, // dynamicPeerLimit
      std::nullopt, // streamSubscriberLimit
      enableNexthopTracking, // enableNexthopTracking
      std::vector<std::string>{}, // includeInterfaceRegexes
      EnableDynamicPolicyEvaluation{false},
      std::nullopt, // thriftServerConfig
      false, // enableEgressQueueBackpressure
      false, // enableUpdateGroup
      UpdateGroupConfig{}, // updateGroupConfig
      enableRibAllocatedPathId, // enableRibAllocatedPathId
      false, // enableOptimizedGR
      false, // enablePolicyDefaultAction
      false, // enableAddPathGrReconcile
      false, // enableLegacyV4NlriEncoding
      std::nullopt, // enableStreamSubscriberBackpressure
      EnableNetlinkDampening{false},
      false, // enableRouteRefresh
      enableFibOutTracking);

  /* Own the rib policy files so a concurrent stress job cannot collide. */
  FLAGS_rp_state_file = (ribPolicyDir_.path() / "rp_state.txt").string();
  FLAGS_rp_change_history_file =
      (ribPolicyDir_.path() / "rp_change_history.txt").string();
}

Config RibFixture::config() const {
  thrift::BgpConfig thriftConfig;
  thriftConfig.router_id() = kPeerAddr3.str();
  thriftConfig.local_as() = kPeerAsn3;
  thriftConfig.hold_time() = kHoldTime.count();
  thriftConfig.graceful_restart_convergence_seconds() = kGrRestartTime.count();
  thriftConfig.listen_addr() = kLocalAddr1.str();
  thriftConfig.eor_time_s() = 45;
  return Config{std::move(thriftConfig)};
}

void RibFixture::createPeerManager() {
  auto configManager =
      std::make_shared<ConfigManager>(std::make_shared<Config>(config_));
  peerManager_ = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, neighborEventQ_);

  sessionMgr_ = std::make_shared<MockSessionManager>(*bgpGlobalConfig1_, false);
  peerManager_->setSessionManager(sessionMgr_);
}

bool RibFixture::isBestPathAndFibProgrammingPaused() const {
  return rib_->pauseBestPathAndFibProgramming_;
}

void RibFixture::ribFixtureDefaultSetup(
    ComputeUcmpFromLbwComm computeUcmpFromLbwComm,
    CountConfedsInAsPathLen countConfedsInAsPathLen,
    EnableNexthopTracking enableNexthopTracking,
    std::shared_ptr<NexthopCache> nexthopCache,
    bool enableFibOutTracking,
    bool enableRibAllocatedPathId) {
  createGlobalConfig(
      computeUcmpFromLbwComm,
      countConfedsInAsPathLen,
      enableNexthopTracking,
      enableFibOutTracking,
      enableRibAllocatedPathId);
  createPeerManager();
  // create attributes
  attr_ =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr_->publish();
  // create attributes with higher preference
  attrHighLocalPref_ = attr_->clone();
  attrHighLocalPref_->setLocalPref(110);
  attrHighLocalPref_->publish();

  // create rib, fib, rib thread
  setUpRibAndFib({}, nexthopCache);
  // Setup Service
  setUpService();
}

void RibFixture::SetUp() {
  ribFixtureDefaultSetup();
}

void RibFixture::TearDown() {
  XLOG(INFO, "Tearing down RibFixture");
  EXPECT_CALL(*fib_, stop()).Times(1);
  // send null msg to ribInQ to stop
  rib_->stop();
  if (fsdbSyncer_) {
    fsdbSyncer_->stop();
  }
  ribThread_.join();
  boost::filesystem::remove(FLAGS_rp_state_file);
  boost::filesystem::remove(FLAGS_rp_change_history_file);

  // stop sessionMgr_ before peerManager_
  sessionMgr_.reset();
  peerManager_.reset();
}

std::unique_ptr<MockRib> RibFixture::createMockRib(
    const std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>&
        localRoutes,
    const std::optional<bgp_policy::BgpPolicies>& policyConfig,
    std::shared_ptr<NexthopCache> nexthopCache) {
  return std::make_unique<MockRib>(
      localRoutes,
      *bgpGlobalConfig1_,
      policyConfig,
      ribInQ_,
      ribOutQ_,
      kDevPlatform,
      fsdbSyncer_.get(),
      nexthopCache);
}

void RibFixture::setUpRibAndFib(
    const std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>&
        localRoutes,
    const std::shared_ptr<NexthopCache>& nexthopCache) {
  // Set gflag to skip rib policy logging
  FLAGS_disable_rib_policy_scuba_logging = true;
  // create rib
  rib_ = createMockRib(localRoutes, std::nullopt, nexthopCache);
  EXPECT_CALL(*rib_, getFibBackoffTimeout())
      .WillRepeatedly(::testing::Return(kFibBackOffTimeoutTest));

  // create fib
  rib_->createFib();
  fib_ = rib_->getMockFib();
  ASSERT_NE(nullptr, fib_);

  // create rib thread
  ribThread_ = std::thread([rawRib = rib_.get()]() { rawRib->run(); });

  // wait for rib evb to start
  rib_->evb_.waitUntilRunning();
}

void RibFixture::setUpService() {
  auto configManager =
      std::make_shared<ConfigManager>(std::make_shared<Config>(config_));

  service_ = std::make_unique<BgpServiceDC>(
      *peerManager_, configManager, *rib_, nullptr, watchdog_, false);
}

/**
 * @brief Delayed start of FIB programming timer.
 */
folly::coro::Task<void> RibFixture::delayedFibProgramSchedule() {
  co_await folly::futures::sleep(std::chrono::seconds(5));
  rib_->schedulePrepareFibProgrammingTimer();
}

void RibFixture::createFsdbTestResources() {
  fsdbServer_ = std::make_unique<FsdbTestServer>();
  FLAGS_fsdbPort = fsdbServer_->getFsdbPort();
  FLAGS_publish_state_to_fsdb = true;
  FLAGS_publish_stats_to_fsdb = true;
  // Use fast reconnect intervals for tests to avoid flaky timeouts
  FLAGS_fsdb_reconnect_ms = 100;
  FLAGS_fsdb_initial_backoff_reconnect_ms = 100;
  FLAGS_fsdb_max_backoff_reconnect_ms = 100;
  fsdbSubscriber_ = std::make_unique<FsdbTestSubscriber>("test-subscriber");
  fsdbSyncerEventBaseThread_ =
      std::make_unique<folly::ScopedEventBaseThread>("FsdbSyncerTest");
  fsdbSyncer_ =
      std::make_unique<FsdbSyncer>(*fsdbSyncerEventBaseThread_->getEventBase());
}

void RibFixture::setUpFsdb() {
  createFsdbTestResources();
  rib_->evb_.runInEventBaseThreadAndWait([this]() {
    rib_->fsdbSyncer_ = fsdbSyncer_.get();
    rib_->configureCanonicalRibExport(*fsdbSyncer_);
  });
}

void RibFixture::completeFibProgrammingPass(bool fullSync) {
  rib_->evb_.runInEventBaseThreadAndWait(
      [this, fullSync]() { rib_->onPrepareFibProgrammingComplete(fullSync); });
}

bool RibFixture::isFsdbSyncerStarted() const {
  bool started = false;
  rib_->evb_.runInEventBaseThreadAndWait(
      [this, &started]() { started = rib_->fsdbSyncerStarted_; });
  return started;
}

bool RibFixture::isCanonicalRibUpdateQueueEmpty() const {
  bool empty = false;
  rib_->evb_.runInEventBaseThreadAndWait([this, &empty]() {
    empty = rib_->canonicalRibUpdateQueue_ &&
        rib_->canonicalRibUpdateQueue_->empty();
  });
  return empty;
}

bool RibFixture::isRibEoRReceived() const {
  bool received = false;
  rib_->evb_.runInEventBaseThreadAndWait(
      [this, &received]() { received = rib_->ribEoRReceived_; });
  return received;
}

bool RibFixture::isInitialPathComputationPending() const {
  bool pending = false;
  rib_->evb_.runInEventBaseThreadAndWait(
      [this, &pending]() { pending = rib_->pendingInitialPathComputation_; });
  return pending;
}

void RibFixture::waitForFsdbPublisherConnected() {
  WITH_RETRIES_N_TIMED(120, std::chrono::milliseconds(500), {
    auto metadata = fsdbServer_->getPublisherRootMetadata("bgp", false);
    ASSERT_EVENTUALLY_TRUE(metadata.has_value());
  });
}

TResult RibFixture::sendRouteAttributePolicySet(TRouteAttributePolicy policy) {
  // call set route attribute policy api
  return rib_->setRouteAttributePolicy(
      std::make_unique<TRouteAttributePolicy>(policy));
}

TResult RibFixture::sendPathSelectionPolicySet(TPathSelectionPolicy policy) {
  // call set route attribute policy api
  return rib_->setPathSelectionPolicy(
      std::make_unique<TPathSelectionPolicy>(policy));
}

void RibFixture::sendRouteFilterPolicySet(TRouteFilterPolicy policy) {
  // call set route filter policy api
  rib_->setRouteFilterPolicy(std::make_unique<TRouteFilterPolicy>(policy));
}

void RibFixture::sendInitialPathComputation(
    std::chrono::milliseconds nexthopResolutionTimeout) {
  ribInQ_.forcePush(RibInInitialPathComputation(nexthopResolutionTimeout));
}

void RibFixture::sendAnnouncement(
    const PrefixPathIds& pfxPathIds,
    const TinyPeerInfo& peer,
    std::shared_ptr<facebook::bgp::BgpPath> attr) {
  ribInQ_.fiberPush(RibInAnnouncement(peer, pfxPathIds, attr));
}

void RibFixture::sendWithdrawal(
    const PrefixPathIds& pfxPathIds,
    const TinyPeerInfo& peer) {
  ribInQ_.fiberPush(RibInWithdrawal(peer, pfxPathIds));
}

// Send announcement to Rib with count number of prefixes
void RibFixture::sendBulkAnnouncement(
    uint32_t count,
    const TinyPeerInfo& peer,
    std::shared_ptr<facebook::bgp::BgpPath> attr,
    int startAddress) {
  PrefixPathIds prefixes;
  prefixes.reserve(count);
  for (auto i = 0; i < count; i++) {
    prefixes.emplace_back(
        folly::CIDRNetwork(folly::IPAddress::fromLongHBO(startAddress + i), 32),
        kDefaultPathID);
  }
  ribInQ_.fiberPush(RibInAnnouncement(peer, std::move(prefixes), attr));
}

// Send withdrawals to Rib with count number of prefixes
void RibFixture::sendBulkWithdrawal(
    uint32_t count,
    const TinyPeerInfo& peer,
    int startAddress) {
  PrefixPathIds prefixes;
  prefixes.reserve(count);
  for (auto i = 0; i < count; i++) {
    prefixes.emplace_back(
        folly::CIDRNetwork(folly::IPAddress::fromLongHBO(startAddress + i), 32),
        kDefaultPathID);
  }
  ribInQ_.fiberPush(RibInWithdrawal(peer, std::move(prefixes)));
}

// Send message to pause best path computation and Fib programming
void RibFixture::sendPauseBestPathAndFibProgramming(
    RibPauseResumeCause ribPauseResumeCause) {
  ribInQ_.fiberPush(PauseBestPathAndFibProgramming(ribPauseResumeCause));
}

// Send message to resume best path computation and Fib programming
void RibFixture::sendResumeBestPathAndFibProgramming(
    RibPauseResumeCause ribPauseResumeCause) {
  ribInQ_.fiberPush(ResumeBestPathAndFibProgramming(ribPauseResumeCause));
}

void RibFixture::updateCacheAndNotifyRib(
    const std::vector<NexthopStatus>& updates) {
  auto statuses = rib_->nexthopCache_->addOrUpdateNextHopStatus(updates);
  if (!statuses.empty()) {
    ribInQ_.fiberPush(RibInNexthopUpdate(std::move(statuses)));
  }
}

void RibFixture::sendNexthopResolutionUpdate(
    NexthopResolutionUpdate nexthopResUpdate) {
  ribInQ_.fiberPush(std::move(nexthopResUpdate));
}

/*
 * Case1:
 * Build 4 paths with first 3 of localPref=200 and 4th one with
 * localPref=100.  In all attrs add 5 extended community attributes as
 * follows:
 * Each has two unique transitive community (one AStype and one IPv4
 * type).
 * Each has 1 LBW attr:- path1 lbw=10G, path2 lbw=5G, path3 lbw=2G,
 * path4 lbw=1G.
 * Each has two non-transitive (one opaque type and one regular type)
 * Path4 does not have the last two non-transitive communities.
 * Call sendInitialPathComputation() which in turn will trigger calling of
 * selectBestPath(multipathSelector, bestpathSelector, ) on above path-set.
 * Verify that :
 *  * Each path should have correct weights
 *  * Path4 should not be in multipath
 *  * Check the weights of the nethops in the fib program call.
 */
void RibFixture::runLbwCommunityBestPathTest(bool computeUcmpFromLbwComm) {
  uint32_t nhWt1{0}, nhWt2{0}, nhWt3{0};
  if (computeUcmpFromLbwComm) {
    nhWt1 = 10;
    nhWt2 = 5;
    nhWt3 = 2;
  } else {
    nhWt1 = nhWt2 = nhWt3 = 0;
  }
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

  // Set extended community attributes of attr1
  {
    nettools::bgplib::BgpAttrExtCommunitiesC extCommunities;
    // AS-specific AS# 1234
    extCommunities.emplace_back(0x00011234, 0x600DCAFE);
    // IPv4-specific IP: 10.10.10.5
    extCommunities.emplace_back(0x01010A0A, 0x0A05CAFE);
    // LBW Community attr with b/w = 10G
    extCommunities.emplace_back(
        nettools::bgplib::BgpExtCommunityLinkBandWidthTypeC(
            uint16_t(kLocalAs1), kLbw10G));
    // Opaque type
    extCommunities.emplace_back(0x43011234, 0x56789ABC);
    // Regular type
    extCommunities.emplace_back(0x45012345, 0x6789ABCD);
    attrs1->setExtCommunities(extCommunities);
  }
  // Set extended community attributes of attr2
  {
    nettools::bgplib::BgpAttrExtCommunitiesC extCommunities;
    // AS-specific AS# 1235
    extCommunities.emplace_back(0x00011235, 0x600DCAFE);
    // IPv4-specific IP: 16.16.16.5
    extCommunities.emplace_back(0x01011010, 0x1005CAFE);
    // LBW Community attr with b/w = 5G
    extCommunities.emplace_back(
        nettools::bgplib::BgpExtCommunityLinkBandWidthTypeC(
            uint16_t(kLocalAs1), kLbw5G));
    // Opaque type
    extCommunities.emplace_back(0x43012345, 0x6789ABCD);
    // Regular type
    extCommunities.emplace_back(0x45023456, 0x789ABCDE);
    attrs2->setExtCommunities(extCommunities);
  }
  // Set extended community attributes of attr3
  {
    nettools::bgplib::BgpAttrExtCommunitiesC extCommunities;
    // AS-specific AS# 1236
    extCommunities.emplace_back(0x00011236, 0x600DCAFE);
    // IPv4-specific IP: 6.6.6.5
    extCommunities.emplace_back(0x01010606, 0x0605CAFE);
    // LBW Community attr with b/w = 2G
    extCommunities.emplace_back(
        nettools::bgplib::BgpExtCommunityLinkBandWidthTypeC(
            uint16_t(kLocalAs1), kLbw2G));
    // Opaque type
    extCommunities.emplace_back(0x43013456, 0x789ABCDE);
    // Regular type
    extCommunities.emplace_back(0x45024567, 0x89ABCDEF);
    attrs3->setExtCommunities(extCommunities);
  }
  // Set extended community attributes of attr4
  {
    nettools::bgplib::BgpAttrExtCommunitiesC extCommunities;
    // AS-specific AS# 1237
    extCommunities.emplace_back(0x00011237, 0x600DCAFE);
    // IPv4-specific IP: 3.3.3.3
    extCommunities.emplace_back(0x01010303, 0x0303CAFE);
    // LBW Community attr with b/w = 1G
    extCommunities.emplace_back(
        nettools::bgplib::BgpExtCommunityLinkBandWidthTypeC(
            uint16_t(kLocalAs1), kLbw1G));
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
     * As LBW community is present in all paths,
     * UCMP will be effective. Hence we should expect
     * weight params of each nh to be proportional to lbw community
     * announced b/w.
     */
    WeightedNexthopMap nhWts = {
        {kV4Nexthop1, nhWt1}, {kV4Nexthop2, nhWt2}, {kV4Nexthop3, nhWt3}};
    EXPECT_CALL(
        *fib_,
        updateUnicastRoute_(
            Eq(kV4Prefix1),
            Eq(attrs1),
            Pointee(nhWts),
            false,
            true,
            testing::_))
        .Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);
  }
  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  rib_->setFibBatchTime(std::chrono::milliseconds(2));
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attrs1);
  sendAnnouncement(prefixBatch, eBgpPeer2_, attrs2);
  sendAnnouncement(prefixBatch, eBgpPeer3_, attrs3);
  sendAnnouncement(prefixBatch, eBgpPeer4_, attrs4);
  fibFuture.wait();
  auto prefix = std::make_unique<std::string>(
      folly::IPAddress::networkToString(kV4Prefix1));
  auto output = rib_->getRibEntryForPrefix(std::move(prefix));
  auto paths = output[0].paths()->find(kBestPathGroup)->second;
  EXPECT_EQ(3, paths.size());
  auto ecmp_nh_set = {
      *paths[0].next_hop()->prefix_bin(),
      *paths[1].next_hop()->prefix_bin(),
      *paths[2].next_hop()->prefix_bin()};
  EXPECT_THAT(
      ecmp_nh_set,
      UnorderedElementsAre(
          network::toBinaryAddress(kV4Nexthop1).addr()->toStdString(),
          network::toBinaryAddress(kV4Nexthop2).addr()->toStdString(),
          network::toBinaryAddress(kV4Nexthop3).addr()->toStdString()));
  auto rejPaths = output[0].paths()->find(kDefaultPathGroup)->second;
  EXPECT_EQ(1, rejPaths.size());
  EXPECT_EQ(
      *rejPaths[0].next_hop()->prefix_bin(),
      network::toBinaryAddress(kV4Nexthop4).addr()->toStdString());
  {
    const auto& ribEntry = (rib_->ribEntries_.find(kV4Prefix1))->second;
    EXPECT_EQ(
        static_cast<uint32_t>(
            ribEntry.getAggregateReceivedUcmpWeight().value() / LbwToUcmpWt),
        static_cast<uint32_t>((kLbw10G + kLbw5G + kLbw2G) / LbwToUcmpWt));
    EXPECT_EQ(ribEntry.getMultipathWeightedNexthops()->size(), 3);
    for (auto& nhwt : *ribEntry.getMultipathWeightedNexthops()) {
      if (nhwt.first == kV4Nexthop1) {
        EXPECT_EQ(nhwt.second, nhWt1);
      } else if (nhwt.first == kV4Nexthop2) {
        EXPECT_EQ(nhwt.second, nhWt2);
      } else if (nhwt.first == kV4Nexthop3) {
        EXPECT_EQ(nhwt.second, nhWt3);
      }
    }
  }

  /*
   * Case2:
   * All attrs are same from all peers except for peer3 which changes the
   * lbw value of attrs3 to 1G from 2G.  If computeUcmpFromLbwComm is
   * turned on, expected behavior is that after bestpath selection new
   * weights are programmed in the fib side but ecmp nexthop should be
   * same as before.
   */
  if (!computeUcmpFromLbwComm) {
    /*
     * If computeUcmpFromLbwComm is not on, then there is no change in
     * weights computed
     */
    return;
  }

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
  {
    InSequence dummy;
    /*
     * As LBW community is present in all paths,
     * UCMP will be effective. Hence we should expect
     * weight params of each nh to be proportional to lbw community
     * announced b/w.
     */
    if (computeUcmpFromLbwComm) {
      nhWt1 = 10;
      nhWt2 = 5;
      nhWt3 = 1;
    } else {
      nhWt1 = nhWt2 = nhWt3 = 0;
    }
    WeightedNexthopMap nhWts = {
        {kV4Nexthop1, nhWt1}, {kV4Nexthop2, nhWt2}, {kV4Nexthop3, nhWt3}};
    EXPECT_CALL(
        *fib_,
        updateUnicastRoute_(
            Eq(kV4Prefix1),
            Eq(attrs1),
            Pointee(nhWts),
            false,
            true,
            testing::_))
        .Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);
  }
  auto new_attrs3 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(2, 1, 0, 2));
  new_attrs3->setNexthop(kV4Nexthop3);
  new_attrs3->setLocalPref(kLocalPref2);
  {
    nettools::bgplib::BgpAttrExtCommunitiesC extCommunities;
    // LBW Community attr with b/w = 1G
    extCommunities.emplace_back(
        nettools::bgplib::BgpExtCommunityLinkBandWidthTypeC(
            uint16_t(kLocalAs1), kLbw1G));
    new_attrs3->setExtCommunities(extCommunities);
  }
  new_attrs3->publish();

  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer3_, new_attrs3);
  fibFuture.wait();
  // Check ribentry has 3 nhs with appropriate weights
  {
    auto& ribEntry = (rib_->ribEntries_.find(kV4Prefix1))->second;
    EXPECT_EQ(
        ribEntry.getAggregateReceivedUcmpWeight().value(),
        (kLbw10G + kLbw5G + kLbw1G));
    EXPECT_EQ(ribEntry.getMultipathWeightedNexthops()->size(), 3);
    for (auto& nhwt : *ribEntry.getMultipathWeightedNexthops()) {
      if (nhwt.first == kV4Nexthop1) {
        EXPECT_EQ(nhwt.second, nhWt1);
      } else if (nhwt.first == kV4Nexthop2) {
        EXPECT_EQ(nhwt.second, nhWt2);
      } else if (nhwt.first == kV4Nexthop3) {
        EXPECT_EQ(nhwt.second, nhWt3);
      }
    }
  }
}

folly::F14FastMap<uint32_t, std::shared_ptr<const BgpPath>>
RibFixture::getPathIdAttrsMapFromAnnouncement(const RibOutAnnouncement& ann) {
  folly::F14FastMap<uint32_t, std::shared_ptr<const BgpPath>> res;
  for (auto it : ann.addPathEntries) {
    res.emplace(it.pathIdToSend, it.attrs);
  }
  return res;
}

folly::F14FastSet<uint32_t> RibFixture::getPathIdSetFromWithdrawal(
    const RibOutWithdrawal& with) {
  folly::F14FastSet<uint32_t> res;
  for (auto it : with.addPathEntries) {
    res.emplace(it.pathIdToSend);
  }
  return res;
}

void RibWithLocalRouteFixture::SetUp() {
  createGlobalConfig();

  // create attributes
  attr_ =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr_->publish();
}

void LocalRouteWithPolicyFixture::SetUp() {
  createGlobalConfig();
}

void LocalRouteWithPolicyFixture::TearDown() {
  if (fsdbSyncer_) {
    fsdbSyncer_->stop();
  }
}

void RibNoUcmpComputeFixture::SetUp() {
  ribFixtureDefaultSetup(ComputeUcmpFromLbwComm{false});
}

void RibFixtureCountConfedsInAsPathLen::SetUp() {
  ribFixtureDefaultSetup(
      ComputeUcmpFromLbwComm{true}, CountConfedsInAsPathLen{true});
}

void RibNexthopTrackingFixture::SetUp() {
  ribFixtureDefaultSetup(
      ComputeUcmpFromLbwComm{true} /*default*/,
      ComputeUcmpFromLbwComm{false} /*default*/,
      EnableNexthopTracking{true} /*enableNexthopTracking*/,
      std::make_shared<NexthopCache>());
}

void RibAllocatedPathIdFixture::SetUp() {
  ribFixtureDefaultSetup(
      ComputeUcmpFromLbwComm{true},
      CountConfedsInAsPathLen{false},
      EnableNexthopTracking{false},
      /*nexthopCache=*/nullptr,
      /*enableFibOutTracking=*/false,
      /*enableRibAllocatedPathId=*/true);
}

} // namespace bgp
} // namespace facebook
