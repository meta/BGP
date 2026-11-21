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

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include <boost/noncopyable.hpp>
#include <folly/IPAddress.h>
#include <folly/coro/AsyncScope.h>

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/common/BgpPath.h"
#include "neteng/fboss/bgp/cpp/common/Structs.h"
#include "neteng/fboss/bgp/cpp/common/Types.h"
#include "neteng/fboss/bgp/cpp/lib/coro/MPMCQueue.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopInfo.h"
#include "neteng/fboss/bgp/cpp/rib/FibOut.h"

namespace folly {
class EventBase;
}

namespace facebook::bgp {

class RibBase;

/**
 * Abstract interface for FIB (Forwarding Information Base) implementations.
 * This class defines the contract that all FIB implementations must follow.
 */
class Fib : public boost::noncopyable {
 public:
  /**
   * Routes associated with one completed platform programming call.
   *
   * The RIB uses this message to gate route advertisement. It does not update
   * FIB-out state. FIB-out records a route when the request leaves the RIB
   * batch and enters the platform adapter batch.
   */
  using FibProgrammedPfxToNexthops = folly::
      F14NodeMap<folly::CIDRNetwork, std::shared_ptr<const WeightedNexthopMap>>;

  /** Group completed routes by the attributes used for advertisement. */
  using FibProgrammedPfxs = folly::
      F14FastMap<std::shared_ptr<const BgpPath>, FibProgrammedPfxToNexthops>;

  /** Platform programming completion used to continue RIB advertisement. */
  struct FibProgrammedMessage {
    // Routing table the programmed routes belong to.
    const VrfId vrfId;
    const FibProgrammedPfxs fibProgrammedPfxs;
    const bool isSync;

    /** Construct a completion for one platform programming call. */
    FibProgrammedMessage(
        VrfId vrfId,
        FibProgrammedPfxs fibProgrammedPfxs,
        bool isSync)
        : vrfId(vrfId),
          fibProgrammedPfxs(std::move(fibProgrammedPfxs)),
          isSync(isSync) {}
  };

  /** Request a full platform FIB synchronization. */
  struct FibSyncReq {};

  /** Message accepted by the RIB's platform-completion queue. */
  using FibMessage = std::variant<FibProgrammedMessage, FibSyncReq>;

  /** Queue carrying platform completions and synchronization requests. */
  using FibMessageQueue = bgp::coro::MPMCQueue<FibMessage>;

  /** Destroy a platform FIB adapter. */
  virtual ~Fib() = default;

  /**
   * Stage one normalized unicast operation in the platform batch.
   *
   * The returned value describes the request staged by this call. A missing
   * value means FIB-out collection was disabled or no platform request was
   * staged. An empty route records a submitted removal. A later `program()`
   * failure does not roll back a staged FIB-out request.
   */
  virtual std::optional<FibOutRoute> updateUnicastRoute(
      const folly::CIDRNetwork& prefix,
      std::shared_ptr<const BgpPath> attrsToBeAdvertised,
      std::shared_ptr<const WeightedNexthopMap> weightedNexthops,
      const bool isLocalRouteBest,
      const bool installToFib,
      const folly::F14NodeMap<folly::IPAddress, facebook::bgp::NexthopInfo>&
          nextHopInfoMap,
      const std::optional<uint32_t>& classId = std::nullopt,
      std::shared_ptr<const NexthopTopoInfoMap> nexthopTopoInfoMap = nullptr,
      const BgpRouteType routeType = BgpRouteType::UNKNOWN,
      bool enableFibOutTracking = false) = 0;

  /**
   * Stage one normalized unicast operation with an optional backup nexthop.
   *
   * Platforms without backup support delegate to `updateUnicastRoute()`.
   */
  virtual std::optional<FibOutRoute> updateUnicastRouteWithBackup(
      const folly::CIDRNetwork& prefix,
      std::shared_ptr<const BgpPath> attrsToBeAdvertised,
      std::shared_ptr<const WeightedNexthopMap> weightedNexthops,
      const bool isLocalRouteBest,
      const bool installToFib,
      const folly::F14NodeMap<folly::IPAddress, facebook::bgp::NexthopInfo>&
          nextHopInfoMap,
      const std::optional<uint32_t>& classId,
      std::shared_ptr<const NexthopTopoInfoMap> nexthopTopoInfoMap,
      const BgpRouteType routeType,
      const std::optional<folly::IPAddress>&,
      bool enableFibOutTracking = false) {
    return updateUnicastRoute(
        prefix,
        std::move(attrsToBeAdvertised),
        std::move(weightedNexthops),
        isLocalRouteBest,
        installToFib,
        nextHopInfoMap,
        classId,
        std::move(nexthopTopoInfoMap),
        routeType,
        enableFibOutTracking);
  }

  /** Return whether the platform FIB endpoint is connected. */
  virtual bool isConnected() const = 0;

  /** Return whether the platform has completed its required initial sync. */
  virtual bool isFullSynced() const = 0;

  /*
   * Submit the currently staged platform batch. `vrfId` is the routing table
   * the batch belongs to and is echoed back in the FibProgrammedMessage.
   */
  virtual folly::coro::Task<void> program(
      bool isSync = false,
      VrfId vrfId = fboss::kDefaultVrf) = 0;

  /** Stop platform activity and release its connection state. */
  virtual void stop() = 0;
};

} // namespace facebook::bgp
