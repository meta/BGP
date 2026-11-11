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
 * Classes for filtering routes. The concrete classes correspond to types of the
 * RouteFilterOpts union in routing.thrift. Route selection is implemented by
 * applying a list of these filters, where the lists are configurable in
 * routing.cconf.
 */

#pragma once

#include <folly/Optional.h>
#include <functional>
#include <map>
#include <memory>
#include <unordered_set>
#include <variant>
#include <vector>

#include "neteng/fboss/bgp/cpp/routelib/RouteBase.h"

namespace facebook {
namespace nettools {
namespace edge {

using AcceptedAndRejectedRoutes = std::pair<
    std::vector<std::shared_ptr<RouteBase>>,
    std::vector<std::shared_ptr<RouteBase>>>;

/*
 * Rule is a function that looks at a set of routes and returns weight for
 * the one at <current> index. We require all routes for every invocation
 * since certain filters, e.g., LowestMedValue, consider other routes'
 * metrics.
 */
using Rule = std::function<__uint128_t(const std::shared_ptr<const RouteBase>)>;

// Please keep this and RouteMetricDescription in sync.
enum class RouteMetric {
  BGP_LOCAL_PREFERENCE = 1,
  BGP_AS_PATH_LEN = 2,
  BGP_ORIGIN_CODE = 3,
  BGP_MED_VALUE = 4,
  DELETED_ROUTE = 5,
  CONTROLLER_COMMUNITY = 6,
  BGP_ROUTER_ID = 7,
  BGP_PEER_ASN = 8,
  CONTROLLER_COMMUNITY_METRO = 9,
  PREFIX_LENGTH = 10,
  PERF_DETOUR_ROUTE_OVERRIDE = 11,
  ALREADY_PREFERRED_ROUTE = 12,
  EXTERNAL_ROUTE = 13,
  LOCAL_ROUTE = 14,
  BGP_PEER_IP = 15,
  BGP_CLUSTER_LIST_LEN = 16,
  CONFED_EXTERNAL_ROUTE = 17,
  /*
   * Count confeds in as path length, then tie break on AS_PATH.
   * https://datatracker.ietf.org/doc/draft-lapukhov-bgp-ecmp-considerations/
   *
   * BGP_AS_PATH_LEN_WITH_CONFED and BGP_AS_PATH_LEN are mutually exclusive,
   * it's an alternative of BGP_AS_PATH_LEN.
   */
  BGP_AS_PATH_LEN_WITH_CONFED = 18,
  /*
   * Tie-breaking over BGP nexthops. This is not a standardized tie-breaking
   * rule from RFC-4721. However, BGP can receive the same prefix from multiple
   * sessions from the same peer. NEXT_HOP will be the last rule to tie-break.
   */
  BGP_PEER_NEXTHOP = 19,
  BGP_WEIGHT_VALUE = 20,
  IGP_COST = 21,
};

enum class HighestLowestRouteFilterAction {
  CHOOSE_HIGHEST = 1,
  CHOOSE_LOWEST = 2,
};

enum class MembershipRouteFilterAction {
  CHOOSE_MEMBER = 1,
  CHOOSE_NONMEMBER = 2,
};

/*
 * Options for route filters that select routes with value of metric between
 * minValue and maxValue, inclusive.
 */
struct RangedRouteFilterOpts {
  int64_t minValue;
  int64_t maxValue;
};

/*
 * Options for route filters that select routes with highest or lowest values
 * of metric, depending on value of action.
 */
struct HighestLowestRouteFilterOpts {
  HighestLowestRouteFilterAction action;
};

/*
 * Options for route filters that select routes with values of metric that are
 * either members or not members of metricValues, depending on value of action.
 */
struct MembershipRouteFilterOpts {
  MembershipRouteFilterAction action;
  std::unordered_set<int64_t> metricValues;
};

// Forward declaration
struct RouteFilterConfig;

/*
 * Options for RecoverEquivalentRouteFilter. This struct is needed so that the
 * filter can be iterated by RouterFilterOpts
 */
struct RecoverEquivalentRouteFilterOpts {
  // Additional tiebreakers to filter routes before recovering
  std::vector<RouteFilterConfig> tiebreakerConfigs;
};

using RouteFilterOpts = std::variant<
    RangedRouteFilterOpts,
    HighestLowestRouteFilterOpts,
    MembershipRouteFilterOpts,
    RecoverEquivalentRouteFilterOpts>;

struct RouteFilterConfig {
  RouteMetric metric;
  RouteFilterOpts opts;
  std::string str() const;
};

/*
 * RouteFilter provides a way to partition the given routes based on
 * routeFilterConfig. The routes could be multiple paths for a prefix
 * or multiple paths for multiple prefixes.
 */
class RouteFilter {
 public:
  virtual ~RouteFilter() {}

  // Partitions routes into accepted and rejected vectors.
  virtual AcceptedAndRejectedRoutes filter(
      const AcceptedAndRejectedRoutes& previousRoutes,
      folly::Optional<std::map<
          std::shared_ptr<RouteBase>,
          nettools::edge::RouteFilterConfig>*> filterMap = folly::none) = 0;

  /*
   * Indicates if a RouterFilter can be used to compare routes
   * Comparators do not make sense for some filters, such as membership filters
   */
  inline virtual bool comparable() {
    return false;
  }

  /*
   * Compares two routes using filters, returning true if ri1 is preferred
   * If both routes are equal, returns false
   * If a comparator is not defined, returns false and logs fatal error
   */
  virtual bool compare(
      const std::shared_ptr<const RouteBase> ri1,
      const std::shared_ptr<const RouteBase> ri2);

  /*
   * Factory method for constructing RouteFilter that filters on the specified
   * metric.
   */
  static std::unique_ptr<RouteFilter> fromRouteFilterConfig(
      const RouteFilterConfig& routeFilterConfig);
  static std::vector<std::unique_ptr<RouteFilter>> fromRouteFilterConfigVec(
      const std::vector<RouteFilterConfig>& routeFilterConfigVec);

  /*
   * Helper function for fromRouteFilterConfig(). Given the routeFilterConfig
   * and the rule, this creates a route filter object.
   */
  static std::unique_ptr<RouteFilter> createRouteFilter(
      const RouteFilterConfig& routeFilterConfig,
      const Rule& rule);

  // Returns the route metric used by this filter.
  RouteMetric getRouteMetric();

  // Returns the filter config.
  RouteFilterConfig getRouteFilterConfig() const;

 protected:
  RouteFilter(const RouteFilterConfig& routeFilterConfig, const Rule& rule)
      : routeFilterConfig_(routeFilterConfig), rule_(rule) {}

  // Helper function to capture rejection filter info
  void captureRejectedFilter(
      folly::Optional<std::map<
          std::shared_ptr<RouteBase>,
          nettools::edge::RouteFilterConfig>*> filterMap,
      std::shared_ptr<RouteBase> rInfo);
  // Helper function to invoke rule_ on each route in routes.
  std::vector<__uint128_t> getRouteWeights(
      const std::vector<std::shared_ptr<RouteBase>> routes);

  // Longest prefix (applied before tiebreakers for a given prefix)
  static uint8_t getBgpPrefixLength(const std::shared_ptr<const RouteBase> ri);

  // Metrics for route selection according to RFC 4271 section 9.1.

  /*
   * TODO (section 9.1.1):
   *   If the route is learned from an external peer, then the local BGP
   *   speaker computes the degree of preference based on preconfigured
   *   policy information.
   */

  /*
   * TODO (section 9.1.2)
   *   No check for unresolvable routes.
   *   No check for AS loops because our ASN is present in every AS path.
   */

  // Rule 9.1.2.2.d in not applicable as every route is learned via IBGP.

  // TODO: Rule 9.1.2.2.e - lowest interior cost.

  // Highest local preference (9.1.1)
  static int64_t getBgpLocalPreference(
      const std::shared_ptr<const RouteBase> ri);
  // Shortest AS path (9.1.2.2.a)
  static int64_t getBgpAsPathLen(const std::shared_ptr<const RouteBase> ri);
  /*
   * Shortest AS path, counting confed asn in the length
   * https://datatracker.ietf.org/doc/draft-lapukhov-bgp-ecmp-considerations/
   */
  static int64_t getBgpAsPathLenWithConfed(
      const std::shared_ptr<const RouteBase> ri);
  // Lowest origin number (9.1.2.2.b)
  static int64_t getBgpOriginCode(const std::shared_ptr<const RouteBase> ri);
  /*
   * Keep only routes with lowest MED (9.1.2.2.c)
   * MED value is reset in inbound BGP policy at PR/BRs and "always-compare-med"
   * is configured on them. Thus, we will compare route MEDs even if the routes
   * are being propaged by different peer AS.
   */
  static int64_t getBgpMedValue(const std::shared_ptr<const RouteBase> ri);
  // Highest Weight
  static uint16_t getBgpWeightValue(const std::shared_ptr<const RouteBase> ri);
  // Lowest BGP ID (9.1.2.2.f)
  static uint64_t getBgpRouterId(const std::shared_ptr<const RouteBase> ri);
  // Shortest Cluster list (RFC 4456 section 9)
  static int64_t getBgpClusterListLen(
      const std::shared_ptr<const RouteBase> ri);
  // Lowest BGP Peer Ip (9.1.2.2.g)
  static __uint128_t getBgpPeerIPAsInt(
      const std::shared_ptr<const RouteBase> ri);
  // Returns 1 if route was learned over EBGP, 0 otherwise.
  static int64_t getIsRouteExternal(const std::shared_ptr<const RouteBase> ri);
  // Returns IGP cost for the next hop.
  static uint32_t getIgpCostValue(const std::shared_ptr<const RouteBase> ri);

  /*
   * [Non-RFC Metrics]
   */

  /*
   * Lowest nexthop address. This tie break can happen when the same prefix with
   * different nexthops are received from the same peer. BGP's best path
   * selection requires a single path to be deterministically chosen. This rule
   * will facilitate the tie-breaking.
   */
  static __uint128_t getBgpNexthopAsInt(
      const std::shared_ptr<const RouteBase> ri);

  /*
   * Useful in recovering routes from the same peer AS
   * Returns the first ASN in AS Path.
   */
  static int64_t getBgpPeerAsn(const std::shared_ptr<const RouteBase> ri);

  // Returns 1 if route was learned over Confed EBGP, 0 otherwise.
  static int64_t getIsRouteConfedExternal(
      const std::shared_ptr<const RouteBase> ri);

  // Returns 1 if route was deleted, 0 otherwise.
  static int64_t getIsRouteDeleted(const std::shared_ptr<const RouteBase> ri);

  // Returns 1 if route is preferred, 0 otherwise.
  static int64_t getIsRoutePreferred(const std::shared_ptr<const RouteBase> ri);

  /*
   * Returns preference at router or metro level based on controller communities
   * (higher preference == better route, can be compared across routes)
   *
   * The impact of controller communities depends on whether we're calculating
   * the preferred routes for an individual router or the entire metro. While
   * we always prioritize metro-mode Edge Fabric injections, we only prioritize
   * router-mode Edge Fabric injections at the router where they were injected
   * (in router-mode, Edge Fabric injections have no impact on neighbors).
   *
   * When EF is running in router-mode, the priority of the injector's BGP
   * session (aka, the administrative distance) is increased to ensure that the
   * route takes priority at the router where it was injected. However, this
   * increase in priority has no impact on other routers. In comparison, when EF
   * is running in metro-mode BGP communities and/or other BGP attributes are
   * used to increase the preference of the injected route at all routers in the
   * metro.
   *
   * Thus, when we're calculating preferred route(s) for an individual router,
   * we increase the preference of a route if it was injected by Edge Fabric
   * in metro-mode or router-mode. However, if we're calculating the preferred
   * route(s) across multiple routers, we only increase the preference of a
   * route if it was injected by EF in metro-mode.
   */
  static int64_t getRouterLevelPreferenceFromControllerCommunities(
      const std::shared_ptr<const RouteBase> ri);
  static int64_t getMetroLevelPreferenceFromControllerCommunities(
      const std::shared_ptr<const RouteBase> ri);

  const RouteFilterConfig routeFilterConfig_;
  const Rule rule_;
};

class HighestLowestRouteFilter : public RouteFilter {
 public:
  HighestLowestRouteFilter(
      const RouteFilterConfig& routeFilterConfig,
      Rule rule,
      HighestLowestRouteFilterAction action)
      : RouteFilter(routeFilterConfig, rule), action_(action) {}

  AcceptedAndRejectedRoutes filter(
      const AcceptedAndRejectedRoutes& previousRoutes,
      folly::Optional<std::map<
          std::shared_ptr<RouteBase>,
          nettools::edge::RouteFilterConfig>*> filterMap =
          folly::none) override;

  // this filter can also be used for comparisons
  inline bool comparable() override {
    return true;
  }

  bool compare(
      const std::shared_ptr<const RouteBase> ri1,
      const std::shared_ptr<const RouteBase> ri2) override;

 private:
  const HighestLowestRouteFilterAction action_;
};

class RangedRouteFilter : public RouteFilter {
 public:
  RangedRouteFilter(
      const RouteFilterConfig& routeFilterConfig,
      Rule rule,
      int64_t minValue,
      int64_t maxValue)
      : RouteFilter(routeFilterConfig, rule),
        minValue_(minValue),
        maxValue_(maxValue) {}

  AcceptedAndRejectedRoutes filter(
      const AcceptedAndRejectedRoutes& previousRoutes,
      folly::Optional<std::map<
          std::shared_ptr<RouteBase>,
          nettools::edge::RouteFilterConfig>*> filterMap =
          folly::none) override;

 private:
  const int64_t minValue_;
  const int64_t maxValue_;
};

class MembershipRouteFilter : public RouteFilter {
 public:
  MembershipRouteFilter(
      const RouteFilterConfig& routeFilterConfig,
      Rule rule,
      MembershipRouteFilterAction action,
      const std::unordered_set<int64_t>& metricValues)
      : RouteFilter(routeFilterConfig, rule),
        action_(action),
        metricValues_(metricValues) {}

  AcceptedAndRejectedRoutes filter(
      const AcceptedAndRejectedRoutes& previousRoutes,
      folly::Optional<std::map<
          std::shared_ptr<RouteBase>,
          nettools::edge::RouteFilterConfig>*> filterMap =
          folly::none) override;

 private:
  const MembershipRouteFilterAction action_;
  const std::unordered_set<int64_t> metricValues_;
};

/*
 * Apply tiebreaker filters on the passed in accepted route set,
 * any rejected route is then recovered based on the recovery config.
 * This is useful when we try to select a subset of multipath routes
 * as the final multipath set based on some parameter of the bestPath
 * route.
 */
class RecoverEquivalentRouteFilter : public RouteFilter {
 public:
  RecoverEquivalentRouteFilter(
      const RouteFilterConfig& routeFilterConfig,
      Rule rule,
      const std::vector<RouteFilterConfig>& tiebreakerConfigs)
      : RouteFilter(routeFilterConfig, rule),
        tiebreakers_(fromRouteFilterConfigVec(tiebreakerConfigs)) {}

  AcceptedAndRejectedRoutes filter(
      const AcceptedAndRejectedRoutes& previousRoutes,
      folly::Optional<std::map<
          std::shared_ptr<RouteBase>,
          nettools::edge::RouteFilterConfig>*> filterMap =
          folly::none) override;

 private:
  const std::vector<std::unique_ptr<RouteFilter>> tiebreakers_;
};

} // namespace edge
} // namespace nettools
} // namespace facebook
