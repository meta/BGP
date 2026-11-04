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

#include "neteng/fboss/bgp/cpp/rib/RibPolicy.h"

#include <re2/re2.h>
#include <algorithm>
#include <limits>
#include <memory>

#include <fmt/format.h>

#include "neteng/fboss/bgp/cpp/BgpServiceUtil.h"
#include "neteng/fboss/bgp/cpp/common/BgpError.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

namespace facebook::bgp {
using namespace rib_policy;

/*
 * BgpPathMatcher
 */
BgpPathMatcher::BgpPathMatcher(const TBgpPathMatcher& matcher)
    : prefersLowestIgpCost_(matcher.prefer_lowest_igp_cost().value_or(false)) {
  if (matcher.community_list()) {
    matches_.emplace_back(std::make_unique<CommunityMatch>(matcher));
  }
  if (matcher.origin()) {
    matches_.emplace_back(std::make_unique<OriginMatch>(matcher));
  }
  if (matcher.as_path_length()) {
    matches_.emplace_back(std::make_unique<AsPathLenMatch>(matcher));
  }
  if (matcher.as_path_regex()) {
    matches_.emplace_back(std::make_unique<AsPathMatch>(matcher));
  }
  if (matcher.min_lbw_bps()) {
    matches_.emplace_back(std::make_unique<MinLbwBpsMatch>(matcher));
  }
  const bool hasOnlyEmptyCommunityMatch = prefersLowestIgpCost_ &&
      matches_.size() == 1 && matcher.community_list() &&
      matcher.community_list()->communities()->empty();
  if (matches_.empty() || hasOnlyEmptyCommunityMatch) {
    throw BgpError("missing matches in bgp path matcher");
  }
}

bool BgpPathMatcher::match(const std::shared_ptr<RouteInfo>& path) const {
  for (const auto& match : matches_) {
    if (!match->Match(path->attrs)) {
      return false;
    }
  }
  return true;
}

/*
 * PathSelectionCriteria
 */
PathSelectionCriteria::PathSelectionCriteria(
    const rib_policy::TPathSelectionCriteria& criteria)
    : tCriteria_(criteria),
      pathMatchers_(getPathMatchers(criteria)),
      hasLowestIgpCostMatcher_(
          std::any_of(
              pathMatchers_.begin(),
              pathMatchers_.end(),
              [](const auto& matcher) {
                return matcher->prefersLowestIgpCost();
              })),
      minNexthop_(criteria.min_nexthop().to_optional()) {}

TPathSelectionCriteria PathSelectionCriteria::toThrift() const {
  return tCriteria_;
}

std::vector<std::unique_ptr<BgpPathMatcher>>
PathSelectionCriteria::getPathMatchers(const TPathSelectionCriteria& criteria) {
  std::vector<std::unique_ptr<BgpPathMatcher>> pathMatchers{};
  if (criteria.path_matchers()->empty()) {
    throw BgpError("missing path matchers in path selection criteria");
  }
  for (const auto& matcher : *criteria.path_matchers()) {
    pathMatchers.emplace_back(std::make_unique<BgpPathMatcher>(matcher));
  }
  return pathMatchers;
}

std::vector<std::shared_ptr<RouteInfo>>
PathSelectionCriteria::tryOverrideMultipathSelection(
    const std::vector<std::shared_ptr<RouteInfo>>& paths) const {
  std::vector<uint32_t> lowestCosts(
      hasLowestIgpCostMatcher_ ? pathMatchers_.size() : 0,
      std::numeric_limits<uint32_t>::max());
  for (size_t i = 0; i < lowestCosts.size(); ++i) {
    const auto& matcher = pathMatchers_.at(i);
    if (!matcher->prefersLowestIgpCost()) {
      continue;
    }
    for (const auto& path : paths) {
      if (matcher->match(path)) {
        lowestCosts.at(i) =
            std::min(lowestCosts.at(i), path->getIgpCostValue());
      }
    }
  }

  std::vector<std::shared_ptr<RouteInfo>> multipaths;
  for (const auto& path : paths) {
    for (size_t i = 0; i < pathMatchers_.size(); ++i) {
      const auto& matcher = pathMatchers_.at(i);
      if (!matcher->match(path)) {
        continue;
      }
      if (matcher->prefersLowestIgpCost() &&
          path->getIgpCostValue() != lowestCosts.at(i)) {
        continue;
      }
      multipaths.emplace_back(path);
      break;
    }
  }

  if (minNexthop_ && (multipaths.size() < *minNexthop_)) {
    // does not satisfy the min nexthop criteria, return an empty set
    return {};
  }
  return multipaths;
}

/*
 * PathSelector
 */
bool PathSelector::operator==(const PathSelector& other) const {
  if (centralizedCriteriaList_.size() !=
      other.centralizedCriteriaList_.size()) {
    return false;
  }
  // Ensure the PathSelectionCriteria pointed by unique_ptr are the same
  for (auto iterator = centralizedCriteriaList_.begin(),
            otherIterator = other.centralizedCriteriaList_.begin();
       iterator != centralizedCriteriaList_.end() &&
       otherIterator != other.centralizedCriteriaList_.end();
       ++iterator, ++otherIterator) {
    if (**iterator != **otherIterator) {
      return false;
    }
  }

  return bgpNativeMinNexthop_ == other.bgpNativeMinNexthop_ &&
      drainOnMinCapacityThresholdViolation_ ==
      other.drainOnMinCapacityThresholdViolation_ &&
      bgpNativeMinAggLbwbps_ == other.bgpNativeMinAggLbwbps_ &&
      relaxBgpNativeMinAggLbwbps_ == other.relaxBgpNativeMinAggLbwbps_;
}

TPathSelector PathSelector::toThrift() const {
  TPathSelector tPathSelector;

  for (const auto& criteria : centralizedCriteriaList_) {
    tPathSelector.criteria_list()->emplace_back(criteria->toThrift());
  }

  if (bgpNativeMinNexthop_.has_value()) {
    tPathSelector.bgp_native_path_selection_min_nexthop() =
        *bgpNativeMinNexthop_;
  }

  if (drainOnMinCapacityThresholdViolation_) {
    tPathSelector.drain_on_min_nexthop_violation() =
        *drainOnMinCapacityThresholdViolation_;
  }

  if (bgpNativeMinAggLbwbps_) {
    tPathSelector.bgp_min_aggregate_lbw_bps() = *bgpNativeMinAggLbwbps_;
  }

  if (relaxBgpNativeMinAggLbwbps_) {
    tPathSelector.relax_bgp_min_aggregate_lbw_bps() =
        *relaxBgpNativeMinAggLbwbps_;
  }

  return tPathSelector;
}

std::vector<std::unique_ptr<PathSelectionCriteria>>
PathSelector::getCentralizedCriteriaList(const TPathSelector& selector) {
  std::vector<std::unique_ptr<PathSelectionCriteria>> centralizedCriteriaList{};
  for (const auto& criteria : *selector.criteria_list()) {
    centralizedCriteriaList.push_back(
        std::make_unique<PathSelectionCriteria>(criteria));
  }
  return centralizedCriteriaList;
}

std::vector<std::shared_ptr<RouteInfo>>
PathSelector::overrideMultipathSelection(
    const std::vector<std::shared_ptr<RouteInfo>>& paths,
    const std::unique_ptr<RouteInfoSelector>& multipathSelector,
    PathSelectionPolicyResult& result) const {
  /*
   * `result` may be a cached struct reused across calls
   * (PathSelectionPolicy::pathSelectionResults_). Reset the drain-transition
   * flag up front so a previous-call value cannot leak into a code path that
   * returns early without overwriting it.
   */
  result.drainOnMinCapacityThresholdViolation = false;
  result.mnhThreshold = 0;
  result.aggLbwBpsThreshold = 0;

  std::vector<std::shared_ptr<RouteInfo>> multipaths;
  for (const auto& criteria : centralizedCriteriaList_) {
    multipaths = criteria->tryOverrideMultipathSelection(paths);
    // filter the paths
    if (!multipaths.empty()) {
      result.activeCriteria = criteria.get();
      result.outcome = PathSelectionPolicyResult::Outcome::CPS;
      return multipaths;
    }
  }
  // none of the criteria yields qualified multipaths at this point

  // Adopt default multipath selector and apply additional criteria
  multipaths = multipathSelector->selectRoutes(paths);

  result.activeCriteria = nullptr;

  if (multipaths.empty() ||
      (!bgpNativeMinNexthop_ && !bgpNativeMinAggLbwbps_)) {
    // there's no available path, or no additional criteria to apply
    result.outcome = PathSelectionPolicyResult::Outcome::BGP;
    return multipaths;
  }

  // at this point we need to examine the additional CPS criteria

  if (bgpNativeMinNexthop_) {
    if (multipaths.size() < *bgpNativeMinNexthop_) {
      XLOGF(
          DBG2,
          "BGP native min nexthop violation: multipaths.size() = {} ,"
          "bgpNativeMinNexthop = {}.",
          multipaths.size(),
          *bgpNativeMinNexthop_);
      result.outcome =
          PathSelectionPolicyResult::Outcome::BGP_FAILED_CPS_MIN_NEXTHOP;
      if (drainOnMinCapacityThresholdViolation_.has_value() &&
          *drainOnMinCapacityThresholdViolation_) {
        result.drainOnMinCapacityThresholdViolation = true;
        result.mnhThreshold = *bgpNativeMinNexthop_;
        XLOGF(
            DBG2,
            "Partial drain triggered on MNH violation (multipaths={}, mnh={}): "
            "bestpath retained, drain community will be attached",
            multipaths.size(),
            *bgpNativeMinNexthop_);
        return multipaths;
      }
      return {};
    }
    result.outcome = PathSelectionPolicyResult::Outcome::BGP;
    return multipaths;
  }

  if (bgpNativeMinAggLbwbps_ && *bgpNativeMinAggLbwbps_ > 0) {
    int64_t aggLbwbps = 0;
    for (const auto& path : multipaths) {
      const auto lbwBytesPerSecond = path->attrs->getNonTransitiveLbw();
      if (!lbwBytesPerSecond) {
        // if any path doesn't have lbw, we abort
        result.outcome = PathSelectionPolicyResult::Outcome::BGP;
        return multipaths;
      }
      aggLbwbps += (lbwBytesPerSecond->second * 8);
    }
    if (aggLbwbps < *bgpNativeMinAggLbwbps_) {
      XLOGF(
          DBG2,
          "BGP native min lbw_bps violation: aggLbwbps = {} "
          "bgpNativeMinAggLbwbps = {}. ",
          aggLbwbps,
          *bgpNativeMinAggLbwbps_);
      result.outcome =
          PathSelectionPolicyResult::Outcome::BGP_FAILED_CPS_MIN_AGG_LBW;
      if (drainOnMinCapacityThresholdViolation_.has_value() &&
          *drainOnMinCapacityThresholdViolation_) {
        result.drainOnMinCapacityThresholdViolation = true;
        result.aggLbwBpsThreshold = *bgpNativeMinAggLbwbps_;
        XLOGF(
            DBG2,
            "Partial drain triggered on LBW violation "
            "(multipaths={}, aggLbwbps={}, minAggLbwbps={}): "
            "bestpath retained, drain community will be attached",
            multipaths.size(),
            aggLbwbps,
            *bgpNativeMinAggLbwbps_);
        return multipaths;
      }
      if (relaxBgpNativeMinAggLbwbps_.value_or(false)) {
        XLOG(
            DBG2,
            "relaxBgpNativeMinAggLbwbps is set to true, "
            "the paths will still be installed to FIB, "
            "but the best path will not be advertised (or it will be withdrawn)");
        return multipaths;
      }
      return {};
    }
  }
  result.outcome = PathSelectionPolicyResult::Outcome::BGP;
  return multipaths;
}

/*
 * RibPolicyRouteMatcher
 */
RibPolicyRouteMatcher::RibPolicyRouteMatcher(const TRibRouteMatcher& matcher)
    : tMatcher_(matcher),
      prefixSet_(getPrefixSet(matcher)),
      communityMatch_(matcher.community_list().to_optional()) {
  if (prefixSet_.empty() && !communityMatch_) {
    throw BgpError("Missing matching attribute in RibPolicyRouteMatcher");
  }
  /*
   * A statement carries exactly one route matcher -- a prefix set or a
   * community list, never both. Matching on both is expressible as two separate
   * statements, and keeping it to one keeps the community->statement index
   * unambiguous.
   */
  if (!prefixSet_.empty() && communityMatch_) {
    throw BgpError(
        "RibPolicyRouteMatcher must specify either a prefix set or a community "
        "list, not both; use separate statements");
  }
}

TRibRouteMatcher RibPolicyRouteMatcher::toThrift() const {
  return tMatcher_;
}

bool RibPolicyRouteMatcher::match(const RibEntry& route) const {
  return matchPrefix(route) || matchCommunity(route);
}

bool RibPolicyRouteMatcher::matchPrefix(const RibEntry& route) const {
  return prefixSet_.contains(route.getPrefix());
}

bool RibPolicyRouteMatcher::matchCommunity(const RibEntry& route) const {
  if (!communityMatch_) {
    return false;
  }
  for (const auto& path : route.getAllPaths()) {
    if (communityMatch_->Match(path->attrs)) {
      return true;
    }
  }
  return false;
}

std::unordered_set<folly::CIDRNetwork> RibPolicyRouteMatcher::getPrefixSet(
    const TRibRouteMatcher& matcher) {
  std::unordered_set<folly::CIDRNetwork> prefixSet{};
  if (matcher.prefixes()) {
    // Populate the match fields
    for (const auto& tPrefix : *matcher.prefixes()) {
      prefixSet.insert(tIpPrefixToNetwork(tPrefix));
    }
  }
  return prefixSet;
}

/*
 * RouteAttributeActions
 */
RouteAttributeActions::RouteAttributeActions(
    const TRouteAttributeActions& actions)
    : linkBandwidth_(
          actions.set_lbw()
              ? std::make_optional<int64_t>(*actions.set_lbw()->lbw())
              : std::nullopt),
      nextHopWeightActions_(
          actions.set_ucmp_weights()
              ? getNexthopWeightActions(*actions.set_ucmp_weights())
              : std::vector<std::unique_ptr<NextHopWeightAction>>{}),
      strictMatchNexthops_(
          actions.set_ucmp_weights()
              ? *actions.set_ucmp_weights()
                     .value()
                     .apply_all_actions_or_fallback_to_ecmp()
              : false),
      divideWeightsByMatchingPathCount_{
          actions.set_ucmp_weights()
              ? actions.set_ucmp_weights()
                    ->divide_weights_by_matching_path_count()
                    .to_optional()
              : std::nullopt} {
  if (!linkBandwidth_ && nextHopWeightActions_.empty()) {
    throw BgpError("Empty route attribute action");
  }
}

TRouteAttributeActions RouteAttributeActions::toThrift() const {
  TRouteAttributeActions actions;

  // set lbw
  if (linkBandwidth_) {
    TRouteAttributeLbwAction tAction;
    tAction.lbw() = *linkBandwidth_;
    actions.set_lbw() = std::move(tAction);
  }

  // Set nexthop_weight actions.
  if (!nextHopWeightActions_.empty()) {
    TRouteAttributeUcmpAction tSetUcmpWeights;
    tSetUcmpWeights.nexthop_weight_actions() = {};
    for (const auto& weightAction : nextHopWeightActions_) {
      tSetUcmpWeights.nexthop_weight_actions()->emplace_back(
          weightAction->toThrift());
    }
    tSetUcmpWeights.apply_all_actions_or_fallback_to_ecmp() =
        strictMatchNexthops_;
    tSetUcmpWeights.divide_weights_by_matching_path_count().from_optional(
        divideWeightsByMatchingPathCount_);
    actions.set_ucmp_weights() = std::move(tSetUcmpWeights);
  }

  return actions;
}

/**
 * Get the collection of next-hop weight actions.
 *
 * @param raAction Route attribute action thrift struct
 *
 * @return Collection of points to next-hop weight action objects.
 */
std::vector<std::unique_ptr<NextHopWeightAction>>
RouteAttributeActions::getNexthopWeightActions(
    const TRouteAttributeUcmpAction& raAction) {
  std::vector<std::unique_ptr<NextHopWeightAction>> nextHopWeightActions{};
  for (const auto& action : *raAction.nexthop_weight_actions()) {
    nextHopWeightActions.push_back(
        std::make_unique<NextHopWeightAction>(action));
  }
  return nextHopWeightActions;
}

bool RouteAttributeActions::updateAttribute(RibEntry& route) const {
  // A route is updated if any of the updates changes the route
  bool updated = false;
  // Update link bandwidth
  updated |= updateLinkBandwidth(route);
  updated |= updateUcmpWeight(route);
  return updated;
}

bool RouteAttributeActions::updateLinkBandwidth(RibEntry& route) const {
  if (!linkBandwidth_) {
    return false;
  }

  // try to modify rib entry's policy-lbw value
  auto ribLBW = route.getRibPolicyUcmpWeight();

  /*
   * bypass lbw update in below condition
   * 1: rib has no lbw and lbw is 0
   * 2: rib has lbw and value matches with action lbw
   */
  if (!ribLBW.has_value() && *linkBandwidth_ == 0) {
    return false;
  } else if (ribLBW == *linkBandwidth_) {
    // same lbw value then no effect taken
    return false;
  }
  // apply new lbw value to route
  route.setRibPolicyUcmpWeight(*linkBandwidth_);
  return true;
}

std::optional<RouteAttributeActions::MatchedWeight>
RouteAttributeActions::findMatchingAction(
    const std::shared_ptr<RouteInfo>& path) const {
  // Iterate actions in index order, first match wins
  for (size_t idx = 0; idx < nextHopWeightActions_.size(); ++idx) {
    auto [isMatch, weight] = nextHopWeightActions_[idx]->Match(path);
    if (isMatch) {
      return MatchedWeight{idx, static_cast<int32_t>(weight)};
    }
  }
  return std::nullopt;
}

std::optional<RouteAttributeActions::MatchedWeight>
RouteAttributeActions::findMatchingActionCached(
    const std::shared_ptr<RouteInfo>& path) const {
  const auto& key = path->attrs;
  auto it = weightIndex_.find(key);
  if (it != weightIndex_.end()) {
    RibStats::STATS_raPolicyWeightIndexHit.add(1);
    return it->second;
  }
  RibStats::STATS_raPolicyWeightIndexMiss.add(1);
  auto matched = findMatchingAction(path);
  weightIndex_.emplace(key, matched);
  return matched;
}

/**
 * Evaluate and apply the next-hop weights for this prefix.
 *
 * @param route The route/prefix under consideration.
 *
 * @return Boolean representing whether next-hops are overridden by policy
 * or not.
 */
bool RouteAttributeActions::updateUcmpWeight(RibEntry& route) const {
  const auto& routeNexthops = route.getMultipathWeightedNexthops();
  if (!routeNexthops) {
    return false;
  }

  /*
   * If strict match mode is specified and route's next-hops are not matching
   * bail out.
   */
  if (strictMatchNexthops_ &&
      (routeNexthops.get()->size() != nextHopWeightActions_.size())) {
    return false;
  }

  /*
   * Only paths whose nexthop is in the multipath-selected set should be
   * considered: those are the only nexthops that will receive a weight, and
   * counting non-selected paths in matchingPathCount inflates the divisor for
   * divide_weights_by_matching_path_count, producing smaller-than-intended
   * per-nexthop weights for the selected paths.
   */
  std::vector<std::shared_ptr<RouteInfo>> selectedPaths;
  for (const auto& path : route.getAllPaths()) {
    if (routeNexthops->contains(path->attrs->getNexthop())) {
      selectedPaths.emplace_back(path);
    }
  }

  // Build nexthop -> (actionIdx, weight) map, enforcing "lowest actionIdx wins"
  folly::F14NodeMap<
      folly::IPAddress /* nexthop */,
      std::pair<size_t /* action index */, int32_t /* weight */>>
      policyNexthopAction;

  for (const auto& path : selectedPaths) {
    auto weightResult = findMatchingActionCached(path);
    if (!weightResult.has_value()) {
      continue;
    }
    const auto& nh = path->attrs->getNexthop();
    const auto [actionIdx, weight] = *weightResult;

    auto it = policyNexthopAction.find(nh);
    if (it == policyNexthopAction.end()) {
      // First path to claim this nexthop
      policyNexthopAction.emplace(nh, std::make_pair(actionIdx, weight));
    } else {
      // Nexthop already claimed. Keep the one with lower actionIdx.
      const auto [existingIdx, existingWeight] = it->second;
      if (actionIdx != existingIdx) {
        /*
         * A nexthop matched by two different actions is a policy
         * misconfiguration (actions are expected to be mutually exclusive).
         * Warn regardless of selectedPaths iteration order; the lowest action
         * index wins.
         */
        XLOGF(
            WARN,
            "NH {} for route {} matches multiple actions ({} and {})",
            nh.str(),
            route.getPrefix().first.str(),
            existingIdx,
            actionIdx);
        if (actionIdx < existingIdx) {
          it->second = std::make_pair(actionIdx, weight);
        }
      }
    }
  }

  /*
   * Compute matchingPathCount[actionIdx] = number of nexthops whose WINNING
   * action is actionIdx. This must be computed AFTER resolving winners.
   */
  std::vector<int> matchingPathCount(nextHopWeightActions_.size(), 0);
  for (const auto& [nh, actionWeightPair] : policyNexthopAction) {
    const auto [actionIdx, _] = actionWeightPair;
    matchingPathCount.at(actionIdx)++;
  }

  // Assign the policy weights to the route.
  WeightedNexthopMap updatedRouteNexthops;
  for (auto& [nh, _] : *routeNexthops) {
    if (const auto& it = policyNexthopAction.find(nh);
        it != policyNexthopAction.end()) {
      const auto [actionIdx, policyWeight] = it->second;
      if (divideWeightsByMatchingPathCount_.value_or(false)) {
        /*
         * matchingPathCount[actionIdx] is guaranteed to be >= 1 here: it was
         * computed above (in the loop over policyNexthopAction) as the number
         * of nexthops whose winning action is actionIdx, so finding `nh` in
         * policyNexthopAction means its winning action contributed at least one
         * such increment. std::max with a floor of 1 is for small-numerator
         * rounding, not divide-by-zero.
         */
        updatedRouteNexthops.emplace(
            nh, std::max(policyWeight / matchingPathCount.at(actionIdx), 1));
      } else {
        updatedRouteNexthops.emplace(nh, policyWeight);
      }
    } else {
      /*
       * All next-hops must have a weight. If a next-hop is not matched by the
       * policy, abort ucmp for the route.
       */
      return false;
    }
  }
  if (*routeNexthops != updatedRouteNexthops) {
    // only overwrite weights when they are different
    route.overrideWeightedNexthops(updatedRouteNexthops);
    return true;
  }

  return false;
}

/*
 * RouteAttributeStatement
 */
TRouteAttributeStatement RouteAttributeStatement::toThrift() const {
  TRouteAttributeStatement tStmt;
  tStmt.matcher() = matcher_.toThrift();
  tStmt.actions() = actions_.toThrift();

  // Set expiration_time_s
  if (expirationTime_) {
    tStmt.expiration_time_s() = *expirationTime_;
  }
  return tStmt;
}

bool RouteAttributeStatement::updateAttribute(RibEntry& route) const {
  return actions_.updateAttribute(route);
}

/*
 * RouteAttributePolicy
 */
/**
 * Create instance of RouteAttributePolicy.
 *
 * @param policy Thrift structure for RouteAttribuePolicy.
 */
RouteAttributePolicy::RouteAttributePolicy(const TRouteAttributePolicy& policy)
    : matchCache_(
          std::make_unique<folly::F14NodeMap<
              folly::CIDRNetwork,
              std::optional<RibPolicyResultBase>>>()) {
  for (const auto& [name, tStmt] : *policy.statements()) {
    statements_.emplace(name, tStmt);
  }
  // Populate prefix matcher list
  for (const auto& [name, stmt] : statements_) {
    if (stmt.hasPrefixSet()) {
      prefixMatcherStatements_.push_back(name);
    }
  }
}

TRouteAttributePolicy RouteAttributePolicy::toThrift() const {
  TRouteAttributePolicy policy;

  // Set statements
  for (const auto& [name, stmt] : statements_) {
    policy.statements()->emplace(name, stmt.toThrift());
  }

  return policy;
}

bool RouteAttributePolicy::match(const RibEntry& route) const {
  for (const auto& [name, stmt] : statements_) {
    if (stmt.match(route)) {
      return true;
    }
  }
  return false;
}

std::optional<std::string> RouteAttributePolicy::matchStatement(
    const RibEntry& route) const {
  // Check prefix matchers first
  const auto& prefix = route.getPrefix();
  for (const auto& name : prefixMatcherStatements_) {
    const auto& stmt = statements_.at(name);
    if (stmt.isActive() && stmt.prefixMatches(prefix)) {
      return name;
    }
  }

  /*
   * Check community matchers. Statements are mutually exclusive, so a
   * community-set matches at most one statement. Cache the first *active*
   * matching statement per interned community-set (or nullopt), mirroring the
   * isActive() filter in the prefix loop above. isActive() is re-checked at
   * lookup so an entry stops matching once its statement expires by wall-clock;
   * a statement reactivated by an expiration extension takes the selective
   * migration path, which rebuilds this index, so a cached nullopt never goes
   * stale within a policy generation.
   *
   * Selection returns the statement of the first path (getAllPaths order) whose
   * community-set maps to an active statement, assuming a route's paths do not
   * carry community-sets mapping to *different* statements (else the choice is
   * path-order dependent, as the pre-index code was statement-order dependent).
   */
  for (const auto& path : route.getAllPaths()) {
    const auto& key =
        path->attrs->getFields()->attrs.get().communities.getSharedPtr();
    auto it = communityToStatement_.find(key);
    if (it == communityToStatement_.end()) {
      RibStats::STATS_raPolicyCommunityIndexMiss.add(1);
      std::optional<std::string> matched;
      for (const auto& [name, stmt] : statements_) {
        if (stmt.hasCommunityMatch() && stmt.isActive() &&
            stmt.communityMatchesPath(path)) {
          matched = name;
          break;
        }
      }
      it = communityToStatement_.emplace(key, matched).first;
    } else {
      RibStats::STATS_raPolicyCommunityIndexHit.add(1);
    }
    if (it->second.has_value()) {
      /*
       * The index value is a statement NAME. moveIndices() swaps this index
       * into a new policy assuming the name set is unchanged; if that contract
       * is ever violated the name may be absent from statements_. Treat a
       * missing statement as a non-match rather than throwing std::out_of_range
       * and aborting the RIB walk.
       */
      auto stmtIt = statements_.find(*it->second);
      if (stmtIt != statements_.end() && stmtIt->second.isActive()) {
        return *it->second;
      }
    }
  }

  return std::nullopt;
}

bool RouteAttributePolicy::overwriteRouteAttributes(
    RibEntry& route,
    RouteAttributePolicy::RibChange& change) const {
  const auto& prefix = route.getPrefix();

  // 1. Check cache first (O(1) lookup)
  auto cachedItem = matchCache_->find(prefix);
  if (cachedItem != matchCache_->end()) {
    RibStats::STATS_raPolicyCacheHit.add(1);
    if (!cachedItem->second) {
      /*
       * Negative cache hit - this prefix was previously evaluated and matched
       * no statement. If we're hitting this code path, the cache entry is still
       * valid (affected prefixes have their cache entries invalidated during
       * migration).
       */
      return false;
    } else {
      // Positive cache hit - use cached statement
      auto stmtIt = statements_.find(cachedItem->second->getStatementName());
      if (stmtIt == statements_.end() || !stmtIt->second.isActive()) {
        /*
         * Statement missing or expired - treat as no match. A missing statement
         * can only arise if moveCache() migrated this entry into a policy whose
         * statement-name set differs (contract violation); degrade gracefully
         * instead of throwing std::out_of_range.
         */
        return false;
      }
      const auto& stmt = stmtIt->second;
      if (stmt.updateAttribute(route)) {
        change.updatedRoutes.emplace(prefix);
        XLOGF(
            DBG2,
            "RibPolicy updated the route (cache hit) {}",
            folly::IPAddress::networkToString(prefix));
      }
      return true;
    }
  }

  // 2. Cache miss - use inverted index to find matched statement
  RibStats::STATS_raPolicyCacheMiss.add(1);
  auto matchedStmtName = matchStatement(route);

  if (matchedStmtName.has_value()) {
    // Found match - cache it
    matchCache_->emplace(prefix, *matchedStmtName);

    const auto& stmt = statements_.at(*matchedStmtName);
    if (stmt.updateAttribute(route)) {
      change.updatedRoutes.emplace(prefix);
      XLOGF(
          DBG2,
          "RibPolicy updated the route {}",
          folly::IPAddress::networkToString(prefix));
    }
    return true;
  }

  // 3. No match - cache negative result
  matchCache_->emplace(prefix, std::nullopt);
  return false;
}

std::optional<rib_policy::TRouteAttributeUcmpAction>
RouteAttributePolicy::getActiveCteUcmpAction(
    const folly::CIDRNetwork& prefix) const {
  auto cachedItem = matchCache_->find(prefix);
  if (cachedItem == matchCache_->end() || !cachedItem->second) {
    return std::nullopt;
  }
  const auto& stmtName = cachedItem->second->getStatementName();
  auto stmtIt = statements_.find(stmtName);
  if (stmtIt == statements_.end() || !stmtIt->second.isActive()) {
    return std::nullopt;
  }
  auto tStmt = stmtIt->second.toThrift();
  const auto& tActions = *tStmt.actions();
  if (tActions.set_ucmp_weights().has_value()) {
    return *tActions.set_ucmp_weights();
  }
  return std::nullopt;
}

/*
 * NextHopWeightAction
 */

/**
 * Get TNextHopWeightAction object, corresponding to this instance, in thrift
 * format
 */
rib_policy::TNextHopWeightAction NextHopWeightAction::toThrift() const {
  return tNexthopWeightAction_;
}

/**
 * Fetch a collection of path mathers contained in thrift style next-hop action.
 *
 * @param action Thrift structure specified in the policy.
 *
 * @return A vector of path matchers.
 * @note It's a logical or operation among different path matchers.
 */
std::vector<std::unique_ptr<BgpPathMatcher>>
NextHopWeightAction::getPathMatchers(const TNextHopWeightAction& action) {
  std::vector<std::unique_ptr<BgpPathMatcher>> pathMatchers{};
  if (action.path_matchers()->empty()) {
    throw BgpError("missing path matchers in next-hop weight action");
  }
  for (const auto& matcher : *action.path_matchers()) {
    if (matcher.prefer_lowest_igp_cost().value_or(false)) {
      throw BgpError(
          "prefer_lowest_igp_cost is only supported for path selection");
    }
    pathMatchers.emplace_back(std::make_unique<BgpPathMatcher>(matcher));
  }
  return pathMatchers;
}

/**
 * Evaluate if a BGP path/route matches the next-hop as specified in policy.
 *
 * @param path The BGP path/route to evalaute
 *
 * @return A pair composed of a bool and an unsigned int. Bool value represents
 * if match succeeded, while the unsigned int represents the next-hop weight to
 * be applied.
 */
std::pair<bool, uint32_t> NextHopWeightAction::Match(
    const std::shared_ptr<RouteInfo>& path) const {
  for (const auto& matcher : nexthopPathMatchers_) {
    if (matcher->match(path)) {
      return std::make_pair(true, nexthopUcmpWeight_);
    }
  }
  return std::make_pair(false, nexthopUcmpWeight_);
}

/*
 * PathSelectionStatement
 */
TPathSelectionStatement PathSelectionStatement::toThrift() const {
  TPathSelectionStatement tStmt;
  tStmt.matcher() = matcher_.toThrift();
  tStmt.multi_path_selector() = pathSelector_.toThrift();
  return tStmt;
}

std::vector<std::shared_ptr<RouteInfo>>
PathSelectionStatement::overrideMultipathSelection(
    const std::vector<std::shared_ptr<RouteInfo>>& paths,
    const std::unique_ptr<RouteInfoSelector>& multipathSelector,
    PathSelectionPolicyResult& result) const {
  return pathSelector_.overrideMultipathSelection(
      paths, multipathSelector, result);
}

/*
 * PathSelectionPolicy
 */
PathSelectionPolicy::PathSelectionPolicy(const TPathSelectionPolicy& policy) {
  for (const auto& [name, tStmt] : *policy.statements()) {
    statements_.emplace(name, tStmt);
  }
  version_ = *policy.version();
}

TPathSelectionPolicy PathSelectionPolicy::toThrift() const {
  TPathSelectionPolicy policy;

  policy.version() = version_;

  // Set statements
  for (const auto& [name, stmt] : statements_) {
    policy.statements()->emplace(name, stmt.toThrift());
  }

  return policy;
}

bool PathSelectionPolicy::match(const RibEntry& route) const {
  for (const auto& [name, stmt] : statements_) {
    if (stmt.match(route)) {
      return true;
    }
  }
  return false;
}

std::vector<std::shared_ptr<RouteInfo>>
PathSelectionPolicy::overrideMultipathSelection(
    const RibEntry& route,
    const std::vector<std::shared_ptr<RouteInfo>>& paths,
    const std::unique_ptr<RouteInfoSelector>& multipathSelector) {
  auto& prefix = route.getPrefix();
  auto cachedItem = pathSelectionResults_.find(prefix);
  if (cachedItem != pathSelectionResults_.end()) {
    // cache hit: we've matched this prefix before
    auto& pathSelectionResult = cachedItem->second;
    if (pathSelectionResult.has_value()) {
      /*
       * use the cached statement to process paths, which could have changed so
       * matched criteria might be different
       */
      return statements_.at(pathSelectionResult->getStatementName())
          .overrideMultipathSelection(
              paths, multipathSelector, *pathSelectionResult);
    } else {
      return multipathSelector->selectRoutes(paths);
    }
  }

  for (const auto& [name, stmt] : statements_) {
    if (stmt.match(route)) {
      XLOGF(
          DBG1,
          "Statement {} matches route {}",
          name,
          folly::IPAddress::networkToString(prefix));
      auto [newCachedItem, _] = pathSelectionResults_.emplace(prefix, name);
      return stmt.overrideMultipathSelection(
          paths, multipathSelector, *(newCachedItem->second));
    }
  }

  pathSelectionResults_.emplace(prefix, std::nullopt);
  // apply the default multipath selector
  return multipathSelector->selectRoutes(paths);
}

std::vector<TPathSelector> PathSelectionPolicy::getActivePathSelectionCriteria(
    const std::vector<std::string>& prefixes) const {
  std::vector<TPathSelector> activePathSelectionCriteria;
  for (const auto& prefixStr : prefixes) {
    auto prefix = folly::IPAddress::createNetwork(prefixStr);
    auto cachedItem = pathSelectionResults_.find(prefix);
    if (cachedItem == pathSelectionResults_.end() || !cachedItem->second) {
      // no such prefix or no statement matched: insert an empty TPathSelector
      activePathSelectionCriteria.emplace_back();
      continue;
    }
    const auto& pathSelectionResult = *cachedItem->second;
    TPathSelector tPathSelector;
    if (pathSelectionResult.activeCriteria) {
      tPathSelector.criteria_list()->emplace_back(
          pathSelectionResult.activeCriteria->toThrift());
    } else {
      const auto& stmt = statements_.at(pathSelectionResult.getStatementName());
      if (stmt.getBgpNativeMinNexthop().has_value()) {
        tPathSelector.bgp_native_path_selection_min_nexthop() =
            *stmt.getBgpNativeMinNexthop();
      }
      /*
       * The drain flag now covers both the MNH and aggregate-LBW thresholds,
       * so it must be emitted independently of which threshold is configured.
       * Otherwise a statement with only bgp_min_aggregate_lbw_bps would lose
       * the flag on round-trip. Mirrors PathSelector::toThrift().
       */
      const auto& drain = stmt.getDrainOnMinCapacityThresholdViolation();
      if (drain.has_value()) {
        tPathSelector.drain_on_min_nexthop_violation() = *drain;
      }
      if (stmt.getBgpNativeMinAggLbwbps().has_value()) {
        tPathSelector.bgp_min_aggregate_lbw_bps() =
            *stmt.getBgpNativeMinAggLbwbps();
        tPathSelector.relax_bgp_min_aggregate_lbw_bps() =
            stmt.getRelaxBgpNativeMinAggLbwbps().value_or(false);
      }
    }
    activePathSelectionCriteria.emplace_back(std::move(tPathSelector));
  }

  return activePathSelectionCriteria;
}

/*
 * RouteFilterPolicy
 */

TRouteFilter RouteFilter::toThrift() const {
  TRouteFilter filter;
  if (prefixList_) {
    filter.prefix_list() = *prefixList_;
  }
  if (permissiveMode_) {
    filter.permissive_mode() = *permissiveMode_;
  }
  return filter;
}

std::tuple<std::vector<folly::CIDRNetwork>, std::vector<folly::CIDRNetwork>>
RouteFilter::applyFilter(
    const std::vector<folly::CIDRNetwork>& prefixes) const {
  if (!prefixTree_) {
    return {prefixes, {}};
  }
  bool permissiveAllow = permissiveMode_ && *permissiveMode_;
  std::vector<folly::CIDRNetwork> allowedPrefixes;
  std::vector<folly::CIDRNetwork> rejectedPrefixes;
  for (const auto& prefix : prefixes) {
    if (prefixTree_->Match(prefix)) {
      if (!permissiveAllow) {
        // don't bother populating allowed list in allowAll mode
        allowedPrefixes.push_back(prefix);
      }
    } else {
      rejectedPrefixes.push_back(prefix);
    }
  }
  if (permissiveAllow) {
    return {prefixes, rejectedPrefixes};
  } else {
    return {allowedPrefixes, rejectedPrefixes};
  }
}

TRouteFilterStatement RouteFilterStatement::toThrift() const {
  TRouteFilterStatement tStmt;
  tStmt.ingress_filter() = ingressFilter_.toThrift();
  tStmt.egress_filter() = egressFilter_.toThrift();
  return tStmt;
}

std::tuple<std::vector<folly::CIDRNetwork>, std::vector<folly::CIDRNetwork>>
RouteFilterStatement::applyIngressFilter(
    const std::vector<folly::CIDRNetwork>& prefixes) const {
  return ingressFilter_.applyFilter(prefixes);
}

std::tuple<std::vector<folly::CIDRNetwork>, std::vector<folly::CIDRNetwork>>
RouteFilterStatement::applyEgressFilter(
    const std::vector<folly::CIDRNetwork>& prefixes) const {
  return egressFilter_.applyFilter(prefixes);
}

bool RouteFilterStatement::hasIngressFilter() const {
  return ingressFilter_.isPrefixListSet() ||
      ingressFilter_.isPermissiveModeSet();
}

bool RouteFilterStatement::hasEgressFilter() const {
  return egressFilter_.isPrefixListSet() || egressFilter_.isPermissiveModeSet();
}

const RouteFilter& RouteFilterStatement::getIngressFilter() const {
  return ingressFilter_;
}

const RouteFilter& RouteFilterStatement::getEgressFilter() const {
  return egressFilter_;
}

TGoldenPrefixPolicy GoldenPrefixPolicy::toThrift() const {
  TGoldenPrefixPolicy policy;
  if (prefixList_) {
    policy.allowed_prefixes() = *prefixList_;
  }
  return policy;
}

GoldenPrefixSubnetCountingTree::GoldenPrefixSubnetCountingTree(
    const routing_policy::PrefixList& tPrefixList) {
  for (const auto& tPrefixListEntry : *tPrefixList.prefixes()) {
    folly::CIDRNetwork prefix;

    // Validate config.
    try {
      prefix = folly::IPAddress::createNetwork(*tPrefixListEntry.base_prefix());
    } catch (folly::IPAddressFormatException&) {
      auto msg = fmt::format(
          "Malformed base prefix in config: {}",
          *tPrefixListEntry.base_prefix());
      XLOGF(ERR, "{}", msg);
      throw BgpError(msg);
    }
    if (!tPrefixListEntry.max_allowed_golden_prefix_subnet_count()) {
      auto msg = fmt::format(
          "Missing required field max_allowed_golden_prefix_subnet_count for golden prefix: {}",
          *tPrefixListEntry.base_prefix());
      XLOGF(ERR, "{}", msg);
      throw BgpError(msg);
    }
    if (const auto existingEntry =
            parentPrefixRadixTree_.exactMatch(prefix.first, prefix.second);
        !existingEntry.atEnd()) {
      if (*tPrefixListEntry.max_allowed_golden_prefix_subnet_count() !=
          existingEntry.value().maxSubnetCount_) {
        auto msg = fmt::format(
            "Found mismatched max_allowed_golden_prefix_subnet_count for prefix {}. max_allowed_golden_prefix_subnet_count must be the same for each entry.",
            *tPrefixListEntry.base_prefix());
        XLOGF(ERR, "{}", msg);
        throw BgpError(msg);
      }
    } else {
      // Insert prefix into radix tree.
      parentPrefixRadixTree_.insert(
          prefix.first,
          prefix.second,
          ParentPrefixTreeNode{
              *tPrefixListEntry.max_allowed_golden_prefix_subnet_count(),
              parseCommunities(tPrefixListEntry)});
    }
  }
}

std::optional<std::set<BgpAttrCommunityC>>
GoldenPrefixSubnetCountingTree::parseCommunities(
    const routing_policy::PrefixListEntry& tPrefixListEntry) {
  if (!tPrefixListEntry.communities().has_value()) {
    return std::nullopt;
  }
  std::set<BgpAttrCommunityC> comms{};
  for (auto commStr : *tPrefixListEntry.communities()) {
    auto comm = BgpAttrCommunityC::createBgpAttrCommunity(commStr);
    if (!comm) {
      auto msg = fmt::format(
          "Unable to parse community {} for prefix {}",
          commStr,
          *tPrefixListEntry.base_prefix());
      XLOGF(ERR, "{}", msg);
      throw BgpError(msg);
    }
    comms.emplace(*comm);
  }
  return comms;
}

bool GoldenPrefixSubnetCountingTree::allowPrefix(
    const folly::CIDRNetwork& prefix,
    const BgpPath& attrs) const {
  auto match = parentPrefixRadixTree_.longestMatch(prefix.first, prefix.second);
  if (match.atEnd()) {
    /*
     * No matching golden parent prefix found => disallow.
     * Note: this should never happen, since we expect to only check the subnet
     * counting tree if we've already found a matching parent in the PrefixTree.
     */
    return false;
  }
  const auto& node = match.value();

  /*
   * Subnet check passes if either:
   * 1. The subnet is already known
   * 2. The subnet is new and the number of subnets is below the limit
   */
  const bool subnetCheck = node.subnets_.contains(prefix) ||
      node.subnets_.size() < node.maxSubnetCount_;
  if (!subnetCheck) {
    XLOGF_EVERY_MS(
        WARN,
        5000,
        "Subnet limit of {} reached for golden prefix {}. Rejecting new subnet {}",
        node.maxSubnetCount_,
        match.ipAddress().str(),
        folly::IPAddress::networkToString(prefix));
    return false;
  }

  /*
   * Community check passes if either:
   * 1. Golden communities are not required for this prefix
   * 2. At least one of the prefix's (prepolicy) communities matches one of the
   * specified golden communities
   */
  if (!node.communities_) {
    // No golden communities required => allow.
    return true;
  }
  for (const auto& comm : attrs.getCommunities().get()) {
    if (node.communities_->contains(comm)) {
      // Golden community found => allow.
      return true;
    }
  }
  // No golden community found => disallow.
  return false;
}

void GoldenPrefixSubnetCountingTree::incrementSubnet(
    const folly::CIDRNetwork& prefix) {
  auto match = parentPrefixRadixTree_.longestMatch(prefix.first, prefix.second);
  if (match.atEnd()) {
    XLOGF(
        WARNING,
        "No matching prefix found for subnet {}",
        folly::IPAddress::networkToString(prefix));
    return;
  }
  auto& node = match.value();
  if (!node.subnets_.contains(prefix)) {
    XLOGF(
        DBG2,
        "Inserting new subnet {}",
        folly::IPAddress::networkToString(prefix));
    node.subnets_[prefix] = 0;
  }
  node.subnets_[prefix]++;
}

void GoldenPrefixSubnetCountingTree::decrementSubnet(
    const folly::CIDRNetwork& prefix) {
  auto match = parentPrefixRadixTree_.longestMatch(prefix.first, prefix.second);
  if (match.atEnd()) {
    XLOGF(
        WARNING,
        "No matching prefix found for subnet {}",
        folly::IPAddress::networkToString(prefix));
    return;
  }
  auto& node = match.value();
  if (--node.subnets_[prefix] <= 0) {
    XLOGF(
        DBG2, "Removing subnet {}", folly::IPAddress::networkToString(prefix));
    node.subnets_.erase(prefix);
  }
}

std::map<std::string, int> GoldenPrefixSubnetCountingTree::getSubnetCounts()
    const {
  std::map<std::string, int> subnetCounts;
  for (auto it = parentPrefixRadixTree_.begin(); !it.atEnd(); it++) {
    subnetCounts[it.ipAddress().str()] = it.value().subnets_.size();
  }
  return subnetCounts;
}

std::map<std::string, int> GoldenPrefixPolicy::getSubnetCounts() const {
  if (!goldenPrefixSubnetCountingTree_) {
    return {};
  }
  return goldenPrefixSubnetCountingTree_->getSubnetCounts();
}

bool GoldenPrefixPolicy::allowPrefix(
    const folly::CIDRNetwork& prefix,
    const BgpPath& attrs) const {
  if (!prefixTree_ || !goldenPrefixSubnetCountingTree_) {
    /*
     * nothing is blocked if prefix list is null
     * this is different from an empty list which blocks everything
     */
    return true;
  }
  return prefixTree_->Match(prefix) &&
      goldenPrefixSubnetCountingTree_->allowPrefix(prefix, attrs);
}

std::vector<folly::CIDRNetwork> GoldenPrefixPolicy::applyFilter(
    const std::vector<folly::CIDRNetwork>& prefixes,
    const BgpPath& attrs) const {
  std::vector<folly::CIDRNetwork> rejectedPrefixes;
  for (const auto& prefix : prefixes) {
    if (!allowPrefix(prefix, attrs)) {
      rejectedPrefixes.push_back(prefix);
    }
  }
  return rejectedPrefixes;
}

void GoldenPrefixPolicy::incrementSubnet(const folly::CIDRNetwork& prefix) {
  if (goldenPrefixSubnetCountingTree_) {
    goldenPrefixSubnetCountingTree_->incrementSubnet(prefix);
  }
}

void GoldenPrefixPolicy::decrementSubnet(const folly::CIDRNetwork& prefix) {
  if (goldenPrefixSubnetCountingTree_) {
    goldenPrefixSubnetCountingTree_->decrementSubnet(prefix);
  }
}

RouteFilterPolicy::RouteFilterPolicy(const TRouteFilterPolicy& policy) {
  keyType_ = policy.key_type().to_optional();

  for (const auto& [name, tStmt] : *policy.statements()) {
    /*
     * verify statement name is a valid regex only if keyType_ is empty or
     * DEVICE_REGEX
     */
    if (!keyType_.has_value() || *keyType_ == KeyType::DEVICE_REGEX) {
      re2::RE2 regex(name);
      if (!regex.ok()) {
        throw BgpError(
            fmt::format(
                "invalid statement name: {}, not a valid regex: {}",
                name,
                regex.error()));
      }
    }
    auto stmt = std::make_shared<RouteFilterStatement>(tStmt);
    statements_.emplace(name, std::move(stmt));
  }
  version_ = *policy.version();
  if (policy.golden_prefix_policy()) {
    goldenPrefixPolicy_ =
        std::make_shared<GoldenPrefixPolicy>(*policy.golden_prefix_policy());
  }
}

TRouteFilterPolicy RouteFilterPolicy::toThrift() const {
  TRouteFilterPolicy policy;

  // Set statements
  for (const auto& [name, stmt] : statements_) {
    policy.statements()->emplace(name, stmt->toThrift());
  }

  policy.version() = version_;

  if (goldenPrefixPolicy_) {
    policy.golden_prefix_policy() = goldenPrefixPolicy_->toThrift();
  }

  if (keyType_) {
    policy.key_type() = *keyType_;
  }

  return policy;
}

/*
 * RibPolicy
 */
RibPolicy::RibPolicy(const TRibPolicy& policy)
    : routeAttributePolicy_(policy.route_attribute_policy().to_optional()),
      pathSelectionPolicy_(policy.path_selection_policy().to_optional()),
      routeFilterPolicy_(policy.route_filter_policy().to_optional()) {
  if (!pathSelectionPolicy_ && !routeAttributePolicy_ && !routeFilterPolicy_) {
    throw BgpError("missing rib policy statement");
  }
}

TRibPolicy RibPolicy::toThrift() const {
  TRibPolicy policy;

  if (routeAttributePolicy_) {
    policy.route_attribute_policy() = routeAttributePolicy_->toThrift();
  }
  if (pathSelectionPolicy_) {
    policy.path_selection_policy() = pathSelectionPolicy_->toThrift();
  }
  if (routeFilterPolicy_) {
    policy.route_filter_policy() = routeFilterPolicy_->toThrift();
  }

  return policy;
}

} // namespace facebook::bgp
