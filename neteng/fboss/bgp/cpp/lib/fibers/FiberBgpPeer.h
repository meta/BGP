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

#include <folly/IPAddress.h>
#include <folly/SocketAddress.h>
#include <folly/coro/AsyncScope.h>
#include <folly/fibers/FiberManager.h>

#include "neteng/fboss/bgp/cpp/config/ConfigStructs.h"
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"
#include "neteng/fboss/bgp/cpp/lib/coro/BackPressuredQueue.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/BgpPeerDisplayInfo.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberBgpParser.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberSocket.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/Queue.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/VersionNumber.h"
#include "neteng/fboss/bgp/cpp/watchdog/MonitoredModule.h"
#include "neteng/fboss/bgp/cpp/watchdog/MonitoredQueue.h"
#include "neteng/fboss/bgp/if/gen-cpp2/BgpStructs_types.h"

namespace facebook {
namespace nettools {
namespace bgplib {

/*
 * Mutable state of peering session
 */
struct PeeringState {
  // The peer starts in OPEN_SENT state
  BgpSessionState state{BgpSessionState::OPEN_SENT};

  // Number uniquely represent remote peer
  uint32_t remoteBgpId{0};

  // Effective remote ASN accepted from OPEN for the current session
  uint32_t remoteAs{0};

  // Negotiated hold time
  std::chrono::seconds holdTime{0};

  // Keepalive time based on negotiated hold time
  std::chrono::seconds keepAliveTime{0};

  // Received hold time from peer
  std::chrono::seconds remoteHoldTime{0};

  /*
   * Capabilities received from the peer. For an accepted AS4 OPEN, asn
   * contains the remote ASN advertised in the AS4 capability.
   */
  BgpCapabilities remoteCapabilities;

  // Negotiated capabilities with peer
  BgpCapabilities negotiatedCapabilities;

  // The last time we reset holdTimer_
  int64_t lastResetHoldTimer;

  // The last time we reset keepAliveTimer_
  int64_t lastResetKeepAliveTimer{0};

  // The last time we get keepAlive
  int64_t lastReceivedKeepAlive;

  // The last time we send keepAlive
  int64_t lastSentKeepAlive;

  // Set only when session goes down from ESTABLISHED state
  std::optional<ResetReason> resetReason;

  // Captured from BgpSessionStop and read at the IDLE emission below.
  bool peerDelete{false};

  /* Number of times sendQueue_ blocked on a push attempt. */
  uint32_t sendQueueBlocks{0};

  /* Total sendQueue_ block duration. */
  uint64_t sendQueueTotalBlockDurationMs{0};

  /* Last epoch time (ms) that sendQueue blocked on a push attempt. */
  uint64_t lastSendQueueBlockTimeMs{0};

  /*
   * Per-message-type count of BGP messages written to / read from this peer's
   * socket. Written only on the I/O evb (tx in sendSocketLoop, rx in the
   * ingress loop); read cross-thread only via the BgpPeerDisplayInfo snapshot
   * taken on this same evb.
   */
  SocketMessageCounters txMsgs;
  SocketMessageCounters rxMsgs;
};

class FiberBgpPeer : public std::enable_shared_from_this<FiberBgpPeer>,
                     public bgp::MonitoredModule {
 public:
  /*
   * Signal to stop BGP session
   *
   * FiberBgpPeer -> AdjRib
   */
  struct BgpSessionStop {
    bool gracefulRestart{};
    // Set by dropPeer to request cleanupPeerState
    bool peerDelete{false};
  };

  /*
   * AdjRib(Out) -> FiberBgpPeer
   */
  using InputMessageT = std::variant<
      std::shared_ptr<const BgpUpdate2>,
      UpdateDescriptor,
      BgpEndOfRib,
      BgpRouteRefresh,
      BgpNotification>;
  using InputQueueT = bgp::MonitoredMPMCQueue<std::optional<InputMessageT>>;
  using BoundedInputQueueT =
      bgp::MonitoredMPMCWQueue<std::optional<InputMessageT>>;

  /*
   * FiberBgpPeer -> AdjRib(In)
   */
  using OutputMessageT = std::variant<
      std::shared_ptr<const BgpUpdate2>,
      BgpEndOfRib,
      BgpRouteRefresh,
      BgpSessionStop>;
  using OutputQueueT = MonitoredBackPressuredQueue<OutputMessageT>;

  /*
   * Signal of BGP session state(ESTABLISHED/IDLE) to upstream
   * (e.g. PeerManagerBase)
   */

  struct ObservableSessionInfo {
    std::optional<BgpPeerDisplayInfo> peerInfo{std::nullopt};
    std::shared_ptr<InputQueueT> inputQueue;
    std::shared_ptr<BoundedInputQueueT> boundedInputQueue;
    std::shared_ptr<OutputQueueT> outputQueue;
    std::shared_ptr<VersionNumber> currentVersion;
  };

  static std::shared_ptr<ObservableSessionInfo> getObservableSessionInfo(
      const BgpPeerDisplayInfo& peerInfo,
      std::shared_ptr<InputQueueT> iqueue,
      std::shared_ptr<BoundedInputQueueT> boundedIqueue,
      std::shared_ptr<OutputQueueT> outputQueue,
      std::shared_ptr<VersionNumber>& currentVersion);

  struct ObservableStateT {
    BgpPeerId peerId;
    BgpSessionState state;
    uint64_t versionNumber;
    uint32_t remoteAs{0};
    std::optional<ResetReason> lastResetReason{std::nullopt};
    std::shared_ptr<ObservableSessionInfo> sessionInfo{nullptr};
    // tells sessionTerminated to schedule cleanupPeerState.
    bool peerDelete{false};
  };

  using ObservableBgpMessageT = std::
      variant<std::shared_ptr<const BgpUpdate2>, BgpEndOfRib, BgpRouteRefresh>;

  struct ObservableMessageT {
    BgpPeerId peerId;
    ObservableBgpMessageT message;
  };

  FiberBgpPeer() = default;
  virtual ~FiberBgpPeer();

  FiberBgpPeer(
      const bgp::PeeringParams& peeringParams,
      folly::fibers::FiberManager& fm,
      folly::EventBase& evb,
      FiberSocket&& sock,
      std::shared_ptr<InputQueueT> iqueue,
      std::shared_ptr<BoundedInputQueueT> boundedIqueue,
      std::shared_ptr<OutputQueueT> oqueue,
      const bool isRestarting = false,
      const bool enableEgressQueueBackpressure = false,
      const bool enableSerializeGroupPdu = false);

  // movable
  FiberBgpPeer(FiberBgpPeer&&) = default;
  FiberBgpPeer& operator=(FiberBgpPeer&&) = default;

  /*
   * Start all the BgpPeer fibers
   */
  void run() noexcept;

  /*
   * Shut down all the BgpPeer fibers. Administrative stop event.
   * peerDelete is forwarded onto BgpSessionStop for the IDLE event.
   */
  void stop(
      const std::optional<BgpNotifCeaseErrSubCode>& ceaseErrSubCode =
          std::nullopt,
      const bool gracefulRestart = true,
      const bool peerDelete = false) noexcept;

  /*
   * Queue to publish internal state transition events
   */
  RQueue<ObservableStateT> getObserverStateQueue() noexcept {
    return observerStateQueue_.getReader();
  }

  /*
   * Queue to publish received Bgp Updates and EoRs
   */
  RQueue<ObservableMessageT> getObserverRcvdMessageQueue() noexcept {
    return observerRcvdMessageQueue_.getReader();
  }

  virtual folly::SocketAddress getLocalSocketAddress() const noexcept {
    return sock_.getLocalAddress();
  }

  virtual folly::SocketAddress getRemoteSocketAddress() const noexcept {
    return sock_.getPeerAddress();
  }

  std::string getRemotePeerDescription() const {
    return peeringParams_.description;
  }

  std::string getRemotePeerAddrWithDescription() const {
    return fmt::format(
        "peerAddr {}:{} ({})",
        peeringParams_.peerAddr.str(),
        peeringParams_.peerPort,
        peeringParams_.description);
  }

  uint32_t getRemoteBgpIdHBO() const {
    return peeringState_.remoteBgpId;
  }

  uint32_t getRemoteAs() const {
    return peeringState_.remoteAs;
  }

  uint32_t getLocalBgpIdHBO() const {
    return peeringParams_.localBgpId.toLongHBO();
  }

  std::optional<uint16_t> getRemoteGrRestartTime() const {
    const auto isGrCapabilityReceived =
        *peeringState_.remoteCapabilities.gracefulRestart();
    return (
        isGrCapabilityReceived &&
                *peeringState_.remoteCapabilities.restartTime()
            ? std::optional<uint16_t>(
                  *peeringState_.remoteCapabilities.restartTime())
            : std::nullopt);
  }

  BgpCapabilities getNegotiatedCapabilities() const {
    return peeringState_.negotiatedCapabilities;
  }

  BgpCapabilities getRemoteCapabilities() const {
    return peeringState_.remoteCapabilities;
  }

  std::optional<std::chrono::seconds> getNegotiatedHoldTime() const {
    return (peeringState_.state >= BgpSessionState::OPEN_CONFIRM)
        ? std::optional<std::chrono::seconds>(peeringState_.holdTime)
        : std::nullopt;
  }

  int64_t getLastResetHoldTimer() const {
    return peeringState_.lastResetHoldTimer;
  }

  int64_t getLastResetKeepAliveTimer() const {
    return peeringState_.lastResetKeepAliveTimer;
  }

  int64_t getLastRcvdKeepAlive() const {
    return peeringState_.lastReceivedKeepAlive;
  }

  int64_t getLastSentKeepAlive() const {
    return peeringState_.lastSentKeepAlive;
  }

  std::optional<ResetReason> getResetReason() const {
    return peeringState_.resetReason;
  }

  uint32_t getSendQueueBlocks() const;
  uint32_t getSocketEgressBufferedEvents() const;
  uint64_t getSendQueueTotalBlockDuration() const;
  uint64_t getLastSendQueueBlockTime() const;
  uint64_t getLastSocketEgressBufferedTime() const;

  const SocketMessageCounters& getTxMessageCounters() const {
    return peeringState_.txMsgs;
  }
  const SocketMessageCounters& getRxMessageCounters() const {
    return peeringState_.rxMsgs;
  }

  // Reset the socket tx (egress) message counters. I/O-evb only.
  void clearTxMessageCounters() {
    peeringState_.txMsgs = SocketMessageCounters{};
  }

  // Reset the socket rx (ingress) message counters. I/O-evb only.
  void clearRxMessageCounters() {
    peeringState_.rxMsgs = SocketMessageCounters{};
  }

 protected:
  // mutable state of peering session
  PeeringState peeringState_;

  // immutable peering parameters
  const bgp::PeeringParams peeringParams_;

  // peer identifier for per-peer ODS counters
  const std::string peerIdOdsStr_;

 private:
  // non-copyable
  FiberBgpPeer(FiberBgpPeer const&) = delete;
  FiberBgpPeer& operator=(FiberBgpPeer const&) = default;

  /*
   * Dummy types used for message passing internally
   */

  // generic type to reflect any parser error
  struct BgpParserError {
    const std::string errorMsg;
  };
  // errror establishing session
  struct BgpSessionError {};
  // Error reading/writing on socket
  struct BgpSocketError {
    FiberSocketError err;
  };
  // to report open msg timer error
  struct BgpOpenMsgTimerExpired {};
  // to report hold timer error
  struct BgpHoldTimerExpired {};
  // to report notification
  struct BgpNotificationError {
    const BgpNotification notifyMsg;
  };

  // aggregate for above types
  using BgpPeerError = std::variant<
      BgpOpenMsgTimerExpired,
      BgpHoldTimerExpired,
      BgpSocketError,
      BgpParserError,
      BgpSessionError,
      BgpNotificationError,
      BgpSessionStop>;

  /*
   * [Inbound Processing]
   *
   * Read raw messages from fiber socket and feed to FiberBgpParser.
   */
  void readSocketLoop() noexcept;

  /*
   * [Inbound Processing]
   *
   * Process ingress BGP messages depending on the state.
   */
  folly::coro::Task<void> processIngressBgpMessageLoop() noexcept;

  /*
   * [Outbound Processing]
   *
   * Process egress BGP messages received from adjRibOutQueue
   */
  folly::coro::Task<void> processEgressBgpMessageLoop() noexcept;
  folly::coro::Task<void>
  processEgressBgpMessageLoopWithBackpressure() noexcept;

  /*
   * [Outbound Processing]
   *
   * Read messages from sendQueue_, serialize and write
   * them to the socket, report errors into errorQueue_;
   */
  void sendSocketLoop() noexcept;

  /*
   * Util functions for various BgpMessage handlers.
   */
  void processOpenMsg(const BgpOpenMsg& msg);
  void processKeepAliveMsg();
  folly::coro::Task<void> processBgpUpdateMsg(
      std::shared_ptr<const BgpUpdate2> const& msg);
  folly::coro::Task<void> processBgpEndOfRibMsg(const BgpEndOfRib& msg);
  void processBgpNotificationMsg(const BgpNotification& msg);
  folly::coro::Task<void> processBgpRouteRefreshMsg(const BgpRouteRefresh& msg);
  void processBgpSocketErrorMsg(const FiberSocketError& msg);
  /*
   * @brief: Queues a notification message for socket.
   *
   * @details: If egress backpressure is enabled, write to boundedSendQueue_;
   * space is guaranteed because all other producers may not write
   * over the queue high watermark. If disabled, write to sendQueue_ which is
   * unbounded.
   */
  void sendBgpMessage(FiberBgpParser::BgpMessageT&& msg);

  /* Closes the socket and cancels the close timer. */
  void closeSocket() noexcept;

  // Util function to extend/reset hold timer
  void resetHoldTimer() noexcept;

  /*
   * Util functions for various BgpErrorMessage handlers.
   */
  bool processOpenMsgTimerExpiration();
  bool processHoldTimerExpiration();
  bool processBgpSessionError();
  bool processSocketError(const BgpSocketError& e);
  bool processBgpParserError(const BgpParserError& e);
  bool processBgpNotificationError(BgpNotificationError const& e);
  bool processBgpSessionStop(BgpSessionStop const& e);

  // Handle parsing exceptions and create notifications
  void makeNotificationFromException(folly::exception_wrapper& ex);

  // local util function to generate the BgpPeerId from peer.
  nettools::bgplib::BgpPeerId getRemoteBgpPeerId() noexcept;

  // util function after OPEN message negotiation
  void scheduleBgpHoldTimer();
  void resetKeepAliveTimer();
  void scheduleBgpKeepAliveTimer();

  // BGP capability deduced from peering parameters for the peers
  const BgpCapabilities caps_;

  // parser of ingress BGP ingress messages
  FiberBgpParser msgParser_;

  // fiber manager and event base for fiber/coro task scheduling
  folly::fibers::FiberManager& fm_;
  folly::EventBase& evb_;

  // per-peer asyncScope for task cancellation
  folly::coro::CancellableAsyncScope asyncScope_;

  // fiber task collection for clean exit
  std::vector<folly::Future<folly::Unit>> fiberTasks_{};

  // socket for message reading/writing
  FiberSocket sock_;

  // flag indicating socket has already been closed
  bool socketClosed_{false};

  /*
   * [Outbound Processing]
   *
   * NOTE: "iqueue" is named after the fact it is "input queue" from
   * FiberBgpPeer pov.
   */
  std::shared_ptr<InputQueueT> iqueue_;

  /**
   * If egress backpressure is enabled, we use boundedIqueue_ instead of
   * iqueue_ to receive InputMessages.
   *
   * This queue can backpressure producers who are filling the
   * queue past queue capacity.
   */
  std::shared_ptr<BoundedInputQueueT> boundedIqueue_;

  /*
   * [Egress Processing]
   *
   * Queue to send update out to socket.
   */
  bgp::MonitoredRWQueue<FiberBgpParser::BgpMessageT> sendQueue_;

  /**
   * If egress backpressure is enabled, we use boundedSendQueue_ instead of
   * sendQueue_ to pass messages to socket.
   */
  bgp::MonitoredMPMCWQueue<std::optional<FiberBgpParser::BgpMessageT>>
      boundedSendQueue_ =
          bgp::MonitoredMPMCWQueue<std::optional<FiberBgpParser::BgpMessageT>>(
              kMaxEgressQueueSize,
              kEgressQueueHighWatermark,
              kEgressQueueLowWatermark);

  /*
   * [Inbound Processing]
   *
   * Queue to process parsed BGP messages from socket.
   * NOTE: "oqueue" is named after the fact it is "output queue" from
   * FiberBgpPeer pov.
   */
  std::shared_ptr<OutputQueueT> oqueue_;
  MonitoredBackPressuredQueue<
      std::optional<folly::Try<FiberBgpParser::BgpMessageT>>>
      rcvdQueue_{kMaxIngressQueueSize};

  /**
   * Flag to enable egress queue backpressure. When this flag is enabled,
   * boundedIqueue_ and boundedSendQueue_ are used to carry messages
   * from egress to socket. The producer to each queue will
   * handle backpressure from the queue.
   */
  const bool enableEgressQueueBackpressure_{false};

  /**
   * Flag to enable group PDU serialization for zero-copy distribution.
   * When enabled, group serializes BGP updates once and distributes
   * UpdateDescriptor to peers. I/O thread clones and mutates nexthop only.
   */
  const bool enableSerializeGroupPdu_{false};

  /*
   * RFC 8538: set when the session becomes ESTABLISHED with the Graceful
   * Restart Notification (N) bit negotiated. NOTIFICATIONs received on this
   * session, except Cease/Hard Reset, follow Graceful Restart.
   */
  bool grNotification_{false};

  /*
   * This reports the state transitions for this peer
   */
  RWQueue<ObservableStateT> observerStateQueue_;

  /*
   * This reports the received Bgp Updates and EoRs from this peer
   */
  RWQueue<ObservableMessageT> observerRcvdMessageQueue_;

  /*
   * [Exception Handling]
   *
   * Error notification to stop the sub-fiber/coros
   */
  RWQueue<BgpPeerError> errorQueue_;

  /*
   * [Timers]
   */

  // Timer to ensure socket will be closed after a certain waiting time.
  std::unique_ptr<folly::AsyncTimeout> socketCloseTimer_;

  // Timer to receive OPEN message
  std::unique_ptr<folly::AsyncTimeout> openMsgTimer_;

  // Timer to mark hold-timer expiration
  std::unique_ptr<folly::AsyncTimeout> holdTimer_;
  std::chrono::milliseconds gracePeriod_{0};

  // Timer to generate a tick every keepAlive period
  std::unique_ptr<folly::AsyncTimeout> keepAliveTimer_;
  long keepAliveJitter_{0};

#ifdef FiberBgpPeer_TEST_FRIENDS
  FiberBgpPeer_TEST_FRIENDS
#endif
};

} // namespace bgplib
} // namespace nettools
} // namespace facebook
