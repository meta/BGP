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

#include <folly/io/async/AsyncTimeout.h>

#include <memory>

#include "neteng/fboss/bgp/cpp/peer/PeerManagerBase.h"

namespace facebook::bgp {

/*
 * BB/EBB PeerManager.
 *
 * Identical to PeerManagerBase except for the EoR-convergence timer policy. On
 * BB the real eor_time_s EoR wait (eorTimer_) is measured from the FIRST
 * established session (onSessionEstablishedEorHook) rather than from daemon
 * startup, so a slow cold boot does not burn the eor_time_s budget before peers
 * establish and send their End-of-RIB. To still force RIB path computation when
 * no session ever comes up, a dedicated boot-relative timer
 * (ribComputationMaxWaitTimer_, armed for kRibComputationMaxWaitMultiplier *
 * eor_time_s at thread start) is owned entirely here; the base/DC never see it.
 * This fixes premature EOR_TIMER_EXPIRED on EBB cold boot.
 */
class PeerManagerBB : public PeerManagerBase {
 public:
  using PeerManagerBase::PeerManagerBase;

  /*
   * Resets the BB-owned ribComputationMaxWaitTimer_ on the evb thread (an armed
   * AsyncTimeout must be destroyed on its EventBase thread) before delegating
   * to the base teardown, keeping the timer's lifecycle wholly within BB.
   */
  void stop() noexcept override;

 protected:
  /*
   * BB EoR-timer policy (see the PeerManagerBase seam for the contract). At
   * startup, create the default VRF's initializedMaxWaitTimer_ and create +
   * arm ONLY the boot-relative ribComputationMaxWaitTimer_ (the no-session
   * safety net) — no eorTimer_ yet. eorTimer_ (the real EoR wait) is created
   * and armed from the first established session in
   * onSessionEstablishedEorHook(), which also cancels
   * ribComputationMaxWaitTimer_.
   */
  void createAndScheduleTimers() noexcept override;
  void onSessionEstablishedEorHook() noexcept override;

 private:
  // Makes ribComputationMaxWaitTimer_ (BB-owned) on the evb thread.
  void createRibComputationMaxWaitTimer() noexcept;

  /*
   * One-shot latch: true once the FIRST established session has armed eorTimer_
   * to the eor_time_s wait. Later sessions must not re-arm and push the
   * convergence deadline out.
   */
  bool eorWaitRearmedOnFirstSession_{false};

  /*
   * BB-owned boot-relative safety net, created + armed for
   * kRibComputationMaxWaitMultiplier * eor_time_s when the PeerManager thread
   * starts. Forces initial RIB path computation even if no peer session ever
   * establishes (so eorTimer_ is never created). Fires below
   * initializedMaxWaitTimer_ so RIB computation is unblocked before the
   * INITIALIZED last line of defense. Cancelled in
   * onSessionEstablishedEorHook() once the first session establishes (eorTimer_
   * takes over). Owned solely by BB; the base/DC are unaware of it.
   */
  std::unique_ptr<folly::AsyncTimeout> ribComputationMaxWaitTimer_;

#ifdef PeerManagerBB_TEST_FRIENDS
  PeerManagerBB_TEST_FRIENDS;
#endif
};

} // namespace facebook::bgp
