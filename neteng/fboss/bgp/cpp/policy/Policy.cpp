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

#include <fmt/format.h>

#include "neteng/fboss/bgp/cpp/common/BgpError.h"
#include "neteng/fboss/bgp/cpp/config/ConfigStructs.h"
#include "neteng/fboss/bgp/cpp/policy/Policy.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyAction.h"

using facebook::routing::classType;

namespace facebook::bgp {

void Policy::populatePolicyTerms(const BgpGlobalConfig* config) {
  for (const auto& term : *policy_.policy_entries()) {
    auto policyTerm = std::make_shared<PolicyTerm>(term, mask_, config);
    policyTerms_.emplace_back(std::move(policyTerm));
  }
}

void Policy::populateDefaultAction(const BgpGlobalConfig* config) {
  if (!config || !config->enablePolicyDefaultAction) {
    return;
  }
  auto result = *policy_.result();
  switch (result) {
    case bgp_policy::FlowControlAction::ACCEPT:
      defaultAllow_ = true;
      break;
    case bgp_policy::FlowControlAction::DENY:
      // This is the default action. No need to override.
      break;
    default: // NEXT_TERM, NEXT_POLICY, LOG_AND_NEXT_TERM, etc.
      throw BgpError(
          fmt::format(
              "PolicyStatement '{}' has invalid result (value={}). "
              "Only ACCEPT or DENY are valid for the policy default action.",
              policyName_,
              static_cast<int>(result)));
  }
}

void Policy::validatePolicyTerms() {
  // First call the base class validation for GOTO term checks
  PolicyBase::validatePolicyTerms();

  /*
   * BGP-specific validation: ExtCommunityAction and LbwExtCommunityAction
   * cannot coexist in the same PolicyStatement
   */
  std::optional<std::string> extCommunityActionTermName;
  std::optional<std::string> lbwExtCommunityActionTermName;

  for (const auto& term : policyTerms_) {
    const auto& termName = term->getTermName();
    for (const auto& action : term->getPolicyActions()) {
      if (action->getClassType() == classType<ExtCommunityAction>()) {
        extCommunityActionTermName = termName;
      } else if (action->getClassType() == classType<LbwExtCommunityAction>()) {
        lbwExtCommunityActionTermName = termName;
      }

      // If both actions are found, throw an error
      if (extCommunityActionTermName && lbwExtCommunityActionTermName) {
        throw BgpError(
            fmt::format(
                "PolicyStatement '{}' cannot contain both ExtCommunityAction "
                "(in term '{}') and LbwExtCommunityAction (in term '{}')",
                policyName_,
                *extCommunityActionTermName,
                *lbwExtCommunityActionTermName));
      }
    }
  }
}

} // namespace facebook::bgp
