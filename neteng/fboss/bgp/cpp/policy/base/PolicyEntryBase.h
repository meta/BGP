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
#include <folly/String.h>
#include <folly/gen/Base.h>
#include <folly/logging/xlog.h>
#include <unordered_set>

#include "neteng/fboss/bgp/cpp/policy/base/PolicyStructsBase.h"

namespace facebook {
namespace routing {

/*
 * Policy Entry
 * This object is created during applyPolicy, passed through each term
 * and necessary actions are accumulated for each prefix.
 * This has capability to handle splitting of pair<attributes, prefixes>
 * while walking a prefix filter based term
 */

template <
    typename Attributes,
    typename PolicyActionData,
    typename PolicyMatchData>
class PolicyEntryBase {
 public:
  explicit PolicyEntryBase(
      const PolicyInMessageBase<Attributes, PolicyActionData, PolicyMatchData>&
          input) noexcept
      : policyActionData_(input.policyActionData),
        policyMatchData_(input.policyMatchData) {
    std::unordered_set<folly::CIDRNetwork> prefixesSet(
        input.prefixes.begin(), input.prefixes.end());
    // Avoid vector resizing by reserving size
    processingEntries_.reserve(input.prefixes.size());
    processingEntries_.emplace_back(
        std::make_tuple(input.attrs, std::move(prefixesSet), std::nullopt));
  }

  virtual ~PolicyEntryBase() = default;

  // Determine if there are any prefixes to be processed
  bool anyPendingPrefixesToProcess() const noexcept {
    return (processingEntries_.size() != 0);
  }

  // Builds policyout message to be returned to caller of applyPolicy
  PolicyOutMessageBase<Attributes> buildPolicyOut() noexcept {
    return {std::move(processedEntries_)};
  }

  // any policy action data that can't be pre-configured in policy
  std::optional<PolicyActionData> policyActionData() const noexcept {
    return policyActionData_;
  }

  std::optional<PolicyMatchData> policyMatchData() const noexcept {
    return policyMatchData_;
  }

  /*
   * To be called when all TERMS are processed. Any remaining entries
   * in processing state are accepted or denied based on the defaultAllow flag.
   * Defaults to DENY for backwards compatibility.
   */
  void completeProcessing(
      const std::string& policyName,
      bool defaultAllow) noexcept {
    for (const auto& processingEntry : processingEntries_) {
      auto attrs = std::get<0>(processingEntry);
      auto prefixSet = std::get<1>(processingEntry);

      auto actionStr = defaultAllow ? "Default allowed" : "Default denied";
      if (XLOG_IS_ON(DBG3)) {
        auto strVec = folly::gen::from(prefixSet) |
            folly::gen::map([](const auto& prefix) {
                        return folly::IPAddress::networkToString(prefix);
                      }) |
            folly::gen::as<std::vector<std::string>>();
        XLOGF(
            DBG3,
            "Prefix {} did not match any policy term in {}. {}",
            folly::join(",", strVec),
            policyName,
            actionStr);
      }
      addPrefixesToProcessedEntries(
          defaultAllow ? attrs : nullptr,
          prefixSet,
          fmt::format(
              "Did not match any policy term in {}. {}",
              policyName,
              actionStr));
    }
    processingEntries_.clear();
  }

  // Return the processing entries and reset it
  std::vector<std::tuple<
      std::shared_ptr<Attributes>,
      std::unordered_set<folly::CIDRNetwork>,
      std::optional<std::string> /* gotoTermName */>>
  getAndResetProcessingEntries() noexcept {
    std::vector<std::tuple<
        std::shared_ptr<Attributes>,
        std::unordered_set<folly::CIDRNetwork>,
        std::optional<std::string> /* gotoTermName */>>
        tmp;

    // Swap will reset or clear processingEntries_
    tmp.swap(processingEntries_);
    return tmp;
  }

  /*
   * Add prefixes to processing:
   *  1. For prefixes that need to go through remaining TERMS,
   *     set gotoTerm = std::nullopt
   *  2. For prefixes that need to jump to a specific term, set gotoTerm
   */
  void addPrefixesToProcessingEntries(
      std::shared_ptr<Attributes> attr,
      std::unordered_set<folly::CIDRNetwork>& prefixesSet,
      std::optional<std::string> gotoTerm = std::nullopt) noexcept {
    processingEntries_.emplace_back(
        std::make_tuple(attr, std::move(prefixesSet), gotoTerm));
  }

  // Add prefixes to processed (i.e. will not go through any more TERMS)
  void addPrefixesToProcessedEntries(
      std::shared_ptr<Attributes> attr,
      std::unordered_set<folly::CIDRNetwork>& prefixesSet,
      const std::string& policyName) noexcept {
    auto attrsPolicy =
        std::make_shared<AttributesAndPolicy<Attributes>>(attr, policyName);
    for (auto& prefix : prefixesSet) {
      CHECK_EQ(processedEntries_.count(prefix), 0) << fmt::format(
          "Prefix {} present in processing and processed policyEntry",
          folly::IPAddress::networkToString(prefix));
      processedEntries_[prefix] = attrsPolicy;
    }
  }

 private:
  /*
   * Still processing entries:
   *   1. entries have not yet matched any ACCEPT/DENY term
   *   2. entried previous matched a term with MatchAction = GOTO | CONTINUE
   *
   * Grouped as tuple: <commomn attributes, set<prefixes>, optional<gotoTerm>>
   *
   * gotoTermName will be populated only if the prefix has previous
   * matched a term with action GOTO. PolicyTerm.applyTerm() will skip
   * processing if its term name does not match the populated gotoTermName here
   */
  std::vector<std::tuple<
      std::shared_ptr<Attributes>,
      std::unordered_set<folly::CIDRNetwork>,
      std::optional<std::string> /* gotoTermName */>>
      processingEntries_;

  // Processing Completed entries. Matched ACCEPT term
  std::unordered_map<
      folly::CIDRNetwork,
      std::shared_ptr<AttributesAndPolicy<Attributes>>>
      processedEntries_;

  /*
   * any policy action data that needs to be passed in outside of
   * policy-configurations
   */
  const std::optional<PolicyActionData> policyActionData_{std::nullopt};

  // any additional information that is used only for matching
  const std::optional<PolicyMatchData> policyMatchData_{std::nullopt};
};
} // namespace routing
} // namespace facebook
