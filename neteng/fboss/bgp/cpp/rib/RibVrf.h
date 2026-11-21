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

#include "neteng/fboss/bgp/cpp/common/Types.h"

namespace facebook::bgp {

class RibVrf final {
 public:
  explicit RibVrf(VrfId vrfId);
  RibVrf(const RibVrf&) = delete;
  RibVrf& operator=(const RibVrf&) = delete;
  RibVrf(RibVrf&&) = delete;
  RibVrf& operator=(RibVrf&&) = delete;

  ~RibVrf() = default;

  VrfId vrfId() const {
    return vrfId_;
  }

 private:
  // ======================== Identity ========================

  // Identifies the routing table owned by this context.
  const VrfId vrfId_;
};

} // namespace facebook::bgp
