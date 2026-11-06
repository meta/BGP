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

#include <folly/ScopeGuard.h>
#include <folly/container/F14Set.h>
#include <folly/coro/BlockingWait.h>
#include <atomic>

#include <fb303/ThreadCachedServiceData.h>
#include "magic_enum/magic_enum.hpp"
#include "neteng/fboss/bgp/cpp/common/Utils.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberBgpPeerManager.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/Queue.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/Utils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

namespace facebook {
namespace nettools {
namespace bgplib {

DEFINE_bool(
    disable_jumbo_frame_for_bgp,
    false,
    "Disable jumbo frame support for BGP peering");

using bgp::TBgpSessionConnectMode;
using folly::AsyncSocket;
using folly::Expected;
using folly::IPAddress;
using folly::makeUnexpected;
using folly::SocketAddress;
using folly::Unit;
using folly::fibers::addTask;
using neteng::fboss::bgp_attr::AdvertiseLinkBandwidth;
using neteng::fboss::bgp_attr::ReceiveLinkBandwidth;
using std::shared_ptr;

namespace {

// Convenience wrapper that bakes in the disable_jumbo_frame_for_bgp gflag.
folly::SocketOptionMap getBgpSockOptions(bool isV6) {
  return getSockOptions(isV6, FLAGS_disable_jumbo_frame_for_bgp);
}

shared_ptr<BgpPeerActiveConnectInfo> populateBgpPeerActiveConnectInfo(
    const IPAddress& peerAddr,
    const uint16_t peerPort,
    const SocketAddress& bindAddr,
    const ConnTimeParams& connTimeParams,
    std::optional<int32_t> ttlSecurityHops = std::nullopt) noexcept {
  auto activeConnectInfo = std::make_shared<BgpPeerActiveConnectInfo>();
  activeConnectInfo->peerAddr = {peerAddr, peerPort};
  activeConnectInfo->localAddr = bindAddr;
  activeConnectInfo->connTimeParams = connTimeParams;
  activeConnectInfo->ttlSecurityHops = ttlSecurityHops;
  if (connTimeParams.isConnectBackoffEnabled()) {
    activeConnectInfo->connectBackoff =
        facebook::fboss::ExponentialBackoff<std::chrono::milliseconds>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                connTimeParams.getMinRetryTimeout()),
            std::chrono::duration_cast<std::chrono::milliseconds>(
                *connTimeParams.getMaxRetryTimeout()));
    // Report error to kickstart exponential backoff with min retry interval
    activeConnectInfo->connectBackoffReportError();
  }
  if (connTimeParams.isSessionBackoffEnabled()) {
    activeConnectInfo->sessionBackoff =
        facebook::fboss::ExponentialBackoff<std::chrono::milliseconds>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                connTimeParams.getMinSessionRetryTimeout()),
            std::chrono::duration_cast<std::chrono::milliseconds>(
                connTimeParams.getMaxSessionRetryTimeout()));
    // Report error to kickstart exponential backoff with min retry interval
    activeConnectInfo->sessionBackoffReportError();
  }
  return activeConnectInfo;
}

std::unique_ptr<BgpEndOfRib> createEndOfRib(BgpUpdateAfi afi) {
  auto eor = std::make_unique<BgpEndOfRib>();
  *eor->isMpEor() = afi != BgpUpdateAfi::AFI_IPv4;
  *eor->afi() = afi;
  *eor->safi() = BgpUpdateSafi::SAFI_UNICAST;
  return eor;
}

/*
 * Apply GTSM (RFC 5082) socket options to a connected socket.
 * Returns true on success, false if any setsockopt failed.
 */
bool applyGtsmSockOptions(
    FiberSocket& socket,
    const folly::IPAddress& peerAddr,
    bool isV6,
    const std::optional<int32_t>& ttlSecurityHops) {
  auto gtsmOptions = getGtsmSockOptions(isV6, ttlSecurityHops);
  for (const auto& option : gtsmOptions) {
    auto ret = socket.setSockOpt(
        option.first.level, option.first.optname, &option.second);
    if (ret) {
      XLOGF(
          ERR,
          "GTSM setsockopt error for {}: level={} optname={} errno={}",
          peerAddr.str(),
          option.first.level,
          option.first.optname,
          ret);
      return false;
    }
  }
  return true;
}

} // namespace

std::ostream& operator<<(std::ostream& os, FiberBgpPeerManager::ErrorCode ec) {
  switch (ec) {
    case FiberBgpPeerManager::ErrorCode::PEER_DOES_NOT_EXIST:
      os << "PEER_DOES_NOT_EXIST";
      break;
    case FiberBgpPeerManager::ErrorCode::BIND_PEER_ADDRS_ARE_DIFF_AFIS:
      os << "BIND_PEER_ADDRS_ARE_DIFF_AFIS";
      break;
    case FiberBgpPeerManager::ErrorCode::PEER_EXISTS_ALREADY:
      os << "PEER_EXISTS_ALREADY";
      break;
    case FiberBgpPeerManager::ErrorCode::PEER_NOT_ESTABLISHED:
      os << "PEER_NOT_ESTABLISHED";
      break;
    default:
      os << static_cast<uint8_t>(ec);
  }

  return os;
}

FiberBgpPeerManager::FiberBgpPeerManager(
    const bgp::BgpGlobalConfig& config,
    folly::fibers::FiberManager& fm,
    folly::EventBase& evb,
    bool enableMessagesOverNotifyQueue,
    bool enableCoroNotifyQueue)
    : bgp::BgpModuleBase("fiber_bgp_peer_manager"),
      bgpGlobalConfig_(config),
      fm_(fm),
      evb_(evb),
      enableMessagesOverNotifyQueue_(enableMessagesOverNotifyQueue),
      enableCoroNotifyQueue_(enableCoroNotifyQueue),
      enableEgressQueueBackpressure_(config.enableEgressQueueBackpressure),
      enableSerializeGroupPdu_(
          config.updateGroupConfig.enableSerializeGroupPdu),
      enableGrNotification_(config.enableGrNotification) {}

FiberBgpPeerManager::FiberBgpPeerManager(
    const bgp::BgpGlobalConfig& config,
    bool enableMessagesOverNotifyQueue,
    bool enableCoroNotifyQueue)
    : bgp::BgpModuleBase("fiber_bgp_peer_manager"),
      bgpGlobalConfig_(config),
      fm_(folly::fibers::getFiberManager(
          bgp::BgpModuleBase::evb_,
          getFiberManagerOptions(64))),
      evb_(bgp::BgpModuleBase::evb_),
      enableMessagesOverNotifyQueue_(enableMessagesOverNotifyQueue),
      enableCoroNotifyQueue_(enableCoroNotifyQueue),
      enableEgressQueueBackpressure_(config.enableEgressQueueBackpressure),
      enableSerializeGroupPdu_(
          config.updateGroupConfig.enableSerializeGroupPdu),
      enableGrNotification_(config.enableGrNotification) {}

FiberBgpPeerManager::~FiberBgpPeerManager() {
  shutdownWithGR(false);
}

void FiberBgpPeerManager::shutdownWithGR(bool gracefulRestart) noexcept {
  alreadyShutdown_ = true;
  errorQueue_.put(BgpPeerManagerStop{gracefulRestart});
}

void FiberBgpPeerManager::setServerSocketOptions() noexcept {
  auto lambdaSetSockOptionsForAFamily = [&](int isIpv6) {
    auto options = getBgpSockOptions(isIpv6);
    for (auto const& option : options) {
      auto ret = serverSocket_->setSockOpt(
          option.first.level,
          option.first.optname,
          &option.second,
          sizeof(option.second));
      if (ret) {
        XLOGF(
            DBG1,
            "Setsockopt error for {} {}: errno = {}",
            option.first.level,
            option.first.optname,
            ret);
      }
    }
  };

  /*
   * Set TOS socket option for both v4 and v6 family on listening socket
   * This ensures that syn-ack which is replied before accept call returns
   * a new socket is also replied with proper TOS values.
   */
  lambdaSetSockOptionsForAFamily(true);
  lambdaSetSockOptionsForAFamily(false);
}

std::unique_ptr<FiberServerSocket> FiberBgpPeerManager::makeServerSocket(
    const std::optional<folly::SocketAddress>& listenAddr) const noexcept {
  return std::make_unique<FiberServerSocket>(listenAddr);
}

void FiberBgpPeerManager::run() noexcept {
  folly::fibers::Semaphore passiveConnectLoopStartSemaphore{1};

  if (bgpGlobalConfig_.enableServerSocket) {
    /*
     * Enable passive connect loop only if required.
     * Services like openr-bgp, vip injector etc do not need to turn it on
     */
    auto listenAddr =
        (bgpGlobalConfig_.listenAddr ? *bgpGlobalConfig_.listenAddr
                                     : AsyncSocket::anyAddress());
    serverSocket_ = makeServerSocket(listenAddr);
    setServerSocketOptions();

    passiveConnectLoopStartSemaphore.wait();

    fm_.addTask([me = shared_from_this(), &passiveConnectLoopStartSemaphore] {
      me->passiveConnectLoop(passiveConnectLoopStartSemaphore);
      XLOG(DBG1, "Passive connect loop finished for FiberBgpPeerManager.");
    });
  }

  auto collectAllPeerWorkers = fm_.addTaskFuture([me = shared_from_this()] {
    bool tryExit{false};
    while (true) {
      auto msg = me->stoppedPeerWorkerIdQ_.get();
      if (!msg) {
        // notice this does not break loop immediately
        tryExit = true;
      } else {
        me->peerWorkerIds_.erase(*msg);
      }
      /*
       * only exit when process signal us it's time to exit,
       * and all peer worker loops finished execution
       */
      if (tryExit && me->peerWorkerIds_.empty()) {
        break;
      }
    }
    XLOGF(
        DBG1,
        "All peer worker loops finished. Current peer worker id = {}",
        me->peerWorkerId_);
  });

  XLOG(DBG1, "FiberPeerManager started all fibers");

  // wait on errorQueue notification to close sub fiber tasks
  auto err = errorQueue_.getReader();
  auto errMsg = err.get();

  shutdownInProgress();

  auto gracefulRestart = folly::variant_match(
      *errMsg,
      [](FiberSocketError const& error) {
        FiberSocketErrorVisitor errVisitor;
        auto errStr = std::visit(errVisitor, error);
        XLOGF(
            ERR,
            "Shutting down peer manager fibers: got socket error {}",
            errStr);
        return true;
      },
      [](BgpPeerManagerStop const& error) {
        XLOGF(
            INFO,
            "Shutting down peer manager fibers: got stop message with GR = {}",
            error.gracefulRestart);
        return error.gracefulRestart;
      });

  if (bgpGlobalConfig_.enableServerSocket) {
    passiveConnectLoopStartSemaphore.wait();
    // stop passiveConnectLoop fiber
    serverSocket_->close();
  }
  // stop all peer workers
  shutdownFibers(gracefulRestart);
  // signal collectAllPeerWorkers loop that process is going down
  stoppedPeerWorkerIdQ_.putNull();
  // wait for all peers to complete
  folly::collect(collectAllPeerWorkers).get();

  // stop notification to client
  notifyQueue_.putNull();

  // wait for all coro tasks to finish
  folly::coro::blockingWait(asyncScope_.cancelAndJoinAsync());

  XLOG(DBG1, "[Exit] FiberBgpPeerManager run loop terminated");
}

void FiberBgpPeerManager::shutdownFibers(bool gracefulRestart) noexcept {
  for (const auto& [_, peerInfo] : allPeers_) {
    for (const auto& [_, connectionInfo] : peerInfo->connectionInfos) {
      // Stop activeConnect
      auto activeConnectInfo = connectionInfo->activeConnectInfo;
      if (activeConnectInfo) {
        if (activeConnectInfo->pendingTimeout) {
          activeConnectInfo->pendingTimeout->cancelTimeout();
          // Reset to destroy the callback lambda and break ref cycles
          activeConnectInfo->pendingTimeout.reset();
        }
        activeConnectInfo->connectBackoffReportSuccess();
        if (activeConnectInfo->isSessionBackoffEnabled()) {
          activeConnectInfo->sessionBackoffReportSuccess();
        }
      }
      // Stop bgp session
      if (connectionInfo->activeSessionInfo) {
        connectionInfo->activeSessionInfo->peer->stop(
            std::nullopt, gracefulRestart);
      }
    }
  }
}

void FiberBgpPeerManager::processObservableEventLoop(
    shared_ptr<BgpPeerInfoInternal> peerInfo,
    shared_ptr<BgpPeerActiveSessionInfo> activeSessionInfo) noexcept {
  if (shutdownInProgress_) {
    return;
  }

  auto reader = activeSessionInfo->observeQueue.getReader();
  while (true) {
    auto msg = reader.get();
    if (!msg) {
      break;
    }
    folly::variant_match(
        *msg,
        [this, peerInfo, activeSessionInfo](
            FiberBgpPeer::ObservableStateT& stateEvt) {
          // Process observable state
          processObservableState(peerInfo, activeSessionInfo, stateEvt);
        },
        [this, peerInfo, activeSessionInfo](
            FiberBgpPeer::ObservableMessageT& msgEvt) {
          // Process observable message
          processObservableMessage(peerInfo, activeSessionInfo, msgEvt);
        });
  }
}

void FiberBgpPeerManager::processObservableState(
    std::shared_ptr<BgpPeerInfoInternal> peerInfo,
    std::shared_ptr<BgpPeerActiveSessionInfo> activeSessionInfo,
    FiberBgpPeer::ObservableStateT& stateEvt) {
  const auto& newState = stateEvt.state;
  const auto& remoteBgpId = stateEvt.peerId.remoteBgpId;
  const auto& localAddr = activeSessionInfo->peer->getLocalSocketAddress();
  const auto& remoteAddr = activeSessionInfo->peer->getRemoteSocketAddress();
  const auto remotePeerDescription =
      activeSessionInfo->peer->getRemotePeerDescription();

  XLOGF(
      DBG3,
      "Session [local [{}]:{}, remote [{} ({})]:{}] trying state transition: "
      "{} -> {} ",
      getAddressStr(localAddr),
      localAddr.getPort(),
      getAddressStr(remoteAddr),
      remotePeerDescription,
      remoteAddr.getPort(),
      getBgpSessionStateName(activeSessionInfo->state),
      getBgpSessionStateName(newState));

  switch (newState) {
    case BgpSessionState::OPEN_CONFIRM:
      /*
       * collision detection case 1: bgp id comparison
       * allow session open if remoteBgpId is different
       */
      for (const auto& [_, connectionInfo] : peerInfo->connectionInfos) {
        const auto& sessionInfo = connectionInfo->activeSessionInfo;
        if (!sessionInfo || (sessionInfo == activeSessionInfo) ||
            (sessionInfo->state != BgpSessionState::OPEN_CONFIRM) ||
            sessionInfo->peer->getRemoteBgpIdHBO() != remoteBgpId) {
          continue;
        }
        auto thisPeer = activeSessionInfo->peer;
        if (!needToKeepThisPeer(getListenAddress(), thisPeer)) {
          shutdownPeerDueToCollision(thisPeer);
          // to ignore any further state transition for this peer
          activeSessionInfo->changeStateTo(BgpSessionState::IDLE);
          return;
        }
        auto otherPeer = sessionInfo->peer;
        shutdownPeerDueToCollision(otherPeer);
        // to ignore any further state transition for this peer
        sessionInfo->changeStateTo(BgpSessionState::IDLE);
      }
      break;

    case BgpSessionState::ESTABLISHED:
      if (activeSessionInfo->state != BgpSessionState::OPEN_CONFIRM) {
        return;
      }
      {
        /*
         * collision detection case 2: keep the already established session
         * bgp id is the same
         */
        for (const auto& [_, sessionInfo] : peerInfo->sessionInfos) {
          if (sessionInfo->establishedSessionInfo &&
              sessionInfo->establishedSessionInfo != activeSessionInfo &&
              sessionInfo->establishedSessionInfo->peer->getRemoteBgpIdHBO() ==
                  remoteBgpId) {
            shutdownPeerDueToCollision(activeSessionInfo->peer);
            // to ignore any further state transition for this peer
            activeSessionInfo->changeStateTo(BgpSessionState::IDLE);
            return;
          }
        }
      }

      // session comes up
      if (!peerInfo->sessionInfos.contains(remoteBgpId)) {
        peerInfo->sessionInfos.emplace(
            remoteBgpId, std::make_shared<BgpSessionInfo>());
      }
      {
        auto sessionInfo = peerInfo->sessionInfos.at(remoteBgpId);

        /**
         * If BGP session is established after dampening duration,
         * since last reset, then reset exponential backoff
         */
        auto activeConnectInfo =
            getListenPortActiveConnectInfoFromPeerInfo(peerInfo);
        if (activeConnectInfo && activeConnectInfo->isSessionBackoffEnabled() &&
            !activeConnectInfo->inSessionDampenDuration(
                sessionInfo->lastResetTime)) {
          activeConnectInfo->sessionBackoffReportSuccess();
        }

        sessionInfo->connectionInfo =
            peerInfo->connectionInfos[remoteAddr.getPort()];
        peerInfo->connectionInfos[remoteAddr.getPort()]->lastSessionInfo =
            std::weak_ptr<BgpSessionInfo>();
        sessionInfo->establishedTime = std::chrono::steady_clock::now();
        auto newVersionNumber = sessionInfo->versionNumber->bumpUp();
        sessionInfo->establishedSessionInfo = activeSessionInfo;
        stateEvt.versionNumber = newVersionNumber;
        stateEvt.remoteAs = activeSessionInfo->peer->getRemoteAs();
        XLOGF(
            DBG1,
            "Session UP from [local [{}]:{}, remote [{} ({})]:{}] version {}",
            getAddressStr(localAddr),
            localAddr.getPort(),
            getAddressStr(remoteAddr),
            remotePeerDescription,
            remoteAddr.getPort(),
            newVersionNumber);

        stateEvt.lastResetReason = sessionInfo->lastResetReason;

        // attach establish session info
        stateEvt.sessionInfo = FiberBgpPeer::getObservableSessionInfo(
            getEstablishedPeerDisplayInfoHelper(
                sessionInfo, peerInfo->peeringParams),
            activeSessionInfo->peerInput,
            activeSessionInfo->boundedPeerInput,
            activeSessionInfo->peerOutput,
            sessionInfo->versionNumber);
      }

      writeToNotifyQueue(std::move(stateEvt));
      break;

    default:
      if ((activeSessionInfo->state == BgpSessionState::ESTABLISHED) &&
          FiberBgpPeerManager::getEstablishedSessionInfoFromPeerInfo(
              peerInfo, remoteBgpId) == activeSessionInfo) {
        // session goes down
        {
          auto sessionInfo = peerInfo->sessionInfos.at(remoteBgpId);
          auto newVersionNumber = sessionInfo->versionNumber->bumpUp();
          stateEvt.versionNumber = newVersionNumber;

          sessionInfo->lastResetReason =
              sessionInfo->establishedSessionInfo->peer->getResetReason();
          sessionInfo->numResets++;
          stateEvt.lastResetReason = sessionInfo->lastResetReason;

          // push event to notifyQueue based on different flavors
          writeToNotifyQueue(std::move(stateEvt));

          /**
           * If BGP session flapped from Established state then
           * exponentially backoff for next session setup.
           * Check against the previous lastResetTime before
           * overwriting it, so that if enough time has elapsed
           * since the last flap, the backoff resets to minimum.
           */
          auto activeConnectInfo =
              getListenPortActiveConnectInfoFromPeerInfo(peerInfo);
          if (activeConnectInfo &&
              activeConnectInfo->isSessionBackoffEnabled()) {
            if (!activeConnectInfo->inSessionDampenDuration(
                    sessionInfo->lastResetTime)) {
              activeConnectInfo->sessionBackoffReportSuccess();
            }
            activeConnectInfo->sessionBackoffReportError();
          }

          sessionInfo->lastResetTime = std::chrono::steady_clock::now();

          /*
           * Set the connectionInfo->lastSessionInfo to point to this
           * sessionInfo so that the IDLE session can retrieve information, e.g.
           * numResets, lastResetReason, and lastResetTime about its last BGP
           * session which WAS established over this port (connectionInfo).
           */
          sessionInfo->connectionInfo->lastSessionInfo =
              std::weak_ptr<BgpSessionInfo>(sessionInfo);
          /*
           * set sessionInfo->connectionInfo and
           * sessionInfo->establishedSessionInfo to nullptr because the session
           * goes down
           */
          sessionInfo->connectionInfo = nullptr;
          sessionInfo->establishedSessionInfo = nullptr;
          XLOGF(
              DBG1,
              "Session DOWN from [local [{}]:{}, remote [{} ({})]:{}] version {}",
              getAddressStr(localAddr),
              localAddr.getPort(),
              getAddressStr(remoteAddr),
              remotePeerDescription,
              remoteAddr.getPort(),
              newVersionNumber);
        }
      }
  } // switch

  // set to new state
  activeSessionInfo->changeStateTo(newState);
}

void FiberBgpPeerManager::processObservableMessage(
    std::shared_ptr<BgpPeerInfoInternal> peerInfo,
    std::shared_ptr<BgpPeerActiveSessionInfo> activeSessionInfo,
    FiberBgpPeer::ObservableMessageT& msgEvt) {
  if (!enableMessagesOverNotifyQueue_) {
    return;
  }

  const auto& remoteBgpId = msgEvt.peerId.remoteBgpId;
  if ((activeSessionInfo->state != BgpSessionState::ESTABLISHED) ||
      !peerInfo->sessionInfos.count(remoteBgpId) ||
      peerInfo->sessionInfos.at(remoteBgpId)->establishedSessionInfo !=
          activeSessionInfo) {
    return;
  }
  writeToNotifyQueue(std::move(msgEvt));
}

void FiberBgpPeerManager::writeToNotifyQueue(
    ObservableEventT&& event) noexcept {
  if (enableCoroNotifyQueue_) {
    notifyCoroQueue_.push(std::move(event));
  } else {
    notifyQueue_.getWriter().put(std::move(event));
  }
}

bool FiberBgpPeerManager::needToKeepThisPeer(
    std::optional<SocketAddress> localListenAddress,
    std::shared_ptr<FiberBgpPeer> peer) {
  CHECK(localListenAddress);

  bool isThisPeerInitiatedLocally =
      (localListenAddress->getPort() !=
       peer->getLocalSocketAddress().getPort());

  // same for both sessions
  bool isLocalBgpIdHigher =
      (peer->getRemoteBgpIdHBO() < peer->getLocalBgpIdHBO());

  /*
   * ----------------------
   * | A\B  | True | False|
   * ----------------------
   * | True | True | False|
   * ----------------------
   * | False| False| True |
   * ----------------------
   */
  return !(isThisPeerInitiatedLocally ^ isLocalBgpIdHigher);
}

void FiberBgpPeerManager::shutdownPeerDueToCollision(
    std::shared_ptr<FiberBgpPeer> peer) {
  const auto& localAddr = peer->getLocalSocketAddress();
  const auto& remoteAddr = peer->getRemoteSocketAddress();
  XLOGF(
      DBG1,
      "Collision detected. Bring down session(local [{},{}]:{} remote "
      "[{},{}]:{})",
      getAddressStr(localAddr),
      folly::IPAddress::fromLongHBO(peer->getLocalBgpIdHBO()).str(),
      localAddr.getPort(),
      getAddressStr(remoteAddr),
      folly::IPAddress::fromLongHBO(peer->getRemoteBgpIdHBO()).str(),
      remoteAddr.getPort());

  /*
   * send notification with the error code cease
   * close the session
   */
  peer->stop(BgpNotifCeaseErrSubCode::BN_CEASE_CONN_COLLISION_RES, false);

  // increment per-peer collision counter
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          "{}.{}.{}.collision",
          bgp::kEbbPlatform,
          bgp::kBgpcppTag,
          getAddressStr(remoteAddr)));
}

Expected<Unit, FiberBgpPeerManager::ErrorCode> FiberBgpPeerManager::addPeer(
    const IPAddress& peerAddr,
    const uint32_t peerAsn,
    const uint32_t myAsn,
    const SocketAddress& bindAddr,
    const uint16_t peerPort,
    const ConnTimeParams& connTimeParams,
    const TBgpSessionConnectMode connectMode,
    const std::optional<uint32_t>& localBgpIdOpt) {
  /*
   * sanity check for overlapping peerPrefix or peerAddress configuration:
   *    E.g. If dynamic peer 10.1.0.0/30 is configured already,
   *         peer 10.1.0.1 will be reject here
   */
  if (getPeerPrefix(peerAddr)) {
    return makeUnexpected(ErrorCode::PEER_EXISTS_ALREADY);
  }

  // TODO: will be integrated to config inheritance
  bgp::PeeringParams params{
      peerAddr,
      std::nullopt, // peerPrefix
      myAsn, // globalAs
      myAsn, // localAs
      peerAsn, // remoteAs
      localBgpIdOpt.has_value()
          ? folly::IPAddressV4::fromLongHBO(localBgpIdOpt.value())
          : bgpGlobalConfig_.routerId.asV4(), // localBgpId
      bgpGlobalConfig_.clusterId.asV4(), // localClusterId
      std::chrono::seconds(bgpGlobalConfig_.holdTime), // holdTime
      bgpGlobalConfig_.grRestartTime, // grRestartTime
      peerPort,
      bindAddr,
      connectMode,
      AdvertiseLinkBandwidth::DISABLE, // advertiseLinkBandwidth
      ReceiveLinkBandwidth::ACCEPT, // receiveLinkBandwidth
      std::nullopt, // linkBandwidthBps
      bgpGlobalConfig_.validateRemoteAs}; // validateRemoteAs

  return addPeerHelper(peerAddr, params, connTimeParams);
}

Expected<Unit, FiberBgpPeerManager::ErrorCode> FiberBgpPeerManager::addPeer(
    const IPAddress& peerAddr,
    const bgp::PeeringParams& params,
    const ConnTimeParams& connTimeParams) {
  /*
   * sanity check for overlapping peerPrefix or peerAddress configuration:
   *    E.g. If dynamic peer 10.1.0.0/30 is configured already,
   *         peer 10.1.0.1 will be reject here
   */
  if (getPeerPrefix(peerAddr)) {
    return makeUnexpected(ErrorCode::PEER_EXISTS_ALREADY);
  }
  return addPeerHelper(peerAddr, params, connTimeParams);
}

Expected<Unit, FiberBgpPeerManager::ErrorCode> FiberBgpPeerManager::addPeer(
    const IPAddress& peerAddr,
    const uint32_t peerAsn,
    const uint32_t myAsn,
    const uint16_t peerPort,
    const ConnTimeParams& connTimeParams,
    const TBgpSessionConnectMode connectMode) {
  return addPeer(
      peerAddr,
      peerAsn,
      myAsn,
      AsyncSocket::anyAddress(),
      peerPort,
      connTimeParams,
      connectMode);
}

Expected<Unit, FiberBgpPeerManager::ErrorCode> FiberBgpPeerManager::addPeer(
    const IPAddress& peerAddr,
    const uint32_t peerAsn,
    const uint16_t peerPort,
    const ConnTimeParams& connTimeParams,
    const TBgpSessionConnectMode connectMode) {
  return addPeer(
      peerAddr,
      peerAsn,
      bgpGlobalConfig_.localAsn,
      AsyncSocket::anyAddress(),
      peerPort,
      connTimeParams,
      connectMode);
}

Expected<Unit, FiberBgpPeerManager::ErrorCode>
FiberBgpPeerManager::addDynamicPeer(
    const folly::CIDRNetwork& peerPrefix,
    const uint32_t peerAsn,
    const uint32_t myAsn,
    const SocketAddress& bindAddr) {
  // sanity check for duplicate peerPrefix configuration
  if (isPeerConfigured(peerPrefix)) {
    return makeUnexpected(ErrorCode::PEER_EXISTS_ALREADY);
  }
  /*
   * sanity check for overlapping peerPrefix or peerAddress configuration.
   * For example, if peer 10.1.0.1 is configured already
   * Here dynamic peer 10.1.0.0/30 will be reject
   */
  if (!getPeerAddrs(peerPrefix).empty()) {
    return makeUnexpected(ErrorCode::PEER_EXISTS_ALREADY);
  }
  if ((bindAddr != AsyncSocket::anyAddress()) &&
      (bindAddr.getIPAddress().family() != peerPrefix.first.family())) {
    return makeUnexpected(ErrorCode::BIND_PEER_ADDRS_ARE_DIFF_AFIS);
  }

  XLOGF(DBG1, "Add dynamic peer {}", IPAddress::networkToString(peerPrefix));
  // TODO: will be integrated to config inheritance
  bgp::PeeringParams params{
      IPAddress(), // dummy
      peerPrefix, // peerPrefix
      myAsn, // localAs
      myAsn, // localAs
      peerAsn, // remoteAs
      bgpGlobalConfig_.routerId.asV4(), // localBgpId
      bgpGlobalConfig_.clusterId.asV4(), // localClusterId
      std::chrono::seconds(bgpGlobalConfig_.holdTime), // holdTime
      bgpGlobalConfig_.grRestartTime, // grRestartTime
      nettools::bgplib::constants::kBgpPort, // dummy
      bindAddr,
      TBgpSessionConnectMode::PASSIVE_ONLY};

  auto peerInfo = std::make_shared<BgpDynamicPeerGroupInfo>();
  peerInfo->peeringParams = params;
  dynamicPeerGroups_[peerPrefix] = peerInfo;

  return Unit();
}

Expected<Unit, FiberBgpPeerManager::ErrorCode>
FiberBgpPeerManager::addDynamicPeer(
    const folly::CIDRNetwork& peerPrefix,
    const bgp::PeeringParams& params) {
  // sanity check for duplicate peerPrefix configuration
  if (isPeerConfigured(peerPrefix)) {
    return makeUnexpected(ErrorCode::PEER_EXISTS_ALREADY);
  }
  /*
   * sanity check for overlapping peerPrefix or peerAddress configuration.
   * For example, if peer 10.1.0.1 is configured already
   * Here dynamic peer 10.1.0.0/30 will be reject
   */
  if (!getPeerAddrs(peerPrefix).empty()) {
    return makeUnexpected(ErrorCode::PEER_EXISTS_ALREADY);
  }
  if ((params.bindAddr != AsyncSocket::anyAddress()) &&
      (params.bindAddr.getIPAddress().family() != peerPrefix.first.family())) {
    return makeUnexpected(ErrorCode::BIND_PEER_ADDRS_ARE_DIFF_AFIS);
  }

  XLOGF(DBG1, "Add dynamic peer {}", IPAddress::networkToString(peerPrefix));
  auto peerInfo = std::make_shared<BgpDynamicPeerGroupInfo>();
  peerInfo->peeringParams = params;
  dynamicPeerGroups_[peerPrefix] = peerInfo;

  return Unit();
}

Expected<Unit, FiberBgpPeerManager::ErrorCode>
FiberBgpPeerManager::addDynamicPeer(
    const folly::CIDRNetwork& peerPrefix,
    const uint32_t peerAsn,
    const uint32_t myAsn) {
  return addDynamicPeer(peerPrefix, peerAsn, myAsn, AsyncSocket::anyAddress());
}

Expected<Unit, FiberBgpPeerManager::ErrorCode>
FiberBgpPeerManager::addDynamicPeer(
    const folly::CIDRNetwork& peerPrefix,
    const uint32_t peerAsn) {
  return addDynamicPeer(
      peerPrefix,
      peerAsn,
      bgpGlobalConfig_.localAsn,
      AsyncSocket::anyAddress());
}

Expected<Unit, FiberBgpPeerManager::ErrorCode> FiberBgpPeerManager::stopPeer(
    const IPAddress& peerAddr,
    bool withGR) {
  if (!isPeerConfigured(peerAddr)) {
    return makeUnexpected(ErrorCode::PEER_DOES_NOT_EXIST);
  }
  XLOGF(
      DBG1,
      "Stop peer {} with GR = {}",
      peerAddr.str(),
      withGR ? "true" : "false");
  auto peerInfo = allPeers_[peerAddr];
  if (peerInfo->peeringParams.isShutdown) {
    return Unit();
  }
  for (const auto& [_, connectionInfo] : peerInfo->connectionInfos) {
    if (connectionInfo->activeSessionInfo) {
      if (withGR) {
        connectionInfo->activeSessionInfo->peer->stop(std::nullopt, true);
      } else {
        connectionInfo->activeSessionInfo->peer->stop(
            BgpNotifCeaseErrSubCode::BN_CEASE_ADMIN_SHUTDOWN, false);
      }
    }
  }
  return Unit();
}

Expected<Unit, FiberBgpPeerManager::ErrorCode>
FiberBgpPeerManager::stopDynamicPeerWithGracefulRestart(
    const folly::CIDRNetwork& peerPrefix) {
  if (!isPeerConfigured(peerPrefix)) {
    return makeUnexpected(ErrorCode::PEER_DOES_NOT_EXIST);
  }
  if (dynamicPeerGroups_[peerPrefix]->peeringParams.isShutdown) {
    // If a prefix is shut down, you can only bring it up through starting
    return Unit();
  }
  XLOGF(
      DBG1,
      "Stop dynamic peer {} with GR = true",
      folly::IPAddress::networkToString(peerPrefix));
  for (const auto& peerAddr : dynamicPeerGroups_[peerPrefix]->activePeers) {
    stopPeer(peerAddr, true /*withGR*/);
  }
  return Unit();
}

Expected<Unit, FiberBgpPeerManager::ErrorCode>
FiberBgpPeerManager::shutdownPeer(
    const folly::IPAddress& peerAddr,
    bool peerDelete) {
  if (!isPeerConfigured(peerAddr)) {
    return makeUnexpected(ErrorCode::PEER_DOES_NOT_EXIST);
  }
  XLOGF(
      DBG1,
      "Shutdown peer {} with GR = false peerDelete={}",
      peerAddr.str(),
      static_cast<bool>(peerDelete));
  const auto& peerInfo = allPeers_[peerAddr];
  for (const auto& [_, connectionInfo] : peerInfo->connectionInfos) {
    if (connectionInfo->activeSessionInfo) {
      connectionInfo->activeSessionInfo->peer->stop(
          BgpNotifCeaseErrSubCode::BN_CEASE_ADMIN_SHUTDOWN,
          /*gracefulRestart=*/false,
          peerDelete);
    }
    const auto activeConnectInfo = connectionInfo->activeConnectInfo;
    if (activeConnectInfo) {
      activeConnectInfo->connectBackoffReportSuccess();
      if (activeConnectInfo->pendingTimeout) {
        activeConnectInfo->pendingTimeout->cancelTimeout();
        activeConnectInfo->pendingTimeout.reset();
      }
    }
  }
  peerInfo->peeringParams.isShutdown = true;
  return Unit();
}

Expected<Unit, FiberBgpPeerManager::ErrorCode>
FiberBgpPeerManager::shutdownDynamicPeer(const folly::CIDRNetwork& peerPrefix) {
  if (!isPeerConfigured(peerPrefix)) {
    return makeUnexpected(ErrorCode::PEER_DOES_NOT_EXIST);
  }
  XLOGF(
      DBG1,
      "Shutdown dynamic peer {}",
      folly::IPAddress::networkToString(peerPrefix));
  dynamicPeerGroups_[peerPrefix]->peeringParams.isShutdown = true;
  for (const auto& peerAddr : dynamicPeerGroups_[peerPrefix]->activePeers) {
    shutdownPeer(peerAddr);
  }
  return Unit();
}

Expected<Unit, FiberBgpPeerManager::ErrorCode> FiberBgpPeerManager::startPeer(
    const folly::IPAddress& peerAddr) {
  if (!isPeerConfigured(peerAddr)) {
    return makeUnexpected(ErrorCode::PEER_DOES_NOT_EXIST);
  }
  const auto& peerInfo = allPeers_[peerAddr];
  const auto& peeringParams = peerInfo->peeringParams;
  XLOGF(
      DBG1,
      "Start peer {} peerPort: {}",
      peerAddr.str(),
      peeringParams.peerPort);
  if (peeringParams.isShutdown) {
    // this peer is shutdown previously
    peerInfo->peeringParams.isShutdown = false;
    for (const auto& [_, connectionInfo] : peerInfo->connectionInfos) {
      auto activeConnectInfo = connectionInfo->activeConnectInfo;
      if (activeConnectInfo &&
          peeringParams.connectMode != TBgpSessionConnectMode::PASSIVE_ONLY) {
        // bring up TCP connection and start peer
        setupAndRunActiveConnectFibers(activeConnectInfo);
      }
    }
  }
  return Unit();
}

Expected<Unit, FiberBgpPeerManager::ErrorCode>
FiberBgpPeerManager::startDynamicPeer(const folly::CIDRNetwork& peerPrefix) {
  if (!isPeerConfigured(peerPrefix)) {
    return makeUnexpected(ErrorCode::PEER_DOES_NOT_EXIST);
  }
  XLOGF(
      DBG1,
      "Start dynamic peer {}",
      folly::IPAddress::networkToString(peerPrefix));
  if (dynamicPeerGroups_[peerPrefix]->peeringParams.isShutdown) {
    dynamicPeerGroups_[peerPrefix]->peeringParams.isShutdown = false;
    for (const auto& peerAddr : dynamicPeerGroups_[peerPrefix]->activePeers) {
      startPeer(peerAddr);
    }
  }
  return Unit();
}

Expected<Unit, FiberBgpPeerManager::ErrorCode> FiberBgpPeerManager::dropPeer(
    const IPAddress& peerAddr,
    bool peerDelete) {
  if (!isPeerConfigured(peerAddr)) {
    return makeUnexpected(ErrorCode::PEER_DOES_NOT_EXIST);
  }
  XLOGF(
      DBG1,
      "Drop peer {} peerDelete={}",
      peerAddr.str(),
      static_cast<bool>(peerDelete));
  shutdownPeer(peerAddr, peerDelete);
  // Seems allPeers_ only use folly::IPAddress as key, should we use BgpPeerId?
  if (allPeers_.erase(peerAddr)) {
    facebook::bgp::BgpStats::decrAllPeersCount();
  }
  return Unit();
}

Expected<Unit, FiberBgpPeerManager::ErrorCode> FiberBgpPeerManager::dropPeer(
    const folly::CIDRNetwork& peerPrefix,
    bool peerDelete) {
  if (!isPeerConfigured(peerPrefix)) {
    return makeUnexpected(ErrorCode::PEER_DOES_NOT_EXIST);
  }
  XLOGF(
      DBG1,
      "Drop dynamic peer {} peerDelete={}",
      folly::IPAddress::networkToString(peerPrefix),
      static_cast<bool>(peerDelete));
  auto& activePeers = dynamicPeerGroups_[peerPrefix]->activePeers;
  facebook::bgp::BgpStats::decrDynamicPeersCount(activePeers.size());
  for (const auto& peerAddr : activePeers) {
    dropPeer(peerAddr, peerDelete);
  }
  dynamicPeerGroups_.erase(peerPrefix);
  return Unit();
}

/**
 * Number of established dynamic peers.
 *
 * @return Number of established dynamic peers.
 */
uint32_t FiberBgpPeerManager::numDynamicPeers(void) {
  uint32_t establishedPeerCount = 0;

  // Iterate over all peers and count the number of established dynamic peers
  for (const auto& [peerAddr, info] : allPeers_) {
    // Skip static peers.
    auto peerPrefix = getPeerPrefix(peerAddr);
    if (!peerPrefix) {
      continue;
    }
    for (const auto& [_, sessionInfo] : info->sessionInfos) {
      if (sessionInfo->establishedSessionInfo) {
        establishedPeerCount++;
      }
    }
  }
  return establishedPeerCount;
}

/**
 * Checks if the number of established dynamic peers exceeds the dynamic peer
 * limit.
 *
 * @return true if the number of established dynamic peers exceeds the dynamic
 * peer limit, false otherwise.
 */
bool FiberBgpPeerManager::exceedsDynamicPeerLimit(void) {
  uint32_t establishedPeerCount = 0;
  // If dynamic peer limit is not set, treat limit as infinite.
  if (!bgpGlobalConfig_.dynamicPeerLimit.has_value()) {
    return false;
  }
  establishedPeerCount = numDynamicPeers();

  XLOGF(
      INFO,
      "Number of established dynamic peers: {} limit: {}",
      establishedPeerCount,
      bgpGlobalConfig_.dynamicPeerLimit.value());

  // Compare the number of established dynamic peers with the dynamic peer limit
  return (establishedPeerCount >= bgpGlobalConfig_.dynamicPeerLimit.value());
}

void FiberBgpPeerManager::passiveConnectLoop(
    folly::fibers::Semaphore& loopStartSemaphore) noexcept {
  const auto& serverAddress = serverSocket_->getListenAddress();
  XLOGF(
      DBG1,
      "Running acceptor on [{}]:{}",
      serverAddress.getAddressStr(),
      serverAddress.getPort());

  loopStartSemaphore.signal();

  while (true) {
    /*
     * There are several possible error cases handled:
     *
     * Case 1: `FiberGenericSocketError::ACCEPT_STOPPED` (Loop Breaks)
     *  - Source: Called from: `serverSocket_->close()`.
     *  - Action: Graceful shutdown signal
     *  - Result: `break` out of while loop
     *
     * Case 2: `folly::AsyncSocketException` (Loop Continues)
     *  - Source: Transient socket errors (e.g., EMFILE, ENOMEM, connection
     * reset)
     *  - Action: Log error and `continue` the loop
     *  - Result: Loop keeps accepting new connections
     *
     * Case 3: Other `FiberGenericSocketError` types (Loop Continues)
     *  - Source: Any `FiberGenericSocketError` that is NOT `ACCEPT_STOPPED`
     *  - Action: Log error and `continue` the loop
     *  - Result: Loop keeps accepting new connections
     */
    auto result = serverSocket_->accept().then([me = shared_from_this()](
                                                   FiberSocket fiberSocket) {
      // filtering logic based on peer address config
      auto peerAddr = fiberSocket.getPeerAddress().getIPAddress();

      if (me->shutdownInProgress_) {
        XLOGF(
            DBG1,
            "Reject tcp connection from peer {}: shutdown is in progress",
            peerAddr.str());
        fiberSocket.close();
        return;
      }

      /*
       * Set socket options, for now it's only TOS
       * Set the socket option based on address family. In daemon mode we always
       * listen to ::, accepted address can be v4 mapped or v6 address, so set
       * TOS accordingly. For tests we some times listen on 0.0.0.0
       */
      auto options = getBgpSockOptions(isV6Peer(peerAddr));
      for (auto const& option : options) {
        auto ret = fiberSocket.setSockOpt(
            option.first.level, option.first.optname, &option.second);
        if (ret) {
          XLOGF(
              DBG1, "Setsockopt error for {}: errno = {}", peerAddr.str(), ret);
        }
      }

      /*
       * Convert IPv4 mapped IPv6 address to IPv4. So that we process it as
       * IPv4 peer and inSubnet etc can be verified.
       */
      if (peerAddr.isIPv4Mapped()) {
        peerAddr = IPAddress::createIPv4(peerAddr);
      }

      if (me->getPeerPrefix(peerAddr) && me->exceedsDynamicPeerLimit()) {
        XLOGF(
            ERR,
            "Max dynamic peers reached, reject tcp connection from {}",
            peerAddr.str());
        fiberSocket.close();
        return;
      }

      if (!me->isPeerConfigured(peerAddr)) {
        // check if peer belongs to one of the dynamic peer prefix
        auto peerPrefix = me->getPeerPrefix(peerAddr);
        if (!peerPrefix) {
          XLOGF(
              DBG1,
              "Reject tcp connection from invalid peer address {}",
              peerAddr.str());
          fiberSocket.close();
          return;
        }
        auto peerConf = me->dynamicPeerGroups_[*peerPrefix];
        auto params = peerConf->peeringParams;
        if (params.isShutdown) {
          /*
           * if a dynamic peer is shutdown,
           * then not allowed to accecpt incoming connections
           */
          XLOGF(
              DBG1,
              "Reject tcp connection from shutdown dynamic peer {}",
              peerAddr.str());
          fiberSocket.close();
          return;
        }

        XLOGF(
            DBG1,
            "Accepted connection from {}, starting workers.",
            peerAddr.str());
        params.peerAddr = peerAddr;
        params.peerPort = fiberSocket.getPeerAddress().getPort();

        me->addPeerHelper(
            peerAddr,
            params,
            ConnTimeParams(
                kDefaultStartAfterDelayMs, kDefaultConnRetryTimeoutMs));
        // add peer into dynamic peer group map
        peerConf->activePeers.insert(peerAddr);
        facebook::bgp::BgpStats::incrDynamicPeersCount();
      }

      // check if peer is ACTIVE_ONLY
      if (me->allPeers_[peerAddr]->peeringParams.connectMode ==
          TBgpSessionConnectMode::ACTIVE_ONLY) {
        XLOGF_EVERY_N(
            DBG1,
            100,
            "Ignore tcp connection from peer address {} for ACTIVE_ONLY peer",
            peerAddr.str());
        fiberSocket.close();
        return;
      }

      // setup and run fibers for a Bgp peer
      const auto& peerInfo = me->allPeers_[peerAddr];

      /*
       * Apply TTL Security / GTSM (RFC 5082) socket options now that we
       * know the peer. If any option fails, close the socket — running
       * without TTL security when it is configured is a security gap.
       */
      const auto& ttlSecurityHops = peerInfo->peeringParams.ttlSecurityHops;
      if (ttlSecurityHops.has_value()) {
        if (!applyGtsmSockOptions(
                fiberSocket, peerAddr, isV6Peer(peerAddr), ttlSecurityHops)) {
          fiberSocket.close();
          return;
        }
      }

      if (peerInfo->peeringParams.isShutdown) {
        XLOGF(
            DBG1,
            "Reject tcp connection from shutdown peer {}",
            peerAddr.str());
        fiberSocket.close();
        return;
      }

      /*
       * TODO: Re-enable passive connect throttling once the fleet is no
       * longer exposed to the session backoff never-reset bug where
       * lastResetTime sequencing causes backoff to stay at max permanently.
       * Throttling passive connections based on session backoff is useful
       * to prevent flapping, but while the backoff can get stuck at max
       * (120s) and never reset, this check causes the remote peer's
       * active connects to silently fail with "Connection reset by peer"
       * for the entire backoff duration, delaying session establishment
       * by ~2 minutes.
       *
       * auto activeConnectInfo =
       *     getListenPortActiveConnectInfoFromPeerInfo(peerInfo);
       * if (activeConnectInfo && activeConnectInfo->isSessionBackoffEnabled())
       * { auto sessionRetryTimeRemaining =
       *       activeConnectInfo->getSessionRetryTimeRemaining();
       *   if (sessionRetryTimeRemaining.count() > 0) {
       *     fiberSocket.close();
       *     return;
       *   }
       * }
       */

      // Validate local address matches configured local address
      const auto& configuredLocalAddr = peerInfo->peeringParams.bindAddr;
      if (configuredLocalAddr != folly::AsyncSocket::anyAddress()) {
        auto localAddr = fiberSocket.getLocalAddress().getIPAddress();
        if (localAddr.isIPv4Mapped()) {
          localAddr = folly::IPAddress::createIPv4(localAddr);
        }
        if (localAddr != configuredLocalAddr.getIPAddress()) {
          XLOGF_EVERY_N(
              DBG1,
              100,
              "Reject tcp connection from peer {}: local address {} "
              "does not match configured local address {}",
              peerAddr.str(),
              localAddr.str(),
              configuredLocalAddr.getAddressStr());
          bgp::BgpStats::incPassiveRejectLocalAddrMismatch();
          fiberSocket.close();
          return;
        }
      }

      // remember this fiber before we start
      auto fiberId = me->peerWorkerId_++;
      me->peerWorkerIds_.emplace(fiberId);
      me->fm_.addTask([me, socket = std::move(fiberSocket), fiberId]() mutable {
        // TODO: support myASN given by addPeer()
        me->runBgpPeer(std::move(socket));
        me->stoppedPeerWorkerIdQ_.put(std::move(fiberId));
      });
    });

    if (result.hasError()) {
      FiberSocketErrorVisitor errVisitor;
      auto errStr = std::visit(errVisitor, result.error());
      XLOGF(DBG1, "Acceptor error: {}", errStr);
      // Only break on ACCEPT_STOPPED, continue on other errors
      if (auto* genErr =
              std::get_if<FiberGenericSocketError>(&result.error())) {
        if (genErr->type_ == FiberGenericSocketErrorType::ACCEPT_STOPPED) {
          break;
        }
      }
      // For other errors (e.g., folly::AsyncSocketException), continue the loop
    }
  }
}

bool FiberBgpPeerManager::isPeerConfigured(
    const IPAddress& peerAddr) const noexcept {
  auto it = allPeers_.find(peerAddr);
  if (it == allPeers_.end()) {
    return false;
  }
  return true;
}

bool FiberBgpPeerManager::isPeerConfigured(
    const folly::CIDRNetwork& peerPrefix) const noexcept {
  auto it = dynamicPeerGroups_.find(peerPrefix);
  if (it == dynamicPeerGroups_.end()) {
    return false;
  }
  return true;
}

folly::Expected<bool, FiberBgpPeerManager::ErrorCode>
FiberBgpPeerManager::isPeerUpExpected(const BgpPeerId& peerId) const noexcept {
  if (!isPeerConfigured(peerId.peerAddr)) {
    return makeUnexpected(ErrorCode::PEER_DOES_NOT_EXIST);
  }
  if (!isPeerUp(peerId)) {
    return makeUnexpected(ErrorCode::PEER_NOT_ESTABLISHED);
  }
  return true;
}

bool FiberBgpPeerManager::isPeerUp(const BgpPeerId& peerId) const noexcept {
  const auto& peerAddr = peerId.peerAddr;
  const auto remoteBgpId = peerId.remoteBgpId;
  if (!isPeerConfigured(peerAddr)) {
    return false;
  }
  if (!allPeers_.at(peerAddr)->sessionInfos.contains(remoteBgpId)) {
    return false;
  }
  return allPeers_.at(peerAddr)
             ->sessionInfos.at(remoteBgpId)
             ->establishedSessionInfo != nullptr;
}

std::optional<folly::CIDRNetwork> FiberBgpPeerManager::getPeerPrefix(
    const IPAddress& peerAddr) const noexcept {
  for (const auto& [peerPrefix, _] : dynamicPeerGroups_) {
    if (peerAddr.inSubnet(peerPrefix.first, peerPrefix.second)) {
      return peerPrefix;
    }
  }
  return std::nullopt;
}

std::vector<IPAddress> FiberBgpPeerManager::getPeerAddrs(
    const folly::CIDRNetwork& peerPrefix) const noexcept {
  std::vector<IPAddress> peerAddrs{};
  for (const auto& [peerAddr, _] : allPeers_) {
    if (peerAddr.inSubnet(peerPrefix.first, peerPrefix.second)) {
      peerAddrs.emplace_back(peerAddr);
    }
  }
  return peerAddrs;
}

void FiberBgpPeerManager::setupAndRunActiveConnectFibers(
    shared_ptr<BgpPeerActiveConnectInfo> activeConnectInfo) noexcept {
  // Compute initial delay
  std::chrono::milliseconds delay;
  if (activeConnectInfo->isSessionBackoffEnabled()) {
    auto sessionRetryTimeRemaining =
        activeConnectInfo->getSessionRetryTimeRemaining();
    if (sessionRetryTimeRemaining.count() > 0) {
      // Adding jitter upto +/- 10 percent of backoff time in milliseconds.
      auto jitter = std::chrono::milliseconds(
          generateJitter(sessionRetryTimeRemaining.count()));
      delay = sessionRetryTimeRemaining + jitter;
    } else {
      // Adding jitter upto +/- 10 percent of initial delay in milliseconds.
      auto initialDelay = std::chrono::duration_cast<std::chrono::milliseconds>(
          activeConnectInfo->connTimeParams.getStartAfterDelay());
      auto jitter =
          std::chrono::milliseconds(generateJitter(initialDelay.count()));
      delay = initialDelay + jitter;
    }
  } else {
    // Adding jitter upto +/- 10 percent of initial delay in milliseconds.
    auto initialDelay = std::chrono::duration_cast<std::chrono::milliseconds>(
        activeConnectInfo->connTimeParams.getStartAfterDelay());
    auto jitter =
        std::chrono::milliseconds(generateJitter(initialDelay.count()));
    delay = initialDelay + jitter;
  }

  /*
   * Schedule initial timeout — callback will spawn a fiber to
   * activeConnect() when it fires.
   */
  scheduleConnectTimeout(activeConnectInfo, delay);
}

bool FiberBgpPeerManager::canRetryConnect(
    const folly::IPAddress& peerAddr) const noexcept {
  if (alreadyShutdown_) {
    return false;
  }
  const auto peerItr = allPeers_.find(peerAddr);
  if (peerItr == allPeers_.cend()) {
    return false;
  }
  if (peerItr->second->peeringParams.isShutdown) {
    return false;
  }
  return true;
}

void FiberBgpPeerManager::scheduleConnRetryWithBackoff(
    const std::shared_ptr<BgpPeerActiveConnectInfo>&
        activeConnectInfo) noexcept {
  activeConnectInfo->connectBackoffReportError();
  /*
   * Adding jitter upto +/- 10 percent of backoff time in milliseconds.
   * Capping the max jitter to +/- 1 second.
   */
  auto nextRetryTime = activeConnectInfo->getNextRetryTime();
  auto jitter =
      std::chrono::milliseconds(generateJitter(nextRetryTime.count()));
  restartConnRetryTimer(activeConnectInfo, nextRetryTime + jitter);
}

void FiberBgpPeerManager::activeConnect(
    shared_ptr<BgpPeerActiveConnectInfo> activeConnectInfo) noexcept {
  const auto& peerSocketAddr = activeConnectInfo->peerAddr;
  const auto peerAddr = peerSocketAddr.getIPAddress();

  // run connector if session is not up or connection is not in progress
  if (!isPeerUp(peerAddr)) {
    XLOGF(
        DBG4,
        "Running connecter to [{}]:{}",
        peerSocketAddr.getAddressStr(),
        peerSocketAddr.getPort());
    const auto& bindSocketAddr = activeConnectInfo->localAddr;
    XLOGF(
        DBG4,
        "Local bind address is [{}]:{}",
        bindSocketAddr.getAddressStr(),
        bindSocketAddr.getPort());
    activeConnectInfo->socket = std::make_unique<FiberSocket>();
    activeConnectInfo->numOfConnectionAttempts++;
    bool isV6 = isV6Peer(peerAddr);

    auto result = [&]() {
      activeConnectInfo->connectInProgress = true;
      SCOPE_EXIT {
        activeConnectInfo->connectInProgress = false;
      };
      return activeConnectInfo->socket->connect(
          peerSocketAddr,
          std::chrono::duration_cast<std::chrono::milliseconds>(
              activeConnectInfo->connTimeParams.getConnectTimeout()),
          bindSocketAddr,
          getBgpSockOptions(isV6));
    }();

    // Re-validate peer state after fiber suspension (connect() suspends)
    bool canRetry = canRetryConnect(peerAddr);

    if (result.hasError()) {
      if (canRetry) {
        XLOGF(
            DBG4,
            "Connect error: {}",
            std::get<folly::AsyncSocketException>(result.error()).what());
        scheduleConnRetryWithBackoff(activeConnectInfo);
      }
      return;
    }

    /*
     * Apply GTSM socket options post-connect with fail-closed behavior,
     * consistent with passive connect path.
     */
    if (activeConnectInfo->ttlSecurityHops.has_value()) {
      if (!applyGtsmSockOptions(
              *activeConnectInfo->socket,
              peerAddr,
              isV6,
              activeConnectInfo->ttlSecurityHops)) {
        activeConnectInfo->socket->close();
        if (canRetry) {
          scheduleConnRetryWithBackoff(activeConnectInfo);
        }
        return;
      }
    }

    // Start BGP peer
    auto fiberId = peerWorkerId_++;
    peerWorkerIds_.emplace(fiberId);
    fm_.addTask(
        [me = shared_from_this(), activeConnectInfo, fiberId]() mutable {
          activeConnectInfo->connectBackoffReportSuccess();
          me->runBgpPeer(std::move(*(activeConnectInfo->socket)));
          me->stoppedPeerWorkerIdQ_.put(std::move(fiberId));
        });
  }
}

void FiberBgpPeerManager::scheduleConnectTimeout(
    std::shared_ptr<BgpPeerActiveConnectInfo> activeConnectInfo,
    std::chrono::milliseconds delay) {
  /*
   * Do not schedule an activeConnect while one is already pending. Otherwise
   * this would let another activeConnect() run while the first is suspended
   * inside FiberSocket::connect(); its
   * `activeConnectInfo->socket = std::make_unique<FiberSocket>()` destroys the
   * FiberSocket that the suspended frame resumes on, which is a
   * use-after-free.
   *
   * Nothing is lost by skipping: whoever owns the pending attempt reschedules
   * once it finishes -- activeConnect() on connect failure, and runBgpPeer()
   * when the session it started ends.
   */
  if (activeConnectInfo->pendingTimeout &&
      activeConnectInfo->pendingTimeout->isScheduled()) {
    XLOGF(
        DBG2,
        "Connect retry already scheduled for {}, not rescheduling",
        activeConnectInfo->peerAddr.describe());
    return;
  }
  if (activeConnectInfo->connectInProgress) {
    XLOGF(
        DBG2,
        "Connect already in progress for {}, not scheduling a retry",
        activeConnectInfo->peerAddr.describe());
    return;
  }

  /*
   * Use weak_ptr to break circular reference:
   * activeConnectInfo->pendingTimeout → AsyncTimeout → lambda →
   * activeConnectInfo
   */
  activeConnectInfo->pendingTimeout = folly::AsyncTimeout::schedule(
      delay,
      evb_,
      [me = shared_from_this(),
       weak = std::weak_ptr(activeConnectInfo)]() noexcept {
        auto activeConnectInfo = weak.lock();
        if (!activeConnectInfo || me->alreadyShutdown_) {
          return;
        }
        me->fm_.addTask(
            [me, activeConnectInfo] { me->activeConnect(activeConnectInfo); });
      });
}

void FiberBgpPeerManager::restartConnRetryTimer(
    std::shared_ptr<BgpPeerActiveConnectInfo> activeConnectInfo,
    const std::chrono::milliseconds& nextRetryTime) noexcept {
  XLOGF(
      DBG4,
      "Scheduling session retry for {} in {}ms (conn retry backoff)",
      activeConnectInfo->peerAddr.describe(),
      nextRetryTime.count());

  scheduleConnectTimeout(activeConnectInfo, nextRetryTime);
}

shared_ptr<BgpPeerActiveSessionInfo>
FiberBgpPeerManager::setupBgpPeerActiveSession(
    shared_ptr<BgpPeerInfoInternal> peerInfo,
    FiberSocket&& socket) noexcept {
  auto activeSessionInfo = std::make_shared<BgpPeerActiveSessionInfo>();
  activeSessionInfo->peer = std::make_shared<FiberBgpPeer>(
      peerInfo->peeringParams,
      fm_,
      evb_,
      std::move(socket),
      activeSessionInfo->peerInput,
      activeSessionInfo->boundedPeerInput,
      enableMessagesOverNotifyQueue_ ? nullptr : activeSessionInfo->peerOutput,
      isRestarting_,
      enableEgressQueueBackpressure_,
      enableSerializeGroupPdu_,
      enableGrNotification_);
  return activeSessionInfo;
}

Expected<
    std::pair<folly::IPAddress, uint16_t /* port */>,
    FiberBgpPeerManager::ErrorCode>
FiberBgpPeerManager::getRemoteSocketAddress(FiberSocket& socket) noexcept {
  try {
    const auto& localSocketAddr = socket.getLocalAddress();
    const auto& remoteSocketAddr = socket.getPeerAddress();
    XLOGF(
        DBG1,
        "TCP connection came up: local [{}]:{} remote [{}]:{}",
        getAddressStr(localSocketAddr),
        localSocketAddr.getPort(),
        getAddressStr(remoteSocketAddr),
        remoteSocketAddr.getPort());

    auto duplicate = isDuplicateBgpPeerActiveSession(remoteSocketAddr);
    if (duplicate) {
      socket.close();
      return folly::makeUnexpected(ErrorCode::PEER_EXISTS_ALREADY);
    }
    return folly::makeExpected<ErrorCode>(std::pair<folly::IPAddress, uint16_t>{
        remoteSocketAddr.getIPAddress(), remoteSocketAddr.getPort()});
  } catch (folly::InvalidAddressFamilyException& ex) {
    XLOGF(
        ERR,
        "Encountered InvalidAddressFamilyException when getting remote socket address. {}",
        folly::exceptionStr(ex));
    return folly::makeUnexpected(ErrorCode::INVALID_PEER);
  }
}

Expected<Unit, FiberBgpPeerManager::ErrorCode> FiberBgpPeerManager::runBgpPeer(
    FiberSocket&& socket) noexcept {
  auto remoteSocketAddr = getRemoteSocketAddress(socket);
  if (remoteSocketAddr.hasError()) {
    return makeUnexpected(remoteSocketAddr.error());
  }
  auto& [peerAddr, peerPort] = remoteSocketAddr.value();

  const auto iter = allPeers_.find(peerAddr);
  if (iter == allPeers_.cend()) {
    XLOGF(
        DBG1, "Peer {} is deleted in the mean-time. Aborting", peerAddr.str());
    socket.close();
    return makeUnexpected(ErrorCode::PEER_DOES_NOT_EXIST);
  }
  auto peerInfo = iter->second;

  auto activeSessionInfo =
      setupBgpPeerActiveSession(peerInfo, std::move(socket));
  // connectionInfo may not be created already for passive connections
  if (!peerInfo->connectionInfos[peerPort]) {
    peerInfo->connectionInfos[peerPort] = std::make_shared<BgpConnectionInfo>();
  }
  peerInfo->connectionInfos[peerPort]->activeSessionInfo = activeSessionInfo;

  /*
   * Setup module monitoring for this specific FiberBgpPeer.
   *
   * NOTE:
   *  - a combination of (peerAddr, peerPort) uniquely identifies the peer.
   *  - BgpPeerId can't be used here since remoteBgpId is not available until
   *    session establishement, which happens later.
   */
  CHECK(activeSessionInfo->peer) << "nullptr access in activeSessionInfo";

  const auto monitorKey = fmt::format("{}-{}", peerAddr.str(), peerPort);
  monitorModule(monitorKey, *activeSessionInfo->peer);

  /*
   * child fibers created for this peer
   * TODO: migrate to folly::coro tasks
   */
  std::vector<folly::Future<Unit>> workers;
  {
    // Observe state changes and received Bgp messages
    auto fiber = fm_.addTaskFuture([activeSessionInfo]() mutable {
      auto writer = activeSessionInfo->observeQueue.getWriter();
      mergeQueuesStatic(
          writer,
          activeSessionInfo->peer->getObserverStateQueue(),
          activeSessionInfo->peer->getObserverRcvdMessageQueue());
    });
    workers.emplace_back(std::move(fiber));
  }
  {
    /*
     * Process observed state changes and received Bgp messages and put events
     * to notifyQueue_
     */
    auto fiber = fm_.addTaskFuture(
        [me = shared_from_this(), peerInfo, activeSessionInfo]() mutable {
          me->processObservableEventLoop(peerInfo, activeSessionInfo);
        });
    workers.emplace_back(std::move(fiber));
  }
  {
    // start fibers of FiberBgpPeer
    auto fiber = fm_.addTaskFuture(
        [activeSessionInfo]() mutable { activeSessionInfo->peer->run(); });
    workers.emplace_back(std::move(fiber));
  }
  // waits for all child fibers to complete
  folly::collectAll(workers.begin(), workers.end()).get();

  /*
   * Stop monitoring to remove the stale FiberBgpPeer to avoid use-after-free.
   * activeSessionInfo will be set to nullptr to destroy FiberBgpPeer.
   */
  stopMonitoring(monitorKey);
  peerInfo->connectionInfos[peerPort]->activeSessionInfo = nullptr;

  /*
   * peerPort is peer's port number for the current connection. If the
   * session was initiated by peer to our listening port, then this is an
   * ephemeral number on which the peer is not listening. When retrying the
   * connection, we must retry on port on which peer is listening (usually
   * port 179), which is in the peeringParams.
   */
  XLOGF(
      DBG1,
      "[Exit] Peer {} stopped previous connection on port: {}",
      peerAddr.str(),
      peerPort);

  auto peerListenPort = peerInfo->peeringParams.peerPort;
  if ((peerInfo->peeringParams.connectMode !=
       TBgpSessionConnectMode::PASSIVE_ONLY) &&
      (!alreadyShutdown_) &&
      (!peerInfo->peeringParams.isShutdown &&
       peerInfo->connectionInfos[peerListenPort]->activeConnectInfo)) {
    auto activeConnectInfo =
        peerInfo->connectionInfos[peerListenPort]->activeConnectInfo;
    auto nextRetryTime = activeConnectInfo->getNextRetryTime();
    if (activeConnectInfo->isSessionBackoffEnabled()) {
      auto sessionRetryTimeRemaining =
          activeConnectInfo->getSessionRetryTimeRemaining();
      if (sessionRetryTimeRemaining.count() > nextRetryTime.count()) {
        nextRetryTime = sessionRetryTimeRemaining;
      }
    }

    // Adding jitter upto +/- 10 percent of backoff time in milliseconds.
    auto jitter =
        std::chrono::milliseconds(generateJitter(nextRetryTime.count()));
    auto scheduledRetryTime = nextRetryTime + jitter;
    XLOGF(
        DBG1,
        "Schedule connect retry for {} in {}ms(retry backoff)",
        peerAddr.str(),
        scheduledRetryTime.count());
    restartConnRetryTimer(activeConnectInfo, scheduledRetryTime);
  }

  // Remove the entry if both activeSessionInfo and activeConnectInfo are null
  if (!peerInfo->connectionInfos[peerPort]->activeSessionInfo &&
      !peerInfo->connectionInfos[peerPort]->activeConnectInfo) {
    /*
     * If the peer reconnects over a new TCP port, we need to pass the
     * lastSessionInfo to the new connectionInfo so that it can retrieve
     * information about its last BGP session which was established over this
     * connection.
     *
     * previous connection was on `peerPort` and we are listening on
     * `peerListenPort`
     */
    { // start of weak_ptr lock()
      std::shared_ptr<BgpSessionInfo> lastSessionInfo =
          peerInfo->connectionInfos[peerPort]->lastSessionInfo.lock();
      if (lastSessionInfo) {
        peerInfo->connectionInfos[peerListenPort]->lastSessionInfo =
            lastSessionInfo;
      } else {
        /*
         * If the lastSessionInfo is nullptr, it is likely that this BGP peer
         * has never been established.
         */
        peerInfo->connectionInfos[peerListenPort]->lastSessionInfo =
            std::weak_ptr<BgpSessionInfo>();
      }
    } // end of weak_ptr lock(), shared_ptr is destroyed

    XLOGF(
        DBG1,
        "Peer:{} removing connectionInfos of peerPort {}",
        peerAddr.str(),
        peerPort);
    peerInfo->connectionInfos.erase(peerPort);
  }
  return Unit();
}

Expected<Unit, FiberBgpPeerManager::ErrorCode>
FiberBgpPeerManager::addPeerHelper(
    const IPAddress& peerAddr,
    const bgp::PeeringParams& peeringParams,
    const ConnTimeParams& connTimeParams) {
  if ((peeringParams.bindAddr != AsyncSocket::anyAddress()) &&
      (peeringParams.bindAddr.getIPAddress().family() != peerAddr.family())) {
    return makeUnexpected(ErrorCode::BIND_PEER_ADDRS_ARE_DIFF_AFIS);
  }
  if (allPeers_.count(peerAddr) &&
      allPeers_.at(peerAddr)->connectionInfos.count(peeringParams.peerPort)) {
    return makeUnexpected(ErrorCode::PEER_EXISTS_ALREADY);
  }

  XLOGF(
      DBG1,
      "Add Peer [{}, {}]:{} with open delay (jitter) = {}ms",
      peerAddr.str(),
      peeringParams.description,
      peeringParams.peerPort,
      std::chrono::duration_cast<std::chrono::milliseconds>(
          connTimeParams.getStartAfterDelay())
          .count());

  bool isNewPeer = !allPeers_.contains(peerAddr);
  std::shared_ptr<BgpPeerInfoInternal> peerInfo;
  if (isNewPeer) {
    peerInfo = std::make_shared<BgpPeerInfoInternal>();
  } else {
    peerInfo = allPeers_.at(peerAddr);
  }
  const auto peerPort = peeringParams.peerPort;
  /*
   * TODO: make hold time also configurable
   *       support configured hold time and negotiated hold time
   */
  const auto connectionInfo = std::make_shared<BgpConnectionInfo>();
  connectionInfo->activeConnectInfo = populateBgpPeerActiveConnectInfo(
      peerAddr,
      peeringParams.peerPort,
      peeringParams.bindAddr,
      connTimeParams,
      peeringParams.ttlSecurityHops);
  peerInfo->connectionInfos[peerPort] = connectionInfo;
  peerInfo->peeringParams = peeringParams;
  allPeers_[peerAddr] = peerInfo;
  if (isNewPeer) {
    facebook::bgp::BgpStats::incrAllPeersCount();
  }

  if (peeringParams.connectMode != TBgpSessionConnectMode::PASSIVE_ONLY) {
    // bring up TCP connection and start peer
    setupAndRunActiveConnectFibers(connectionInfo->activeConnectInfo);
  }

  return Unit();
}

bool FiberBgpPeerManager::isDuplicateBgpPeerActiveSession(
    const SocketAddress& remoteSocketAddr) const {
  const auto& peerAddr = remoteSocketAddr.getIPAddress();
  const auto& peerPort = remoteSocketAddr.getPort();

  const auto it = allPeers_.find(peerAddr);
  if (it == allPeers_.end()) {
    return false;
  }

  CHECK_NOTNULL(it->second.get());
  const auto& peerInfo = it->second;
  if (!peerInfo->connectionInfos.contains(peerPort)) {
    return false;
  }
  if (!peerInfo->connectionInfos.at(peerPort)->activeSessionInfo) {
    return false;
  }

  XLOGF(
      DBG1,
      "BgpPeerActiveSessionInfo present already for peerAddr {} port {}",
      peerAddr.str(),
      peerPort);
  return true;
}

std::optional<SocketAddress> FiberBgpPeerManager::getListenAddress()
    const noexcept {
  if (serverSocket_) {
    return serverSocket_->getListenAddress();
  }
  return std::nullopt;
}

RQueue<ObservableEventT> FiberBgpPeerManager::getNotifyQueue() noexcept {
  return notifyQueue_.getReader();
}

bgp::coro::MPMCQueue<ObservableEventT>&
FiberBgpPeerManager::getNotifyCoroQueue() noexcept {
  return notifyCoroQueue_;
}

/**
 * Determines whether peer's version client (PeerManagerBase) is querying, is
 * the same as FiberPeerManager's version of the peer.
 *
 * Args:
 *  peerId: BGP peer's address and identifier.
 *  versionNumber: Version client is requesting to be checked.
 *
 * Returns:
 *  True if version is in sync, False otherwise.
 */
bool FiberBgpPeerManager::isPeerVersionValid(
    const BgpPeerId& peerId,
    const uint64_t versionNumber) const noexcept {
  const auto& peerAddr = peerId.peerAddr;
  const auto remoteBgpId = peerId.remoteBgpId;
  if (!isPeerConfigured(peerAddr)) {
    XLOGF(
        ERR,
        "Peer version {} validation failed for {}. No such peer configured.",
        versionNumber,
        peerId.str());
    return false;
  }
  if (!allPeers_.at(peerAddr)->sessionInfos.contains(remoteBgpId)) {
    XLOGF(
        DBG1,
        "Peer version {} validation failed for {}. Session does not exist.",
        versionNumber,
        peerId.str());
    return false;
  }

  const auto sessionInfo = allPeers_.at(peerAddr)->sessionInfos.at(remoteBgpId);

  auto currentVersion = sessionInfo->versionNumber->get();
  XLOGF_IF(
      DBG1,
      currentVersion != versionNumber,
      "Peer version mismatch for {}. Queried {}, current {}.",
      peerId.str(),
      versionNumber,
      currentVersion);
  return currentVersion == versionNumber;
}

folly::Expected<
    std::shared_ptr<BgpPeerActiveSessionInfo>,
    FiberBgpPeerManager::ErrorCode>
FiberBgpPeerManager::getEstablishedSessionInfo(
    const BgpPeerId& peerId) const noexcept {
  const auto peerUpExpected = isPeerUpExpected(peerId);
  if (peerUpExpected.hasError()) {
    return makeUnexpected(peerUpExpected.error());
  }
  return allPeers_.at(peerId.peerAddr)
      ->sessionInfos.at(peerId.remoteBgpId)
      ->establishedSessionInfo;
}

std::shared_ptr<BgpPeerActiveSessionInfo>
FiberBgpPeerManager::getEstablishedSessionInfoFromPeerInfo(
    const std::shared_ptr<BgpPeerInfoInternal> peerInfo,
    const uint32_t remoteBgpId) {
  if (!peerInfo->sessionInfos.contains(remoteBgpId)) {
    return nullptr;
  }
  if (peerInfo->sessionInfos.at(remoteBgpId)->connectionInfo) {
    return peerInfo->sessionInfos.at(remoteBgpId)->establishedSessionInfo;
  }
  return nullptr;
}

/**
 * @brief  If it is non-passive session, the session should have
 *         activeConnectInfo for port 179 that BGP listens onto.
 *         return that activeConnectInfo
 *
 * @param  peerInfo - A specific peer whose activeConnectInfo to
 *                    be retrieved
 *
 * @return BgpPeerActiveConnectInfo or nullptr
 */
std::shared_ptr<BgpPeerActiveConnectInfo>
FiberBgpPeerManager::getListenPortActiveConnectInfoFromPeerInfo(
    const std::shared_ptr<BgpPeerInfoInternal> peerInfo) {
  if (peerInfo->peeringParams.connectMode !=
      TBgpSessionConnectMode::PASSIVE_ONLY) {
    auto peerListenPort = peerInfo->peeringParams.peerPort;
    if (peerInfo->connectionInfos.find(peerListenPort) !=
        peerInfo->connectionInfos.end()) {
      return (peerInfo->connectionInfos[peerListenPort]->activeConnectInfo);
    }
  }
  return nullptr;
}

// bgp peer accessor
std::shared_ptr<FiberBgpPeer::OutputQueueT>
FiberBgpPeerManager::getPeerOutputQueue(const BgpPeerId& peerId) noexcept {
  if (enableMessagesOverNotifyQueue_) {
    XLOGF(
        ERR,
        "Querying output queue without enableMessagesOverNotifyQueue_ {}",
        peerId.str());
    return nullptr;
  }
  const auto establishedSessionInfo = getEstablishedSessionInfo(peerId);
  if (establishedSessionInfo.hasError()) {
    XLOGF(
        DBG1,
        "Failed to get peerOutputQueue for {} with code: {}",
        peerId.str(),
        magic_enum::enum_name(establishedSessionInfo.error()));
    return nullptr;
  }
  return establishedSessionInfo.value()->peerOutput;
}

// Get writer Queue to send out BgpUpdates
std::shared_ptr<FiberBgpPeer::InputQueueT>
FiberBgpPeerManager::getPeerInputQueue(const BgpPeerId& peerId) noexcept {
  const auto establishedSessionInfo = getEstablishedSessionInfo(peerId);
  if (establishedSessionInfo.hasError()) {
    XLOGF(
        DBG1,
        "Failed to get peerInputQueue for {} with code: {}",
        peerId.str(),
        magic_enum::enum_name(establishedSessionInfo.error()));
    return nullptr;
  }
  return establishedSessionInfo.value()->peerInput;
}

// Get writer Queue to send out BgpUpdates
std::shared_ptr<FiberBgpPeer::BoundedInputQueueT>
FiberBgpPeerManager::getBoundedPeerInputQueue(
    const BgpPeerId& peerId) noexcept {
  const auto establishedSessionInfo = getEstablishedSessionInfo(peerId);
  if (establishedSessionInfo.hasError()) {
    XLOGF(
        DBG1,
        "Failed to get boundedPeerInputQueue for {} with code: {}",
        peerId.str(),
        magic_enum::enum_name(establishedSessionInfo.error()));
    return nullptr;
  }
  return establishedSessionInfo.value()->boundedPeerInput;
}

Expected<Unit, FiberBgpPeerManager::ErrorCode> FiberBgpPeerManager::sendUpdate(
    const BgpPeerId& peerId,
    std::unique_ptr<BgpUpdate2> update) const {
  CHECK(!enableEgressQueueBackpressure_)
      << "Caller should not have backpressure enabled.";
  const auto establishedSessionInfo = getEstablishedSessionInfo(peerId);
  if (establishedSessionInfo.hasError()) {
    return makeUnexpected(establishedSessionInfo.error());
  }
  CHECK(establishedSessionInfo.value());
  establishedSessionInfo.value()->peerInput->push(
      folly::to_shared_ptr(std::move(update)));
  return Unit();
}

Expected<Unit, FiberBgpPeerManager::ErrorCode> FiberBgpPeerManager::sendUpdates(
    const BgpPeerId& peerId,
    std::vector<std::unique_ptr<BgpUpdate2>>&& updates) const {
  CHECK(!enableEgressQueueBackpressure_)
      << "Caller should not have backpressure enabled.";
  const auto establishedSessionInfo = getEstablishedSessionInfo(peerId);
  if (establishedSessionInfo.hasError()) {
    return makeUnexpected(establishedSessionInfo.error());
  }
  CHECK(establishedSessionInfo.value());
  for (auto& update : updates) {
    establishedSessionInfo.value()->peerInput->push(
        folly::to_shared_ptr(std::move(update)));
  }
  return Unit();
}

Expected<Unit, FiberBgpPeerManager::ErrorCode>
FiberBgpPeerManager::sendEndOfRib(const BgpPeerId& peerId) const {
  CHECK(!enableEgressQueueBackpressure_)
      << "Caller should not have backpressure enabled.";
  auto establishedSessionInfo = getEstablishedSessionInfo(peerId);
  if (establishedSessionInfo.hasError()) {
    return makeUnexpected(establishedSessionInfo.error());
  }

  // create EndOfRib for v4 + v6
  std::vector<std::unique_ptr<BgpEndOfRib>> eors;
  eors.emplace_back(createEndOfRib(BgpUpdateAfi::AFI_IPv4));
  eors.emplace_back(createEndOfRib(BgpUpdateAfi::AFI_IPv6));

  // send them out
  CHECK(establishedSessionInfo.value());
  for (const auto& eor : eors) {
    establishedSessionInfo.value()->peerInput->push(*eor);
  }
  return Unit();
}

bool FiberBgpPeerManager::isPeerUp(const IPAddress& peerAddr) const {
  if (!isPeerConfigured(peerAddr)) {
    return false;
  }
  auto it = allPeers_.find(peerAddr);
  auto& peerInfo = it->second;
  CHECK(peerInfo);
  if (peerInfo->sessionInfos.empty()) {
    return false;
  }

  /*
   * Preserve the existing lifetime of sessionInfos: detached entries carry
   * status and reset history and removing them could regress other consumers.
   * A static address represents one configured neighbor, so any established
   * entry makes that address live even when an old router-ID entry remains.
   * Dynamic peer addresses can represent multiple logical speakers and retain
   * their existing requirement that every known session be established.
   */
  // A matching configured prefix identifies this address as a dynamic peer.
  const bool isDynamicPeer = getPeerPrefix(peerAddr).has_value();
  bool hasEstablishedSession{false};
  for (const auto& [_, sessionInfo] : peerInfo->sessionInfos) {
    if (sessionInfo->establishedSessionInfo) {
      hasEstablishedSession = true;
    } else if (isDynamicPeer) {
      return false;
    }
  }
  return hasEstablishedSession;
}

std::optional<std::vector<BgpPeerDisplayInfo>>
FiberBgpPeerManager::getPeerDisplayInfo(const IPAddress& peerAddr) {
  if (!isPeerConfigured(peerAddr)) {
    XLOGF(
        DBG1,
        "Get peer info requested for not configured peer {}",
        peerAddr.str());
    return std::nullopt;
  }
  auto peer = allPeers_[peerAddr];
  std::vector<BgpPeerDisplayInfo> bgpDisplayInfos;
  for (const auto& [_, bgpSessionInfo] : peer->sessionInfos) {
    if (bgpSessionInfo->establishedSessionInfo) {
      bgpDisplayInfos.emplace_back(getEstablishedPeerDisplayInfoHelper(
          bgpSessionInfo, peer->peeringParams));
    }
  }
  if (bgpDisplayInfos.size() > 0) {
    return bgpDisplayInfos;
  }

  // if no established peer, check connectInfos for active sessions
  for (const auto& [_, bgpConnectionInfo] : peer->connectionInfos) {
    // Peer is active if activeSessionInfo is not nullptr (TCP connection up)
    if (bgpConnectionInfo->activeSessionInfo != nullptr) {
      bgpDisplayInfos.emplace_back(getActivePeerDisplayInfoHelper(
          bgpConnectionInfo, peer->peeringParams));
    }
  }
  if (bgpDisplayInfos.size() > 0) {
    return bgpDisplayInfos;
  }

  // if no active session, look for idle sessions
  for (const auto& [_, bgpConnectionInfo] : peer->connectionInfos) {
    // Peer is idle if activeSessionInfo is nullptr (TCP connection not up)
    if (bgpConnectionInfo->activeSessionInfo == nullptr) {
      bgpDisplayInfos.emplace_back(
          getIdlePeerDisplayInfoHelper(bgpConnectionInfo, peer->peeringParams));
    }
  }
  return bgpDisplayInfos;
}

std::optional<std::vector<BgpPeerDisplayInfo>>
FiberBgpPeerManager::getPeerDisplayInfo(const BgpPeerId& peerId) {
  std::optional<BgpPeerDisplayInfo> info =
      getEstablishedPeerDisplayInfo(peerId);
  if (!info) {
    return std::nullopt;
  }
  std::vector<BgpPeerDisplayInfo> bgpDisplayInfos;
  bgpDisplayInfos.emplace_back(info.value());
  return bgpDisplayInfos;
}

std::optional<BgpPeerDisplayInfo>
FiberBgpPeerManager::getEstablishedPeerDisplayInfo(const BgpPeerId& peerId) {
  const auto& peerAddr = peerId.peerAddr;
  if (!isPeerConfigured(peerAddr)) {
    XLOGF(
        DBG1, "Get peer info requested, peer not configured {}", peerId.str());
    return std::nullopt;
  }
  if (!isPeerUp(peerId)) {
    XLOGF(
        DBG1, "Get peer info requested, peer session not up: {}", peerId.str());
    return std::nullopt;
  }
  return getEstablishedPeerDisplayInfoHelper(
      allPeers_.at(peerAddr)->sessionInfos.at(peerId.remoteBgpId),
      allPeers_.at(peerAddr)->peeringParams);
}

std::vector<BgpPeerDisplayInfo>
FiberBgpPeerManager::getAllEstablishedPeerDisplayInfo() {
  std::vector<BgpPeerDisplayInfo> bgpDisplayInfos = {};
  for (const auto& [_, info] : allPeers_) {
    for (const auto& [_, sessionInfo] : info->sessionInfos) {
      if (sessionInfo->establishedSessionInfo) {
        bgpDisplayInfos.emplace_back(getEstablishedPeerDisplayInfoHelper(
            sessionInfo, info->peeringParams));
      }
    }
  }
  return bgpDisplayInfos;
}

BgpPeerDisplayInfo FiberBgpPeerManager::getEstablishedPeerDisplayInfoHelper(
    std::shared_ptr<BgpSessionInfo> sessionInfo,
    const bgp::PeeringParams& peeringParams) {
  const auto activeConnectInfo = sessionInfo->connectionInfo->activeConnectInfo;
  const auto establishedSession = sessionInfo->establishedSessionInfo;
  BgpPeerDisplayInfo info = {
      peeringParams,
      establishedSession->peer->getRemoteBgpIdHBO(),
      establishedSession->peer->getRemoteGrRestartTime(),
      establishedSession->state,
      establishedSession->peer->getLocalSocketAddress(),
      establishedSession->startTime,
      sessionInfo->establishedTime,
      establishedSession->peer->getNegotiatedCapabilities(),
      establishedSession->peer->getNegotiatedHoldTime(),
      activeConnectInfo ? activeConnectInfo->numOfConnectionAttempts : 0,
      establishedSession->peer->getLastResetHoldTimer(),
      establishedSession->peer->getLastResetKeepAliveTimer(),
      establishedSession->peer->getLastRcvdKeepAlive(),
      establishedSession->peer->getLastSentKeepAlive(),
      establishedSession->peer->getSendQueueBlocks(),
      establishedSession->peer->getSocketEgressBufferedEvents(),
      establishedSession->peer->getSendQueueTotalBlockDuration(),
      establishedSession->peer->getLastSendQueueBlockTime(),
      establishedSession->peer->getLastSocketEgressBufferedTime(),
      sessionInfo->lastResetReason,
      sessionInfo->lastResetTime,
      static_cast<int64_t>(sessionInfo->numResets),
      establishedSession->peer->getTxMessageCounters(),
      establishedSession->peer->getRxMessageCounters(),
      establishedSession->peer->getRemoteCapabilities(),
      establishedSession->peer->getRemoteAs(),
  };
  info.peeringParams.peerPrefix = std::nullopt;
  return info;
}

BgpPeerDisplayInfo FiberBgpPeerManager::getIdlePeerDisplayInfoHelper(
    std::shared_ptr<BgpConnectionInfo> connectionInfo,
    const bgp::PeeringParams& peeringParams) {
  BgpPeerDisplayInfo peerInfo;
  peerInfo.peeringParams = peeringParams;
  // Applicable only for dynamic peers
  peerInfo.peeringParams.peerPrefix = std::nullopt;
  peerInfo.remoteBgpId = 0;
  peerInfo.state = BgpSessionState::IDLE;
  peerInfo.localAddr = connectionInfo->activeConnectInfo->localAddr;
  peerInfo.negotiatedHoldTime = std::nullopt;
  peerInfo.numOfConnectionAttempts =
      connectionInfo->activeConnectInfo->numOfConnectionAttempts;
  peerInfo.lastResetHoldTimer = 0;
  peerInfo.lastResetKeepAliveTimer = 0;
  peerInfo.lastReceivedKeepAlive = 0;
  peerInfo.lastSentKeepAlive = 0;

  { // start of weak_ptr lock()
    std::shared_ptr<BgpSessionInfo> lastSessionInfo =
        connectionInfo->lastSessionInfo.lock();
    if (lastSessionInfo) {
      peerInfo.lastResetTime = lastSessionInfo->lastResetTime;
      peerInfo.lastResetReason = lastSessionInfo->lastResetReason;
      peerInfo.numResets = lastSessionInfo->numResets;
    } else {
      peerInfo.lastResetReason = std::nullopt;
      peerInfo.numResets = 0;
    }
  } // end of weak_ptr lock(), shared_ptr is destroyed
  return peerInfo;
}

BgpPeerDisplayInfo FiberBgpPeerManager::getActivePeerDisplayInfoHelper(
    std::shared_ptr<BgpConnectionInfo> connectionInfo,
    const bgp::PeeringParams& peeringParams) {
  BgpPeerDisplayInfo peerInfo;
  peerInfo.peeringParams = peeringParams;
  // Applicable only for dynamic peers
  peerInfo.peeringParams.peerPrefix = std::nullopt;
  peerInfo.remoteBgpId = 0;
  peerInfo.negotiatedHoldTime = std::nullopt;
  peerInfo.state = connectionInfo->activeSessionInfo->state;
  peerInfo.remoteAs = connectionInfo->activeSessionInfo->peer->getRemoteAs();
  peerInfo.localAddr =
      connectionInfo->activeSessionInfo->peer->getLocalSocketAddress();
  /*
   * for ACTIVE session, we use the startTime from activeSessionInfo because the
   * TCP socket has been connected
   */
  peerInfo.startTime = connectionInfo->activeSessionInfo->startTime;
  peerInfo.numOfConnectionAttempts = connectionInfo->activeConnectInfo
      ? connectionInfo->activeConnectInfo->numOfConnectionAttempts
      : 0;
  peerInfo.lastResetHoldTimer =
      connectionInfo->activeSessionInfo->peer->getLastResetHoldTimer();
  peerInfo.lastResetKeepAliveTimer =
      connectionInfo->activeSessionInfo->peer->getLastResetKeepAliveTimer();
  peerInfo.lastReceivedKeepAlive =
      connectionInfo->activeSessionInfo->peer->getLastRcvdKeepAlive();
  peerInfo.lastSentKeepAlive =
      connectionInfo->activeSessionInfo->peer->getLastSentKeepAlive();

  { // start of weak_ptr lock()
    std::shared_ptr<BgpSessionInfo> lastSessionInfo =
        connectionInfo->lastSessionInfo.lock();
    if (lastSessionInfo) {
      peerInfo.lastResetTime = lastSessionInfo->lastResetTime;
      peerInfo.lastResetReason = lastSessionInfo->lastResetReason;
      peerInfo.numResets = lastSessionInfo->numResets;
    } else {
      peerInfo.lastResetReason = std::nullopt;
      peerInfo.numResets = 0;
    }
  } // end of weak_ptr lock(), shared_ptr is destroyed

  peerInfo.txMsgs =
      connectionInfo->activeSessionInfo->peer->getTxMessageCounters();
  peerInfo.rxMsgs =
      connectionInfo->activeSessionInfo->peer->getRxMessageCounters();

  return peerInfo;
}

std::unordered_multimap<IPAddress, shared_ptr<BgpPeerDisplayInfo>>
FiberBgpPeerManager::getAllPeerDisplayInfos() {
  std::unordered_multimap<IPAddress, shared_ptr<BgpPeerDisplayInfo>>
      allPeersInfo;
  // User configured peers, active (discovered) dynamic peer sessions
  for (const auto& [peerAddr, _] : allPeers_) {
    const auto peerInfos = getPeerDisplayInfo(peerAddr);
    if (!peerInfos.has_value()) {
      continue;
    }
    for (const auto& info : peerInfos.value()) {
      allPeersInfo.emplace(
          peerAddr, std::make_shared<BgpPeerDisplayInfo>(info));
    }
  }

  // User configured dynamic peers (Prefix)
  for (const auto& [peerPrefix, peer] : dynamicPeerGroups_) {
    auto peerInfo = std::make_shared<BgpPeerDisplayInfo>();
    peerInfo->peeringParams = peer->peeringParams;
    peerInfo->remoteBgpId = 0;
    peerInfo->state = BgpSessionState::IDLE;
    peerInfo->localAddr = peer->peeringParams.bindAddr;
    peerInfo->negotiatedHoldTime = std::nullopt;

    allPeersInfo.emplace(peerPrefix.first, peerInfo);
  }

  return allPeersInfo;
}

folly::coro::Task<void> FiberBgpPeerManager::co_clearSocketCounters(
    const std::vector<folly::IPAddress>& peers) {
  const bool clearAll = peers.empty();
  const folly::F14FastSet<folly::IPAddress> filter(peers.begin(), peers.end());
  co_await co_withExecutor(
      &evb_, [this, clearAll, filter]() -> folly::coro::Task<void> {
        for (const auto& [peerAddr, peerInfo] : allPeers_) {
          if (!clearAll && !filter.contains(peerAddr)) {
            continue;
          }
          for (const auto& [_, connectionInfo] : peerInfo->connectionInfos) {
            if (connectionInfo->activeSessionInfo &&
                connectionInfo->activeSessionInfo->peer) {
              connectionInfo->activeSessionInfo->peer->clearTxMessageCounters();
              connectionInfo->activeSessionInfo->peer->clearRxMessageCounters();
            }
          }
        }

        /*
         * Warn on requested peers that matched no known peer, so an operator
         * typo is not silently masked by the RPC's success response (the CLI
         * otherwise reports "cleared for <ip>" even when nothing was cleared).
         */
        if (!clearAll) {
          for (const auto& requested : filter) {
            if (!allPeers_.contains(requested)) {
              XLOG(WARNING)
                  << "clear bgp counters: requested peer " << requested.str()
                  << " did not match any known peer; nothing cleared for it";
            }
          }
        }
        co_return;
      }());
}

void FiberBgpPeerManager::scheduleDeleteTerminatedSession(
    const BgpPeerId& peerId,
    uint64_t terminatedVersion,
    uint32_t expectedRemoteAs) noexcept {
  /*
   * PeerManagerBase::markDaemonShutdown() runs synchronously on the
   * PeerManager EventBase before SessionManager::stop() requests cancellation
   * here. An in-flight sessionTerminated() therefore finishes this check and
   * add before cancellation starts, while later calls stop at its shutdown
   * guard.
   */
  if (!asyncScope_.isScopeCancellationRequested()) {
    /*
     * deleteTerminatedSessionTask() takes peerId by value, so constructing the
     * Task copies it into the coroutine frame before this borrowed reference
     * can go out of scope.
     */
    asyncScope_.add(co_withExecutor(
        &evb_,
        deleteTerminatedSessionTask(
            peerId, terminatedVersion, expectedRemoteAs)));
  }
}

folly::coro::Task<void> FiberBgpPeerManager::deleteTerminatedSessionTask(
    BgpPeerId peerId,
    uint64_t terminatedVersion,
    uint32_t expectedRemoteAs) noexcept {
  const auto peerIt = allPeers_.find(peerId.peerAddr);
  if (peerIt == allPeers_.end()) {
    co_return;
  }

  const auto& peerInfo = peerIt->second;
  /*
   * Accept either configured ASN: the caller gates on the ASN the
   * session was established with, which during an ASN migration can be
   * additionalRemoteAs rather than the configured primary. Comparing
   * against remoteAs alone would reject such a peer and leak its
   * terminated sessionInfos entry.
   */
  if (!peerInfo->peeringParams.acceptsRemoteAs(expectedRemoteAs)) {
    co_return;
  }

  /*
   * A static peer can use the VIP ASN. Require a configured dynamic
   * prefix so this path only removes state created from a peer group.
   */
  const auto peerPrefix = getPeerPrefix(peerId.peerAddr);
  if (!peerPrefix || !dynamicPeerGroups_.contains(*peerPrefix)) {
    co_return;
  }

  auto& sessionInfos = peerInfo->sessionInfos;
  const auto sessionIt = sessionInfos.find(peerId.remoteBgpId);
  if (sessionIt == sessionInfos.end()) {
    co_return;
  }

  /*
   * The address and BGP ID can be reused by a newer session. Delete
   * only the terminated incarnation after it has released live state.
   */
  const auto& sessionInfo = sessionIt->second;
  if (sessionInfo->versionNumber->getWithoutLock() != terminatedVersion ||
      sessionInfo->connectionInfo || sessionInfo->establishedSessionInfo) {
    co_return;
  }

  sessionInfos.erase(sessionIt);
}

std::optional<std::shared_ptr<BgpSessionInfo>>
FiberBgpPeerManager::getBgpSessionInfo(const BgpPeerId& peerId) const noexcept {
  const auto& peerAddr = peerId.peerAddr;
  const auto remoteBgpId = peerId.remoteBgpId;
  if (!allPeers_.contains(peerAddr)) {
    return std::nullopt;
  }
  if (!allPeers_.at(peerAddr)->sessionInfos.contains(remoteBgpId)) {
    return std::nullopt;
  }
  auto sessionInfo = allPeers_.at(peerAddr)->sessionInfos.at(remoteBgpId);
  return sessionInfo;
}

void FiberBgpPeerManager::startSession(
    const folly::IPAddress& peerAddr) noexcept {
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait(
      [&]() { startPeer(peerAddr); });
}

void FiberBgpPeerManager::restartSession(
    const folly::IPAddress& peerAddr) noexcept {
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait(
      [&]() { stopPeer(peerAddr, true /*withGR*/); });
}

void FiberBgpPeerManager::shutdownSession(
    const folly::IPAddress& peerAddr) noexcept {
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait(
      [&]() { shutdownPeer(peerAddr); });
}

void FiberBgpPeerManager::startSession(
    const folly::CIDRNetwork& peerPrefix) noexcept {
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait(
      [&]() { startDynamicPeer(peerPrefix); });
}

void FiberBgpPeerManager::restartSession(
    const folly::CIDRNetwork& peerPrefix) noexcept {
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait(
      [&]() { stopDynamicPeerWithGracefulRestart(peerPrefix); });
}

void FiberBgpPeerManager::shutdownSession(
    const folly::CIDRNetwork& peerPrefix) noexcept {
  evb_.runImmediatelyOrRunInEventBaseThreadAndWait(
      [&]() { shutdownDynamicPeer(peerPrefix); });
}

} // namespace bgplib
} // namespace nettools
} // namespace facebook
