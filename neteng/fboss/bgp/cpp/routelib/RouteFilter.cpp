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
 * Implementation of route filters.
 */
#include "RouteFilter.h"

#include <fmt/format.h>
#include <folly/Optional.h>
#include <folly/Overload.h>
#include <folly/logging/xlog.h>

namespace facebook {
namespace nettools {
namespace edge {

using std::shared_ptr;
using std::unique_ptr;
using std::vector;

std::string RouteFilterConfig::str() const {
  static std::map<RouteMetric, const std::string> routeMetricDescription = {
      {RouteMetric::BGP_LOCAL_PREFERENCE, "Local Preference"},
      {RouteMetric::BGP_AS_PATH_LEN, "AS-Path Len"},
      {RouteMetric::BGP_ORIGIN_CODE, "Origin Code"},
      {RouteMetric::BGP_MED_VALUE, "MED"},
      {RouteMetric::DELETED_ROUTE, "Deleted Route"},
      {RouteMetric::CONTROLLER_COMMUNITY, "Controller-Community"},
      {RouteMetric::BGP_ROUTER_ID, "Router-Id"},
      {RouteMetric::BGP_PEER_ASN, "Peer ASN"},
      {RouteMetric::CONTROLLER_COMMUNITY_METRO, "Metro-Community"},
      {RouteMetric::PREFIX_LENGTH, "Prefix Length"},
      {RouteMetric::PERF_DETOUR_ROUTE_OVERRIDE, "Detour Route Override"},
      {RouteMetric::ALREADY_PREFERRED_ROUTE,
       "Already Preferred (In previous pass)"},
      {RouteMetric::EXTERNAL_ROUTE, "External Route"},
      {RouteMetric::LOCAL_ROUTE, "Local Route"},
      {RouteMetric::BGP_PEER_IP, "Peer IP"},
      {RouteMetric::BGP_CLUSTER_LIST_LEN, "Cluster List Length"},
      {RouteMetric::CONFED_EXTERNAL_ROUTE, "Confed External Route"},
      {RouteMetric::BGP_AS_PATH_LEN_WITH_CONFED, "AS-Path Len with Confed"},
      {RouteMetric::BGP_PEER_NEXTHOP, "Peer Nexthop"},
      {RouteMetric::BGP_WEIGHT_VALUE, "Weight"},
      {RouteMetric::IGP_COST, "IGP Cost"}};

  auto describeFilter = folly::overload(
      [](const RangedRouteFilterOpts& rangeFilter) -> std::string {
        return (fmt::format(
            "Select Within Range [{}, {}]",
            rangeFilter.minValue,
            rangeFilter.maxValue));
      },
      [](const HighestLowestRouteFilterOpts& hlFilter) {
        if (hlFilter.action == HighestLowestRouteFilterAction::CHOOSE_HIGHEST) {
          return std::string("Choose Highest Value");
        } else if (
            hlFilter.action == HighestLowestRouteFilterAction::CHOOSE_LOWEST) {
          return std::string("Choose Lowest Value");
        } else {
          return std::string("Invalid Action (HighLow filter)!");
        }
      },
      [](const MembershipRouteFilterOpts& memFilter) {
        std::string str;
        if (memFilter.action == MembershipRouteFilterAction::CHOOSE_MEMBER) {
          str = "Choose Member from set {";
        } else if (
            memFilter.action == MembershipRouteFilterAction::CHOOSE_NONMEMBER) {
          str = " Choose Non-Member from set {";
        } else {
          return std::string(" Invalid Action (Membership filter)!");
        }
        std::for_each(
            memFilter.metricValues.cbegin(),
            memFilter.metricValues.cend(),
            [&str, &memFilter](auto val) {
              if (val == *(memFilter.metricValues.cbegin())) {
                str.append(fmt::format("{}", val));
              } else {
                str.append(fmt::format(",{}", val));
              }
            });

        str.append("}");
        return str;
      },
      [](const RecoverEquivalentRouteFilterOpts& recoverEquivFilter) {
        std::string str("RecoverEquivalentRouteFilter (member filters) :-");
        for_each(
            recoverEquivFilter.tiebreakerConfigs.cbegin(),
            recoverEquivFilter.tiebreakerConfigs.cend(),
            [&str](const RouteFilterConfig& filter) {
              str.append(fmt::format("\n {}", filter.str()));
            });
        return str;
      });
  auto filterCriterion = boost::apply_visitor(describeFilter, opts);
  return fmt::format(
      "{}, Filter Criterion: {}",
      routeMetricDescription.count(metric) ? routeMetricDescription.at(metric)
                                           : "Invalid",
      filterCriterion);
}

unique_ptr<RouteFilter> RouteFilter::fromRouteFilterConfig(
    const RouteFilterConfig& routeFilterConfig) {
  Rule rule;
  switch (routeFilterConfig.metric) {
    case RouteMetric::BGP_LOCAL_PREFERENCE:
      rule = RouteFilter::getBgpLocalPreference;
      break;
    case RouteMetric::BGP_AS_PATH_LEN:
      rule = RouteFilter::getBgpAsPathLen;
      break;
    case RouteMetric::BGP_AS_PATH_LEN_WITH_CONFED:
      rule = RouteFilter::getBgpAsPathLenWithConfed;
      break;
    case RouteMetric::BGP_ORIGIN_CODE:
      rule = RouteFilter::getBgpOriginCode;
      break;
    case RouteMetric::BGP_MED_VALUE:
      rule = RouteFilter::getBgpMedValue;
      break;
    case RouteMetric::DELETED_ROUTE:
      rule = RouteFilter::getIsRouteDeleted;
      break;
    case RouteMetric::CONTROLLER_COMMUNITY:
      rule = RouteFilter::getRouterLevelPreferenceFromControllerCommunities;
      break;
    case RouteMetric::BGP_ROUTER_ID:
      rule = RouteFilter::getBgpRouterId;
      break;
    case RouteMetric::BGP_PEER_ASN:
      rule = RouteFilter::getBgpPeerAsn;
      break;
    case RouteMetric::CONTROLLER_COMMUNITY_METRO:
      rule = RouteFilter::getMetroLevelPreferenceFromControllerCommunities;
      break;
    case RouteMetric::PREFIX_LENGTH:
      rule = RouteFilter::getBgpPrefixLength;
      break;
    case RouteMetric::ALREADY_PREFERRED_ROUTE:
      rule = RouteFilter::getIsRoutePreferred;
      break;
    case RouteMetric::EXTERNAL_ROUTE:
      rule = RouteFilter::getIsRouteExternal;
      break;
    case RouteMetric::BGP_PEER_IP:
      rule = RouteFilter::getBgpPeerIPAsInt;
      break;
    case RouteMetric::BGP_PEER_NEXTHOP:
      rule = RouteFilter::getBgpNexthopAsInt;
      break;
    case RouteMetric::BGP_CLUSTER_LIST_LEN:
      rule = RouteFilter::getBgpClusterListLen;
      break;
    case RouteMetric::CONFED_EXTERNAL_ROUTE:
      rule = RouteFilter::getIsRouteConfedExternal;
      break;
    case RouteMetric::BGP_WEIGHT_VALUE:
      rule = RouteFilter::getBgpWeightValue;
      break;
    case RouteMetric::IGP_COST:
      rule = RouteFilter::getIgpCostValue;
      break;
    default:
      XLOGF(FATAL, "Invalid metric: {}", int(routeFilterConfig.metric));
  }

  auto routeFilter = RouteFilter::createRouteFilter(routeFilterConfig, rule);
  return routeFilter;
}

vector<unique_ptr<RouteFilter>> RouteFilter::fromRouteFilterConfigVec(
    const vector<RouteFilterConfig>& routeFilterConfigVec) {
  vector<unique_ptr<RouteFilter>> routeFilters;
  for (const auto& config : routeFilterConfigVec) {
    routeFilters.emplace_back(fromRouteFilterConfig(config));
  }
  return routeFilters;
}

unique_ptr<RouteFilter> RouteFilter::createRouteFilter(
    const RouteFilterConfig& routeFilterConfig,
    const Rule& rule) {
  unique_ptr<RouteFilter> routeFilter;
  auto createFilter = folly::overload(
      [&](RangedRouteFilterOpts const& rangedFilterOpts) {
        routeFilter.reset(new RangedRouteFilter(
            routeFilterConfig,
            rule,
            rangedFilterOpts.minValue,
            rangedFilterOpts.maxValue));
      },
      [&](HighestLowestRouteFilterOpts const& highestLowestFilterOpts) {
        routeFilter.reset(new HighestLowestRouteFilter(
            routeFilterConfig, rule, highestLowestFilterOpts.action));
      },
      [&](MembershipRouteFilterOpts const& membershipFilterOpts) {
        routeFilter.reset(new MembershipRouteFilter(
            routeFilterConfig,
            rule,
            membershipFilterOpts.action,
            membershipFilterOpts.metricValues));
      },
      [&](RecoverEquivalentRouteFilterOpts const& recoverEquivalentFilterOpts) {
        routeFilter.reset(new RecoverEquivalentRouteFilter(
            routeFilterConfig,
            rule,
            recoverEquivalentFilterOpts.tiebreakerConfigs));
      });
  boost::apply_visitor(createFilter, routeFilterConfig.opts);
  return routeFilter;
}

RouteMetric RouteFilter::getRouteMetric() {
  return routeFilterConfig_.metric;
}

uint8_t RouteFilter::getBgpPrefixLength(const shared_ptr<const RouteBase> ri) {
  return ri->getBgpPrefixLength();
}

int64_t RouteFilter::getBgpLocalPreference(
    const shared_ptr<const RouteBase> ri) {
  return ri->getBgpLocalPreference();
}

int64_t RouteFilter::getBgpAsPathLen(const shared_ptr<const RouteBase> ri) {
  return ri->getBgpAsPathLen();
}

int64_t RouteFilter::getBgpAsPathLenWithConfed(
    const shared_ptr<const RouteBase> ri) {
  return ri->getBgpAsPathLenWithConfed();
}

int64_t RouteFilter::getBgpOriginCode(const shared_ptr<const RouteBase> ri) {
  return ri->getBgpOriginCode();
}

int64_t RouteFilter::getBgpMedValue(const shared_ptr<const RouteBase> ri) {
  return ri->getBgpMedValue();
}

uint16_t RouteFilter::getBgpWeightValue(const shared_ptr<const RouteBase> ri) {
  return ri->getBgpWeightValue();
}

int64_t RouteFilter::getIsRouteExternal(const shared_ptr<const RouteBase> ri) {
  return ri->getIsRouteExternal();
}

int64_t RouteFilter::getIsRouteConfedExternal(
    const shared_ptr<const RouteBase> ri) {
  return ri->getIsRouteConfedExternal();
}

int64_t RouteFilter::getIsRouteDeleted(const shared_ptr<const RouteBase> ri) {
  return ri->getIsRouteDeleted();
}

int64_t RouteFilter::getIsRoutePreferred(const shared_ptr<const RouteBase> ri) {
  return ri->getIsRoutePreferred();
}

int64_t RouteFilter::getRouterLevelPreferenceFromControllerCommunities(
    const shared_ptr<const RouteBase> ri) {
  return ri->getRouterLevelPreferenceFromControllerCommunities();
}

int64_t RouteFilter::getMetroLevelPreferenceFromControllerCommunities(
    const shared_ptr<const RouteBase> ri) {
  return ri->getMetroLevelPreferenceFromControllerCommunities();
}

uint64_t RouteFilter::getBgpRouterId(const shared_ptr<const RouteBase> ri) {
  return ri->getBgpRouterId();
}

__uint128_t RouteFilter::getBgpPeerIPAsInt(
    const shared_ptr<const RouteBase> ri) {
  return ri->getBgpPeerIPAsInt();
}

int64_t RouteFilter::getBgpPeerAsn(const shared_ptr<const RouteBase> ri) {
  return ri->getOriginAsnAndPeerAsn().second;
}

__uint128_t RouteFilter::getBgpNexthopAsInt(
    const shared_ptr<const RouteBase> ri) {
  return ri->getBgpNexthopAsInt();
}

uint32_t RouteFilter::getIgpCostValue(const shared_ptr<const RouteBase> ri) {
  return ri->getIgpCostValue();
}

vector<__uint128_t> RouteFilter::getRouteWeights(
    const vector<shared_ptr<RouteBase>> routes) {
  vector<__uint128_t> routeWeights(routes.size());
  for (int i = 0; i < routes.size(); ++i) {
    routeWeights[i] = rule_(routes[i]);
  }
  return routeWeights;
}

int64_t RouteFilter::getBgpClusterListLen(
    const shared_ptr<const RouteBase> ri) {
  return ri->getBgpClusterListLen();
}

bool RouteFilter::compare(
    const shared_ptr<const RouteBase> /* ri1 */,
    const shared_ptr<const RouteBase> /* ri2 */) {
  XLOGF(FATAL, "Filter does not have a comparator defined");
}

void RouteFilter::captureRejectedFilter(
    folly::Optional<std::map<
        std::shared_ptr<RouteBase>,
        nettools::edge::RouteFilterConfig>*> filterMap,
    std::shared_ptr<RouteBase> rInfo) {
  if (filterMap && (*filterMap)->count(rInfo) == 0) {
    (*(*filterMap))[rInfo] = getRouteFilterConfig();
  }
}

RouteFilterConfig RouteFilter::getRouteFilterConfig() const {
  return routeFilterConfig_;
}

AcceptedAndRejectedRoutes HighestLowestRouteFilter::filter(
    const AcceptedAndRejectedRoutes& previousRoutes,
    folly::Optional<std::map<
        std::shared_ptr<RouteBase>,
        nettools::edge::RouteFilterConfig>*> filterMap) {
  AcceptedAndRejectedRoutes filteredRoutes;
  // We only care about the accepted routes at this stage
  const auto& routes = previousRoutes.first;
  if (routes.size() == 0) {
    return filteredRoutes;
  }
  vector<__uint128_t> routeWeights = getRouteWeights(routes);
  __uint128_t targetWeight;
  switch (action_) {
    case HighestLowestRouteFilterAction::CHOOSE_HIGHEST:
      targetWeight =
          *std::max_element(routeWeights.begin(), routeWeights.end());
      break;
    case HighestLowestRouteFilterAction::CHOOSE_LOWEST:
      targetWeight =
          *std::min_element(routeWeights.begin(), routeWeights.end());
      break;
    default:
      XLOGF(FATAL, "Unknown HighestLowestRouteFilterAction: {}", int(action_));
  }
  vector<shared_ptr<RouteBase>> allowedRoutes;
  for (int i = 0; i < routes.size(); ++i) {
    if (routeWeights[i] == targetWeight) {
      filteredRoutes.first.push_back(routes[i]);
    } else {
      filteredRoutes.second.push_back(routes[i]);
      captureRejectedFilter(filterMap, routes[i]);
    }
  }
  return filteredRoutes;
}

bool HighestLowestRouteFilter::compare(
    const shared_ptr<const RouteBase> ri1,
    const shared_ptr<const RouteBase> ri2) {
  switch (action_) {
    case HighestLowestRouteFilterAction::CHOOSE_HIGHEST:
      return rule_(ri1) < rule_(ri2);
    case HighestLowestRouteFilterAction::CHOOSE_LOWEST:
      return rule_(ri1) > rule_(ri2);
    default:
      XLOGF(FATAL, "Unknown HighestLowestRouteFilterAction: {}", int(action_));
  }
}

AcceptedAndRejectedRoutes RangedRouteFilter::filter(
    const AcceptedAndRejectedRoutes& previousRoutes,
    folly::Optional<std::map<
        std::shared_ptr<RouteBase>,
        nettools::edge::RouteFilterConfig>*> filterMap) {
  // We only care about the accepted routes at this stage
  const auto& routes = previousRoutes.first;
  AcceptedAndRejectedRoutes filteredRoutes;
  vector<__uint128_t> routeWeights = getRouteWeights(routes);
  for (int i = 0; i < routes.size(); ++i) {
    if (routeWeights[i] >= minValue_ && routeWeights[i] <= maxValue_) {
      filteredRoutes.first.push_back(routes[i]);
    } else {
      filteredRoutes.second.push_back(routes[i]);
      captureRejectedFilter(filterMap, routes[i]);
    }
  }
  return filteredRoutes;
}

AcceptedAndRejectedRoutes MembershipRouteFilter::filter(
    const AcceptedAndRejectedRoutes& previousRoutes,
    folly::Optional<std::map<
        std::shared_ptr<RouteBase>,
        nettools::edge::RouteFilterConfig>*> filterMap) {
  // We only care about the accepted routes at this stage
  const auto& routes = previousRoutes.first;
  AcceptedAndRejectedRoutes filteredRoutes;
  vector<__uint128_t> routeWeights = getRouteWeights(routes);
  for (int i = 0; i < routes.size(); ++i) {
    switch (action_) {
      case MembershipRouteFilterAction::CHOOSE_MEMBER:
        if (metricValues_.find(routeWeights[i]) == metricValues_.end()) {
          filteredRoutes.second.push_back(routes[i]);
          captureRejectedFilter(filterMap, routes[i]);
        } else {
          filteredRoutes.first.push_back(routes[i]);
        }
        break;
      case MembershipRouteFilterAction::CHOOSE_NONMEMBER:
        if (metricValues_.find(routeWeights[i]) == metricValues_.end()) {
          filteredRoutes.first.push_back(routes[i]);
        } else {
          filteredRoutes.second.push_back(routes[i]);
          captureRejectedFilter(filterMap, routes[i]);
        }
        break;
      default:
        XLOGF(FATAL, "Unknown MembershipRouteFilterAction: {}", int(action_));
    }
  }
  return filteredRoutes;
}

AcceptedAndRejectedRoutes RecoverEquivalentRouteFilter::filter(
    const AcceptedAndRejectedRoutes& previousRoutes,
    folly::Optional<std::map<
        std::shared_ptr<RouteBase>,
        nettools::edge::RouteFilterConfig>*> filterMap) {
  // Routes from previous round
  auto prevRoutes = previousRoutes;
  auto rejectedRoutes = previousRoutes.second;

  // Additional filtering before recovering equivalent routes
  if (!tiebreakers_.empty()) {
    rejectedRoutes.clear();
  }
  for (const auto& tiebreaker : tiebreakers_) {
    prevRoutes = tiebreaker->filter(prevRoutes, filterMap);
    // Keep all rejected routes
    std::copy(
        prevRoutes.second.begin(),
        prevRoutes.second.end(),
        std::inserter(rejectedRoutes, rejectedRoutes.begin()));
  }

  // Routes after additional filtering
  const auto& routes = prevRoutes.first;
  // Routes in this round
  AcceptedAndRejectedRoutes filteredRoutes;
  vector<__uint128_t> routeWeights = getRouteWeights(routes);

  std::vector<std::shared_ptr<RouteBase>> remainingRejectedRoutes;
  for (int i = 0; i < routes.size(); ++i) {
    // Keep using current accepted routes
    filteredRoutes.first.push_back(routes[i]);
    auto metricValue = routeWeights[i];
    for (const auto& rejectedRoute : rejectedRoutes) {
      if (rule_(rejectedRoute) == metricValue) {
        // Welcome! You're equivalent to the current route
        filteredRoutes.first.push_back(rejectedRoute);
      } else {
        remainingRejectedRoutes.push_back(rejectedRoute);
      }
    }
    /*
     * Get ready for the next iteration and only keep any routes that are still
     * rejected
     */
    rejectedRoutes = remainingRejectedRoutes;
    remainingRejectedRoutes = std::vector<std::shared_ptr<RouteBase>>();
  }
  filteredRoutes.second = std::move(rejectedRoutes);
  return filteredRoutes;
}

} // namespace edge
} // namespace nettools
} // namespace facebook
