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

#include "neteng/fboss/bgp/cpp/peer/PeerManagerVrf.h"

#include "neteng/fboss/bgp/cpp/config/ConfigStructs.h"

namespace facebook::bgp {

PeerManagerVrf::PeerManagerVrf(
    VrfId vrfId,
    PeerManagerBase& owner,
    folly::coro::CancellableAsyncScope& asyncScope)
    : vrfId_(vrfId), owner_(owner), asyncScope_(asyncScope) {}

/**
 * @brief  Build a Group Name as a key for a specific peer
 *
 * @params peerId peer for which to build groupName key
 * @params peeringParams peering parameters for passed in peer
 *
 * @return std::string
 */
std::string PeerManagerVrf::buildAdjRibOutGroupName(
    const nettools::bgplib::BgpPeerId& peerId,
    const PeeringParams& peeringParams) noexcept {
  if (peeringParams.peerGroupName.has_value()) {
    return *peeringParams.peerGroupName;
  }

  return peerId.peerAddr.str();
}

} // namespace facebook::bgp
