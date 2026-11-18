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

#include "neteng/fboss/bgp/cpp/config/Config.h"

namespace facebook::bgp {

class ConfigBB : public Config {
 public:
  using Config::Config;

  std::shared_ptr<Config> createConfigFromFile(
      const std::string& configFileName) const override;
  std::shared_ptr<const Config> createConfig(
      thrift::BgpConfig config) const override;

  const std::shared_ptr<thrift::BgpNetServiceThriftConfig> getNetServiceConfig()
      const;

  /*
   * Return true only when net_service_config sets
   * enable_agentic_caller_enforcement to true. A missing net_service_config or
   * an unset field returns false.
   */
  bool isAgenticCallerEnforcementEnabled() const;
};

} // namespace facebook::bgp
