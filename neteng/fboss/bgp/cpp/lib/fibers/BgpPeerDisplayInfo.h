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

#include "neteng/fboss/bgp/cpp/config/ConfigStructs.h"
#include "neteng/fboss/bgp/if/gen-cpp2/BgpStructs_types.h"

namespace facebook::nettools::bgplib {

enum class BgpSessionState : uint8_t {
  IDLE = 1,
  ACTIVE,
  CONNECT,
  OPEN_SENT,
  OPEN_CONFIRM,
  ESTABLISHED
};

std::string getBgpSessionStateName(BgpSessionState state);

enum class ResetReason : uint8_t {
  OPEN_MSG_TIMER_EXPIRE = 0,
  HOLD_TIMER_EXPIRE,
  SOCKET_ERR,
  PARSE_ERR,
  SESSION_ERR,
  NOTIFICATION_RCVD,
  MANUAL_STOP
};

std::string getResetReasonName(ResetReason reason);

/*
 * A NOTIFICATION sent to or received from a peer, with when it happened. Held
 * in one optional so the time cannot exist without its notification.
 */
struct TimestampedNotification {
  BgpNotification notification;
  // Milliseconds since epoch
  int64_t timeMs{0};
};

/*
 * Per-peer count of BGP messages actually written to / read from the peer's
 * socket, by message type. The source of truth lives on the I/O
 * (SessionManager) thread (FiberBgpPeer::sendSocketLoop for tx, the ingress
 * loop for rx) and is snapshotted into BgpPeerDisplayInfo on that same thread;
 * anything counted before the socket is not trustworthy. Correct even for
 * in-sync update-group peers, since each peer drains its own socket queue.
 */
struct SocketMessageCounters {
  uint64_t open{0};
  uint64_t update{0};
  uint64_t keepAlive{0};
  uint64_t notification{0};
  uint64_t routeRefresh{0};
  uint64_t endOfRib{0};
};

/*
 * All the information needed to be displayed
 * To hide implementation details of FiberBgpPeer we are copying the data
 * to new struct and returning rather than returning allPeers_ etc
 * This should handle both static and dynamic peers
 */
struct BgpPeerDisplayInfo {
  bgp::PeeringParams peeringParams;
  uint32_t remoteBgpId;
  std::optional<uint16_t> remoteGrRestartTime;
  BgpSessionState state;
  folly::SocketAddress localAddr;
  std::chrono::steady_clock::time_point startTime;
  std::chrono::steady_clock::time_point establishedTime;
  BgpCapabilities negotiatedCapabilities;
  std::optional<std::chrono::seconds> negotiatedHoldTime;
  uint32_t numOfConnectionAttempts;
  int64_t lastResetHoldTimer;
  int64_t lastResetKeepAliveTimer{0};
  int64_t lastReceivedKeepAlive;
  int64_t lastSentKeepAlive;
  uint32_t sendQueueBlocks{0};
  uint32_t totalSocketEgressBufferedEvents{0};
  uint64_t sendQueueTotalBlockDurationMs{0};
  uint64_t lastSendQueueBlockTimeMs{0};
  uint64_t lastSocketEgressBufferedTimeMs{0};
  std::optional<ResetReason> lastResetReason;
  std::chrono::steady_clock::time_point lastResetTime;
  int64_t numResets;
  SocketMessageCounters txMsgs;
  SocketMessageCounters rxMsgs;
  /*
   * Last NOTIFICATION seen on the session that went down, in each direction.
   * lastResetReason says the session reset; these say what the NOTIFICATION
   * actually carried, which the reason alone loses -- NOTIFICATION_RCVD does
   * not distinguish a peer's administrative shutdown from a collision
   * resolution. Unset until a NOTIFICATION is sent/received on the peer.
   */
  std::optional<TimestampedNotification> lastNotificationSent;
  std::optional<TimestampedNotification> lastNotificationReceived;
  /*
   * Remote (pre-negotiation) capabilities the peer advertised in its OPEN,
   * copied whole to mirror negotiatedCapabilities. Only mpExtExist() is
   * consumed today: it detects capability-less peers for legacy v4-unicast NLRI
   * encoding (a peer that advertised no MP-EXT receives classic NLRI). Unlike
   * negotiatedCapabilities.mpExtV4Unicast (deliberately true for
   * capability-less peers so v4 is still announced), this reflects what the
   * peer actually advertised.
   */
  BgpCapabilities remoteCapabilities;
  // Effective remote ASN accepted from OPEN for the current session.
  uint32_t remoteAs{0};
};

} // namespace facebook::nettools::bgplib
