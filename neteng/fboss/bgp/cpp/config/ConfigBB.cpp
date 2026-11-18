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

#include "neteng/fboss/bgp/cpp/config/ConfigBB.h"

#include <utility>

namespace facebook::bgp {

std::shared_ptr<Config> ConfigBB::createConfigFromFile(
    const std::string& configFileName) const {
  return std::make_shared<ConfigBB>(configFileName);
}

std::shared_ptr<const Config> ConfigBB::createConfig(
    thrift::BgpConfig config) const {
  return std::make_shared<const ConfigBB>(
      std::move(config), std::nullopt, splitConfigPolicy());
}

const std::shared_ptr<thrift::BgpNetServiceThriftConfig>
ConfigBB::getNetServiceConfig() const {
  if (!config_.net_service_config()) {
    return nullptr;
  }
  return std::make_shared<thrift::BgpNetServiceThriftConfig>(
      *config_.net_service_config());
}

bool ConfigBB::isAgenticCallerEnforcementEnabled() const {
  auto netServiceConfig = config_.net_service_config();
  return netServiceConfig.has_value() &&
      netServiceConfig->enable_agentic_caller_enforcement().value_or(false);
}

} // namespace facebook::bgp
