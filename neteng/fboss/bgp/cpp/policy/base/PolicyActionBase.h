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

#include <functional>
#include <memory>
#include <stdexcept>
#include <typeindex>

#include <fmt/format.h>

namespace facebook {
namespace routing {

/*
 * PolicyActionBase
 */

class PolicyActionBase {
 public:
  explicit PolicyActionBase(
      const std::string& actionName,
      bool logEnabled = false)
      : actionName_(actionName), logEnabled_(logEnabled) {}
  virtual ~PolicyActionBase() = default;

  const std::string& getActionName() const {
    return actionName_;
  }

  /*
   * return run time class type
   * e.g. class DenyAction : public PolicyActionBase {}
   *      DenyAction d;
   *      d.getClassType() returns std::type_index(typeid(DenyAction)
   */
  std::type_index getClassType() const {
    return std::type_index(typeid(*this));
  }

  virtual const std::string str() const noexcept {
    return "";
  }

  bool getLogSetting() const {
    return logEnabled_;
  }

 private:
  // Name of this policy Action struct, optional
  const std::string actionName_;
  const bool logEnabled_;
};

/*
 * PolicyAttributesActionBase - child class needs to implement applyAction()
 * call
 *
 * PolicyActionData - capture any data that's needed to apply a action
 * but can NOT be pre-configured in Policy Configuration
 * (e.g some data needs to be dynamically derived on the fly).
 */
template <typename Attributes, typename PolicyActionData>
class PolicyAttributesActionBase : public PolicyActionBase {
 public:
  explicit PolicyAttributesActionBase(const std::string& actionName)
      : PolicyActionBase(actionName) {}
  virtual ~PolicyAttributesActionBase() = default;

  /*
   * Implementation Note:
   * Create a new object by coping *attr content, modify the new object
   * content and change the share_ptr to point to it.
   * Directly modification on the attrs will not reflect in policy out
   * as Policy Engine take <const Attributes&> in policy input to avoid
   * accedential modification of pass in attributes.
   */
  virtual void applyAction(
      std::shared_ptr<Attributes>&, /* attr */
      std::optional<PolicyActionData> policyActionData =
          std::nullopt) const noexcept = 0;
};

/*
 * PolicyFlowControlAction - does not support applyAction() call
 */

class DenyAction : public PolicyActionBase {
 public:
  DenyAction(const bool enableLog = false) : PolicyActionBase("", enableLog) {}
  const std::string str() const noexcept override {
    return "Deny";
  }
};

class AllowAction : public PolicyActionBase {
 public:
  AllowAction(const bool enableLog = false) : PolicyActionBase("", enableLog) {}
  const std::string str() const noexcept override {
    return "Allow";
  }
};

class ContinueAction : public PolicyActionBase {
 public:
  explicit ContinueAction(const bool enableLog = false)
      : PolicyActionBase("", enableLog) {}
  ~ContinueAction() override = default;
  const std::string str() const noexcept override {
    return "NextRule";
  }
};

class LogContinueAction : public PolicyActionBase {
 public:
  explicit LogContinueAction(const bool enableLog = false)
      : PolicyActionBase("", enableLog) {}
  virtual ~LogContinueAction() override = default;
  const std::string str() const noexcept override {
    return "LogAndNextRule";
  }
};

class GotoAction : public PolicyActionBase {
 public:
  explicit GotoAction(const std::string& gotoTermName)
      : PolicyActionBase(""), term_(gotoTermName) {
    if (term_.empty()) {
      throw std::invalid_argument(
          fmt::format("Malformed GOTO config: next_term_id = {}", term_));
    }
  }
  virtual ~GotoAction() = default;
  const std::string& getTerm() const {
    return term_;
  }

 private:
  const std::string term_;
};

} // namespace routing
} // namespace facebook
