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

#include <optional>

#include <fmt/format.h>
#include <folly/String.h>
#include <folly/gen/Base.h>
#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/if/gen-cpp2/policy_thrift_types.h"

#include "PolicyEntryBase.h"
#include "PolicyEvaluationLoggerBase.h"
#include "neteng/fboss/bgp/cpp/policy/base/PolicyActionBase.h"
#include "neteng/fboss/bgp/cpp/policy/base/PolicyMatchBase.h"

namespace facebook {
namespace routing {

template <class C>
std::type_index classType() {
  return std::type_index(typeid(C));
}

template <
    typename Attributes,
    typename PolicyActionData,
    typename PolicyMatchData>
class PolicyTermBase {
  using PolicyAttributesAction =
      PolicyAttributesActionBase<Attributes, PolicyActionData>;
  using PolicyAttributesMatch =
      PolicyMatchBase<std::shared_ptr<const Attributes>, PolicyMatchData>;
  using PolicyPrefixMatch =
      PolicyMatchBase<folly::CIDRNetwork, PolicyMatchData>;
  using PolicyEntry =
      PolicyEntryBase<Attributes, PolicyActionData, PolicyMatchData>;

 public:
  /*
   * constructor with default values for
   * termMissFlowControlAction_ and termMatchFlowControlAction_
   */
  PolicyTermBase(
      const std::string& termName,
      const std::string& termDescription)
      : termName_(termName),
        termDescription_(termDescription),
        termMissFlowControlAction_(std::make_shared<ContinueAction>()),
        termMatchFlowControlAction_(std::make_shared<AllowAction>()) {}

  // explicit pass in termMissFlowControlAction_ and termMatchFlowControlAction_
  PolicyTermBase(
      const std::string& termName,
      const std::string& termDescription,
      std::shared_ptr<PolicyActionBase> termMissFlowControlAction,
      std::shared_ptr<PolicyActionBase> termMatchFlowControlAction)
      : termName_(termName),
        termDescription_(termDescription),
        termMissFlowControlAction_(std::move(termMissFlowControlAction)),
        termMatchFlowControlAction_(std::move(termMatchFlowControlAction)) {}

  virtual ~PolicyTermBase() = default;

  const std::vector<std::shared_ptr<PolicyAttributesMatch>>&
  getPolicyAttributeMatches() const {
    return attributesMatches_;
  }
  const std::vector<std::shared_ptr<PolicyPrefixMatch>>&
  getPolicyPrefixMatches() const {
    return prefixMatches_;
  }
  const std::vector<std::shared_ptr<PolicyAttributesAction>>& getPolicyActions()
      const {
    return actions_;
  }
  const std::string& getTermName() const {
    return termName_;
  }
  const std::string& getTermDescription() const {
    return termDescription_;
  }

  /*
   * Used in upper level policy processing to determine next policy to run.
   * return: 1. empty string means continue to next policy.
   *         2. if specific name is returned, jump to that term.
   */
  std::string getMatchGotoTerm() const {
    const auto actionType = termMatchFlowControlAction_->getClassType();
    if (actionType != classType<GotoAction>()) {
      return {};
    }

    auto action = dynamic_cast<GotoAction*>(termMatchFlowControlAction_.get());
    CHECK(action); // check dynamic_cast will not resolve to nullptr

    return action->getTerm();
  }

  /**
  Check if policy flow action has requested logging on not matching.

  Returns:
      true if logging action is specified, false otherwise.
  */
  bool shouldLogOnMiss() {
    return termMissFlowControlAction_->getLogSetting();
  }

  /*
   * Increase the hit count by the number of count
   * User typically increases by number of prefixes for which policy is
   * being applied.
   */
  void incrementHitCount(uint32_t hitCount) {
    hitCount_ += hitCount;
  }

  // Return the number of prefixes hit this term.
  uint64_t getHitCount() const {
    return hitCount_;
  }

  /*
   * applyTerm walks through each match-condition and depending on criteria
   * if it matches all/(TODO any) match-condition, necessary actions are taken.
   */

  /*
   * NOTE: PolicyEntry as it is passed through each term, it will accumulate the
   *       changes of each TERM like which prefixes are permitted, which
   *       prefixes are denied, any modification to attributes etc. i.e.
   *       PolicyEntry uses IN & OUT behavior. It is passed in and get's
   *       modified as it walks through TERMs.
   */
  void applyTerm(
      PolicyEntry& entry,
      const std::string& policyStatementName) noexcept {
    auto processingEntries = entry.getAndResetProcessingEntries();

    for (auto& processingEntry : processingEntries) {
      auto attrs = std::get<0>(processingEntry);
      auto prefixSet = std::get<1>(processingEntry);
      auto gotoTerm = std::get<2>(processingEntry);

      /*
       * this set of prefixes have matches on a term with specific goto
       * skip any term in between.
       */
      if (gotoTerm.has_value() && gotoTerm.value() != termName_) {
        entry.addPrefixesToProcessingEntries(attrs, prefixSet, gotoTerm);
        continue;
      }

      if (XLOG_IS_ON(DBG4)) {
        auto strVec = folly::gen::from(prefixSet) |
            folly::gen::map([&](const auto& prefix) {
                        return folly::IPAddress::networkToString(prefix);
                      }) |
            folly::gen::as<std::vector<std::string>>();
        XLOGF(
            DBG4,
            "Applying term {} to prefixesSet: {}",
            getTermName(),
            folly::join(", ", strVec));
      }

      const auto& policyMatchData = entry.policyMatchData();
      // If no match conditions then all prefixes match the term
      auto matchResult = true;

      for (const auto& attrMatch : attributesMatches_) {
        matchResult = attrMatch->Match(attrs, policyMatchData);
        // Attributes have not matched, no need to process remaining matches
        if (!matchResult) {
          break;
        }
      }

      /*
       * Attributes have not matched, no need to check prefixes etc
       * Move all the prefixes to processing for next term without modifying
       * any attributes (no need to apply any actions)
       */
      if (!matchResult) {
        applyActionsOnMiss(entry, attrs, prefixSet, policyStatementName);
        continue;
      }

      // Apply actions (There are no per prefix matches)
      if (prefixMatches_.empty()) {
        applyActions(entry, attrs, prefixSet, policyStatementName);
        continue;
      }

      // Do per prefix split and apply actions
      applyPerPrefixMatches(
          entry, prefixMatches_, attrs, prefixSet, policyStatementName);
    }
  }

  // Get term statistics
  neteng::routing::policy::thrift::TPolicyTermStats getTermStats()
      const noexcept {
    neteng::routing::policy::thrift::TPolicyTermStats termStats;
    *termStats.name() = getTermName();
    *termStats.description() = getTermDescription();
    *termStats.prefix_hit_count() = getHitCount();
    return termStats;
  }

 private:
  /*
   * This applies prefix list match condition. It will walk through all the
   * prefixes, apply a prefix list, split the prefixes along with attributes
   * into matched and unmatched, apply actions related to the term only to
   * matched prefixes
   */
  void applyPerPrefixMatches(
      PolicyEntry& entry,
      const std::vector<std::shared_ptr<PolicyPrefixMatch>>& matches,
      std::shared_ptr<Attributes> attr,
      std::unordered_set<folly::CIDRNetwork>& prefixesSet,
      const std::string& policyName) noexcept {
    std::unordered_set<folly::CIDRNetwork> matchedPrefixes;
    std::unordered_set<folly::CIDRNetwork> unmatchedPrefixes;

    bool logOnMiss = shouldLogOnMiss();
    for (const auto& prefix : prefixesSet) {
      /*
       * Check if prefix matched all the per prefix matches (all prefix lists)
       * NOTE: As of now we only support match_logic_type AND, so here
       * we implicitly assume AND logic. Need to modify this if other
       * match_logic_types are supported.
       */
      bool allMatch = true;
      for (const auto& match : matches) {
        if (!match->Match(prefix)) {
          allMatch = false;
          if (logOnMiss) {
            XLOGF(
                INFO,
                "Prefix {} failed to match prefix-list {} of term {} in policy {}",
                folly::IPAddress::networkToString(prefix),
                match->getMatchName(),
                getTermName(),
                policyName);
            if (logger_) {
              logger_->log(
                  prefix,
                  *attr,
                  *match,
                  *termMissFlowControlAction_,
                  getTermName(),
                  policyName);
            }
          }
          break;
        }
      }

      if (allMatch) {
        // Ignoring return value, as we know there cannot be any duplicates
        matchedPrefixes.insert(prefix);
      } else {
        unmatchedPrefixes.insert(prefix);
      }
    }

    if (!unmatchedPrefixes.empty()) {
      applyActionsOnMiss(entry, attr, unmatchedPrefixes, policyName);
    }

    // None of the prefixes matched this term
    if (matchedPrefixes.empty()) {
      return;
    }

    // Apply actions to all the prefixes which matched this term
    applyActions(entry, attr, matchedPrefixes, policyName);
  }

  /*
   * Possible actions on miss are a subset of the ones on hit.
   * TODO: This behavior actually very implicit, will either
   * align behaviors (i.e, we will call applyActions instead) OR and
   * add checks and validators
   */
  void applyActionsOnMiss(
      PolicyEntry& entry,
      std::shared_ptr<Attributes> attr,
      std::unordered_set<folly::CIDRNetwork>& prefixSet,
      const std::string& policyName) noexcept {
    const auto actionType = termMissFlowControlAction_->getClassType();
    if (actionType == classType<DenyAction>()) {
      applyDenyAction(entry, prefixSet, policyName);
    } else if (actionType == classType<AllowAction>()) {
      applyAllowAction(entry, attr, prefixSet, policyName);
    } else {
      entry.addPrefixesToProcessingEntries(attr, prefixSet);
    }
  }

  /*
   * NOTE: PolicyEntry as it is passed through each term, it will accumulate the
   *       changes of each TERM like which prefixes are permitted, which
   *       prefixes are denied, any modification to attributes etc. i.e.
   *       PolicyEntry uses IN & OUT behavior. It is passed in and get's
   *       modified as it walks through TERMs.
   */
  void applyActions(
      PolicyEntry& entry,
      std::shared_ptr<Attributes> attr,
      std::unordered_set<folly::CIDRNetwork>& prefixesSet,
      const std::string& policyName) noexcept {
    incrementHitCount(prefixesSet.size());
    auto actionType = termMatchFlowControlAction_->getClassType();
    // All these prefixes are denied so no further processing required
    if (actionType == classType<DenyAction>()) {
      applyDenyAction(entry, prefixesSet, policyName);
      return;
    }

    /*
     * Apply all actions
     * NOTE: All actions will be applied in the order in which they are
     * configured. Distinct actions like med/local preference etc do not have
     * any impact due to order of executing actions of a TERM, but actions of
     * same type like AS path prepend or community order effects how the
     * the attributes are modified. We will apply actions in the order in
     * which they are configured.
     */
    if (getPolicyActions().size()) {
      // make a modifiable copy of *attr and point attr to the new object.
      copyAttr(attr);
      for (const auto& action : getPolicyActions()) {
        action->applyAction(attr, entry.policyActionData());
      }
    }

    // Deny route if actions produced invalid attributes
    auto denyReason =
        hasInvalidAttrsPostAction(attr, entry, prefixesSet, policyName);
    if (denyReason.has_value()) {
      applyDenyAction(entry, prefixesSet, policyName, *denyReason);
      return;
    }

    /*
     * ContinueAction: add prefixes back to processing entries for
     * further processing
     */
    if (actionType == classType<ContinueAction>()) {
      XLOGF(
          DBG4,
          "Continue to Next term processing {} prefixes due to policy {} term {}",
          prefixesSet.size(),
          policyName,
          getTermName());
      entry.addPrefixesToProcessingEntries(attr, prefixesSet);
      return;
    }

    /*
     * LogContinueAction: add prefixes back to processing entries for
     * further processing
     */
    if (actionType == classType<LogContinueAction>()) {
      XLOGF(
          INFO,
          "Log and continue processing {} prefixes due to policy {} term {}",
          prefixesSet.size(),
          policyName,
          getTermName());
      entry.addPrefixesToProcessingEntries(attr, prefixesSet);
      return;
    }

    /*
     * GotoAction: add prefixes back to processing entries and jump to
     * specified term for fuether processing
     */
    if (actionType == classType<GotoAction>()) {
      XLOGF(
          DBG4,
          "Continue processing {} prefixes due to policy {} term {}, goto term = {}",
          prefixesSet.size(),
          policyName,
          getTermName(),
          getMatchGotoTerm());
      entry.addPrefixesToProcessingEntries(
          attr, prefixesSet, getMatchGotoTerm());
      return;
    }

    if (actionType == classType<AllowAction>()) {
      applyAllowAction(entry, attr, prefixesSet, policyName);
      return;
    }
    /*
     * Not throwing here since the entire call stack is declared noexcept
     * Should never reach here as we have the checks at ctor
     */
    XLOGF(
        ERR,
        "Unrecognized term action on policy {} term {}",
        policyName,
        getTermName());
  }

  void applyDenyAction(
      PolicyEntry& entry,
      std::unordered_set<folly::CIDRNetwork>& prefixesSet,
      const std::string& policyName,
      const std::string& reasonSuffix = "") noexcept {
    XLOGF(
        DBG4,
        "Denying {} prefixes due to {} term {}",
        prefixesSet.size(),
        policyName,
        termName_);

    auto policyTermName = getTermName().empty() ? "N/A" : getTermName();
    entry.addPrefixesToProcessedEntries(
        nullptr,
        prefixesSet,
        fmt::format(
            "Denied by {} term {}{}",
            policyName,
            policyTermName,
            reasonSuffix.empty() ? "" : " " + reasonSuffix));
    return;
  }

  void applyAllowAction(
      PolicyEntry& entry,
      std::shared_ptr<Attributes> attr,
      std::unordered_set<folly::CIDRNetwork>& prefixesSet,
      const std::string& policyName) noexcept {
    XLOGF(
        DBG4,
        "Accepting {} prefixes due to policy {} term {}",
        prefixesSet.size(),
        policyName,
        getTermName());

    /*
     * Move these prefixes to processed (No further processing is needed for
     * these prefixes)
     */
    auto policyTermName = getTermName().empty() ? "N/A" : getTermName();
    entry.addPrefixesToProcessedEntries(
        attr,
        prefixesSet,
        fmt::format(
            "Accepted/Modified by {} term {}", policyName, policyTermName));
    return;
  }

  /*
   * copyAttr() is used in applyActions() before policy engine modify
   * attributes. Policy Engine take <const Attributes&> in policy input to avoid
   * accedential modification of pass in attributes. Therefore we always
   * make a copy if attributes content will be changed.
   */
  virtual void copyAttr(std::shared_ptr<Attributes>& attr) = 0;
  /*
   * ** Implementation Note **
   * Create a new object by coping *attr content, modify the new object
   * content and change the share_ptr attr to point to it.
   * e.g.
   * void copyAttr(std::shared_ptr<Attributes>& attr) {
   *    auto newAttr = std::make_shared<Attributes>(*attr);
   *    attr.swap(newAttr);
   * }
   */

  /*
   * Returns std::nullopt if attributes are valid, or a reason suffix string
   * (e.g., "invalid GAR weights") to append to the deny reason.
   */
  virtual std::optional<std::string> hasInvalidAttrsPostAction(
      const std::shared_ptr<Attributes>& /* attr */,
      const PolicyEntry& /* entry */,
      const std::unordered_set<folly::CIDRNetwork>& /* prefixesSet */,
      const std::string& /* policyName */) const noexcept {
    return std::nullopt;
  }

 protected:
  // Number of times this term is hit
  uint64_t hitCount_{0};
  // name of term struct, optional
  const std::string termName_;
  const std::string termDescription_;

  std::shared_ptr<PolicyEvaluationLoggerBase<Attributes, PolicyMatchData>>
      logger_;

  // matches
  std::vector<std::shared_ptr<PolicyAttributesMatch>> attributesMatches_;
  std::vector<std::shared_ptr<PolicyPrefixMatch>> prefixMatches_;

  // actions
  std::vector<std::shared_ptr<PolicyAttributesAction>> actions_;
  /*
   * Flow control action on prefixes that did not match this term:
   * NEXT_TERM | DENY | LOG_AND_NEXT_TERM
   */
  std::shared_ptr<PolicyActionBase> termMissFlowControlAction_;
  /*
   * Flow control action on prefixes that match this term:
   * PERMIT | DENY | CONTINUE | GOTO
   * If there are matches, and no explicit decision action is given
   * we will treat it as permit, similar to vendor implementations
   */
  std::shared_ptr<PolicyActionBase> termMatchFlowControlAction_;
};

} // namespace routing
} // namespace facebook
