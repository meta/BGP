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

#include "neteng/fboss/bgp/cpp/stats/StatsBB.h"

namespace facebook::bgp {

namespace BgpStatsBB {

void initCounters() {
  fb303::ThreadCachedServiceData::get()->setCounter(kAddPeersSuccess, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kAddPeersRejected, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kDelPeersSuccess, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kDelPeersRejected, 0);
}

void incrAddPeersSuccess() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(kAddPeersSuccess, 1);
}

void incrAddPeersRejected() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(kAddPeersRejected, 1);
}

void incrDelPeersSuccess() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(kDelPeersSuccess, 1);
}

void incrDelPeersRejected() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(kDelPeersRejected, 1);
}

} // namespace BgpStatsBB

namespace RibStatsBB {

DEFINE_timeseries(unsupportedPolicyMsg, kUnsupportedPolicyMsg, fb303::COUNT);

DEFINE_timeseries(nhtLinkHoldStarted, kNhtLinkHoldStarted, fb303::COUNT);
DEFINE_timeseries(nhtLinkHoldReleased, kNhtLinkHoldReleased, fb303::COUNT);

void setNhtLinkHoldActive(int64_t count) {
  fb303::ThreadCachedServiceData::get()->setCounter(kNhtLinkHoldActive, count);
}

void incrNhtLinkHoldStarted() {
  STATS_nhtLinkHoldStarted.add(1);
}

void incrNhtLinkHoldReleased() {
  STATS_nhtLinkHoldReleased.add(1);
}

void initCounters() {
  /*
   * Set them to zero, so that the keys are present on a device where
   * dampening is off.
   */
  fb303::ThreadCachedServiceData::get()->setCounter(kNhtLinkHoldActive, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kNhtLinkHoldStarted + ".count", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kNhtLinkHoldStarted + ".count.60", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kNhtLinkHoldReleased + ".count", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kNhtLinkHoldReleased + ".count.60", 0);
}

} // namespace RibStatsBB

void initStatsBB() {
  BgpStatsBB::initCounters();
  RibStatsBB::initCounters();
}

} // namespace facebook::bgp
