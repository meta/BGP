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

#include <folly/IPAddress.h>
#include <folly/logging/xlog.h>

#include "configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h"
#include "neteng/fboss/bgp/cpp/common/FeatureFlags.h"
#include "neteng/fboss/bgp/cpp/common/Types.h"
#include "neteng/fboss/bgp/cpp/rib/RouteFilterConfig.h"
#include "neteng/fboss/bgp/cpp/rib/RouteInfoSelector.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"

namespace {
using facebook::bgp::NexthopInfo;
using facebook::bgp::thrift::BgpConfig;
using facebook::bgp::thrift::BgpSettingConfig;

const uint32_t kDefaultIgpCostMax = std::numeric_limits<uint32_t>::max();

const std::vector<folly::IPAddress> kDefaultPeerAddrs = {
    folly::IPAddress("10.1.1.1"),
    folly::IPAddress("11.1.1.1"),
    folly::IPAddress("12.1.1.1")};
const std::vector<folly::IPAddress> kDefaultNexthops = {
    folly::IPAddress("1.1.1.1"),
    folly::IPAddress("1.1.1.2"),
    folly::IPAddress("1.1.1.3")};
const std::vector<uint32_t> kDefaultPeerRouterIds = {
    0x0a010101,
    0x0b010101,
    0x0c010101};
const std::vector<uint32_t> kDefaultIgpCosts = {
    kDefaultIgpCostMax,
    kDefaultIgpCostMax,
    kDefaultIgpCostMax};

const std::vector<facebook::nettools::bgplib::BgpAttrClusterListC>
    kDefaultClusterLists = {
        facebook::nettools::bgplib::BgpAttrClusterListC{},
        facebook::nettools::bgplib::BgpAttrClusterListC{},
        facebook::nettools::bgplib::BgpAttrClusterListC{}};
const std::vector<facebook::bgp::BgpSessionType> kDefaultSessionTypes = {
    facebook::bgp::BgpSessionType::IBGP,
    facebook::bgp::BgpSessionType::IBGP,
    facebook::bgp::BgpSessionType::IBGP};

using facebook::nettools::bgplib::BgpAttrAsPathC;
using facebook::nettools::bgplib::BgpAttrAsPathSegmentC;
BgpAttrAsPathC createAsPath(
    const std::vector<uint32_t>& asSequence,
    const std::vector<uint32_t>& asConfedSequence = {}) {
  BgpAttrAsPathC asPath;
  if (asSequence.size()) {
    BgpAttrAsPathSegmentC seg;
    seg.asSequence = asSequence;
    asPath.push_back(seg);
  }
  if (asConfedSequence.size()) {
    BgpAttrAsPathSegmentC seg;
    seg.asConfedSequence = asConfedSequence;
    asPath.push_back(seg);
  }
  return asPath;
}

/*
 * Extends best path computation with multipath tiebreakers (with recovery).
 *
 * These filters are designed for Juniper's BGP multipath behavior. Note that
 * cluster list tie breaker's position is as per RFC, different from Juniper.
 */
using facebook::nettools::edge::HighestLowestRouteFilterAction;
using facebook::nettools::edge::RouteMetric;
std::vector<facebook::nettools::edge::RouteFilterConfig> getRouteFilterConfigs(
    facebook::bgp::CountConfedsInAsPathLen countConfedsInAsPathLen =
        facebook::bgp::CountConfedsInAsPathLen{false}) {
  return facebook::nettools::edge::concatRouteFilterConfigs(
      facebook::bgp::getBaseRouteFilterConfigsMultiPath(
          countConfedsInAsPathLen),
      {getRecoverEquivalentRouteFilterConfig(
          RouteMetric::BGP_PEER_ASN,
          // Tiebreakers before recovering equivalent routes
          {getHighestLowestRouteFilterConfig(
               RouteMetric::BGP_ROUTER_ID,
               HighestLowestRouteFilterAction::CHOOSE_LOWEST),
           getHighestLowestRouteFilterConfig(
               RouteMetric::BGP_CLUSTER_LIST_LEN,
               HighestLowestRouteFilterAction::CHOOSE_LOWEST),
           getHighestLowestRouteFilterConfig(
               RouteMetric::BGP_PEER_IP,
               HighestLowestRouteFilterAction::CHOOSE_LOWEST)})});
}

void loadBgpBestpathFeatures(
    bool enableMedComparison = false,
    bool enabledMedMissingAsWorst = false,
    bool enabledWeightComparison = false,
    bool enabledNextHopTracking = false,
    bool enabledEiBgpMultipath = false) {
  BgpConfig thriftConfig;
  BgpSettingConfig tBgpSettingConfig;
  tBgpSettingConfig.enable_med_comparison() = enableMedComparison;
  tBgpSettingConfig.enable_med_missing_as_worst() = enabledMedMissingAsWorst;
  tBgpSettingConfig.enable_weight_comparison() = enabledWeightComparison;
  tBgpSettingConfig.enable_next_hop_tracking() = enabledNextHopTracking;
  tBgpSettingConfig.enable_eibgp_multipath() = enabledEiBgpMultipath;
  thriftConfig.bgp_setting_config() = std::move(tBgpSettingConfig);
  facebook::bgp::FeatureFlags::LoadFromThriftConfig(thriftConfig);
}

} // namespace

namespace facebook::bgp {

using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;

using folly::IPAddress;
using nettools::bgplib::BgpAttrOrigin;
using std::shared_ptr;
using std::vector;

// tests route selection when comparing routes at an individual router
class RouteInfoSelectorTest : public ::testing::Test {
 public:
  RouteInfoSelectorTest() {
    multipathSelector_ = std::make_unique<RouteInfoSelector>(
        getBaseRouteFilterConfigsMultiPath(CountConfedsInAsPathLen{false}));
    multipathSamePeerAsnSelector_ =
        std::make_unique<RouteInfoSelector>(getRouteFilterConfigs());
    bestpathSelector_ =
        std::make_unique<RouteInfoSelector>(getRouteFilterConfigsBestPath());
  }
  ~RouteInfoSelectorTest() override = default;

 protected:
  std::unique_ptr<RouteInfoSelector> multipathSamePeerAsnSelector_{nullptr};
  std::unique_ptr<RouteInfoSelector> multipathSelector_{nullptr};
  std::unique_ptr<RouteInfoSelector> bestpathSelector_{nullptr};

  // get BGP attributes with default values
  vector<shared_ptr<BgpPath>> getDefaultAttrs(
      const vector<IPAddress>& nexthops = kDefaultNexthops,
      const vector<facebook::nettools::bgplib::BgpAttrClusterListC>&
          clusterLists = kDefaultClusterLists) {
    // TODO: currently only 3 attributes are created
    CHECK_EQ(3, nexthops.size());
    CHECK_EQ(3, clusterLists.size());

    vector<shared_ptr<BgpPath>> attrs;
    for (int i = 0; i < 3; i++) {
      auto attr = std::make_shared<BgpPath>();
      attr->setNexthop(nexthops[i]);
      attr->setLocalPref(kLocalPref); // set optional field
      attr->setClusterList(clusterLists[i]);
      attrs.emplace_back(attr);
    }
    return attrs;
  }

  // create three test RouteInfos with given BGP attributes
  vector<shared_ptr<RouteInfo>> createTestRouteInfos(
      const vector<shared_ptr<BgpPath>>& attrs,
      const vector<uint32_t>& peerRouterIds = kDefaultPeerRouterIds,
      const vector<IPAddress>& peerAddrs = kDefaultPeerAddrs,
      const vector<BgpSessionType>& sessionTypes = kDefaultSessionTypes,
      const vector<uint32_t>& igpCosts = kDefaultIgpCosts) {
    // TODO: currently only 3 RouteInfos are created
    CHECK_EQ(3, attrs.size());
    CHECK_EQ(3, peerRouterIds.size());
    CHECK_EQ(3, peerAddrs.size());
    CHECK_EQ(3, sessionTypes.size());
    CHECK_EQ(3, igpCosts.size());

    for (const auto& attr : attrs) {
      if (!attr->isPublished()) {
        attr->publish();
      }
    }

    RibEntry testRibEntry(kV4Prefix1);

    /*
     * Store NexthopInfo objects as shared_ptr to ensure proper lifetime
     * management
     */
    static std::vector<std::shared_ptr<NexthopInfo>> nextHopInfos;
    nextHopInfos.clear();
    nextHopInfos.reserve(3);

    vector<shared_ptr<RouteInfo>> rInfos;
    for (int i = 0; i < 3; i++) {
      nextHopInfos.emplace_back(
          std::make_shared<NexthopInfo>(NexthopStatus(
              {.ip = kDefaultNexthops[i]}, // nexthop
              true, // isReachable
              igpCosts[i]))); // igpCost

      auto routeInfo = std::make_shared<RouteInfo>(
          kV4Prefix1,
          TinyPeerInfo(
              peerAddrs[i],
              kDefaultAsn,
              peerRouterIds[i],
              sessionTypes[i],
              false // isRrClient
              ),
          attrs[i],
          kDefaultPathID,
          testRibEntry);

      routeInfo->setNexthopInfo(nextHopInfos[i].get());
      rInfos.emplace_back(routeInfo);
    }
    return rInfos;
  }
};

class RouteInfoSelectorTestAsPathLenWithConfed : public RouteInfoSelectorTest {
 public:
  RouteInfoSelectorTestAsPathLenWithConfed() {
    multipathSelector_ = std::make_unique<RouteInfoSelector>(
        getBaseRouteFilterConfigsMultiPath(CountConfedsInAsPathLen{true}));
    multipathSamePeerAsnSelector_ = std::make_unique<RouteInfoSelector>(
        getRouteFilterConfigs(CountConfedsInAsPathLen{true}));
    bestpathSelector_ =
        std::make_unique<RouteInfoSelector>(getRouteFilterConfigsBestPath());
  }
  ~RouteInfoSelectorTestAsPathLenWithConfed() override = default;
};

class RouteInfoSelectorTestWithMedEnabled : public RouteInfoSelectorTest {
 public:
  RouteInfoSelectorTestWithMedEnabled() {
    // Enable Med Comparison
    loadBgpBestpathFeatures(true);
    multipathSelector_ = std::make_unique<RouteInfoSelector>(
        getBaseRouteFilterConfigsMultiPath(CountConfedsInAsPathLen{false}));
  }
  ~RouteInfoSelectorTestWithMedEnabled() override = default;
};

class RouteInfoSelectorTestWithMedMissingAsWorstEnabled
    : public RouteInfoSelectorTest {
 public:
  RouteInfoSelectorTestWithMedMissingAsWorstEnabled() {
    // Enable Med Comparison and Med Missing As Worst
    loadBgpBestpathFeatures(true, true);
    multipathSelector_ = std::make_unique<RouteInfoSelector>(
        getBaseRouteFilterConfigsMultiPath(CountConfedsInAsPathLen{false}));
  }
  ~RouteInfoSelectorTestWithMedMissingAsWorstEnabled() override = default;
};

class RouteInfoSelectorTestWithWeightEnabled : public RouteInfoSelectorTest {
 public:
  RouteInfoSelectorTestWithWeightEnabled() {
    // Enable Weight Comparison
    loadBgpBestpathFeatures(false, false, true);
    multipathSelector_ = std::make_unique<RouteInfoSelector>(
        getBaseRouteFilterConfigsMultiPath(CountConfedsInAsPathLen{false}));
  }
  ~RouteInfoSelectorTestWithWeightEnabled() override = default;
};

class RouteInfoSelectorTestWithNextHopEnabled : public RouteInfoSelectorTest {
 public:
  RouteInfoSelectorTestWithNextHopEnabled() {
    // Enable Next Hop Comparison
    loadBgpBestpathFeatures(false, false, false, true);
    multipathSelector_ = std::make_unique<RouteInfoSelector>(
        getBaseRouteFilterConfigsMultiPath(CountConfedsInAsPathLen{false}));
  }
  ~RouteInfoSelectorTestWithNextHopEnabled() override = default;
};

TEST_F(RouteInfoSelectorTest, InvalidAttributesTest) {
  auto attrs = getDefaultAttrs();
  auto attr = attrs[0];
  RibEntry testRibEntry(kV4Prefix1);

  // unset localPref
  attr->setLocalPref(std::nullopt);
  attr->publish();
  EXPECT_DEATH(
      RouteInfo(
          kV4Prefix1,
          TinyPeerInfo(
              kPeerAddr2,
              kPeerAsn2,
              kPeerRouterId2,
              BgpSessionType::EBGP,
              false // isRrClient
              ),
          attr,
          kDefaultPathID,
          testRibEntry),
      "Local Preference should have a value.");
}

TEST_F(RouteInfoSelectorTest, UninstallDeletedRoutesTest) {
  {
    /*
     * routes
     * routes are preferred in the order of 3 > 1, 2 as 3 is the only valid
     * route.
     */
    auto rInfos = createTestRouteInfos(getDefaultAttrs());
    rInfos[0]->setRoutePreferred();
    rInfos[0]->setRouteDeleted();
    rInfos[1]->setRoutePreferred();
    rInfos[1]->setRouteDeleted();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[2]));
  }
  {
    /*
     * routes
     * all routes are invalid.
     */
    auto rInfos = createTestRouteInfos(getDefaultAttrs());
    rInfos[0]->setRouteDeleted();
    rInfos[1]->setRouteDeleted();
    rInfos[2]->setRouteDeleted();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(0, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre());
  }
}

TEST_F(RouteInfoSelectorTest, LocalPreferenceWinsTest) {
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2 > 3 based on BGP localperf.
     */
    auto attrs = getDefaultAttrs();
    attrs[0]->setLocalPref(300);
    attrs[1]->setLocalPref(200);
    attrs[2]->setLocalPref(100);
    auto rInfos = createTestRouteInfos(attrs);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2 > 3 based on BGP localperf,
     * but they are preferred in the order of 2 > 1, 3 based on being local.
     */
    auto attrs = getDefaultAttrs();
    attrs[0]->setLocalPref(300);
    attrs[1]->setLocalPref(200);
    attrs[2]->setLocalPref(100);
    auto rInfos = createTestRouteInfos(attrs);

    rInfos[1]->setRouteLocal();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2 > 3 based on BGP localperf,
     * but they are preferred in the order of 2 > 1, 3 based on ASPath length.
     */
    auto attrs = getDefaultAttrs();
    attrs[0]->setLocalPref(300);
    auto asPath = createAsPath({2001});
    attrs[0]->setAsPath(asPath);
    attrs[1]->setLocalPref(200);
    attrs[2]->setLocalPref(100);
    attrs[2]->setAsPath(asPath);
    auto rInfos = createTestRouteInfos(attrs);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2 > 3 based on BGP localperf,
     * but they are preferred in the order of 2 > 1 > 3 based on Origin.
     */
    auto attrs = getDefaultAttrs();
    attrs[0]->setLocalPref(300);
    attrs[0]->setOrigin(BgpAttrOrigin::BGP_ORIGIN_EGP);
    attrs[1]->setLocalPref(200);
    attrs[1]->setOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
    attrs[2]->setLocalPref(100);
    attrs[2]->setOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
    auto rInfos = createTestRouteInfos(attrs);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2 > 3 based on BGP localperf,
     * but they are preferred in the order of 3 > 1, 2 based on Med.
     */
    auto attrs = getDefaultAttrs();
    attrs[0]->setLocalPref(300);
    attrs[0]->setMed(10);
    attrs[1]->setLocalPref(200);
    attrs[1]->setMed(10);
    attrs[2]->setLocalPref(100);
    attrs[2]->setMed(1);
    auto rInfos = createTestRouteInfos(attrs);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2 > 3 based on BGP localperf,
     * but they are preferred in the order of 2 > 1, 3 based on being external.
     */
    auto attrs = getDefaultAttrs();
    attrs[0]->setLocalPref(300);
    attrs[1]->setLocalPref(200);
    attrs[2]->setLocalPref(100);
    auto rInfos = createTestRouteInfos(attrs);

    rInfos[1]->setRouteExternal();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }
}

TEST_F(RouteInfoSelectorTest, LocalRouteWinsTest) {
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2, 3 based on being local.
     */
    auto rInfos = createTestRouteInfos(getDefaultAttrs());
    rInfos[0]->setRouteLocal();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2, 3 based on being local.
     * but they are preferred in the order of 2 > 1, 3 based on ASPath length.
     */
    auto attrs = getDefaultAttrs();
    auto asPath = createAsPath({2001});
    attrs[0]->setAsPath(asPath);
    attrs[2]->setAsPath(asPath);
    auto rInfos = createTestRouteInfos(attrs);

    rInfos[0]->setRouteLocal();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2, 3 based on being local.
     * but they are preferred in the order of 2 > 1 > 3 based on Origin.
     */
    auto attrs = getDefaultAttrs();
    attrs[0]->setOrigin(BgpAttrOrigin::BGP_ORIGIN_EGP);
    attrs[1]->setOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
    attrs[2]->setOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
    auto rInfos = createTestRouteInfos(attrs);

    rInfos[0]->setRouteLocal();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2, 3 based on being local.
     * but they are preferred in the order of 3 > 1, 2 based on Med.
     */
    auto attrs = getDefaultAttrs();
    attrs[0]->setMed(10);
    attrs[1]->setMed(10);
    attrs[2]->setMed(1);
    auto rInfos = createTestRouteInfos(attrs);

    rInfos[0]->setRouteLocal();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2, 3 based on being local.
     * but they are preferred in the order of 2 > 1, 3 based on being external.
     */
    auto rInfos = createTestRouteInfos(getDefaultAttrs());
    rInfos[0]->setRouteLocal();
    rInfos[1]->setRouteExternal();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }
}

TEST_F(RouteInfoSelectorTest, ShortestASPathWinsTest) {
  {
    /*
     * routes
     * routes are preferred in the order of 2 > 1, 3 based on ASPath length.
     */
    auto attrs = getDefaultAttrs();
    auto asPath = createAsPath({2001});
    attrs[0]->setAsPath(asPath);
    attrs[2]->setAsPath(asPath);
    auto rInfos = createTestRouteInfos(attrs);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[1]));
  }
  {
    /*
     * routes
     * routes are preferred in the order of 2 > 1, 3 based on ASPath length,
     * as confed as segments are ignored.
     */
    auto attrs = getDefaultAttrs();
    auto asPath = createAsPath({2001});
    auto asPathWithConfedAsSeq = createAsPath({}, {5000, 5001});
    attrs[0]->setAsPath(asPath);
    attrs[1]->setAsPath(asPathWithConfedAsSeq);
    attrs[2]->setAsPath(asPath);
    auto rInfos = createTestRouteInfos(attrs);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[1]));
  }

  {
    /*
     * routes
     * routes are preferred in the order of 2 > 1, 3 based on ASPath length.
     * but they are preferred in the order of 2 > 1 > 3 based on Origin.
     */
    auto attrs = getDefaultAttrs();
    auto asPath = createAsPath({2001});
    attrs[0]->setAsPath(asPath);
    attrs[0]->setOrigin(BgpAttrOrigin::BGP_ORIGIN_EGP);
    attrs[1]->setOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
    attrs[2]->setAsPath(asPath);
    attrs[2]->setOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
    auto rInfos = createTestRouteInfos(attrs);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[1]));
  }
  {
    /*
     * routes
     * routes are preferred in the order of 2 > 1, 3 based on ASPath length.
     * but they are preferred in the order of 3 > 1, 2 based on Med.
     */
    auto attrs = getDefaultAttrs();
    auto asPath = createAsPath({2001});
    attrs[0]->setAsPath(asPath);
    attrs[0]->setMed(10);
    attrs[1]->setMed(10);
    attrs[2]->setAsPath(asPath);
    attrs[2]->setMed(1);
    auto rInfos = createTestRouteInfos(attrs);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[1]));
  }
  {
    /*
     * routes
     * routes are preferred in the order of 2 > 1, 3 based on ASPath length.
     * but they are preferred in the order of 1 > 2, 3 based on being external.
     */
    auto attrs = getDefaultAttrs();
    auto asPath = createAsPath({2001});
    attrs[0]->setAsPath(asPath);
    attrs[2]->setAsPath(asPath);
    auto rInfos = createTestRouteInfos(attrs);

    rInfos[0]->setRouteExternal();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[1]));
  }
}

TEST_F(RouteInfoSelectorTestAsPathLenWithConfed, ShortestASPathWinsTest) {
  {
    /*
     * routes
     * routes are preferred in the order of 2 > 1, 3 based on ASPath length with
     * confed the route with shorter confed as win
     */
    auto attrs = getDefaultAttrs();
    auto asPathWithConfedAsSeqShort = createAsPath({2001}, {5000});
    auto asPathWithConfedAsSeqLong = createAsPath({2001}, {5000, 5001});
    attrs[0]->setAsPath(asPathWithConfedAsSeqLong);
    attrs[1]->setAsPath(asPathWithConfedAsSeqShort);
    attrs[2]->setAsPath(asPathWithConfedAsSeqLong);
    auto rInfos = createTestRouteInfos(attrs);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[1]));
  }
  {
    /*
     * routes
     * routes are preferred in the order of 2 = 1 = 3 based on ASPath length,
     * a route with 1 as + 1 confed = a route with 2 confed =  a route with 2 as
     */
    auto attrs = getDefaultAttrs();
    auto asPathWithConfedAsSeq = createAsPath({2001}, {5000});
    auto asPathWithConfedAsSeqOnly = createAsPath({}, {5000, 5001});
    auto asPath = createAsPath({2001, 2002});
    attrs[0]->setAsPath(asPathWithConfedAsSeq);
    attrs[1]->setAsPath(asPathWithConfedAsSeqOnly);
    attrs[2]->setAsPath(asPath);
    auto rInfos = createTestRouteInfos(attrs);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(3, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAreArray(rInfos));
  }
}

TEST_F(RouteInfoSelectorTest, LowestOriginNumberWinsTest) {
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2 > 3 based on Origin.
     */
    auto attrs = getDefaultAttrs();
    attrs[0]->setOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
    attrs[1]->setOrigin(BgpAttrOrigin::BGP_ORIGIN_EGP);
    attrs[2]->setOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
    auto rInfos = createTestRouteInfos(attrs);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2 > 3 based on Origin.
     * but they are preferred in the order of 3 > 1, 2 based on Med.
     */
    auto attrs = getDefaultAttrs();
    attrs[0]->setOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
    attrs[0]->setMed(10);
    attrs[1]->setOrigin(BgpAttrOrigin::BGP_ORIGIN_EGP);
    attrs[1]->setMed(10);
    attrs[2]->setOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
    attrs[2]->setMed(1);
    auto rInfos = createTestRouteInfos(attrs);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2 > 3 based on Origin.
     * but they are preferred in the order of 1 > 2, 3 based on being external.
     */
    auto attrs = getDefaultAttrs();
    attrs[0]->setOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
    attrs[1]->setOrigin(BgpAttrOrigin::BGP_ORIGIN_EGP);
    attrs[2]->setOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
    auto rInfos = createTestRouteInfos(attrs);

    rInfos[0]->setRouteExternal();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }
}

/* TODO: Need to implement correctly as per RFC 4271 9.1.2.2
 */
TEST_F(RouteInfoSelectorTestWithMedEnabled, LowestMedWinsTest) {
  {
    /*
     * routes
     * routes are preferred in the order of 2 > 0, 1 based on Med.
     */
    auto attrs = getDefaultAttrs();
    attrs[0]->setMed(10);
    attrs[1]->setMed(10);
    attrs[2]->setMed(1);
    auto rInfos = createTestRouteInfos(attrs);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[2]));
  }
  {
    // routes are preferred in the order of 1, 2 > 0 based on Med.

    auto attrs = getDefaultAttrs();
    attrs[0]->setMed(10);

    auto rInfos = createTestRouteInfos(attrs);

    rInfos[0]->setRouteExternal();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[1], rInfos[2]));
  }

  {
    // routes are preferred in the order of 2 > 0, 1 based on Med.

    auto attrs = getDefaultAttrs();
    attrs[0]->setMed(20);
    attrs[1]->setMed(10);

    auto rInfos = createTestRouteInfos(attrs);

    rInfos[0]->setRouteExternal();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[2]));
  }
}

TEST_F(RouteInfoSelectorTestWithMedMissingAsWorstEnabled, LowestMedWinsTest) {
  {
    /*
     * routes
     * routes are preferred in the order of 2 > 0, 1 based on Med.
     */
    auto attrs = getDefaultAttrs();
    attrs[0]->setMed(10);
    attrs[1]->setMed(10);
    attrs[2]->setMed(1);
    auto rInfos = createTestRouteInfos(attrs);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[2]));
  }
  {
    // routes are preferred in the order of 1, 2 > 0 based on Med.

    auto attrs = getDefaultAttrs();
    attrs[0]->setMed(10);

    auto rInfos = createTestRouteInfos(attrs);

    rInfos[0]->setRouteExternal();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }

  {
    // routes are preferred in the order of 2 > 0, 1 based on Med.

    auto attrs = getDefaultAttrs();
    attrs[0]->setMed(20);
    attrs[1]->setMed(10);

    auto rInfos = createTestRouteInfos(attrs);

    rInfos[0]->setRouteExternal();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[1]));
  }
}

TEST_F(RouteInfoSelectorTestWithWeightEnabled, HighestWeightWinsTest) {
  {
    /*
     * routes
     * routes are preferred in the order of 0, 1 > 2 based on Weight.
     */
    auto attrs = getDefaultAttrs();
    attrs[0]->setWeight(10);
    attrs[1]->setWeight(10);
    attrs[2]->setWeight(1);
    auto rInfos = createTestRouteInfos(attrs);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0], rInfos[1]));
  }
  {
    // routes are preferred in the order of 1 > 0, 2  based on Weight.

    auto attrs = getDefaultAttrs();
    attrs[1]->setWeight(10);

    auto rInfos = createTestRouteInfos(attrs);

    rInfos[0]->setRouteExternal();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[1]));
  }

  {
    // routes are preferred in the order of 0 > 1 > 2 based on Weight.

    auto attrs = getDefaultAttrs();
    attrs[0]->setWeight(20);
    attrs[1]->setWeight(10);

    auto rInfos = createTestRouteInfos(attrs);

    rInfos[0]->setRouteExternal();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }

  // Boundary conditions test
  {
    // routes are preferred in the order of 2 > 1 > 0 based on Weight.

    auto attrs = getDefaultAttrs();
    attrs[0]->setWeight(0);
    attrs[1]->setWeight(10);
    attrs[2]->setWeight(kWeightMax);

    auto rInfos = createTestRouteInfos(attrs);

    rInfos[0]->setRouteExternal();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[2]));
  }
}

TEST_F(RouteInfoSelectorTestWithNextHopEnabled, LowestIgpCostWinsTest) {
  {
    /*
     * routes
     * routes are preferred in the order of 2 > 0, 1 based on IgpCost.
     */
    auto attrs = getDefaultAttrs();

    std::vector<uint32_t> igpCosts = {10, 10, 1};

    auto rInfos = createTestRouteInfos(
        attrs,
        kDefaultPeerRouterIds,
        kDefaultPeerAddrs,
        kDefaultSessionTypes,
        igpCosts); // IgpCost

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[2]));
  }
  {
    // routes are preferred in the order of 0 > 1, 2  based on IgpCost.

    auto attrs = getDefaultAttrs();
    std::vector<uint32_t> igpCosts = {
        10, kDefaultIgpCostMax, kDefaultIgpCostMax};

    auto rInfos = createTestRouteInfos(
        attrs,
        kDefaultPeerRouterIds,
        kDefaultPeerAddrs,
        kDefaultSessionTypes,
        igpCosts); // IgpCost

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }

  {
    // routes are preferred in the order of 1 > 0 > 2 based on IgpCost.

    auto attrs = getDefaultAttrs();
    std::vector<uint32_t> igpCosts = {20, 10, kDefaultIgpCostMax};

    auto rInfos = createTestRouteInfos(
        attrs,
        kDefaultPeerRouterIds,
        kDefaultPeerAddrs,
        kDefaultSessionTypes,
        igpCosts); // IgpCost

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[1]));
  }
}

class RouteInfoSelectorTestWithEiBgpEnabled : public RouteInfoSelectorTest {
 public:
  RouteInfoSelectorTestWithEiBgpEnabled() {
    // Enable eiBGP multipath
    loadBgpBestpathFeatures(false, false, false, false, true);
    multipathSelector_ = std::make_unique<RouteInfoSelector>(
        getBaseRouteFilterConfigsMultiPath(CountConfedsInAsPathLen{false}));
  }
  ~RouteInfoSelectorTestWithEiBgpEnabled() override = default;
};

TEST_F(
    RouteInfoSelectorTestWithEiBgpEnabled,
    ExternalRouteDoesNotBreakTieTest) {
  /*
   * With eiBGP enabled, external route attribute should not cause preference.
   * All routes should be selected as multipath.
   */
  auto rInfos = createTestRouteInfos(getDefaultAttrs());
  rInfos[0]->setRouteExternal();

  // when
  auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

  // then - all 3 routes selected (EXTERNAL_ROUTE filter skipped)
  EXPECT_EQ(3, chosenRoutes.size());
  EXPECT_THAT(
      chosenRoutes, UnorderedElementsAre(rInfos[0], rInfos[1], rInfos[2]));
}

TEST_F(RouteInfoSelectorTestWithEiBgpEnabled, EbgpAndIbgpEqualizedTest) {
  {
    /*
     * With eiBGP enabled, EBGP and IBGP sessions should be treated equally.
     * All routes should be selected as multipath.
     */
    auto attrs = getDefaultAttrs();
    vector<BgpSessionType> sessionTypes = {
        BgpSessionType::EBGP, BgpSessionType::EBGP, BgpSessionType::IBGP};
    auto rInfos = createTestRouteInfos(
        attrs, kDefaultPeerRouterIds, kDefaultPeerAddrs, sessionTypes);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then - all 3 routes selected (EXTERNAL_ROUTE filter skipped)
    EXPECT_EQ(3, chosenRoutes.size());
    EXPECT_THAT(
        chosenRoutes, UnorderedElementsAre(rInfos[0], rInfos[1], rInfos[2]));
  }
  {
    // Single EBGP with two IBGP should also be equalized.
    auto attrs = getDefaultAttrs();
    vector<BgpSessionType> sessionTypes = {
        BgpSessionType::EBGP, BgpSessionType::IBGP, BgpSessionType::IBGP};
    auto rInfos = createTestRouteInfos(
        attrs, kDefaultPeerRouterIds, kDefaultPeerAddrs, sessionTypes);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then - all 3 routes selected
    EXPECT_EQ(3, chosenRoutes.size());
    EXPECT_THAT(
        chosenRoutes, UnorderedElementsAre(rInfos[0], rInfos[1], rInfos[2]));
  }
}

TEST_F(
    RouteInfoSelectorTestWithEiBgpEnabled,
    ConfedEbgpStillPreferredOverIbgpTest) {
  /*
   * With eiBGP enabled, CONFED_EXTERNAL_ROUTE filter is still active.
   * ConfedEBGP should still be preferred over IBGP.
   */
  auto attrs = getDefaultAttrs();
  vector<BgpSessionType> sessionTypes = {
      BgpSessionType::ConfedEBGP, BgpSessionType::IBGP, BgpSessionType::IBGP};
  auto rInfos = createTestRouteInfos(
      attrs, kDefaultPeerRouterIds, kDefaultPeerAddrs, sessionTypes);

  // when
  auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

  // then - only ConfedEBGP route selected (CONFED_EXTERNAL_ROUTE still active)
  EXPECT_EQ(1, chosenRoutes.size());
  EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
}

TEST_F(RouteInfoSelectorTestWithEiBgpEnabled, OtherFiltersStillApplyTest) {
  {
    // Local preference should still break ties with eiBGP enabled.
    auto attrs = getDefaultAttrs();
    attrs[0]->setLocalPref(300);
    attrs[1]->setLocalPref(200);
    attrs[2]->setLocalPref(100);
    auto rInfos = createTestRouteInfos(attrs);

    rInfos[1]->setRouteExternal();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then - local preference wins over external route
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }
  {
    // AS path length should still break ties with eiBGP enabled.
    auto attrs = getDefaultAttrs();
    auto asPath = createAsPath({2001});
    attrs[0]->setAsPath(asPath);
    attrs[2]->setAsPath(asPath);
    auto rInfos = createTestRouteInfos(attrs);

    rInfos[0]->setRouteExternal();

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then - shortest AS path wins
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[1]));
  }
}

TEST_F(RouteInfoSelectorTest, ExternalRouteBreaksTieTest) {
  /*
   * routes
   * routes are preferred in the order of 1 > 2, 3 based on being external.
   */
  auto rInfos = createTestRouteInfos(getDefaultAttrs());
  rInfos[0]->setRouteExternal();

  // when
  auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

  // then
  EXPECT_EQ(1, chosenRoutes.size());
  EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
}

TEST_F(RouteInfoSelectorTest, MultipathRecoveryTest) {
  /*
   * routes
   * routes are preferred in the order of 1, 2 > 3 based on multipath criteria.
   */
  auto attrs = getDefaultAttrs();
  auto asPath1 = createAsPath({2001, 3000});
  auto asPath2 = createAsPath({2002, 3000});
  attrs[0]->setAsPath(asPath1);
  attrs[1]->setAsPath(asPath1);
  attrs[2]->setAsPath(asPath2);
  // overwrite default router id
  vector<uint32_t> peerRouterIds = {1, 2, 3};
  auto rInfos = createTestRouteInfos(attrs, peerRouterIds);

  // when
  auto chosenRoutes = multipathSamePeerAsnSelector_->selectRoutes(rInfos);

  /*
   * then
   * we should choose the path from the lowest peer ID (peer1), and then recover
   * equivalent paths from peer2
   */
  EXPECT_EQ(2, chosenRoutes.size());
  EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0], rInfos[1]));
}

TEST_F(RouteInfoSelectorTest, BestpathSelectionTest) {
  /*
   * routes
   * all routes are preferred based on multipath criteria.
   */
  auto attrs = getDefaultAttrs();
  auto asPath1 = createAsPath({2001, 3000});
  auto asPath2 = createAsPath({2002, 3000});
  attrs[0]->setAsPath(asPath1);
  attrs[1]->setAsPath(asPath1);
  attrs[2]->setAsPath(asPath2);
  // overwrite default router id
  vector<uint32_t> peerRouterIds = {1, 2, 3};
  auto rInfos = createTestRouteInfos(attrs, peerRouterIds);

  // when running multipath selector
  auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

  /*
   * then
   * we should choose the path from the lowest peer ID (peer1), and then recover
   * equivalent paths from peer2
   */
  EXPECT_EQ(3, chosenRoutes.size());
  EXPECT_THAT(
      chosenRoutes, UnorderedElementsAre(rInfos[0], rInfos[1], rInfos[2]));

  // tie break using bestpath selector
  auto bestpath = bestpathSelector_->selectRoutes(chosenRoutes);

  /*
   * then
   * we should choose the path from the lowest peer ID (peer1) as the best
   */
  EXPECT_EQ(1, bestpath.size());
  EXPECT_THAT(bestpath[0], rInfos[0]);
}

TEST_F(RouteInfoSelectorTest, OriginatorIdOverwritesRouterIdTest) {
  /*
   * routes
   * routes are preferred in the order of 1 > 3 > 2 based on router id filter
   * Note here RouteInfo will use originator id instead of router id based
   * on [RFC 4456] Route Reflection
   * all routes are preferred based on multipath criteria.
   */
  auto attrs = getDefaultAttrs();
  attrs[0]->setOriginatorId(1);
  // overwrite default router id
  vector<uint32_t> peerRouterIds = {4, 3, 2};
  auto rInfos = createTestRouteInfos(attrs, peerRouterIds);

  // when
  auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

  /*
   * then
   * we should choose the path from the lowest peer ID (peer1), and then recover
   * equivalent paths from peer2
   */
  EXPECT_EQ(3, chosenRoutes.size());
  EXPECT_THAT(
      chosenRoutes, UnorderedElementsAre(rInfos[0], rInfos[1], rInfos[2]));

  // tie break using bestpath selector
  auto bestpath = bestpathSelector_->selectRoutes(chosenRoutes);

  /*
   * then
   * we should choose the path from the lowest peer ID (peer1) as the best
   */
  EXPECT_EQ(1, bestpath.size());
  EXPECT_THAT(bestpath[0], rInfos[0]);
}

using facebook::nettools::bgplib::BgpAttrClusterListC;
TEST_F(RouteInfoSelectorTest, ClusterListLenTest) {
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2 > 3 due to cluster list len.
     * all routes are preferred based on multipath criteria.
     */

    // set different cluster list
    vector<BgpAttrClusterListC> clusterLists = {
        BgpAttrClusterListC{{}},
        BgpAttrClusterListC{{1001}},
        BgpAttrClusterListC{{1001, 1002}}};
    auto attrs = getDefaultAttrs(kDefaultNexthops, clusterLists);
    auto asPath1 = createAsPath({2001, 3000});
    auto asPath2 = createAsPath({2002, 3000});
    attrs[0]->setAsPath(asPath1);
    attrs[1]->setAsPath(asPath1);
    attrs[2]->setAsPath(asPath2);
    auto rInfos =
        createTestRouteInfos(attrs, kDefaultPeerRouterIds, kDefaultPeerAddrs);
    // verify the setup
    EXPECT_EQ(clusterLists[0], rInfos[0]->attrs->getClusterList().get());
    EXPECT_EQ(clusterLists[1], rInfos[1]->attrs->getClusterList().get());
    EXPECT_EQ(clusterLists[2], rInfos[2]->attrs->getClusterList().get());

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_THAT(chosenRoutes, SizeIs(3));
    EXPECT_THAT(
        chosenRoutes, UnorderedElementsAre(rInfos[0], rInfos[1], rInfos[2]));
  }
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2 > 3 due to cluster list len.
     * 1, 2 are preferred based on multipath with same peer as criteria.
     * set different cluster list
     */
    vector<facebook::nettools::bgplib::BgpAttrClusterListC> clusterLists = {
        facebook::nettools::bgplib::BgpAttrClusterListC{},
        facebook::nettools::bgplib::BgpAttrClusterListC{{1001}},
        facebook::nettools::bgplib::BgpAttrClusterListC{{1001, 1002}}};
    auto attrs = getDefaultAttrs(kDefaultNexthops, clusterLists);
    auto asPath1 = createAsPath({2001, 3000});
    auto asPath2 = createAsPath({2002, 3000});
    attrs[0]->setAsPath(asPath1);
    attrs[1]->setAsPath(asPath1);
    attrs[2]->setAsPath(asPath2);
    auto rInfos =
        createTestRouteInfos(attrs, kDefaultPeerRouterIds, kDefaultPeerAddrs);
    // verify the setup
    EXPECT_EQ(clusterLists[0], rInfos[0]->attrs->getClusterList().get());
    EXPECT_EQ(clusterLists[1], rInfos[1]->attrs->getClusterList().get());
    EXPECT_EQ(clusterLists[2], rInfos[2]->attrs->getClusterList().get());

    // when
    auto chosenRoutes = multipathSamePeerAsnSelector_->selectRoutes(rInfos);

    /*
     * then
     * we should choose the path with the shorted cluster list, and then recover
     * equivalent paths from peer2
     */
    EXPECT_THAT(chosenRoutes, SizeIs(2));
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0], rInfos[1]));
  }
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2 > 3 due to cluster list len
     * with no chance of multipath with same peer as criteria.
     */
    vector<facebook::nettools::bgplib::BgpAttrClusterListC> clusterLists = {
        facebook::nettools::bgplib::BgpAttrClusterListC{},
        facebook::nettools::bgplib::BgpAttrClusterListC{{1001}},
        facebook::nettools::bgplib::BgpAttrClusterListC{{1001, 1002}}};
    auto attrs = getDefaultAttrs(kDefaultNexthops, clusterLists);
    auto asPath1 = createAsPath({2001, 3000});
    auto asPath2 = createAsPath({2002, 3000});
    auto asPath3 = createAsPath({2003, 3000});
    attrs[0]->setAsPath(asPath1);
    attrs[1]->setAsPath(asPath2);
    attrs[2]->setAsPath(asPath3);
    // set different cluster list
    auto rInfos =
        createTestRouteInfos(attrs, kDefaultPeerRouterIds, kDefaultPeerAddrs);
    // verify the setup
    EXPECT_EQ(clusterLists[0], rInfos[0]->attrs->getClusterList().get());
    EXPECT_EQ(clusterLists[1], rInfos[1]->attrs->getClusterList().get());
    EXPECT_EQ(clusterLists[2], rInfos[2]->attrs->getClusterList().get());

    // when
    auto chosenRoutes = multipathSamePeerAsnSelector_->selectRoutes(rInfos);

    /*
     * then
     * we should choose the path with the shorted cluster list.
     */
    EXPECT_THAT(chosenRoutes, SizeIs(1));
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }
  {
    /*
     * routes
     * routes are preferred in the order of 1, 2 > 3 due to cluster list len
     * with no chance of multipath with same peer as criteria.
     */
    vector<facebook::nettools::bgplib::BgpAttrClusterListC> clusterLists = {
        facebook::nettools::bgplib::BgpAttrClusterListC{{1001}},
        facebook::nettools::bgplib::BgpAttrClusterListC{{1001}},
        facebook::nettools::bgplib::BgpAttrClusterListC{{1001, 1002}}};
    auto attrs = getDefaultAttrs(kDefaultNexthops, clusterLists);
    auto asPath1 = createAsPath({2001, 3000});
    auto asPath2 = createAsPath({2002, 3000});
    auto asPath3 = createAsPath({2003, 3000});
    attrs[0]->setAsPath(asPath1);
    attrs[1]->setAsPath(asPath2);
    attrs[2]->setAsPath(asPath3);
    // set different cluster list
    auto rInfos =
        createTestRouteInfos(attrs, kDefaultPeerRouterIds, kDefaultPeerAddrs);
    // verify the setup
    EXPECT_EQ(clusterLists[0], rInfos[0]->attrs->getClusterList().get());
    EXPECT_EQ(clusterLists[1], rInfos[1]->attrs->getClusterList().get());
    EXPECT_EQ(clusterLists[2], rInfos[2]->attrs->getClusterList().get());

    // when
    auto chosenRoutes = multipathSamePeerAsnSelector_->selectRoutes(rInfos);

    /*
     * then
     * we should choose the path with the shorted cluster list.
     */
    EXPECT_THAT(chosenRoutes, SizeIs(1));
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));
  }
}

TEST_F(RouteInfoSelectorTest, PreferEBgpOverConfedEBgpOverIBgpTest) {
  {
    /*
     * routes
     * all routes are preferred based on multipath criteria.
     */
    auto attrs = getDefaultAttrs();
    vector<BgpSessionType> sessionTypes = {
        BgpSessionType::IBGP, BgpSessionType::IBGP, BgpSessionType::IBGP};
    auto rInfos = createTestRouteInfos(
        attrs, kDefaultPeerRouterIds, kDefaultPeerAddrs, sessionTypes);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(3, chosenRoutes.size());
    EXPECT_THAT(
        chosenRoutes, UnorderedElementsAre(rInfos[0], rInfos[1], rInfos[2]));

    // tie break using bestpath selector
    auto bestpath = bestpathSelector_->selectRoutes(chosenRoutes);

    /*
     * then
     * we should choose the path from the lowest peer ID (peer1) as the best
     */
    EXPECT_EQ(1, bestpath.size());
    EXPECT_THAT(bestpath[0], rInfos[0]);
  }
  {
    /*
     * routes
     * all routes are preferred based on multipath criteria.
     */
    auto attrs = getDefaultAttrs();
    vector<BgpSessionType> sessionTypes = {
        BgpSessionType::EBGP, BgpSessionType::EBGP, BgpSessionType::EBGP};
    auto rInfos = createTestRouteInfos(
        attrs, kDefaultPeerRouterIds, kDefaultPeerAddrs, sessionTypes);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(3, chosenRoutes.size());
    EXPECT_THAT(
        chosenRoutes, UnorderedElementsAre(rInfos[0], rInfos[1], rInfos[2]));

    // tie break using bestpath selector
    auto bestpath = bestpathSelector_->selectRoutes(chosenRoutes);

    /*
     * then
     * we should choose the path from the lowest peer ID (peer1) as the best
     */
    EXPECT_EQ(1, bestpath.size());
    EXPECT_THAT(bestpath[0], rInfos[0]);
  }
  {
    /*
     * routes
     * routes are preferred in the order of 1, 2 > 3 based on multipath
     * criteria.
     */
    auto attrs = getDefaultAttrs();
    vector<BgpSessionType> sessionTypes = {
        BgpSessionType::EBGP, BgpSessionType::EBGP, BgpSessionType::IBGP};
    auto rInfos = createTestRouteInfos(
        attrs, kDefaultPeerRouterIds, kDefaultPeerAddrs, sessionTypes);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(2, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0], rInfos[1]));

    // tie break using bestpath selector
    auto bestpath = bestpathSelector_->selectRoutes(chosenRoutes);

    /*
     * then
     * we should choose the path from the lowest peer ID (peer1) as the best
     */
    EXPECT_EQ(1, bestpath.size());
    EXPECT_THAT(bestpath[0], rInfos[0]);
  }
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2, 3 based on multipath
     * criteria.
     */
    auto attrs = getDefaultAttrs();
    vector<BgpSessionType> sessionTypes = {
        BgpSessionType::EBGP, BgpSessionType::ConfedEBGP, BgpSessionType::IBGP};
    auto rInfos = createTestRouteInfos(
        attrs, kDefaultPeerRouterIds, kDefaultPeerAddrs, sessionTypes);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));

    // tie break using bestpath selector
    auto bestpath = bestpathSelector_->selectRoutes(chosenRoutes);

    /*
     * then
     * we should choose the path from the lowest peer ID (peer1) as the best
     */
    EXPECT_EQ(1, bestpath.size());
    EXPECT_THAT(bestpath[0], rInfos[0]);
  }
  {
    /*
     * routes
     * routes are preferred in the order of 1 > 2, 3 based on multipath
     * criteria.
     */
    auto attrs = getDefaultAttrs();
    vector<BgpSessionType> sessionTypes = {
        BgpSessionType::ConfedEBGP, BgpSessionType::IBGP, BgpSessionType::IBGP};
    auto rInfos = createTestRouteInfos(
        attrs, kDefaultPeerRouterIds, kDefaultPeerAddrs, sessionTypes);

    // when
    auto chosenRoutes = multipathSelector_->selectRoutes(rInfos);

    // then
    EXPECT_EQ(1, chosenRoutes.size());
    EXPECT_THAT(chosenRoutes, UnorderedElementsAre(rInfos[0]));

    // tie break using bestpath selector
    auto bestpath = bestpathSelector_->selectRoutes(chosenRoutes);

    /*
     * then
     * we should choose the path from the lowest peer ID (peer1) as the best
     */
    EXPECT_EQ(1, bestpath.size());
    EXPECT_THAT(bestpath[0], rInfos[0]);
  }
}
} // namespace facebook::bgp
