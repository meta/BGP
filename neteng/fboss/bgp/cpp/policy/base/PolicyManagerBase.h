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

#include <fmt/format.h>
#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/cpp/policy/base/PolicyStructsBase.h"
#include "neteng/fboss/bgp/if/gen-cpp2/policy_thrift_types.h"

#include "PolicyBase.h"

namespace facebook {
namespace routing {

template <class Attributes, class PolicyActionData, class PolicyMatchData>
class PolicyManagerBase {
 public:
  PolicyManagerBase() = default;
  virtual ~PolicyManagerBase() = default;

  PolicyBase<Attributes, PolicyActionData, PolicyMatchData>* FOLLY_NULLABLE
  getPolicyFromName(const std::string& policyName) {
    auto itr = policyMap_.find(policyName);
    if (itr != policyMap_.end()) {
      return &itr->second;
    }
    return nullptr;
  }

  bool isPolicyPresent(const std::string& policyName) const {
    return policyMap_.find(policyName) != policyMap_.end();
  }

  PolicyOutMessageBase<Attributes> applyPolicy(
      const std::string& policyStatementName,
      const PolicyInMessageBase<Attributes, PolicyActionData, PolicyMatchData>&
          policyIn) noexcept {
    std::chrono::time_point<std::chrono::system_clock> startTime =
        std::chrono::system_clock::now();
    auto policy = getPolicyFromName(policyStatementName);
    if (!policy) {
      CHECK(false) << fmt::format(
          "Non existing policy {} applied", policyStatementName);
    }

    XLOGF(
        DBG4,
        "Applying policy {} to {} prefixes",
        policyStatementName,
        policyIn.prefixes.size());

    /*
     * TODO: In a single policyIn message we shouldn't have
     *       same prefix given multiple times with same or different attributes
     *       AdjRib has to ensure that only latest attributes are associated
     *       with a prefix. (Will be done in AdjRib during integration).
     */
    auto entry = PolicyEntryBase<Attributes, PolicyActionData, PolicyMatchData>(
        policyIn);

    // Walk all terms and apply each term
    for (const auto& term : policy->getPolicyTerms()) {
      term->applyTerm(entry, policyStatementName);

      // Nothing more to process bailout early
      if (!entry.anyPendingPrefixesToProcess()) {
        break;
      }
    }

    /*
     * Completed processing all terms of the policy. Accept or deny
     * unmatched prefixes based on the policy's default action.
     */
    entry.completeProcessing(policyStatementName, policy->getDefaultAllow());
    // Update execution statistics
    policy->updatePolicyExecutionStats(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now() - startTime),
        policyIn.prefixes.size());
    return entry.buildPolicyOut();
  }

  // Get all policy statistics
  void getPolicyStats(
      neteng::routing::policy::thrift::TPolicyStats& stats) const noexcept {
    for (const auto& iterPolicy : policyMap_) {
      // Get stats for each policy statement
      stats.policy_statement_stats()->emplace_back(
          iterPolicy.second.getPolicyStatementStats());
    }
  }

 protected:
  // internal policy database
  std::unordered_map<
      std::string,
      PolicyBase<Attributes, PolicyActionData, PolicyMatchData>>
      policyMap_;
};
} // namespace routing
} // namespace facebook
