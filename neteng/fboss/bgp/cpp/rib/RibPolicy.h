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

#pragma once

#include "neteng/fboss/bgp/cpp/rib/RibEntry.h"

#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/rib_policy_types.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyMatch.h"

namespace facebook::bgp {

using nettools::bgplib::BgpAttrCommunityC;

/**
 * Represents `rib_policy::TBgpPathMatcher`.
 */
class BgpPathMatcher {
 public:
  explicit BgpPathMatcher(const rib_policy::TBgpPathMatcher& matcher);

  /**
   * Check if the path satisfies all the matches
   */
  bool match(const std::shared_ptr<RouteInfo>& path) const;

  bool prefersLowestIgpCost() const {
    return prefersLowestIgpCost_;
  }

 private:
  std::vector<std::unique_ptr<AttributesMatch>> matches_{};
  const bool prefersLowestIgpCost_{false};

/*
 * per class placeholder for code injection
 * only need to setup once
 */
#ifdef BgpPathMatcher_TEST_FRIENDS
  BgpPathMatcher_TEST_FRIENDS
#endif
};

class RibPolicyResultBase {
 public:
  explicit RibPolicyResultBase(const std::string& stmtName)
      : statementName_(stmtName) {}

  const std::string& getStatementName() const {
    return statementName_;
  }

 private:
  std::string statementName_;
};

/**
 * Represents `rib_policy::TPathSelectionCriteria`.
 *
 * instead of keeping the raw specifications, transform the criteria into
 * RouteInfoSelector for RibEntry::selectBestPath
 */
class PathSelectionCriteria {
 public:
  explicit PathSelectionCriteria(
      const rib_policy::TPathSelectionCriteria& criteria);

  bool operator==(const PathSelectionCriteria& other) const {
    return tCriteria_ == other.tCriteria_;
  }
  bool operator!=(const PathSelectionCriteria& other) const {
    return !(*this == other);
  }

  /**
   * Get PathSelectionCriteria object in thrift format
   */
  rib_policy::TPathSelectionCriteria toThrift() const;

  /**
   * Try to override the multipath selection logic to filter the paths. This API
   * apply action on the paths.
   *
   * @returns selected paths, an empty list means current paths does NOT meet
   * this criteria and move to the next Criteria
   */
  std::vector<std::shared_ptr<RouteInfo>> tryOverrideMultipathSelection(
      const std::vector<std::shared_ptr<RouteInfo>>& paths) const;

 private:
  static std::vector<std::unique_ptr<BgpPathMatcher>> getPathMatchers(
      const rib_policy::TPathSelectionCriteria& criteria);

  // save for easier retrieve
  const rib_policy::TPathSelectionCriteria tCriteria_;

  // Here pointers are needed so that we could perform polymorphism
  const std::vector<std::unique_ptr<BgpPathMatcher>> pathMatchers_{};
  const bool hasLowestIgpCostMatcher_{false};
  const std::optional<int32_t> minNexthop_{std::nullopt};

/*
 * per class placeholder for code injection
 * only need to setup once
 */
#ifdef PathSelectionCriteria_TEST_FRIENDS
  PathSelectionCriteria_TEST_FRIENDS
#endif
};

class PathSelectionPolicyResult : public RibPolicyResultBase {
 public:
  enum class Outcome {
    // paths are selected directly by CPS path selector
    CPS,
    // paths are selected by BGP native selector but rejected by CPS min nexthop
    BGP_FAILED_CPS_MIN_NEXTHOP,
    // paths are selected by BGP native selector but rejected by CPS min agg lbw
    BGP_FAILED_CPS_MIN_AGG_LBW,
    // paths are selected by BGP native selector with no CPS influence
    BGP
  };

  explicit PathSelectionPolicyResult(const std::string& stmtName)
      : RibPolicyResultBase(stmtName) {}

  /*
   * True when the outcome indicates a CPS capacity-threshold violation —
   * either insufficient nexthops (MIN_NEXTHOP) or insufficient aggregate
   * link bandwidth (MIN_AGG_LBW).
   */
  bool isCapacityThresholdViolation() const noexcept {
    return outcome == Outcome::BGP_FAILED_CPS_MIN_NEXTHOP ||
        outcome == Outcome::BGP_FAILED_CPS_MIN_AGG_LBW;
  }

  /*
   * True when the result is a CPS capacity-threshold violation that should
   * cause bestpath nullification. When drainOnMinCapacityThresholdViolation
   * is set, partial drain takes effect instead and this returns false even
   * though the underlying outcome is a violation.
   */
  bool isFailedCpsNativeCriteria() const noexcept {
    return isCapacityThresholdViolation() &&
        !drainOnMinCapacityThresholdViolation;
  }

  const PathSelectionCriteria* activeCriteria{nullptr};
  Outcome outcome;
  bool drainOnMinCapacityThresholdViolation{false};
  /*
   * Min-nexthop threshold (count) captured when
   * drainOnMinCapacityThresholdViolation is set via the MNH branch of
   * overrideMultipathSelection. 0 when not applicable (LBW branch or no
   * drain). Surfaced as TMinCapacityThreshold.mnh on
   * TPartiallyDrainedPrefix.
   */
  int32_t mnhThreshold{0};
  /*
   * Aggregate LBW threshold (bps) captured when
   * drainOnMinCapacityThresholdViolation is set via the LBW branch of
   * overrideMultipathSelection. 0 when not applicable (MNH branch or no
   * drain). Surfaced as TMinCapacityThreshold.agg_lbw_bps on
   * TPartiallyDrainedPrefix.
   */
  int64_t aggLbwBpsThreshold{0};
};

/**
 * Represents `rib_policy::TPathSelector`.
 */
class PathSelector {
 public:
  explicit PathSelector(const rib_policy::TPathSelector& selector)
      : centralizedCriteriaList_(getCentralizedCriteriaList(selector)),
        bgpNativeMinNexthop_(
            selector.bgp_native_path_selection_min_nexthop().to_optional()),
        drainOnMinCapacityThresholdViolation_(
            selector.drain_on_min_nexthop_violation().to_optional()),
        bgpNativeMinAggLbwbps_(
            selector.bgp_min_aggregate_lbw_bps().to_optional()),
        relaxBgpNativeMinAggLbwbps_(
            selector.relax_bgp_min_aggregate_lbw_bps().to_optional()) {}

  bool operator==(const PathSelector& other) const;
  bool operator!=(const PathSelector& other) const {
    return !(*this == other);
  }

  /**
   * Get PathSelector object in thrift format
   */
  rib_policy::TPathSelector toThrift() const;

  /**
   * Override the multipath selection logic to filter the paths. This API apply
   * action on the paths.
   *
   * @returns a pair of
   *   selected paths, an empty list would lead to the drop of the route, and
   *   a pointer to matched criteria
   */
  std::vector<std::shared_ptr<RouteInfo>> overrideMultipathSelection(
      const std::vector<std::shared_ptr<RouteInfo>>& paths,
      const std::unique_ptr<RouteInfoSelector>& multipathSelector,
      PathSelectionPolicyResult& result) const;

  const std::optional<int32_t>& getBgpNativeMinNexthop() const {
    return bgpNativeMinNexthop_;
  }

  const std::optional<bool>& getDrainOnMinCapacityThresholdViolation() const {
    return drainOnMinCapacityThresholdViolation_;
  }

  const std::optional<int64_t>& getBgpNativeMinAggLbwbps() const {
    return bgpNativeMinAggLbwbps_;
  }

  const std::optional<bool>& getRelaxBgpNativeMinAggLbwbps() const {
    return relaxBgpNativeMinAggLbwbps_;
  }

 private:
  static std::vector<std::unique_ptr<PathSelectionCriteria>>
  getCentralizedCriteriaList(const rib_policy::TPathSelector& selector);

  /*
   * the list of centralized criteria list
   * We need to use unique_ptr<PathSelectionCriteria> here instead of
   * PathSelectionCriteria as vector could grow when emplacing new criteria at
   * the back, which involves moving the BgpPathMatcher (unique_ptr).
   */
  const std::vector<std::unique_ptr<PathSelectionCriteria>>
      centralizedCriteriaList_{};

  // the additional Bgp criterion
  const std::optional<int32_t> bgpNativeMinNexthop_{std::nullopt};
  const std::optional<bool> drainOnMinCapacityThresholdViolation_{std::nullopt};
  const std::optional<int64_t> bgpNativeMinAggLbwbps_{std::nullopt};
  const std::optional<bool> relaxBgpNativeMinAggLbwbps_{std::nullopt};

/*
 * per class placeholder for code injection
 * only need to setup once
 */
#ifdef PathSelector_TEST_FRIENDS
  PathSelector_TEST_FRIENDS
#endif
};

/**
 * Represents `rib_policy::TRibRouteMatcher`. Implements the route matcher.
 */
class RibPolicyRouteMatcher {
 public:
  explicit RibPolicyRouteMatcher(const rib_policy::TRibRouteMatcher& matcher);

  bool operator==(const RibPolicyRouteMatcher& other) const {
    return tMatcher_ == other.tMatcher_;
  }
  bool operator!=(const RibPolicyRouteMatcher& other) const {
    return !(*this == other);
  }

  /**
   * Get RibRouteMatcher object in thrift format
   */
  rib_policy::TRibRouteMatcher toThrift() const;

  /**
   * Checks if route qualifies the match criteria
   */
  bool match(const RibEntry& route) const;

  bool hasCommunityMatch() const {
    return communityMatch_.has_value();
  }

  bool hasPrefixSet() const {
    return !prefixSet_.empty();
  }

  bool communityMatchesPath(const std::shared_ptr<RouteInfo>& path) const {
    return communityMatch_.has_value() && communityMatch_->Match(path->attrs);
  }

  bool prefixMatches(const folly::CIDRNetwork& prefix) const {
    return prefixSet_.contains(prefix);
  }

 private:
  static std::unordered_set<folly::CIDRNetwork> getPrefixSet(
      const rib_policy::TRibRouteMatcher& matcher);

  bool matchPrefix(const RibEntry& route) const;

  bool matchCommunity(const RibEntry& route) const;

  // save for easier retrieve
  const rib_policy::TRibRouteMatcher tMatcher_;

  /*
   * Unordered set for efficient lookup on matching
   * NOTE: The matching requires the same prefix representation (fully
   * qualified)
   */
  const std::unordered_set<folly::CIDRNetwork> prefixSet_{};

  const std::optional<CommunityMatch> communityMatch_{std::nullopt};
};

/**
 * Representation of rib_policy::TNextHopWeightAction.
 *
 * This encapsulates a mechanism to identify a next-hop and the weight
 * to be applied to the next-hop. The next-hop is identify by matching on
 * BGP attributes (viz., AS-path regex, community, AS-path len or origin)
 * of the route that contributed the next-hop.
 */
class NextHopWeightAction {
 public:
  /**
   * Construct instance of NextHopWeightAction.
   *
   * @param action Thrift structure specified in the policy.
   */
  explicit NextHopWeightAction(const rib_policy::TNextHopWeightAction& action)
      : tNexthopWeightAction_(action),
        nexthopPathMatchers_(getPathMatchers(action)),
        nexthopUcmpWeight_(*action.weight()) {}

  /**
   * Equality comparison operator.
   *
   * Compare the current object with the other object for equality.
   *
   * @param other The object to compare with.
   *
   * @return True if the objects are equal, false otherwise.
   */
  bool operator==(const NextHopWeightAction& other) const {
    return (tNexthopWeightAction_ == other.tNexthopWeightAction_);
  }

  /**
   * Inequality comparison operator.
   *
   * Compare the current object with the other object for inequality.
   *
   * @param other The object to compare with.
   *
   * @return True if the objects are not equal, false otherwise.
   */
  bool operator!=(const NextHopWeightAction& other) const {
    return !(*this == other);
  }

  /**
   * Get TNextHopWeightAction object, corresponding to this instance, in thrift
   * format
   */
  rib_policy::TNextHopWeightAction toThrift() const;

  /**
   * Evaluate if a BGP path/route matches the next-hop as specified in policy.
   *
   * @param path The BGP path/route to evalaute
   *
   * @return A pair composed of a bool and an unsigned int. Bool value
   * represents if match succeeded, while the unsigned int represents the
   * next-hop weight to be applied.
   */
  std::pair<bool, uint32_t> Match(const std::shared_ptr<RouteInfo>& path) const;

 private:
  /**
   * Fetch a collection of path mathers contained in thrift style next-hop
   * action.
   *
   * @param action Thrift structure specified in the policy.
   *
   * @return A vector of path matchers.
   * @note It's a logical or operation among different path matchers.
   */
  static std::vector<std::unique_ptr<BgpPathMatcher>> getPathMatchers(
      const rib_policy::TNextHopWeightAction& action);

  /**
   * TNextHopWeightAction thrift struct, from which this instance is created. It
   * is saved during initialization for easy retrieval.
   */
  const rib_policy::TNextHopWeightAction tNexthopWeightAction_;

  /**
   * Collection of path matchers (matching on BGP attributes viz., AS-path
   * regex, community, AS-path len or origin to identify a next-hop.
   */
  const std::vector<std::unique_ptr<BgpPathMatcher>> nexthopPathMatchers_{};

  /**
   * The next-hop weight to use if path match succeeds.
   */
  uint32_t nexthopUcmpWeight_;

  // Grant tests access to private and protected members of this class.
#ifdef NextHopWeightAction_TEST_FRIENDS
  NextHopWeightAction_TEST_FRIENDS
#endif
};

/**
 * Represents `rib_policy::TRouteAttributeActions`.
 */
class RouteAttributeActions {
 public:
  explicit RouteAttributeActions(
      const rib_policy::TRouteAttributeActions& actions);

  bool operator==(const RouteAttributeActions& other) const {
    return (toThrift() == other.toThrift());
  }
  bool operator!=(const RouteAttributeActions& other) const {
    return !(*this == other);
  }

  rib_policy::TRouteAttributeActions toThrift() const;

  /**
   * The weight action that matched a path: its index in the action list and
   * its weight.
   */
  struct MatchedWeight {
    size_t actionIndex{0};
    int32_t weight{0};
  };

  /**
   * Update route attributes.
   *
   * @returns boolean indicating if route is updated or not.
   */
  bool updateAttribute(RibEntry& route) const;

  void moveWeightIndex(const RouteAttributeActions& other) const {
    std::swap(weightIndex_, other.weightIndex_);
  }

 private:
  /**
   * Scan the actions in order and find the first one that matches `path`.
   * Returns its index and weight, or std::nullopt if none matches. Called on
   * a cache miss by findMatchingActionCached().
   */
  std::optional<MatchedWeight> findMatchingAction(
      const std::shared_ptr<RouteInfo>& path) const;

  /**
   * Checks the action cache (weightIndex_) for the matching action for this
   * path's attributes: returns it if present, otherwise calls
   * findMatchingAction() and stores its result. Called by updateUcmpWeight()
   * for each selected path.
   */
  std::optional<MatchedWeight> findMatchingActionCached(
      const std::shared_ptr<RouteInfo>& path) const;

  /**
   * Update the link bandwidth of the route.
   *
   * @returns boolean indicating if route is updated or not.
   */
  bool updateLinkBandwidth(RibEntry& route) const;

  /**
   * Update the UCMP weight of the route
   *
   * @returns boolean indicating if route is updated or not.
   */
  bool updateUcmpWeight(RibEntry& route) const;

  /**
   * Get the collection of next-hop weight actions.
   *
   * @param raAction Route attribute action thrift struct
   *
   * @return Collection of points to next-hop weight action objects.
   */
  static std::vector<std::unique_ptr<NextHopWeightAction>>
  getNexthopWeightActions(
      const rib_policy::TRouteAttributeUcmpAction& raAction);

  const std::optional<int64_t> linkBandwidth_{std::nullopt};

  /**
   * Collection of pointers to next-hop weight actions.
   */
  const std::vector<std::unique_ptr<NextHopWeightAction>>
      nextHopWeightActions_{};

  /**
   * Setting to control BGP's behavior in case of policy next-hops
   * are out of sync with next-hops in the BGP RIB.
   */
  bool strictMatchNexthops_{false};

  /**
   * Behavior to apply when a route has multiple paths matching the same action.
   * - false: weights are assigned as-is to each path. The total weight for the
   *   matching path set is equal to the product of
   *   (configured weight) * (number of paths in the set).
   * - true: weights for each path are divided by the number of paths in the
   *   set, such that the total weight of the path set is equal to the
   *   configured weight.
   */
  std::optional<bool> divideWeightsByMatchingPathCount_;

  /*
   * Inverted index: path attrs -> first matching action (MatchedWeight) or
   * nullopt if no action matches. Keyed on interned BgpPath for dedup across
   * prefixes. Mutable for lazy population in findMatchingActionCached().
   */
  mutable folly::
      F14FastMap<std::shared_ptr<const BgpPath>, std::optional<MatchedWeight>>
          weightIndex_;

  // Grant tests access to private and protected members of this class.
#ifdef RouteAttributeUcmpAction_TEST_FRIENDS
  RouteAttributeUcmpAction_TEST_FRIENDS
#endif
};

/**
 * Represents the route attribute overwrites in
 * `rib_policy::TRouteAttributeStatement`. Implements the transformation
 * criteria via updateAttribute.
 */
class RouteAttributeStatement {
 public:
  explicit RouteAttributeStatement(
      const rib_policy::TRouteAttributeStatement& tStmt)
      : matcher_(*tStmt.matcher()),
        actions_(*tStmt.actions()),
        expirationTime_(tStmt.expiration_time_s().to_optional()) {}

  bool operator==(const RouteAttributeStatement& other) const {
    return (matcher_ == other.matcher_) && (actions_ == other.actions_) &&
        (getExpirationTime() == other.getExpirationTime());
  }
  bool operator!=(const RouteAttributeStatement& other) const {
    return !(*this == other);
  }
  struct ReEvalResult {
    bool changed{false}; // any difference (matcher, actions, or expiration)
    bool needsReEval{false}; // needs re-evaluation of affected prefixes
    bool matcherChanged{false}; // matcher specifically changed
  };

  /**
   * Single-pass comparison returning ReEvalResult.
   * - changed: any difference (matcher, actions, or expiration time)
   * - needsReEval: content changed (matcher/actions) or either expired,
   *   but only when there is actually a change
   * - matcherChanged: matcher specifically changed (requires cache
   *   invalidation rather than preservation during migration)
   */
  ReEvalResult needsReEvaluation(const RouteAttributeStatement& other) const {
    auto now = std::chrono::seconds(std::time(nullptr)).count();
    bool eitherExpired =
        (getExpirationTime() <= now) || (other.getExpirationTime() <= now);
    bool expirationDiffers = (getExpirationTime() != other.getExpirationTime());
    bool matcherDiffers = (matcher_ != other.matcher_);
    bool actionsDiffer = (actions_ != other.actions_);
    bool contentChanged = actionsDiffer || matcherDiffers;
    bool changed = expirationDiffers || contentChanged;
    bool needsReEval = changed && (contentChanged || eitherExpired);
    return {changed, needsReEval, matcherDiffers};
  }

  /**
   * Get RouteAttributeStatement object in thrift format
   */
  rib_policy::TRouteAttributeStatement toThrift() const;

  /**
   * Checks if route qualifies the match criteria for policy.
   */
  bool match(const RibEntry& route) const {
    return matcher_.match(route);
  }
  /**
   * Returns the expiration time in epoch seconds of this policy
   */
  int64_t getExpirationTime() const {
    return expirationTime_.value_or(INT_MAX);
  }

  /**
   * Is the policy statement still active
   */
  bool isActive() const {
    return getExpirationTime() >
        std::chrono::seconds(std::time(nullptr)).count();
  }

  /**
   * Transform route.
   *
   * @returns boolean indicating if route is transformed or not.
   */
  bool updateAttribute(RibEntry& route) const;

  bool hasCommunityMatch() const {
    return matcher_.hasCommunityMatch();
  }

  bool communityMatchesPath(const std::shared_ptr<RouteInfo>& path) const {
    return matcher_.communityMatchesPath(path);
  }

  bool hasPrefixSet() const {
    return matcher_.hasPrefixSet();
  }

  bool prefixMatches(const folly::CIDRNetwork& prefix) const {
    return matcher_.prefixMatches(prefix);
  }

  void moveWeightIndex(const RouteAttributeStatement& other) const {
    actions_.moveWeightIndex(other.actions_);
  }

 private:
  const RibPolicyRouteMatcher matcher_;

  const RouteAttributeActions actions_;

  // in epoch seconds
  const std::optional<int64_t> expirationTime_{std::nullopt};
};

/**
 * Represents `rib_policy::TRouteAttributePolicy`.
 */
class RouteAttributePolicy {
 public:
  explicit RouteAttributePolicy(
      const rib_policy::TRouteAttributePolicy& policy);

  bool operator==(const RouteAttributePolicy& other) const {
    return statements_ == other.statements_;
  }
  bool operator!=(const RouteAttributePolicy& other) const {
    return !(*this == other);
  }
  /**
   * Get RouteAttributePolicy object in thrift format
   */
  rib_policy::TRouteAttributePolicy toThrift() const;

  /**
   * Checks if route qualifies the match criteria for policy. First successful
   * match with a PolicyStatement will be returned.
   */
  bool match(const RibEntry& route) const;

  struct RibChange {
    std::unordered_set<folly::CIDRNetwork> updatedRoutes{};
  };

  /**
   * Calls updateAttribute on the given route and save the changes in
   * change
   *
   * Notice that only one matched statement will be applied. The controller
   * must ensure that each route would not match two statements.
   *
   * @returns false if no statement matches (or matching statement expired),
   * otherwise returns true
   */
  bool overwriteRouteAttributes(RibEntry& route, RibChange& change) const;

  /**
   * Get the active CTE UCMP action for a given prefix, if any.
   * Looks up the match cache to find which statement matched,
   * then returns the UCMP action from that statement's actions.
   *
   * @returns the active TRouteAttributeUcmpAction if a statement matched
   * and has UCMP weights, otherwise std::nullopt
   */
  std::optional<rib_policy::TRouteAttributeUcmpAction> getActiveCteUcmpAction(
      const folly::CIDRNetwork& prefix) const;

  /**
   * @returns most recent expiration time
   */
  int64_t getMostRecentExpirationTime() const {
    int64_t ret = INT_MAX;
    for (const auto& [_, stmt] : statements_) {
      ret = std::min(stmt.getExpirationTime(), ret);
    }
    return ret;
  }

  /**
   * @returns most recent unexpired expiration time
   */
  int64_t getMostRecentActiveExpirationTime() const {
    int64_t ret = INT_MAX;
    auto now = std::chrono::seconds(std::time(nullptr)).count();
    for (const auto& [_, stmt] : statements_) {
      auto expTime = stmt.getExpirationTime();
      if (expTime > now) {
        ret = std::min(expTime, ret);
      }
    }
    return ret;
  }

  /**
   * Access statements map for cache migration.
   * Used to compare statements between old and new policies.
   */
  const folly::F14NodeMap<std::string, RouteAttributeStatement>& getStatements()
      const {
    return statements_;
  }

  /**
   * Access match cache for migration to new policy.
   * @returns const reference to the match cache
   */
  const folly::
      F14NodeMap<folly::CIDRNetwork, std::optional<RibPolicyResultBase>>&
      getCache() const {
    return *matchCache_;
  }

  /**
   * Set a single cache entry. Used during selective cache migration.
   * @param prefix The prefix to cache
   * @param result The match result (nullopt for negative cache entry)
   */
  void setCacheEntry(
      const folly::CIDRNetwork& prefix,
      const std::optional<RibPolicyResultBase>& result) {
    matchCache_->insert_or_assign(prefix, result);
  }

  /**
   * Bulk move entire cache from another policy.
   * Used for expiration-only updates where all cache entries remain valid.
   * Swaps unique_ptr ownership for O(1) transfer, leaving both policies
   * in a valid state.
   * @param other The source policy to swap cache with
   */
  void moveCache(RouteAttributePolicy& other) {
    std::swap(matchCache_, other.matchCache_);
  }

  /**
   * Migrate inverted indices from another policy on expiration-only change.
   * Swaps the community index and per-statement weight indices for O(1)
   * transfer. Community index is keyed on statement NAME (valid when matchers
   * unchanged), and weight indices are keyed on path attrs (valid when actions
   * unchanged). For content changes, do NOTHING (new policy has empty indices,
   * which rebuild lazily).
   * @param other The source policy to swap indices with
   */
  void moveIndices(RouteAttributePolicy& other) {
    std::swap(communityToStatement_, other.communityToStatement_);
    for (const auto& [name, stmt] : statements_) {
      auto it = other.statements_.find(name);
      if (it != other.statements_.end()) {
        stmt.moveWeightIndex(it->second);
      }
    }
  }

 private:
  std::optional<std::string> matchStatement(const RibEntry& route) const;

  // unordered maps: statement name -> statement
  folly::F14NodeMap<std::string, RouteAttributeStatement> statements_{};

  /*
   * Cache: prefix -> matched statement name (or nullopt if no match)
   * This cache is mutable because it's a performance optimization that
   * doesn't affect the logical const-ness of overwriteRouteAttributes().
   * The cache is automatically invalidated when a new RouteAttributePolicy
   * object is created (each policy push creates a new object with empty cache).
   * Using unique_ptr for O(1) cache migration during policy updates.
   */
  mutable std::unique_ptr<
      folly::F14NodeMap<folly::CIDRNetwork, std::optional<RibPolicyResultBase>>>
      matchCache_;

  /*
   * Inverted index: interned community-set -> statement name (or nullopt if
   * none). Mutable for lazy population in overwriteRouteAttributes. Keyed on
   * nettools::bgplib::BgpAttrCommunitiesC shared_ptr for dedup across prefixes.
   */
  mutable folly::F14FastMap<
      std::shared_ptr<const nettools::bgplib::BgpAttrCommunitiesC>,
      std::optional<std::string>>
      communityToStatement_;

  /*
   * Names of statements with non-empty prefix sets, checked linearly per
   * prefix. Populated in constructor.
   */
  std::vector<std::string> prefixMatcherStatements_;

  // Grant tests access to private and protected members of this class.
#ifdef RouteAttributePolicy_TEST_FRIENDS
  RouteAttributePolicy_TEST_FRIENDS
#endif
};

/**
 * Represents the path selection overrides in
 * `rib_policy::TPathSelectionStatement`. Implements the selection criteria via
 * overrideMultipathSelection.
 */
class PathSelectionStatement {
 public:
  explicit PathSelectionStatement(
      const rib_policy::TPathSelectionStatement& tStmt)
      : matcher_(*tStmt.matcher()),
        pathSelector_(*tStmt.multi_path_selector()) {}

  bool operator==(const PathSelectionStatement& other) const {
    return (matcher_ == other.matcher_) &&
        (pathSelector_ == other.pathSelector_);
  }
  bool operator!=(const PathSelectionStatement& other) const {
    return !(*this == other);
  }

  /**
   * Get PathSelectionStatement object in thrift format
   */
  rib_policy::TPathSelectionStatement toThrift() const;

  /**
   * Checks if route qualifies the match criteria for policy.
   */
  bool match(const RibEntry& route) const {
    return matcher_.match(route);
  }

  /**
   * Override the multipath selection logic to filter the paths.
   *
   * @returns selected paths
   */
  std::vector<std::shared_ptr<RouteInfo>> overrideMultipathSelection(
      const std::vector<std::shared_ptr<RouteInfo>>& paths,
      const std::unique_ptr<RouteInfoSelector>& multipathSelector,
      PathSelectionPolicyResult& result) const;

  const std::optional<int32_t>& getBgpNativeMinNexthop() const {
    return pathSelector_.getBgpNativeMinNexthop();
  }

  const std::optional<bool> getDrainOnMinCapacityThresholdViolation() const {
    return pathSelector_.getDrainOnMinCapacityThresholdViolation();
  }

  const std::optional<int64_t>& getBgpNativeMinAggLbwbps() const {
    return pathSelector_.getBgpNativeMinAggLbwbps();
  }

  const std::optional<bool>& getRelaxBgpNativeMinAggLbwbps() const {
    return pathSelector_.getRelaxBgpNativeMinAggLbwbps();
  }

 private:
  const RibPolicyRouteMatcher matcher_;

  // PathSelector
  const PathSelector pathSelector_;
};

/**
 * Represents `rib_policy::TPathSelectionPolicy`.
 */
class PathSelectionPolicy {
 public:
  explicit PathSelectionPolicy(const rib_policy::TPathSelectionPolicy& policy);

  bool operator==(const PathSelectionPolicy& other) const {
    return (statements_ == other.statements_) && (version_ == other.version_);
  }
  bool operator!=(const PathSelectionPolicy& other) const {
    return !(*this == other);
  }

  /**
   * Get PathSelectionPolicy object in thrift format
   */
  rib_policy::TPathSelectionPolicy toThrift() const;

  /**
   * Checks if route qualifies the match criteria for policy. First successful
   * match with a PolicyStatement will be returned.
   */
  bool match(const RibEntry& route) const;

  std::optional<PathSelectionPolicyResult> getPathSelectionPolicyResult(
      const folly::CIDRNetwork& cidr) const {
    auto it = pathSelectionResults_.find(cidr);
    if (it == pathSelectionResults_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  /**
   * Override the path selection logic to filter the paths. This API applies
   * action on the paths.
   *
   * @returns the set of selected paths
   */
  std::vector<std::shared_ptr<RouteInfo>> overrideMultipathSelection(
      const RibEntry& route,
      const std::vector<std::shared_ptr<RouteInfo>>& paths,
      const std::unique_ptr<RouteInfoSelector>& multipathSelector);

  /**
   * Get the active path selection criteria for the given prefixes
   *
   * @returns a list of TPathSelector which contains the path selection
   *   criteria that is active for the corresponding prefix, i.e., it has
   *   at most one TPathSelectionCriteria in criteria_list. If it is empty,
   *   and bgp_native_path_selection_min_nexthop is not set, the prefix
   *   either does not exist or applies default multipathSelector
   */
  std::vector<rib_policy::TPathSelector> getActivePathSelectionCriteria(
      const std::vector<std::string>& prefixes) const;

  /**
   * Get current version of PathSelectionPolicy
   */
  int64_t getVersion() {
    return version_;
  }

 private:
  // unordered maps: statement name -> statement
  folly::F14NodeMap<std::string, PathSelectionStatement> statements_{};

  // path selection policy snapshot version
  int64_t version_{0};

  /*
   * map: the prefix of a route -> active path selection result
   * The map records the active path selection and serves as a cache
   * If the prefix matches no statement, we map it to std::nullopt
   */
  folly::
      F14NodeMap<folly::CIDRNetwork, std::optional<PathSelectionPolicyResult>>
          pathSelectionResults_{};

/*
 * per class placeholder for code injection
 * only need to setup once
 */
#ifdef PathSelectionPolicy_TEST_FRIENDS
  PathSelectionPolicy_TEST_FRIENDS
#endif
};

/**
 * Represents `rib_policy::TRouteFilter`.
 */
class RouteFilter {
 public:
  explicit RouteFilter(const rib_policy::TRouteFilter& tFilter)
      : prefixList_(tFilter.prefix_list().to_optional()),
        permissiveMode_(tFilter.permissive_mode().to_optional()) {
    if (prefixList_) {
      prefixTree_ = std::make_unique<routing::PrefixTree>(
          PrefixTreeMatch::validateAndCreatePrefixTree(*prefixList_));
    }
  }

  /*
   * define copy constructor because unique_ptr<PrefixTree> cannot be implicitly
   * copied
   */
  RouteFilter(RouteFilter const& other)
      : prefixList_(other.prefixList_), permissiveMode_(other.permissiveMode_) {
    if (prefixList_) {
      prefixTree_ = std::make_unique<routing::PrefixTree>(
          PrefixTreeMatch::validateAndCreatePrefixTree(*prefixList_));
    }
  }

  bool operator==(const RouteFilter& other) const {
    return prefixList_ == other.prefixList_ &&
        permissiveMode_ == other.permissiveMode_;
  }

  bool operator!=(const RouteFilter& other) const {
    return !(*this == other);
  }

  /**
   * Get RouteFilter object in thrift format
   */
  rib_policy::TRouteFilter toThrift() const;

  /**
   * use prefixTree_ to apply prefixList_ (allowlist) to prefixes
   * @returns <remaining prefixes, filtered prefixes>
   *
   * remaining prefixes: ALL prefixes in permissive mode, otherwise
   * all prefixes minus filtered prefixes
   *
   * filtered prefixes: those that do not pass the filter
   */
  std::tuple<std::vector<folly::CIDRNetwork>, std::vector<folly::CIDRNetwork>>
  applyFilter(const std::vector<folly::CIDRNetwork>& prefixes) const;

  /**
   * Check if prefix list is set
   * @returns true if prefixList_ has a value
   */
  bool isPrefixListSet() const {
    return prefixList_ != std::nullopt;
  }

  /**
   * Check if permissive mode is set
   * @returns true if permissiveMode_ has a value
   */
  bool isPermissiveModeSet() const {
    return permissiveMode_ != std::nullopt;
  }

 private:
  std::optional<routing_policy::PrefixList> prefixList_{std::nullopt};
  std::optional<bool> permissiveMode_{std::nullopt};

  std::unique_ptr<routing::PrefixTree> prefixTree_{nullptr};
};

/**
 * Represents `rib_policy::TRouteFilterStatement`.
 */
class RouteFilterStatement {
 public:
  explicit RouteFilterStatement(const rib_policy::TRouteFilterStatement& tStmt)
      : ingressFilter_(*tStmt.ingress_filter()),
        egressFilter_(*tStmt.egress_filter()) {}

  bool operator==(const RouteFilterStatement& other) const {
    return (ingressFilter_ == other.ingressFilter_) &&
        (egressFilter_ == other.egressFilter_);
  }
  bool operator!=(const RouteFilterStatement& other) const {
    return !(*this == other);
  }

  /**
   * Get RouteFilterStatement object in thrift format
   */
  rib_policy::TRouteFilterStatement toThrift() const;

  /**
   * apply ingress filter
   * @returns <remaining prefixes, filtered prefixes>
   *
   * remaining prefixes: ALL prefixes in permissive mode, otherwise
   * all prefixes minus filtered prefixes
   *
   * filtered prefixes: those that do not pass the filter
   */
  std::tuple<std::vector<folly::CIDRNetwork>, std::vector<folly::CIDRNetwork>>
  applyIngressFilter(const std::vector<folly::CIDRNetwork>& prefixes) const;

  /**
   * apply egress filter
   * @returns <remaining prefixes, filtered prefixes>
   *
   * remaining prefixes: ALL prefixes in permissive mode, otherwise
   * all prefixes minus filtered prefixes
   *
   * filtered prefixes: those that do not pass the filter
   */
  std::tuple<std::vector<folly::CIDRNetwork>, std::vector<folly::CIDRNetwork>>
  applyEgressFilter(const std::vector<folly::CIDRNetwork>& prefixes) const;

  /**
   * Check if the ingress filter is set
   * @returns true if ingress filter has prefix_list or permissive_mode set
   */
  bool hasIngressFilter() const;

  /**
   * Check if the egress filter is set
   * @returns true if egress filter has prefix_list or permissive_mode set
   */
  bool hasEgressFilter() const;

  /**
   * Get the ingress filter
   * @returns reference to the ingress RouteFilter
   */
  const RouteFilter& getIngressFilter() const;

  /**
   * Get the egress filter
   * @returns reference to the egress RouteFilter
   */
  const RouteFilter& getEgressFilter() const;

 private:
  RouteFilter ingressFilter_;
  RouteFilter egressFilter_;
};

class GoldenPrefixSubnetCountingTree {
 public:
  explicit GoldenPrefixSubnetCountingTree(
      const routing_policy::PrefixList& prefixList);
  bool allowPrefix(const folly::CIDRNetwork& prefix, const BgpPath& attrs)
      const;
  void incrementSubnet(const folly::CIDRNetwork& prefix);
  void decrementSubnet(const folly::CIDRNetwork& prefix);
  std::map<std::string, int> getSubnetCounts() const;

 private:
  std::optional<std::set<BgpAttrCommunityC>> parseCommunities(
      const routing_policy::PrefixListEntry& tPrefixListEntry);

  struct ParentPrefixTreeNode {
    explicit ParentPrefixTreeNode(
        int maxSubnetCount,
        const std::optional<std::set<BgpAttrCommunityC>>& communities)
        : maxSubnetCount_(maxSubnetCount), communities_(communities) {}
    // The configured subnet limit for this parent prefix.
    int maxSubnetCount_;
    // The golden communities. If nullopt, no special communities are required.
    std::optional<std::set<BgpAttrCommunityC>> communities_;
    /*
     * Maps each of the parent prefix's subnets to that subnet's reference
     * count.
     */
    folly::F14NodeMap<folly::CIDRNetwork, int> subnets_;
  };
  facebook::network::RadixTree<folly::IPAddress, ParentPrefixTreeNode>
      parentPrefixRadixTree_;
};

/**
 * Represents `rib_policy::TGoldenPrefixPolicy`.
 */
class GoldenPrefixPolicy {
 public:
  explicit GoldenPrefixPolicy(const rib_policy::TGoldenPrefixPolicy& tPolicy)
      : prefixList_(tPolicy.allowed_prefixes().to_optional()) {
    if (prefixList_) {
      prefixTree_ = std::make_unique<routing::PrefixTree>(
          PrefixTreeMatch::validateAndCreatePrefixTree(*prefixList_));
      goldenPrefixSubnetCountingTree_ =
          std::make_unique<GoldenPrefixSubnetCountingTree>(*prefixList_);
    }
  }

  GoldenPrefixPolicy(GoldenPrefixPolicy const& other)
      : prefixList_(other.prefixList_) {
    if (prefixList_) {
      prefixTree_ = std::make_unique<routing::PrefixTree>(
          PrefixTreeMatch::validateAndCreatePrefixTree(*prefixList_));
      goldenPrefixSubnetCountingTree_ =
          std::make_unique<GoldenPrefixSubnetCountingTree>(*prefixList_);
    }
  }

  GoldenPrefixPolicy& operator=(GoldenPrefixPolicy const&) = delete;

  bool operator==(const GoldenPrefixPolicy& other) const {
    return prefixList_ == other.prefixList_;
  }

  bool operator!=(const GoldenPrefixPolicy& other) const {
    return !(*this == other);
  }

  /**
   * Get GoldenPrefixPolicy object in thrift format
   */
  rib_policy::TGoldenPrefixPolicy toThrift() const;

  /** Whether to allow the prefix */
  bool allowPrefix(const folly::CIDRNetwork& prefix, const BgpPath& attrs)
      const;

  /**
   * use prefixTree_ to apply prefixList_ (allowlist) to prefixes
   * @returns <filtered prefixes>
   *
   * filtered prefixes: those that do not pass the filter
   */
  std::vector<folly::CIDRNetwork> applyFilter(
      const std::vector<folly::CIDRNetwork>& prefixes,
      const BgpPath& attrs) const;

  void incrementSubnet(const folly::CIDRNetwork& prefix);

  void decrementSubnet(const folly::CIDRNetwork& prefix);

  std::map<std::string, int> getSubnetCounts() const;

 private:
  std::optional<routing_policy::PrefixList> prefixList_{std::nullopt};

  // Used to match prefixes against golden prefix list.
  std::unique_ptr<routing::PrefixTree> prefixTree_{nullptr};

  // Used to track the number of subnets for each golden parent prefix.
  std::unique_ptr<GoldenPrefixSubnetCountingTree>
      goldenPrefixSubnetCountingTree_{nullptr};
};

/**
 * Represents `rib_policy::TRouteFilterPolicy`.
 */
class RouteFilterPolicy {
 public:
  explicit RouteFilterPolicy(const rib_policy::TRouteFilterPolicy& policy);
  bool operator==(const RouteFilterPolicy& other) const {
    if (version_ != other.version_) {
      return false;
    }
    if (keyType_ != other.keyType_) {
      return false;
    }
    if (statements_.size() != other.statements_.size()) {
      return false;
    }
    for (const auto& [name, stmt] : statements_) {
      auto it = other.statements_.find(name);
      if (it == other.statements_.end() ||
          (it->second != stmt && *it->second != *stmt)) {
        return false;
      }
    }
    return goldenPrefixPolicy_ == other.goldenPrefixPolicy_ ||
        (goldenPrefixPolicy_ && other.goldenPrefixPolicy_ &&
         *goldenPrefixPolicy_ == *other.goldenPrefixPolicy_);
  }
  bool operator!=(const RouteFilterPolicy& other) const {
    return !(*this == other);
  }

  /**
   * Get RouteFilterPolicy object in thrift format
   */
  rib_policy::TRouteFilterPolicy toThrift() const;

  std::unordered_map<std::string, std::shared_ptr<const RouteFilterStatement>>
  getStatements() const {
    return statements_;
  }

  /**
   * Get current version of RouteFilterPolicy
   */
  int64_t getVersion() {
    return version_;
  }

  std::shared_ptr<GoldenPrefixPolicy> getGoldenPrefixPolicy() const {
    return goldenPrefixPolicy_;
  }

  /**
   * Check if route filter should match against peer group name
   */
  bool matchAgainstPeerGroupName() const {
    return keyType_.has_value() &&
        *keyType_ == rib_policy::KeyType::PEER_GROUP_NAME;
  }

 private:
  /*
   * unordered map: statement name -> statement
   *
   * use shared_ptr for statements to avoid copying because adjribs also store
   * statements
   */
  std::unordered_map<std::string, std::shared_ptr<const RouteFilterStatement>>
      statements_{};
  // route filter policy snapshot version
  int64_t version_{0};
  /*
   * use shared_ptr for goldenPrefixPolicy to avoid copying because adjribs
   * share the same goldenPrefixPolicy
   */
  std::shared_ptr<GoldenPrefixPolicy> goldenPrefixPolicy_{nullptr};
  // route filter statements key type
  std::optional<rib_policy::KeyType> keyType_{std::nullopt};

/*
 * per class placeholder for code injection
 * only need to setup once
 */
#ifdef RouteFilterPolicy_TEST_FRIENDS
  RouteFilterPolicy_TEST_FRIENDS
#endif
};

/**
 * Represents `rib_policy::TRibPolicy`. Defines efficient data structures for
 * efficient processing of policy. Provides APIs for easier code intengration
 * for route policing.
 *
 * Refer to `struct TRibPolicy` in `rib_policy.thrift` for more documentation.
 */
class RibPolicy {
 public:
  explicit RibPolicy(const rib_policy::TRibPolicy& ribPolicy);

  bool operator==(const RibPolicy& other) const {
    return (routeAttributePolicy_ == other.routeAttributePolicy_) &&
        (pathSelectionPolicy_ == other.pathSelectionPolicy_) &&
        (routeFilterPolicy_ == other.routeFilterPolicy_);
  }
  bool operator!=(const RibPolicy& other) const {
    return !(*this == other);
  }

  /**
   * Get RibPolicy object in thrift format
   */
  rib_policy::TRibPolicy toThrift() const;

  // Check sub policies
  bool hasRouteAttributePolicy() {
    return routeAttributePolicy_.has_value();
  }

  bool hasPathSelectionPolicy() {
    return pathSelectionPolicy_.has_value();
  }

  bool hasRouteFilterPolicy() {
    return routeFilterPolicy_.has_value();
  }

  // For backward compatibility before fully deprecating RibPolicy
  std::optional<RouteAttributePolicy>& getRouteAttributePolicy() {
    return routeAttributePolicy_;
  }

  // For backward compatibility before fully deprecating RibPolicy
  std::optional<PathSelectionPolicy>& getPathSelectionPolicy() {
    return pathSelectionPolicy_;
  }

  std::optional<RouteFilterPolicy> getRouteFilterPolicy() {
    return routeFilterPolicy_;
  }

 private:
  std::optional<RouteAttributePolicy> routeAttributePolicy_{std::nullopt};
  std::optional<PathSelectionPolicy> pathSelectionPolicy_{std::nullopt};
  std::optional<RouteFilterPolicy> routeFilterPolicy_{std::nullopt};
};

} // namespace facebook::bgp
