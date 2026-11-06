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

#include <folly/Random.h>
#include <folly/coro/AsyncScope.h>
#include <folly/coro/BlockingWait.h>
#include <folly/logging/xlog.h>

#include <fb303/ThreadCachedServiceData.h>
#include "configerator/structs/neteng/fboss/bgp/if/gen-cpp2/bgp_attr_types.h"
#include "neteng/fboss/bgp/cpp/lib/BgpException.h"
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"
#include "neteng/fboss/bgp/cpp/lib/BgpUtil.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/BgpSerializer.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberBgpPeer.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/Utils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

using namespace std::chrono_literals;

namespace facebook {
namespace nettools {
namespace bgplib {

namespace {
BgpCapabilities buildBgpCapabilities(
    const uint32_t asn,
    const bool isRestarting,
    const std::optional<std::chrono::seconds>& restartTime,
    const bool mpExtV4Unicast,
    const bool mpExtV6Unicast,
    const bool v4OverV6Nexthop,
    const std::optional<neteng::fboss::bgp_attr::AddPath>& addPath,
    const bool mpExtV4LU,
    const bool mpExtV6LU,
    const bool enhancedRouteRefresh,
    const bool routeRefresh,
    const bool grNotification) {
  BgpCapabilities capabilities;
  capabilities.mpExtV4Unicast() = mpExtV4Unicast;
  capabilities.mpExtV6Unicast() = mpExtV6Unicast;
  capabilities.mpExtV4LU() = mpExtV4LU;
  capabilities.mpExtV6LU() = mpExtV6LU;
  capabilities.as4byte() = true;
  capabilities.asn() = asn;
  capabilities.enhancedRouteRefresh() = enhancedRouteRefresh;
  capabilities.routeRefresh() = routeRefresh;
  if (restartTime) {
    capabilities.gracefulRestart() = true;
    /*
     * Tell BGP peer to advertise bgp updates without waiting for EndOfRib
     * marker
     */
    capabilities.isRestarting() = isRestarting;
    // Estimated time for re-establishing connection after restart
    capabilities.restartTime() = restartTime->count();
    // RFC 8538 Notification (N) bit
    capabilities.grNotification() = grNotification;
    /*
     * Address families which support GR
     * forwardingState is set to true to workaround NX-OS's issue of purging
     * routes when this is false.
     */
    BgpGrCapability grCapa;
    grCapa.afi() = BgpUpdateAfi::AFI_IPv4;
    grCapa.safi() = BgpUpdateSafi::SAFI_UNICAST;
    grCapa.forwardingState() = true;
    capabilities.grCapabilities()->push_back(grCapa);
    grCapa.afi() = BgpUpdateAfi::AFI_IPv6;
    grCapa.safi() = BgpUpdateSafi::SAFI_UNICAST;
    grCapa.forwardingState() = true;
    capabilities.grCapabilities()->push_back(grCapa);
  }
  // RFC 5549
  if (v4OverV6Nexthop) {
    /*
     * We only support two the capability values with <1,1,2> and <1,4,2>
     * <1,1,2>
     */
    if (*capabilities.mpExtV4Unicast()) {
      BgpExtNHEncodingCapability capa;
      capa.nlriAfi() = BgpUpdateAfi::AFI_IPv4;
      capa.nlriSafi() = BgpUpdateSafi::SAFI_UNICAST;
      capa.nhAfi() = BgpUpdateAfi::AFI_IPv6;
      capabilities.extNHEncodingCapabilities()->push_back(capa);
    }
    // <1,4,2>
    if (*capabilities.mpExtV4Unicast() && *capabilities.mpExtV4LU()) {
      BgpExtNHEncodingCapability capa;
      capa.nlriAfi() = BgpUpdateAfi::AFI_IPv4;
      capa.nlriSafi() = BgpUpdateSafi::SAFI_LABELED_UNICAST;
      capa.nhAfi() = BgpUpdateAfi::AFI_IPv6;
      capabilities.extNHEncodingCapabilities()->push_back(capa);
    }
  }

  if (addPath) {
    BgpAddPathCapability addPathCapa;
    addPathCapa.afi() = BgpUpdateAfi::AFI_IPv4;
    addPathCapa.safi() = BgpUpdateSafi::SAFI_UNICAST;
    addPathCapa.sor() = BgpAddPathSendRec(int(*addPath));
    capabilities.addPathCapabilities()->push_back(addPathCapa);
    addPathCapa.afi() = BgpUpdateAfi::AFI_IPv6;
    addPathCapa.safi() = BgpUpdateSafi::SAFI_UNICAST;
    addPathCapa.sor() = BgpAddPathSendRec(int(*addPath));
    capabilities.addPathCapabilities()->push_back(addPathCapa);
  }
  return capabilities;
}
} // namespace

std::shared_ptr<FiberBgpPeer::ObservableSessionInfo>
FiberBgpPeer::getObservableSessionInfo(
    const BgpPeerDisplayInfo& peerInfo,
    std::shared_ptr<InputQueueT> iqueue,
    std::shared_ptr<BoundedInputQueueT> boundedIqueue,
    std::shared_ptr<OutputQueueT> oqueue,
    std::shared_ptr<VersionNumber>& currentVersion) {
  auto sessionInfo = std::make_shared<FiberBgpPeer::ObservableSessionInfo>(
      peerInfo,
      iqueue, // no queue destruction involved with shared_ptr
      boundedIqueue, // no queue destruction involved with shared_ptr
      oqueue, // no queue destruction involved with shared_ptr
      currentVersion);

  return sessionInfo;
}

FiberBgpPeer::FiberBgpPeer(
    const bgp::PeeringParams& peeringParams,
    folly::fibers::FiberManager& fm,
    folly::EventBase& evb,
    FiberSocket&& sock,
    std::shared_ptr<InputQueueT> iqueue,
    std::shared_ptr<BoundedInputQueueT> boundedIqueue,
    std::shared_ptr<OutputQueueT> oqueue,
    const bool isRestarting,
    const bool enableEgressQueueBackpressure,
    const bool enableSerializeGroupPdu,
    const bool enableGrNotification)
    : peeringParams_(peeringParams),
      peerIdOdsStr_(peeringParams.getUniquePeerId()),
      caps_(buildBgpCapabilities(
          peeringParams.localAs,
          isRestarting,
          peeringParams.grRestartTime,
          peeringParams.isAfiIpv4Configured,
          peeringParams.isAfiIpv6Configured,
          peeringParams.v4OverV6Nexthop,
          peeringParams.addPath,
          peeringParams.isAfiIpv4LUConfigured,
          peeringParams.isAfiIpv6LUConfigured,
          peeringParams.isEnhancedRouteRefreshConfigured,
          peeringParams.isRouteRefreshConfigured,
          enableGrNotification)),
      msgParser_(FiberBgpParser(caps_, rcvdQueue_)),
      fm_{fm},
      evb_{evb},
      sock_{std::move(sock)},
      iqueue_{iqueue},
      boundedIqueue_{boundedIqueue},
      oqueue_{oqueue},
      enableEgressQueueBackpressure_(enableEgressQueueBackpressure),
      enableSerializeGroupPdu_(enableSerializeGroupPdu) {
  /*
   * Monitor per-peer ingress queues
   */
  if (oqueue_) {
    monitorQueue(
        bgp::kQueueNameAdjRibIn,
        *oqueue_,
        bgp::MonitorableQueueTrace::Direction::OUT);
  }
  monitorQueue(
      bgp::kQueueNameParserOut,
      rcvdQueue_,
      bgp::MonitorableQueueTrace::Direction::OUT);

  /*
   * Monitor per-peer egress queues
   */
  if (enableEgressQueueBackpressure_) {
    if (boundedIqueue_) {
      monitorQueue(
          bgp::kQueueNameAdjRibOut,
          *boundedIqueue_,
          bgp::MonitorableQueueTrace::Direction::IN);
    }
    monitorQueue(
        bgp::kQueueNameSocketOut,
        boundedSendQueue_,
        bgp::MonitorableQueueTrace::Direction::OUT);
  } else {
    if (iqueue_) {
      monitorQueue(
          bgp::kQueueNameAdjRibOut,
          *iqueue_,
          bgp::MonitorableQueueTrace::Direction::IN);
    }
    monitorQueue(
        bgp::kQueueNameSocketOut,
        sendQueue_,
        bgp::MonitorableQueueTrace::Direction::OUT);
  }

  /*
   * Max-cap timer to wait for receiving of OPEN messages
   */
  openMsgTimer_ = folly::AsyncTimeout::make(evb_, [this]() noexcept {
    XLOGF(
        DBG1,
        "Open message timer expired for peer: [{}]",
        getRemotePeerAddrWithDescription());

    errorQueue_.put(BgpOpenMsgTimerExpired{});
  });

  /*
   * Max-cap timer to guarantee socket is closed.
   * NOTE: this timer is only scheduled when destruction happens.
   */
  socketCloseTimer_ = folly::AsyncTimeout::make(evb_, [this]() noexcept {
    if (socketClosed_) {
      return;
    }
    sock_.close();

    XLOG(DBG1, "[Exit] Socket is forced to close after max waiting time.");
  });
}

FiberBgpPeer::~FiberBgpPeer() {
  // close all timers
  socketCloseTimer_.reset();
  openMsgTimer_.reset();
}

BgpPeerId FiberBgpPeer::getRemoteBgpPeerId() noexcept {
  return BgpPeerId{
      peeringParams_.peerAddr,
      peeringState_.remoteBgpId,
      peeringParams_.description};
}

uint32_t FiberBgpPeer::getSendQueueBlocks() const {
  return peeringState_.sendQueueBlocks;
}

uint32_t FiberBgpPeer::getSocketEgressBufferedEvents() const {
  return sock_.getTotalAsyncSocketBufferedEvents();
}

uint64_t FiberBgpPeer::getSendQueueTotalBlockDuration() const {
  return peeringState_.sendQueueTotalBlockDurationMs;
}

uint64_t FiberBgpPeer::getLastSendQueueBlockTime() const {
  return peeringState_.lastSendQueueBlockTimeMs;
}

uint64_t FiberBgpPeer::getLastSocketEgressBufferedTime() const {
  return sock_.getLastSocketBufferedTimeMs();
}

bool FiberBgpPeer::processOpenMsgTimerExpiration() {
  XLOGF(
      ERR,
      "Open msg timer expired for peer: {}",
      getRemotePeerAddrWithDescription());

  // record the reason to reset
  peeringState_.resetReason = ResetReason::OPEN_MSG_TIMER_EXPIRE;

  // eligible for graceful restart
  return true;
}

bool FiberBgpPeer::processHoldTimerExpiration() {
  XLOGF(
      ERR,
      "Hold timer expired for peer: {}",
      getRemotePeerAddrWithDescription());

  // record the reason to reset
  peeringState_.resetReason = ResetReason::HOLD_TIMER_EXPIRE;

  // RFC 8538: eligible for graceful restart if the N bit is negotiated
  return grNotification_;
}

bool FiberBgpPeer::processBgpSessionError() {
  XLOGF(ERR, "Session error for peer {}", getRemotePeerAddrWithDescription());

  // record the reason to reset
  peeringState_.resetReason = ResetReason::SESSION_ERR;

  // not eligible for graceful restart
  return false;
}

bool FiberBgpPeer::processSocketError(const BgpSocketError& e) {
  FiberSocketErrorVisitor errVisitor;
  auto errStr = std::visit(errVisitor, e.err);

  XLOGF(
      ERR,
      "Socket error for peer [{}]: {}",
      getRemotePeerAddrWithDescription(),
      errStr);

  // record the reason to reset
  peeringState_.resetReason = ResetReason::SOCKET_ERR;

  // eligible for graceful restart
  return true;
}

bool FiberBgpPeer::processBgpParserError(const BgpParserError& e) {
  XLOGF(
      ERR,
      "Parser error for peer [{}]: {}",
      getRemotePeerAddrWithDescription(),
      e.errorMsg);

  // record the reason to reset
  peeringState_.resetReason = ResetReason::PARSE_ERR;

  // not eligible for graceful restart
  return false;
}

bool FiberBgpPeer::processBgpNotificationError(const BgpNotificationError& e) {
  /*
   * Any notification we will treat it as GR false. Going forward
   * we may expand it so that only certain notifications like admin shutdown
   * etc only lead to GR failure.
   * RFC 8538: if the N bit is negotiated, any notification except Cease/Hard
   * Reset is eligible for GR.
   */
  const bool isHardReset = isBgpHardResetNotification(e.notifyMsg);
  std::optional<BgpNotification> nestedNotification;
  if (isHardReset) {
    try {
      nestedNotification = decodeBgpHardResetNotification(*e.notifyMsg.data());
      XLOGF(
          ERR,
          "Cease Hard Reset from peer [{}]: Code = {} SubCode = {}. Msg: {}",
          getRemotePeerAddrWithDescription(),
          static_cast<uint16_t>(*nestedNotification->errCode()),
          *nestedNotification->errSubCode(),
          *nestedNotification->errSubCodeStr());
    } catch (const BgpException& ex) {
      XLOGF(
          ERR,
          "Malformed Hard Reset from peer [{}]: {}, not sending notification "
          "on already torn down session",
          getRemotePeerAddrWithDescription(),
          ex.what());
    }
  } else {
    XLOGF(
        ERR,
        "Notification from peer [{}]: Code = {} SubCode = {}. Msg: {}",
        getRemotePeerAddrWithDescription(),
        static_cast<uint16_t>(*e.notifyMsg.errCode()),
        *e.notifyMsg.errSubCode(),
        *e.notifyMsg.errSubCodeStr());
  }
  // Per-subcode handling uses the NOTIFICATION wrapped in a Hard Reset
  const auto& notifyMsg =
      nestedNotification ? *nestedNotification : e.notifyMsg;

  // record the reason to reset
  peeringState_.resetReason = ResetReason::NOTIFICATION_RCVD;
  /*
   * Retain the notification itself, not just the reason: NOTIFICATION_RCVD
   * alone cannot distinguish an administrative shutdown from a collision
   * resolution. Surfaced by `show bgp neighbors`.
   */
  peeringState_.lastNotificationReceived =
      TimestampedNotification{e.notifyMsg, getCurrentTimeMs()};

  if (*notifyMsg.errCode() == BgpNotifErrCode::BN_CEASE &&
      *notifyMsg.errSubCode() ==
          static_cast<int16_t>(
              BgpNotifCeaseErrSubCode::BN_CEASE_CONN_COLLISION_RES)) {
    bgp::PeerStats::incrConnectionCollisionClosedByPeer();
  }

  // RFC 8538: eligible for graceful restart unless it is a Hard Reset
  return grNotification_ && !isHardReset;
}

bool FiberBgpPeer::processBgpSessionStop(const BgpSessionStop& e) {
  XLOGF(
      INFO,
      "Administrative shutdown peer [{}], enter GR helper mode = {}, "
      "peerDelete = {}",
      getRemotePeerAddrWithDescription(),
      static_cast<bool>(e.gracefulRestart),
      static_cast<bool>(e.peerDelete));

  // record the reason to reset
  peeringState_.resetReason = ResetReason::MANUAL_STOP;
  peeringState_.peerDelete = e.peerDelete;

  // the eligibility of GR depends on BgpSessionStop message
  return e.gracefulRestart;
}

void FiberBgpPeer::run() noexcept {
  if (peeringParams_.holdTime.count() != 0 &&
      peeringParams_.holdTime.count() < 3) {
    XLOGF(
        ERR,
        "Hold time must be 0 or >= 3 seconds. Configured {}s for peer [{}]",
        peeringParams_.holdTime.count(),
        getRemotePeerAddrWithDescription());
    return;
  }

  /*
   * For config simplicity, use the holdTime value for BGP open message
   * receiving timer.
   */
  if (peeringParams_.holdTime.count() > 0) {
    openMsgTimer_->scheduleTimeout(
        std::chrono::seconds(peeringParams_.holdTime));
  }

  /*
   * [Inbound Processing]
   *
   * BGP ingress messages will be processed in 2 steps:
   *  1. read raw bytes from socket and parse into structured message
   *  2. process structured BGP message and send to AdjRib
   */
  {
    auto task = fm_.addTaskFuture([this] {
      readSocketLoop();
      XLOGF(
          DBG1,
          "[Exit] Successfully stopped socket reading task for {}",
          getRemotePeerAddrWithDescription());
    });
    fiberTasks_.emplace_back(std::move(task));
  }
  asyncScope_.add(co_withExecutor(&evb_, processIngressBgpMessageLoop()));

  /*
   * [Outbound Processing]
   *
   * BGP egress messages will be processed in 2 steps:
   *  1. read structured BGP message from iQueue, aka, adjRibOutQueue
   *  2. serialize message into bytes and send over socket
   *
   * If backpressure is enabled, this loop may yield to allow other
   * tasks to run when the bounded send queue is blocked for writing.
   */
  if (enableEgressQueueBackpressure_) {
    asyncScope_.add(
        co_withExecutor(&evb_, processEgressBgpMessageLoopWithBackpressure()));
  } else {
    asyncScope_.add(co_withExecutor(&evb_, processEgressBgpMessageLoop()));
  }
  {
    auto task = fm_.addTaskFuture([this] {
      sendSocketLoop();
      XLOGF(
          DBG1,
          "[Exit] Successfully stopped socket sending task for {}",
          getRemotePeerAddrWithDescription());

      // clear sendQueue_
      sendQueue_.close();
      /* Close boundedSendQueue_. */
      boundedSendQueue_.close();
    });
    fiberTasks_.emplace_back(std::move(task));
  }

  // RFC 6793/4893 Use AS_TRANS for open msg when using no mappable 4 bytes ASN
  int64_t asn_for_open_msg = static_cast<int64_t>(peeringParams_.localAs);
  if (*caps_.as4byte() && asn_for_open_msg > k2BytesAsnLimit) {
    asn_for_open_msg = kAsTrans;
  }

  BgpOpenMsg openMsg;
  openMsg.version() = kBgpVersion;
  openMsg.asn() = asn_for_open_msg;
  openMsg.holdTime() = static_cast<int32_t>(peeringParams_.holdTime.count());
  openMsg.bgpID() =
      static_cast<uint32_t>(peeringParams_.localBgpId.toLongHBO());
  openMsg.capabilities() = caps_;
  /* Space is guaranteed for open msg as nothing else should be queued yet. */
  sendBgpMessage(std::move(openMsg));

  // state is OPEN_SENT initially
  observerStateQueue_.put({getRemoteBgpPeerId(), peeringState_.state, 0});

  XLOGF(DBG1, "Peer {} started all fibers", getRemotePeerAddrWithDescription());

  // blocking call here to wait for signal of errors
  auto err = errorQueue_.getReader();
  auto errMsg = err.get();

  /*
   * NOTE: folly::variant_match will evaluate the callback functions by order.
   * Consider the frequency of the lambda functions when changing the order.
   */
  auto gracefulRestart = folly::variant_match(
      *errMsg,
      [this](const BgpSessionStop& e) { return processBgpSessionStop(e); },
      [this](const BgpSocketError& e) { return processSocketError(e); },
      [this](const BgpHoldTimerExpired&) {
        return processHoldTimerExpiration();
      },
      [this](const BgpNotificationError& e) {
        return processBgpNotificationError(e);
      },
      [this](const BgpOpenMsgTimerExpired&) {
        return processOpenMsgTimerExpiration();
      },
      [this](const BgpSessionError&) { return processBgpSessionError(); },
      [this](const BgpParserError& e) { return processBgpParserError(e); });

  if (!gracefulRestart) {
    bgp::PeerStats::incrNoGrRestart();
    bgp::PeerStats::incrPeerNoGrRestart(getRemoteBgpPeerId().str());
  }

  XLOGF(
      DBG1,
      "[Exit] Terminating all fibers for {} with GR flag = {}",
      getRemotePeerAddrWithDescription(),
      gracefulRestart ? "True" : "False");

  // reset all scheduled timers
  openMsgTimer_.reset();
  holdTimer_.reset();
  keepAliveTimer_.reset();

  /* Reset per-session counters. */
  peeringState_.sendQueueBlocks = 0;
  peeringState_.sendQueueTotalBlockDurationMs = 0;
  peeringState_.lastSendQueueBlockTimeMs = 0;

  // notify PeerManagerBase for session DOWN
  peeringState_.state = BgpSessionState::IDLE;
  observerStateQueue_.put(
      {getRemoteBgpPeerId(),
       peeringState_.state,
       0,
       peeringState_.remoteAs,
       std::nullopt,
       nullptr,
       peeringState_.peerDelete});

  // stop consumer of observerStateQueue_ and observerRcvdMessageQueue_
  observerStateQueue_.putNull();
  observerRcvdMessageQueue_.putNull();

  // start timer to make sure socket is closed
  socketCloseTimer_->scheduleTimeout(kSocketCloseWaitingTime);

  // stop processEgressBgpMessageLoop()
  if (enableEgressQueueBackpressure_) {
    boundedIqueue_->push(std::nullopt);
  } else {
    iqueue_->push(std::nullopt);
  }

  // notify AdjRib to stop processing updates
  if (oqueue_) {
    // stop pushing anything into the queue, only the BgpSessionStop below
    oqueue_->close();
    oqueue_->forcePush(BgpSessionStop{gracefulRestart});
  }

  // blocking wait to ensure all tasks are closed.
  folly::collectAll(fiberTasks_.begin(), fiberTasks_.end()).get();
  folly::coro::blockingWait(asyncScope_.cancelAndJoinAsync());

  XLOGF(
      INFO,
      "[Exit] All tasks terminiated for {}",
      getRemotePeerAddrWithDescription());
}

void FiberBgpPeer::stop(
    const std::optional<BgpNotifCeaseErrSubCode>& ceaseErrSubCode,
    const bool gracefulRestart,
    const bool peerDelete) noexcept {
  if (ceaseErrSubCode) {
    sendBgpMessage(buildBgpNotification(
        BgpNotifErrCode::BN_CEASE,
        static_cast<uint16_t>(*ceaseErrSubCode),
        "Cease Notification",
        ""));
    if (*ceaseErrSubCode ==
        BgpNotifCeaseErrSubCode::BN_CEASE_CONN_COLLISION_RES) {
      bgp::PeerStats::incrConnectionCollisionClosed();
    }
  }
  errorQueue_.put(BgpSessionStop{gracefulRestart, peerDelete});
}

void FiberBgpPeer::closeSocket() noexcept {
  sock_.close();
  socketClosed_ = true;
  if (socketCloseTimer_->isScheduled()) {
    socketCloseTimer_.reset();
  }
}

void FiberBgpPeer::sendSocketLoop() noexcept {
  sock_.setBufferCallback(peerIdOdsStr_);
  /*
   * Get and serialize BGP messages
   */
  uint16_t msgCnt{0};
  while (true) {
    /**
     * Don't run continuously to avoid hogging CPU by yielding after certain
     * number of messages are processed. The yield is necessary for:
     *  - unblocking ingress with keepalive receiving
     *  - unblocking egress with message sending
     */
    if (++msgCnt > kMsgBatchSizeToYield) {
      fiberSleepFor(1ms);
      msgCnt = 0;
    }

    FiberBgpParser::BgpMessageT msgVal;
    if (enableEgressQueueBackpressure_) {
      const auto& msg = boundedSendQueue_.get();
      if (!msg) {
        // this will cause the read loop to die
        closeSocket();
        break;
      }
      msgVal = *msg;
    } else {
      const auto& msg = sendQueue_.get();
      if (!msg) {
        // this will cause the read loop to die
        closeSocket();
        break;
      }
      msgVal = *msg;
    }

    // TODO: use folly::variant_match to simplify this usage
    BgpSerializer visitor(peeringState_.negotiatedCapabilities);
    auto msgBufPtr = std::visit(visitor, msgVal);

    // empty message
    if (msgBufPtr->empty()) {
      continue;
    }

    /*
     * The msgBufPtr is a circular doubly linked list of IOBufs.
     * When the list is size 1, there is no problem. But when the
     * list is greater than 1, we must be careful about the API we use to write.
     *
     *  msgBufPtr
     *     |
     *     v
     *   IOBuf1 <-> IOBuf2 <-> IOBuf3 (<-> IOBuf1)
     *
     * FiberSocket::write will pass the addr of one IOBuf (the 'head' of
     * the IOBuf chain) to AsyncSocket, but the underlying call is
     * AsyncSocket::writeChain. This API writes the entire chain (linked-list),
     * starting from the provided 'head' and ending at the 'tail'.
     * So we only need to invoke writeChain once.
     */
    auto const result = sock_.write(std::make_unique<folly::IOBuf>(*msgBufPtr));
    if (!result.hasValue()) {
      // trigger the error handling path to stop FiberBgpPeer
      errorQueue_.put(BgpSocketError{result.error()});

      bgp::PeerStats::incrMessagesSentSocketFailures(peerIdOdsStr_);
      sock_.close();
      socketClosed_ = true;
      if (socketCloseTimer_->isScheduled()) {
        socketCloseTimer_.reset();
      }
      break;
    }
    size_t bytesWritten = *result;
    resetKeepAliveTimer();

    // bump counters: bytes written and sent msg count according to msg type
    bgp::PeerStats::addBytesWrittenToAvg(bytesWritten);
    folly::variant_match(
        msgVal,
        [this](const BgpKeepAlive&) {
          bgp::PeerStats::incrKeepAliveMessagesSent(peerIdOdsStr_);
          ++peeringState_.txMsgs.keepAlive;
        },
        [this, bytesWritten](std::shared_ptr<const BgpUpdate2> const&) {
          bgp::PeerStats::incrUpdateMessagesSent();
          bgp::PeerStats::addUpdateBytesSentToAvg(bytesWritten);
          ++peeringState_.txMsgs.update;
        },
        [this, bytesWritten](const UpdateDescriptor&) {
          bgp::PeerStats::incrUpdateMessagesSent();
          bgp::PeerStats::addUpdateBytesSentToAvg(bytesWritten);
          ++peeringState_.txMsgs.update;
        },
        [this](const BgpOpenMsg&) {
          bgp::PeerStats::incrOpenMessagesSent(peerIdOdsStr_);
          ++peeringState_.txMsgs.open;
        },
        [this](const BgpEndOfRib&) {
          bgp::PeerStats::incrEndOfRibMessagesSent(peerIdOdsStr_);
          ++peeringState_.txMsgs.endOfRib;
        },
        [this](const BgpRouteRefresh&) {
          bgp::PeerStats::incrRouteRefreshMessagesSent(peerIdOdsStr_);
          ++peeringState_.txMsgs.routeRefresh;
        },
        [this](const BgpNotification& notification) {
          bgp::PeerStats::incrNotificationMessagesSent(peerIdOdsStr_);
          ++peeringState_.txMsgs.notification;
          /*
           * Captured here rather than at the ~9 buildBgpNotification call
           * sites: this visitor is the single point every sent NOTIFICATION
           * passes through, including the one AdjRibIn pushes onto the
           * adjRibOut queue instead of calling sendBgpMessage.
           */
          peeringState_.lastNotificationSent =
              TimestampedNotification{notification, getCurrentTimeMs()};
        },
        [](const auto&) {});
  } // while
}

void FiberBgpPeer::readSocketLoop() noexcept {
  /*
   * Input: content read from FiberSocket
   * Output: content write to FiberBgpParser
   */
  uint16_t msgCnt{0};
  while (true) {
    /**
     * Don't run continuously to avoid hogging CPU by yielding after certain
     * number of messages are processed.
     */
    if (++msgCnt > kMsgBatchSizeToYield) {
      fiberSleepFor(1ms);
      msgCnt = 0;
    }

    auto result = sock_.read(kMaxBgpMsgLen);

    // hold timer only exists after OPEN_CONFIRM
    if (holdTimer_) {
      resetHoldTimer();
    }

    // report error if socket read fails.
    if (!result.hasValue()) {
      // stop parser
      auto msg = FiberSocketError{result.error()};
      rcvdQueue_.fiberPush(
          folly::Try<FiberBgpParser::BgpMessageT>(std::move(msg)));
      rcvdQueue_.fiberPush(std::nullopt);
      break;
    }

    // chocked on EOF
    if ((*result)->length() == 0) {
      // stop parser
      auto msg = FiberGenericSocketError("detected EOF");
      rcvdQueue_.fiberPush(
          folly::Try<FiberBgpParser::BgpMessageT>(std::move(msg)));
      rcvdQueue_.fiberPush(std::nullopt);
      break;
    }

    // process message buf read from socket
    bgp::PeerStats::addBytesReadToAvg((*result)->length());
    msgParser_.processBgpMsgBuf(std::move(result.value()));
  }
}

void FiberBgpPeer::resetHoldTimer() noexcept {
  XLOGF(DBG4, "Reset hold timer for {}", getRemotePeerAddrWithDescription());

  // extend holdtimer with gracePeriod_ to prevent expiration
  holdTimer_->scheduleTimeout(peeringState_.holdTime + gracePeriod_);

  // remember last time we reset holdtimer
  peeringState_.lastResetHoldTimer = getCurrentTimeMs();
}

folly::coro::Task<void> FiberBgpPeer::processIngressBgpMessageLoop() noexcept {
  XLOGF(
      INFO,
      "Starting ingress BGP message processing coro task for {}",
      getRemotePeerAddrWithDescription());

  // We cannot do static -- "this" varies from peer to peer
  auto overload = folly::overload(
      [this](const BgpOpenMsg& msg) -> folly::coro::Task<void> {
        processOpenMsg(msg);
        bgp::PeerStats::addPeerMessagesRecvOpen(peerIdOdsStr_);
        ++peeringState_.rxMsgs.open;
        co_return;
      },
      [this](std::shared_ptr<const BgpUpdate2> const& msg)
          -> folly::coro::Task<void> {
        co_await processBgpUpdateMsg(msg);
        ++peeringState_.rxMsgs.update;
      },
      [](const UpdateDescriptor& msg) -> folly::coro::Task<void> {
        /*
         * UpdateDescriptor is handled on egress path, not ingress
         * If received on ingress, it's an internal error
         */
        XLOG(
            ERR,
            "Received UpdateDescriptor on ingress path - should not happen");
        co_return;
      },
      [this](const BgpKeepAlive&) -> folly::coro::Task<void> {
        processKeepAliveMsg();
        bgp::PeerStats::addPeerMessagesRecvKeepAlive(peerIdOdsStr_);
        ++peeringState_.rxMsgs.keepAlive;
        co_return;
      },
      [this](const BgpEndOfRib& msg) -> folly::coro::Task<void> {
        co_await processBgpEndOfRibMsg(msg);
        ++peeringState_.rxMsgs.endOfRib;
      },
      [this](const BgpNotification& msg) -> folly::coro::Task<void> {
        processBgpNotificationMsg(msg);
        bgp::PeerStats::addPeerMessagesRecvNotification(peerIdOdsStr_);
        ++peeringState_.rxMsgs.notification;
        co_return;
      },
      [this](const BgpRouteRefresh& msg) -> folly::coro::Task<void> {
        co_await processBgpRouteRefreshMsg(msg);
        bgp::PeerStats::addPeerMessagesRecvRouteRefresh(peerIdOdsStr_);
        ++peeringState_.rxMsgs.routeRefresh;
      },
      [this](const FiberSocketError& msg) -> folly::coro::Task<void> {
        processBgpSocketErrorMsg(msg);
        co_return;
      });

  // rcvdQueue_ now has a consumer
  auto consumerScope = rcvdQueue_.getConsumerScope();

  while (true) {
    auto maybeMsg = co_await co_awaitTry(rcvdQueue_.pop());

    /*
     * Termination condition
     *  1. exception happens
     *  2. received std::nullopt
     */
    if (!maybeMsg.hasValue() || !(*maybeMsg).has_value()) {
      // report generic parsing error
      errorQueue_.put(BgpParserError{"received null msg"});
      break;
    }

    // de-reference with folly::Try<T> and std::optional
    folly::Try<FiberBgpParser::BgpMessageT>& msg = **maybeMsg;
    if (msg.hasException()) {
      // handle parsing errors
      auto ex = msg.exception();
      auto errorMsg = folly::exceptionStr(ex).toStdString();
      errorQueue_.put(BgpParserError{errorMsg});
      makeNotificationFromException(ex);
      break;
    }

    // unpack folly::Try<BgpMessageT>
    co_await std::visit(overload, *msg);
  }

  XLOGF(
      INFO,
      "[Exit] Successfully stopped processIngressBgpMessageLoop for {}",
      getRemotePeerAddrWithDescription());
}

folly::coro::Task<void>
FiberBgpPeer::processEgressBgpMessageLoopWithBackpressure() noexcept {
  XLOGF(
      INFO,
      "Starting egress BGP message processing coro task with backpressure for {}",
      getRemotePeerAddrWithDescription());

  while (true) {
    auto maybeMsg = co_await co_awaitTry(boundedIqueue_->pop());

    if (boundedSendQueue_.isBlocked()) {
      ++peeringState_.sendQueueBlocks;

      /* Wait for space to free up in the bounded send queue before sending. */
      auto beforeWait = getCurrentTimeMs();

      co_await boundedSendQueue_.waitToPush();

      peeringState_.lastSendQueueBlockTimeMs = beforeWait;
      peeringState_.sendQueueTotalBlockDurationMs +=
          (getCurrentTimeMs() - beforeWait);
    }
    /*
     * Termination condition
     *  1. exception happens
     *  2. received std::nullopt
     */
    if (!maybeMsg.hasValue() || !(*maybeMsg).has_value()) {
      /*
       * The notification to stop the socketSendLoop() MUST happen AFTER
       * processing all items pushed into the queue. Otherwise, messages
       * over-the-air will be ignored.
       */
      boundedSendQueue_.push(std::nullopt);
      break;
    }

    // Convert variant to a super set variant.
    FiberBgpParser::BgpMessageT bgpMsg;
    folly::variant_match(
        **maybeMsg, // dereference folly::Try<T> and std::optional
        [&](std::shared_ptr<const BgpUpdate2> arg) { bgpMsg = arg; },
        [&](const UpdateDescriptor& arg) { bgpMsg = arg; },
        [&](BgpEndOfRib arg) { bgpMsg = arg; },
        [&](BgpNotification arg) { bgpMsg = arg; },
        [&](BgpRouteRefresh arg) { bgpMsg = arg; },
        [&](const auto& arg __attribute__((unused))) { assert(false); });

    boundedSendQueue_.push(std::move(bgpMsg));
  }

  XLOGF(
      INFO,
      "[Exit] Successfully stopped processEgressBgpMessageLoopWithBackpressure for {}",
      getRemotePeerAddrWithDescription());
}

folly::coro::Task<void> FiberBgpPeer::processEgressBgpMessageLoop() noexcept {
  XLOGF(
      INFO,
      "Starting egress BGP message processing coro task for {}",
      getRemotePeerAddrWithDescription());

  while (true) {
    auto maybeMsg = co_await co_awaitTry(iqueue_->pop());
    /*
     * Termination condition
     *  1. exception happens
     *  2. received std::nullopt
     */
    if (!maybeMsg.hasValue() || !(*maybeMsg).has_value()) {
      /*
       * The notification to stop the socketSendLoop() MUST happen AFTER
       * processing all items pushed into the queue. Otherwise, messages
       * over-the-air will be ignored.
       */
      sendQueue_.putNull();
      break;
    }

    // Convert variant to a super set variant.
    FiberBgpParser::BgpMessageT bgpMsg;
    folly::variant_match(
        **maybeMsg, // dereference folly::Try<T> and std::optional
        [&](std::shared_ptr<const BgpUpdate2> arg) { bgpMsg = arg; },
        [&](const UpdateDescriptor& arg) { bgpMsg = arg; },
        [&](BgpEndOfRib arg) { bgpMsg = arg; },
        [&](BgpNotification arg) { bgpMsg = arg; },
        [&](BgpRouteRefresh arg) { bgpMsg = arg; },
        [&](const auto& arg __attribute__((unused))) { assert(false); });

    sendQueue_.put(std::move(bgpMsg));
  }

  XLOGF(
      INFO,
      "[Exit] Successfully stopped processEgressBgpMessageLoop for {}",
      getRemotePeerAddrWithDescription());
}

void FiberBgpPeer::sendBgpMessage(FiberBgpParser::BgpMessageT&& msg) {
  if (enableEgressQueueBackpressure_) {
    /*
     * Special messages such as the OPEN msg, first KeepAlive,
     * and notification messages are an exception. All other producers
     * may not write over the highWm; this is how we guarantee
     * there is space for the notification message to be written
     * without fail. The caller of this method should verify if there
     * is space in the first unless this is a special message with
     * guaranteed space.
     */
    boundedSendQueue_.push(std::move(msg));
  } else {
    sendQueue_.put(std::move(msg));
  }
}

void FiberBgpPeer::processOpenMsg(const BgpOpenMsg& msg) {
  switch (peeringState_.state) {
    case BgpSessionState::OPEN_SENT: {
      openMsgTimer_.reset();

      /*
       * Process remote information
       */
      /*
       * RFC 6793 carries the ASN in the AS4 capability when present;
       * otherwise the two-byte ASN comes from the OPEN header.
       */
      const auto remoteAs = static_cast<uint32_t>(
          *msg.capabilities()->as4byte() ? *msg.capabilities()->asn()
                                         : *msg.asn());

      // only validate remote asn for things other than monitor.
      if (peeringParams_.validateRemoteAs) {
        const bool isRemoteAsAccepted =
            peeringParams_.acceptsRemoteAs(remoteAs);
        if (!isRemoteAsAccepted) {
          if (peeringParams_.additionalRemoteAs.has_value()) {
            XLOGF(
                ERR,
                "Peer {} has wrong ASN configured, ASN in MSG:{} does not "
                "match configured ASNs:{} or {}. Session will be terminated",
                peeringParams_.peerAddr.str(),
                remoteAs,
                peeringParams_.remoteAs,
                *peeringParams_.additionalRemoteAs);
          } else {
            XLOGF(
                ERR,
                "Peer {} has wrong ASN configured, ASN in MSG:{} != ASN "
                "configured:{}. Session will be terminated",
                peeringParams_.peerAddr.str(),
                remoteAs,
                peeringParams_.remoteAs);
          }
          sendBgpMessage(buildBgpNotification(
              BgpNotifErrCode::BN_OPEN_MSG_ERR,
              static_cast<uint16_t>(
                  BgpNotifOpenMsgErrSubCode::BN_OM_BAD_PEER_AS),
              "Peer has wrong ASN configured",
              std::string(
                  reinterpret_cast<const char*>(&remoteAs), sizeof(remoteAs))));
          bgp::BgpStats::incOpenRejectAsnMismatch();
          errorQueue_.put(BgpSessionError{});
          return;
        } // if
      }

      /*
       * The first keep alive sent by us is needed for transitioning
       * the session in peer's state machine. With egress backpressure
       * enabled, similar to notification,
       * we guarantee space in the queue to send the first KeepAlive.
       */
      sendBgpMessage(BgpKeepAlive{});

      peeringState_.remoteBgpId = *msg.bgpID();
      peeringState_.remoteAs = remoteAs;
      /*
       * negotiate capability mismatch
       */
      peeringState_.remoteCapabilities = *msg.capabilities();
      peeringState_.negotiatedCapabilities =
          negotiateCapabilities(caps_, *msg.capabilities());

      // Set peeringState with received information
      peeringState_.remoteHoldTime = std::chrono::seconds(*msg.holdTime());
      /*
       * Note that holdTime/keepAliveTime cannot be a non-zero value less than
       * 3s/1s at this point, respectively. remoteHoldTime verification is done
       * in BgpMessageParser and peeringParams_.holdTime verification is done at
       * the start of run().
       */
      peeringState_.holdTime =
          std::min(peeringState_.remoteHoldTime, peeringParams_.holdTime);
      peeringState_.keepAliveTime = peeringState_.holdTime / 3;

      if (peeringState_.holdTime.count() > 0) {
        // Create and schedule hold timer
        scheduleBgpHoldTimer();

        // Create and schedule keepAlive timer
        scheduleBgpKeepAliveTimer();
      }
      peeringState_.state = BgpSessionState::OPEN_CONFIRM;
      observerStateQueue_.put({getRemoteBgpPeerId(), peeringState_.state, 0});

      break;
    }
    default: {
      sendBgpMessage(buildBgpNotification(
          BgpNotifErrCode::BN_FSM_ERROR, 0, "Unexpected state for OPEN", ""));
      errorQueue_.put(BgpSessionError{});
      break;
    }
  } // switch
}

void FiberBgpPeer::scheduleBgpHoldTimer() {
  /*
   * Kick start BGP hold timer.
   *
   * Adding 20% of keepalive time as graceperiod for holdtime to
   * accommodate for jitter and message transport time from sender.
   * Capping maximum to 1.5 seconds.
   */
  holdTimer_ = folly::AsyncTimeout::make(evb_, [this]() noexcept {
    if (!sock_.readable()) {
      // notify peers over the socket
      sendBgpMessage(buildBgpNotification(
          BgpNotifErrCode::BN_HOLD_TIMER_EXPIRED,
          static_cast<uint16_t>(0),
          "0",
          ""));

      // trigger error handling processing flow
      errorQueue_.put(BgpHoldTimerExpired{});
    }
  });

  peeringState_.lastResetHoldTimer = 0;
  gracePeriod_ = std::chrono::milliseconds(
      std::min<long>(1500L, peeringState_.keepAliveTime.count() * (1000 / 5)));
  holdTimer_->scheduleTimeout(peeringState_.holdTime + gracePeriod_);
}

void FiberBgpPeer::resetKeepAliveTimer() {
  if (keepAliveTimer_) {
    keepAliveTimer_->scheduleTimeout(
        std::chrono::milliseconds(
            peeringState_.keepAliveTime.count() * 1000 + keepAliveJitter_));

    // remember last time we reset keepalive timer
    peeringState_.lastResetKeepAliveTimer = getCurrentTimeMs();
  }
}

void FiberBgpPeer::scheduleBgpKeepAliveTimer() {
  /*
   * Kick start BGP keep alive timer.
   *
   * Adding jitter upto +/- 10 percent of keepalive time in milliseconds.
   * Capping the max jitter to +/- 1 second.
   */
  keepAliveJitter_ = generateJitter(peeringState_.keepAliveTime.count() * 1000);

  auto keepAliveTimer = folly::AsyncTimeout::make(
      evb_, [this, jitter = keepAliveJitter_]() noexcept {
        if (!enableEgressQueueBackpressure_ || !boundedSendQueue_.isBlocked()) {
          /*
           * Without egress backpressure enabled, we will just queue KeepAlive
           * without question.
           * With egress backpressure enabled, we should only send KeepAlive
           * if the queue isn't blocked. This is because
           * KeepAlive is sent if the there is no traffic going to the peer.
           * If the queue is blocked, then the messages in the queue would
           * keep the session alive..
           */
          sendBgpMessage(BgpKeepAlive{});
          peeringState_.lastSentKeepAlive = getCurrentTimeMs();
        }

        // schedule keepAliveTimer_ periodically
        CHECK(keepAliveTimer_);
        keepAliveTimer_->scheduleTimeout(
            std::chrono::milliseconds(
                peeringState_.keepAliveTime.count() * 1000 + jitter));

        peeringState_.lastResetKeepAliveTimer = getCurrentTimeMs();
      });

  keepAliveTimer_ = std::move(keepAliveTimer);
  keepAliveTimer_->scheduleTimeout(
      std::chrono::milliseconds(
          peeringState_.keepAliveTime.count() * 1000 + keepAliveJitter_));
  peeringState_.lastResetKeepAliveTimer = 0;
}

void FiberBgpPeer::processKeepAliveMsg() {
  // remembered last time we get keepalive
  peeringState_.lastReceivedKeepAlive = getCurrentTimeMs();
  switch (peeringState_.state) {
    case BgpSessionState::OPEN_CONFIRM: {
      peeringState_.state = BgpSessionState::ESTABLISHED;
      grNotification_ =
          isGrNotificationNegotiated(peeringState_.negotiatedCapabilities);
      observerStateQueue_.put({getRemoteBgpPeerId(), peeringState_.state, 0});
      break;
    }
    case BgpSessionState::IDLE:
    case BgpSessionState::ESTABLISHED: {
      break;
    }
    default: {
      sendBgpMessage(buildBgpNotification(
          BgpNotifErrCode::BN_FSM_ERROR, 0, "Unexpected Keepalive", ""));
      errorQueue_.put(BgpSessionError{});
      break;
    }
  }
}

folly::coro::Task<void> FiberBgpPeer::processBgpUpdateMsg(
    std::shared_ptr<const BgpUpdate2> const& msg) {
  if (oqueue_) {
    co_await oqueue_->push(msg);
  }
  observerRcvdMessageQueue_.put({getRemoteBgpPeerId(), msg});
  co_return;
}

folly::coro::Task<void> FiberBgpPeer::processBgpEndOfRibMsg(
    const BgpEndOfRib& msg) {
  if (oqueue_) {
    co_await oqueue_->push(msg);
  }
  observerRcvdMessageQueue_.put({getRemoteBgpPeerId(), msg});
  co_return;
}

void FiberBgpPeer::processBgpNotificationMsg(const BgpNotification& msg) {
  switch (peeringState_.state) {
    case BgpSessionState::IDLE: {
      break;
    }
    default: {
      peeringState_.state = BgpSessionState::IDLE;
      observerStateQueue_.put({getRemoteBgpPeerId(), peeringState_.state, 0});
      errorQueue_.put(BgpNotificationError{msg});
      break;
    }
  }
}

folly::coro::Task<void> FiberBgpPeer::processBgpRouteRefreshMsg(
    const BgpRouteRefresh& msg) {
  if (oqueue_) {
    co_await oqueue_->push(msg);
  }
  observerRcvdMessageQueue_.put({getRemoteBgpPeerId(), msg});
  co_return;
}

void FiberBgpPeer::processBgpSocketErrorMsg(const FiberSocketError& msg) {
  // NOTE: notify errorQueue_ processing to stop fiber bgp peer
  errorQueue_.put(BgpSocketError{msg});
}

void FiberBgpPeer::makeNotificationFromException(folly::exception_wrapper& ex) {
  ex.handle(
      // Handle some of RFC 4271 section 6 error and create notification
      [&](BgpOpenMsgException& openMsgEx) {
        sendBgpMessage(buildBgpNotification(
            BgpNotifErrCode::BN_OPEN_MSG_ERR,
            static_cast<uint16_t>(openMsgEx.getSubCode()),
            "Invalid open message from peer",
            openMsgEx.getData()));
      },
      [&](BgpHeaderException& hdrMsgEx) {
        sendBgpMessage(buildBgpNotification(
            BgpNotifErrCode::BN_MSG_HDR_ERR,
            static_cast<uint16_t>(hdrMsgEx.getSubCode()),
            "Invalid header message from peer",
            hdrMsgEx.getData()));
      },
      [&](BgpUpdateMsgException& updateMsgEx) {
        sendBgpMessage(buildBgpNotification(
            BgpNotifErrCode::BN_UPDATE_MSG_ERR,
            static_cast<uint16_t>(updateMsgEx.getSubCode()),
            "Invalid update message from peer",
            updateMsgEx.getData()));
      },
      // Error handling for Route Refresh message per RFC 7313(Section 5)
      [&](BgpRouteRefreshMsgException& routeRefreshMsgEx) {
        BgpNotification bgpNotif;
        bgpNotif.errCode() = BgpNotifErrCode::BN_ROUTE_REFRESH_MSG_ERR;
        bgpNotif.errSubCode() =
            static_cast<uint16_t>(routeRefreshMsgEx.getSubCode());
        bgpNotif.data() = routeRefreshMsgEx.getData();
        sendBgpMessage(std::move(bgpNotif));
      },
      [&](...) {});
}
} // namespace bgplib
} // namespace nettools
} // namespace facebook
