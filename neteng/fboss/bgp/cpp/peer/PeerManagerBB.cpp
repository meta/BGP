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

#include "neteng/fboss/bgp/cpp/peer/PeerManagerBB.h"

#include <folly/io/async/AsyncTimeout.h>
#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/cpp/common/Consts.h"

namespace facebook::bgp {

void PeerManagerBB::createRibComputationMaxWaitTimer() noexcept {
  /*
   * BB-owned boot-relative safety net: forces initial RIB path computation even
   * if no peer session ever comes up. Made on the evb thread; callback runs
   * there too.
   */
  // TODO_PMTRIZED_VRF: Run a RIB-computation max-wait timer for every VRF.
  ribComputationMaxWaitTimer_ = folly::AsyncTimeout::make(
      getEventBase(),
      [this]() noexcept { getDefaultVrf().onEorConvergenceTimeout(); });
}

void PeerManagerBB::createAndScheduleTimers() noexcept {
  /*
   * BB cold-start policy: do NOT create/arm eorTimer_ at boot. Create the
   * default VRF's initialized max-wait timer, then create + arm only the
   * boot-relative RIB-computation max wait (the no-session /
   * very-slow-first-session safety net). The real eor_time_s EoR wait
   * (eorTimer_) is created and armed from the first established session in
   * onSessionEstablishedEorHook(), so a slow cold boot does not burn the
   * eor_time_s budget before peering begins.
   */
  getDefaultVrf().createInitializedMaxWaitTimer();
  createRibComputationMaxWaitTimer();
  scheduleTimer(
      ribComputationMaxWaitTimer_,
      "RIB-computation max-wait timer",
      kRibComputationMaxWaitMultiplier * eorWaitDuration_);
}

void PeerManagerBB::onSessionEstablishedEorHook() noexcept {
  if (eorWaitRearmedOnFirstSession_) {
    return;
  }
  eorWaitRearmedOnFirstSession_ = true;

  /*
   * First session up: create + arm the real EoR wait (eor_time_s) measured from
   * now, then cancel the boot-relative ribComputationMaxWaitTimer_ — its
   * no-session safety-net job is done now that a session has established.
   */
  getDefaultVrf().createEorTimer();
  if (scheduleTimer(
          getDefaultVrf().getEorTimer(), "EoR timer", eorWaitDuration_)) {
    XLOG(DBG1, "First session established; started the real EoR wait.");
  }

  ribComputationMaxWaitTimer_.reset();
}

void PeerManagerBB::stop() noexcept {
  /*
   * Reset the BB-owned timer on the evb thread before base teardown: an armed
   * AsyncTimeout must be destroyed on its EventBase thread (mirrors how the
   * base resets eorTimer_/initializedMaxWaitTimer_ inside stop()).
   */
  getEventBase().runImmediatelyOrRunInEventBaseThreadAndWait(
      [this]() noexcept { ribComputationMaxWaitTimer_.reset(); });

  PeerManagerBase::stop();
}

} // namespace facebook::bgp
