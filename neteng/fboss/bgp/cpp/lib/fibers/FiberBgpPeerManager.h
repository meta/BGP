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

#include <chrono>

#include <folly/coro/AsyncScope.h>

#include "fboss/lib/ExponentialBackoff.h"
#include "neteng/fboss/bgp/cpp/common/BgpModuleBase.h"
#include "neteng/fboss/bgp/cpp/config/ConfigStructs.h"
#include "neteng/fboss/bgp/cpp/lib/coro/MPMCQueue.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/BgpPeerDisplayInfo.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberBgpPeer.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberServerSocket.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/VersionNumber.h"
#include "neteng/fboss/bgp/cpp/watchdog/MonitoredModule.h"
#include "neteng/fboss/bgp/cpp/watchdog/MonitoredQueue.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook {
namespace nettools {
namespace bgplib {

// Signal to stop BGP peer manager
struct BgpPeerManagerStop {
  bool gracefulRestart;
};
using BgpPeerManagerError = std::variant<FiberSocketError, BgpPeerManagerStop>;
using Duration = std::chrono::steady_clock::duration;

constexpr std::chrono::milliseconds kDefaultConnectTimeoutMs{500};
constexpr std::chrono::milliseconds kDefaultConnRetryTimeoutMs{100};
constexpr std::chrono::milliseconds kDefaultStartAfterDelayMs{0};

// Store various connection time parameters
class ConnTimeParams {
 public:
  ConnTimeParams() = default;

  // Connection parameters when exponential backoff is not needed
  explicit ConnTimeParams(const Duration& startAfterDelay)
      : startAfterDelay(startAfterDelay),
        minRetryTimeout(kDefaultConnRetryTimeoutMs) {}

  ConnTimeParams(
      const Duration& startAfterDelay,
      const Duration& minRetryTimeout)
      : startAfterDelay(startAfterDelay), minRetryTimeout(minRetryTimeout) {}

  // Connection parameters when exponential backoff is needed
  ConnTimeParams(
      const Duration& startAfterDelay,
      const Duration& minRetryTimeout,
      const Duration& maxRetryTimeout,
      const Duration& connectTimeout = kDefaultConnectTimeoutMs)
      : startAfterDelay(startAfterDelay),
        minRetryTimeout(minRetryTimeout),
        connectTimeout(connectTimeout),
        maxRetryTimeout(maxRetryTimeout) {
    enableConnectExponentialBackoff = true;
  }

  // Connection parameters when exponential backoff is needed
  ConnTimeParams(
      const Duration& startAfterDelay,
      const Duration& minRetryTimeout,
      const Duration& maxRetryTimeout,
      const Duration& connectTimeout,
      const Duration& minSessionRetryTimeout,
      const Duration& maxSessionRetryTimeout,
      const Duration& sessionDampenDuration)
      : startAfterDelay(startAfterDelay),
        minRetryTimeout(minRetryTimeout),
        connectTimeout(connectTimeout),
        maxRetryTimeout(maxRetryTimeout),
        minSessionRetryTimeout(minSessionRetryTimeout),
        maxSessionRetryTimeout(maxSessionRetryTimeout),
        sessionDampenDuration(sessionDampenDuration) {
    enableConnectExponentialBackoff = true;
    enableSessionExponentialBackoff = true;
  }

  Duration getMinRetryTimeout() const {
    return minRetryTimeout;
  }

  Duration getConnectTimeout() const {
    return connectTimeout;
  }

  std::optional<Duration> getMaxRetryTimeout() const {
    return maxRetryTimeout;
  }

  Duration getStartAfterDelay() const {
    return startAfterDelay;
  }

  Duration getMinSessionRetryTimeout() const {
    return minSessionRetryTimeout;
  }

  Duration getMaxSessionRetryTimeout() const {
    return maxSessionRetryTimeout;
  }

  Duration getSessionDampenDuration() const {
    return sessionDampenDuration;
  }

  bool isConnectBackoffEnabled() const {
    return enableConnectExponentialBackoff;
  }

  bool isSessionBackoffEnabled() const {
    return enableSessionExponentialBackoff;
  }

 private:
  Duration startAfterDelay{kDefaultStartAfterDelayMs};
  bool enableConnectExponentialBackoff{false};
  Duration minRetryTimeout{kDefaultConnRetryTimeoutMs};
  Duration connectTimeout{kDefaultConnectTimeoutMs};
  // Max retry timeout applicable only when exponentional backoff is turned ON
  std::optional<Duration> maxRetryTimeout;
  bool enableSessionExponentialBackoff{false};
  Duration minSessionRetryTimeout{};
  Duration maxSessionRetryTimeout{};
  Duration sessionDampenDuration{};
};

// Active here means ACTIVE_PASSIVE connection
struct BgpPeerActiveConnectInfo {
  folly::SocketAddress peerAddr;
  folly::SocketAddress localAddr;
  ConnTimeParams connTimeParams;
  uint32_t numOfConnectionAttempts{0};
  std::optional<int32_t> ttlSecurityHops;
  std::unique_ptr<folly::AsyncTimeout> pendingTimeout;
  std::unique_ptr<FiberSocket> socket{nullptr};
  /*
   * Set while activeConnect() is suspended inside FiberSocket::connect().
   * `socket` must not be replaced while this is set: connect() resumes by
   * dereferencing members of the FiberSocket it was called on, so destroying
   * it mid-flight is a use-after-free.
   */
  bool connectInProgress{false};
  /**
   * TCP connection backoff. Failure to establish TCP connection will
   * backoff for the next attempt
   */
  std::optional<facebook::fboss::ExponentialBackoff<std::chrono::milliseconds>>
      connectBackoff;
  /**
   * BGP session backoff. Excluding TCP connection attempts, failures
   * to keep BGP sessions up and running within dampening duration
   * results in this backoff
   */
  std::optional<facebook::fboss::ExponentialBackoff<std::chrono::milliseconds>>
      sessionBackoff;

  void connectBackoffReportError() {
    if (connectBackoff) {
      connectBackoff->reportError();
    }
  }

  void connectBackoffReportSuccess() {
    if (connectBackoff) {
      connectBackoff->reportSuccess();
    }
  }

  void sessionBackoffReportError() {
    if (sessionBackoff) {
      sessionBackoff->reportError();
    }
  }

  void sessionBackoffReportSuccess() {
    if (sessionBackoff) {
      sessionBackoff->reportSuccess();
    }
  }

  /*
   * If backoff not enabled, return minimum retry tiemout
   * Else return configured (or default) dampening duration
   */
  std::chrono::milliseconds getSessionDampenDuration() {
    if (!sessionBackoff) {
      // Exponential backoff is not configured, return minimum retry timeout
      return std::chrono::duration_cast<std::chrono::milliseconds>(
          connTimeParams.getMinRetryTimeout());
    }

    return std::chrono::duration_cast<std::chrono::milliseconds>(
        connTimeParams.getSessionDampenDuration());
  }

  std::chrono::milliseconds getNextRetryTime() {
    if (!connectBackoff) {
      // Exponential backoff is not configured, return minimum retry timeout
      return std::chrono::duration_cast<std::chrono::milliseconds>(
          connTimeParams.getMinRetryTimeout());
    }
    /*
     * When no error is reported or success is just reported,
     * getTimeRemainingUntilRetry returns 0, but we want nextRetryTime
     * to be be minRetryTimeout.
     */
    auto nextRetryTime = connectBackoff->getTimeRemainingUntilRetry();
    if (!nextRetryTime.count()) {
      nextRetryTime = std::chrono::duration_cast<std::chrono::milliseconds>(
          connTimeParams.getMinRetryTimeout());
    }
    return nextRetryTime;
  }

  bool isSessionBackoffEnabled() {
    return connTimeParams.isSessionBackoffEnabled();
  }

  std::chrono::milliseconds getSessionRetryTimeRemaining() {
    if (!sessionBackoff) {
      return std::chrono::milliseconds(0);
    }
    return sessionBackoff->getTimeRemainingUntilRetry();
  }

  /*
   * Suggests if last connection attempt for the session was within
   * configured (if one configured) dampening duration
   */
  bool inSessionDampenDuration(
      std::chrono::steady_clock::time_point lastSessionReset) {
    auto sinceLastResetTime =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - lastSessionReset);

    auto sessionDampenDuration = getSessionDampenDuration().count();
    return (
        sessionDampenDuration &&
        (sinceLastResetTime.count() < sessionDampenDuration));
  }
};

using ObservableEventT = std::
    variant<FiberBgpPeer::ObservableStateT, FiberBgpPeer::ObservableMessageT>;

/*
 * Stores state to do with currently established TCP connection
 * For a Bgp peer, there can be multiple TCP sessions till collision resolution.
 * Active here means ACTIVE_PASSIVE connection
 */
struct BgpPeerActiveSessionInfo {
  /*
   * This is the entrace point to zoom into the per-peer view.
   */
  std::shared_ptr<FiberBgpPeer> peer;

  /*
   * This is the input queue to FiberBgpPeer, aka, the egress from AdjRib.
   */
  std::shared_ptr<FiberBgpPeer::InputQueueT> peerInput =
      std::make_shared<FiberBgpPeer::InputQueueT>();

  /*
   * This is the input queue to FiberBgpPeer, aka, the egress from AdjRib,
   * but bounded with backpressure.
   */
  std::shared_ptr<FiberBgpPeer::BoundedInputQueueT> boundedPeerInput =
      std::make_shared<FiberBgpPeer::BoundedInputQueueT>(
          kMaxEgressQueueSize,
          kEgressQueueHighWatermark,
          kEgressQueueLowWatermark);

  /*
   * This is the output queue from FiberBgpPeer, aka, the ingress from AdjRib.
   */
  std::shared_ptr<FiberBgpPeer::OutputQueueT> peerOutput =
      std::make_shared<FiberBgpPeer::OutputQueueT>(kMaxIngressQueueSize);

  /*
   * This is the state updates to notify session UP/DOWN state from
   * FiberBgpPeer to FiberBgpPeerManager.
   */
  RWQueue<ObservableEventT> observeQueue;

  // Set based on observeQueue data
  BgpSessionState state{BgpSessionState::IDLE};

  // Creation time (TCP session uptime)
  const std::chrono::steady_clock::time_point startTime{
      std::chrono::steady_clock::now()};

  void changeStateTo(BgpSessionState newState) {
    const auto& localAddr = peer->getLocalSocketAddress();
    const auto& remoteAddr = peer->getRemoteSocketAddress();
    XLOGF(
        DBG1,
        "Session[local [{}]:{}, remote [{} ({})]:{} ] state transition: {} -> {}",
        getAddressStr(localAddr),
        localAddr.getPort(),
        getAddressStr(remoteAddr),
        peer->getRemotePeerDescription(),
        remoteAddr.getPort(),
        getBgpSessionStateName(state),
        getBgpSessionStateName(newState));
    state = newState;
  }
};

struct BgpSessionInfo; // forward declaration of struct BgpSessionInfo

/*
 * Stores state of BGP session and associated TCP connection
 * Includes BgpPeerActiveConnectInfo and BgpPeerActiveSessionInfo, things that
 * we manage/create when socket is connected or peer is added. Note that they
 * can be nullptr, (if both, the entry will be removed).
 * When a peer is added (either static or dynamic),
 *  activeConnectInfo will be created to acitvely connect the peer,
 *  activeSessionInfo will be null until the socket is connected.
 * If a passive connect is established on a different peer port, a new
 * BgpConnectionInfo will be created for that port but with a null
 * activeConnectInfo.
 */
struct BgpConnectionInfo {
  // nullptr if it's a passive connection
  std::shared_ptr<BgpPeerActiveConnectInfo> activeConnectInfo;
  // nullptr if lose connection
  std::shared_ptr<BgpPeerActiveSessionInfo> activeSessionInfo;

  /*
   * empty weak_ptr if there has never been an established BGP session on this
   * connectionInfo
   */
  std::weak_ptr<BgpSessionInfo> lastSessionInfo{
      std::weak_ptr<BgpSessionInfo>()};
};

/*
 * Stores state of a BGP session
 * created after BGP session established, keeps tracking session status
 * afterwards
 */
struct BgpSessionInfo {
  BgpSessionInfo() : versionNumber(std::make_shared<VersionNumber>()) {}

  // Pointer to associated connectionInfo, nullptr when session is down
  std::shared_ptr<BgpConnectionInfo> connectionInfo;
  /*
   * Pointer to associated activeSessionInfo, nullptr indicates session is not
   * in established state, while connectionInfo->activeSessionInfo is there as
   * long as socket is still alive
   */
  std::shared_ptr<BgpPeerActiveSessionInfo> establishedSessionInfo;
  // BGP session established time
  std::chrono::steady_clock::time_point establishedTime;
  /*
   * Track successive flaps using incarnation or version number
   * This counter monotonically increases for each transition from
   * established --> terminate or terminate --> established
   */
  std::shared_ptr<VersionNumber> versionNumber;
  // Set only when session goes down from ESTABLISHED state
  std::optional<ResetReason> lastResetReason;
  /*
   * Last NOTIFICATION in each direction on the session that went down,
   * captured alongside lastResetReason. Held here rather than read from the
   * peer at display time because the peer object is replaced when the session
   * re-forms, which would lose the reason the previous one ended.
   */
  std::optional<TimestampedNotification> lastNotificationSent;
  std::optional<TimestampedNotification> lastNotificationReceived;
  // number of times peer went down
  uint64_t numResets{0};
  // last time peer went down
  std::chrono::steady_clock::time_point lastResetTime;
};

/*
 * Stores state to do with a given Bgp peer.
 * Created for each configured static peer or dynamic peer.
 */
struct BgpPeerInfoInternal {
  bgp::PeeringParams peeringParams;

  /*
   * BgpConnectionInfo per port, each created when the static peer is added or
   * when the TCP connection is established.
   */
  std::unordered_map<
      uint16_t /* remote port */,
      std::shared_ptr<BgpConnectionInfo>>
      connectionInfos;
  /*
   * BgpEstablishedSessionInfo per remoteBgpId, each created when BGP session is
   * established. SessionInfo is intentionally retained after the session goes
   * down so existing consumers continue to have its reset and status history.
   * Address-level liveness must ignore detached historical entries rather than
   * deleting them as part of a liveness check.
   */
  std::
      unordered_map<uint32_t /* remoteBgpId */, std::shared_ptr<BgpSessionInfo>>
          sessionInfos;
};

/*
 * Stores common configuration and a group of established bgp peer sessions
 * related to a dynamic bgp peer group.
 */
struct BgpDynamicPeerGroupInfo {
  bgp::PeeringParams peeringParams; // peerAddr and peerPort are incomplete
  std::set<folly::IPAddress> activePeers;
};

/**
 * This class provides mechanism to receive high level state information
 * from BgpPeer so that you can take actions like process updates or try
 * to reconnect if session got terminated.
 */
class FiberBgpPeerCallback {
 public:
  virtual ~FiberBgpPeerCallback() {}

  /**
   * This will be invoked when a BGP session is established peer after
   * initial BGP handshake and peers are now ready to exchange routing
   * information!
   */
  virtual void sessionEstablished(const BgpPeerId& peerId) noexcept = 0;

  /**
   * This will be called when a BGP session is terminated or reset because
   * of Notification message, network error or any other kind of error. You
   * must call BgpPeer::start() to initiate the connection with
   * peer.
   */
  virtual void sessionTerminated(const BgpPeerId& peerId) noexcept = 0;

  /**
   * This will be invoked when new BgpUpdates are received. peerAddr is
   * always passed so that callback implementation can be shared
   * with others
   */
  virtual void bgpUpdatesReceived(
      const BgpPeerId& peerId,
      const BgpUpdate2& update) noexcept = 0;

  /**
   * This will be invoked when new BgpEndOfRib is received. peerAddr is
   * always passed so that callback implementation can be shared
   * with others
   */
  virtual void bgpEndOfRibReceived(
      const BgpPeerId& peerId,
      const BgpEndOfRib& eor) noexcept = 0;

  /**
   * Invoked when Route Refresh message with any route refresh message subtype
   * is received. peerAddr is always passed so that callback implementation can
   * be shared with others
   */
  virtual void bgpRouteRefreshReceived(
      const BgpPeerId& peerId,
      const BgpRouteRefresh& routeRefresh) noexcept = 0;
};

/**
 * Provides API for connecting to BGP Peers.
 */
class FiberBgpPeerManager
    : public std::enable_shared_from_this<FiberBgpPeerManager>,
      public bgp::MonitoredModule,
      public bgp::BgpModuleBase {
 public:
  using BgpUpdateVariant = std::variant<BgpUpdate2, BgpEndOfRib>;
  struct InputMessagesT {
    folly::IPAddress peerAddress;
    BgpUpdateVariant update;
  };
  enum class ErrorCode : uint8_t {
    PEER_DOES_NOT_EXIST = 1,
    BIND_PEER_ADDRS_ARE_DIFF_AFIS,
    PEER_EXISTS_ALREADY,
    PEER_NOT_ESTABLISHED,
    INVALID_PEER,
  };

  /*
   * read and announce InputMessagesT. empty message
   * terminates this peer
   */
  using InputQueueT = bgp::MonitoredRWQueue<InputMessagesT>;

  /*
   * Creates FiberBgpPeerManager instance with given config. Automatically
   * creates fibers to use for TCP connection setup.
   *
   *  - If enableMessagesOverNotifyQueue is true, received BGP messages will be
   *    copied to notifyQueue_.
   *  - Otherwise, received BGP messages will be copied to peer output queue
   *    (oqueue_).
   *
   * NOTE: if you are using FiberBgpPeerManager as a library, please set
   * `enableMessagesOverNotifyQueue` to true.
   */
  FiberBgpPeerManager(
      const bgp::BgpGlobalConfig& config,
      folly::fibers::FiberManager& fm,
      folly::EventBase& evb,
      bool enableMessagesOverNotifyQueue = true,
      bool enableCoroNotifyQueue = false);

 protected:
  /*
   * When evb is not provided, refer to the evb_ defined in BgpModuleBase
   * This allows FiberBgpPeerManager to be run one its own event base thread
   * Only used by bgp::SessionManager
   */
  explicit FiberBgpPeerManager(
      const bgp::BgpGlobalConfig& config,
      bool enableMessagesOverNotifyQueue = true,
      bool enableCoroNotifyQueue = false);

 public:
  // Destructor terminates peer connections and kills all fibers.
  virtual ~FiberBgpPeerManager();

  /*
   * Main fiber routine which needs to be run inside a fiber.
   * This creates child fibers and waits on errorQueue_.
   * When receiving an error, this stops all child fibers and terminates.
   */
  void run() noexcept override;

  /*
   * Initiate shut down process by putting messages to errorQueue_ to indicate
   * GR (graceful restart) status
   */
  void shutdownWithGR(bool gracefulRestart) noexcept;

 private:
  // avoid the masking issue
  void stop() noexcept override {}

 public:
  /*
   * Add a peer referenced by peerAddr. Automatically attempts to peer with
   * the peer. If for any reason the connection stops or is never made, active
   * retry attempts will be made at an interval of connRetryTimeout_
   */
  folly::Expected<folly::Unit, ErrorCode> addPeer(
      const folly::IPAddress& peerAddr,
      const uint32_t peerAsn,
      const uint16_t port = constants::kBgpPort,
      const ConnTimeParams& connTimeParams = ConnTimeParams(
          kDefaultStartAfterDelayMs,
          kDefaultConnRetryTimeoutMs,
          kDefaultConnectTimeoutMs),
      const bgp::TBgpSessionConnectMode connectMode =
          bgp::TBgpSessionConnectMode::PASSIVE_ACTIVE);
  folly::Expected<folly::Unit, ErrorCode> addPeer(
      const folly::IPAddress& peerAddr,
      const uint32_t peerAsn,
      const uint32_t myAsn,
      const uint16_t port = constants::kBgpPort,
      const ConnTimeParams& connTimeParams =
          ConnTimeParams(kDefaultStartAfterDelayMs, kDefaultConnRetryTimeoutMs),
      const bgp::TBgpSessionConnectMode connectMode =
          bgp::TBgpSessionConnectMode::PASSIVE_ACTIVE);
  virtual folly::Expected<folly::Unit, ErrorCode> addPeer(
      const folly::IPAddress& peerAddr,
      const uint32_t peerAsn,
      const uint32_t myAsn,
      const folly::SocketAddress& bindAddr,
      const uint16_t port = constants::kBgpPort,
      const ConnTimeParams& connTimeParams =
          ConnTimeParams(kDefaultStartAfterDelayMs, kDefaultConnRetryTimeoutMs),
      const bgp::TBgpSessionConnectMode connectMode =
          bgp::TBgpSessionConnectMode::PASSIVE_ACTIVE,
      const std::optional<uint32_t>& localBgpIdOpt = std::nullopt);
  virtual folly::Expected<folly::Unit, ErrorCode> addPeer(
      const folly::IPAddress& peerAddr,
      const bgp::PeeringParams& params,
      const ConnTimeParams& connTimeParams = ConnTimeParams(
          kDefaultStartAfterDelayMs,
          kDefaultConnRetryTimeoutMs));

  MAKE_CORO_FUNCTION(addPeer)

  /*
   * Add a dynamic peer referenced by peerPrefix.
   * Automatically establish peer session upon active connection from peer.
   */
  folly::Expected<folly::Unit, ErrorCode> addDynamicPeer(
      const folly::CIDRNetwork& peerPrefix,
      const uint32_t peerAsn);
  folly::Expected<folly::Unit, ErrorCode> addDynamicPeer(
      const folly::CIDRNetwork& peerPrefix,
      const uint32_t peerAsn,
      const uint32_t myAsn);
  folly::Expected<folly::Unit, ErrorCode> addDynamicPeer(
      const folly::CIDRNetwork& peerPrefix,
      const uint32_t peerAsn,
      const uint32_t myAsn,
      const folly::SocketAddress& bindAddr);
  virtual folly::Expected<folly::Unit, ErrorCode> addDynamicPeer(
      const folly::CIDRNetwork& peerPrefix,
      const bgp::PeeringParams& params);

  /*
   * Terminate connection with peer and stop attempting connection,
   * but keep the peer in the idle state and do not drop it.
   */
  virtual folly::Expected<folly::Unit, ErrorCode> shutdownPeer(
      const folly::IPAddress& peerAddr,
      bool peerDelete = false);

  MAKE_CORO_FUNCTION(shutdownPeer)

  /*
   * If withGR is true, Stop peer with graceful restart
   * If withGR is false, Terminate connection with peer,
   * but continue to retry connecting.
   */
  virtual folly::Expected<folly::Unit, ErrorCode> stopPeer(
      const folly::IPAddress& peerAddr,
      bool withGR);

  MAKE_CORO_FUNCTION(stopPeer)

  // Terminate connection with peer and configure to retry connecting,
  virtual folly::Expected<folly::Unit, ErrorCode> startPeer(
      const folly::IPAddress& peerAddr);

  // Stop dynamic peer with graceful restart.
  virtual folly::Expected<folly::Unit, ErrorCode>
  stopDynamicPeerWithGracefulRestart(const folly::CIDRNetwork& peerPrefix);

  /*
   * Terminate connection with dynamic peer and disallow to accept
   * incoming connections, keep the peer in the idle state and do not drop it
   */
  virtual folly::Expected<folly::Unit, ErrorCode> shutdownDynamicPeer(
      const folly::CIDRNetwork& peerPrefix);

  /*
   * Terminate connection with dynamic peer and
   * allow to accept retried incoming connection,
   * but not initiate connection
   */
  virtual folly::Expected<folly::Unit, ErrorCode> startDynamicPeer(
      const folly::CIDRNetwork& peerPrefix);

  // Terminates connection with peer and stops attempting connections.
  virtual folly::Expected<folly::Unit, ErrorCode> dropPeer(
      const folly::IPAddress& peerAddr,
      bool peerDelete = false);
  folly::Expected<folly::Unit, ErrorCode> dropPeer(
      const folly::CIDRNetwork& peerPrefix,
      bool peerDelete = false);

  MAKE_CORO_FUNCTION(dropPeer)

  /*
   * APIs to send BgpUpdate to specified peer. This API is async and will
   * return immediately after putting message to a queue.
   */
  folly::Expected<folly::Unit, ErrorCode> sendUpdate(
      const BgpPeerId& peerId,
      std::unique_ptr<BgpUpdate2> update) const;
  folly::Expected<folly::Unit, ErrorCode> sendUpdates(
      const BgpPeerId& peerId,
      std::vector<std::unique_ptr<BgpUpdate2>>&& updates) const;

  /*
   * API to send BGP EndOfRib for v4/v6 to given peerId.
   * You can only call this API after session is established.
   */
  folly::Expected<folly::Unit, ErrorCode> sendEndOfRib(
      const BgpPeerId& peerId) const;

  /*
   * Returns true when a static peer has any established session, ignoring
   * retained state for previous router IDs. Dynamic peers retain the existing
   * requirement that all known sessions are established.
   */
  bool isPeerUp(const folly::IPAddress& peerAddr) const;

  // Returns true if a connection has been fully established with peer
  bool isPeerUp(const BgpPeerId& peerId) const noexcept;

  // Determines if peer version client is using still valid for peer
  virtual bool isPeerVersionValid(
      const BgpPeerId& peerId,
      const uint64_t versionNumber) const noexcept;

  // accessor
  std::optional<folly::SocketAddress> getListenAddress() const noexcept;
  RQueue<ObservableEventT> getNotifyQueue() noexcept;
  bgp::coro::MPMCQueue<ObservableEventT>& getNotifyCoroQueue() noexcept;

  folly::Expected<std::shared_ptr<BgpPeerActiveSessionInfo>, ErrorCode>
  getEstablishedSessionInfo(const BgpPeerId& peerId) const noexcept;

  // Accessor of peerOutput/adjRibIn queue
  virtual std::shared_ptr<FiberBgpPeer::OutputQueueT> getPeerOutputQueue(
      const BgpPeerId& peerId) noexcept;

  // Accessor of peerInput/adjRibOut queue
  virtual std::shared_ptr<FiberBgpPeer::InputQueueT> getPeerInputQueue(
      const BgpPeerId& peerId) noexcept;

  // Accessor of boundedPeerInput/boundedAdjRibOut queue
  virtual std::shared_ptr<FiberBgpPeer::BoundedInputQueueT>
  getBoundedPeerInputQueue(const BgpPeerId& peerId) noexcept;

  // Get all peers (including dynamic) info
  virtual std::
      unordered_multimap<folly::IPAddress, std::shared_ptr<BgpPeerDisplayInfo>>
      getAllPeerDisplayInfos();

  MAKE_CORO_FUNCTION(getAllPeerDisplayInfos)

  /*
   * Reset every peer's socket tx (egress) and rx (ingress) message counters.
   *
   * FiberBgpPeer state is single-writer on the I/O (SessionManager) EventBase
   * (the same invariant that lets sendSocketLoop touch it lock-free), so the
   * walk over allPeers_ MUST run on evb_. This coroutine hops onto evb_ itself
   * before mutating peer state, so callers on other threads (e.g. the thrift
   * handler BgpServiceBase::co_clearCounters) can simply co_await it.
   */
  folly::coro::Task<void> co_clearSocketCounters(
      const std::vector<folly::IPAddress>& peers);

  /*
   * Schedule removal of the exact DOWN session on the SessionManager
   * EventBase and return immediately. Removal revalidates the configuration
   * and lifecycle state; the address-level allPeers_ entry is retained.
   */
  virtual void scheduleDeleteTerminatedSession(
      const BgpPeerId& peerId,
      uint64_t terminatedVersion,
      uint32_t expectedRemoteAs) noexcept;

  std::optional<std::shared_ptr<BgpSessionInfo>> getBgpSessionInfo(
      const BgpPeerId& peerId) const noexcept;

  // returns a vector because there could be more than one active session
  virtual std::optional<std::vector<BgpPeerDisplayInfo>> getPeerDisplayInfo(
      const folly::IPAddress& peerAddr);

  virtual std::optional<std::vector<BgpPeerDisplayInfo>> getPeerDisplayInfo(
      const BgpPeerId& peerId);

  MAKE_CORO_FUNCTION(getPeerDisplayInfo)

  virtual std::optional<BgpPeerDisplayInfo> getEstablishedPeerDisplayInfo(
      const BgpPeerId& peerId);

  inline void shutdownInProgress() noexcept {
    shutdownInProgress_ = true;
  }

  // Util functions used inside observable event processing
  static bool needToKeepThisPeer(
      std::optional<folly::SocketAddress> localListenAddress,
      std::shared_ptr<FiberBgpPeer> peer);

  // get establishedSessionInfo from peerInfo for the given bgpId
  static std::shared_ptr<BgpPeerActiveSessionInfo>
  getEstablishedSessionInfoFromPeerInfo(
      const std::shared_ptr<BgpPeerInfoInternal> peerInfo,
      const uint32_t remoteBgpId);

  static std::shared_ptr<BgpPeerActiveConnectInfo>
  getListenPortActiveConnectInfoFromPeerInfo(
      const std::shared_ptr<BgpPeerInfoInternal> peerInfo);

  // get all established peers info
  virtual std::vector<BgpPeerDisplayInfo> getAllEstablishedPeerDisplayInfo();

  MAKE_CORO_FUNCTION(getAllEstablishedPeerDisplayInfo)

  inline void setRestartingState(bool val) {
    // Once all EoRs have been received we are no longer in "restarting" state
    isRestarting_ = val;
  }

  /**
   * Number of established dynamic peers.
   *
   * @return Number of established dynamic peers.
   */
  uint32_t numDynamicPeers(void);

  /**
   * Checks if the number of established dynamic peers exceeds the dynamic peer
   * limit.
   *
   * @return true if the number of established dynamic peers exceeds the dynamic
   * peer limit, false otherwise.
   */
  bool exceedsDynamicPeerLimit();

  // Bgp Service interfaces
  void startSession(const folly::IPAddress& peerAddr) noexcept;
  void restartSession(const folly::IPAddress& peerAddr) noexcept;
  void shutdownSession(const folly::IPAddress& peerAddr) noexcept;

  void startSession(const folly::CIDRNetwork& peerPrefix) noexcept;
  void restartSession(const folly::CIDRNetwork& peerPrefix) noexcept;
  void shutdownSession(const folly::CIDRNetwork& peerPrefix) noexcept;

 private:
  folly::coro::Task<void> deleteTerminatedSessionTask(
      BgpPeerId peerId,
      uint64_t terminatedVersion,
      uint32_t expectedRemoteAs) noexcept;

  // Main function to setup and run fibers for a Bgp peer
  folly::Expected<folly::Unit, ErrorCode> runBgpPeer(
      FiberSocket&& socket) noexcept;
  // Get the IP and port for the socket.
  folly::Expected<
      std::pair<folly::IPAddress, uint16_t /* port */>,
      FiberBgpPeerManager::ErrorCode>
  getRemoteSocketAddress(FiberSocket& socket) noexcept;
  /*
   * Helper function to add a Peer, this will only check for
   * duplicate peer configuration in allPeers_
   */
  folly::Expected<folly::Unit, ErrorCode> addPeerHelper(
      const folly::IPAddress& peerAddr,
      const bgp::PeeringParams& peeringParams,
      const ConnTimeParams& connTimeParams = ConnTimeParams(
          std::chrono::milliseconds{0},
          kDefaultConnRetryTimeoutMs));
  /*
   * Helper function to check if there is a duplicate TCP session to the
   * given remote socket address
   */
  bool isDuplicateBgpPeerActiveSession(
      const folly::SocketAddress& remoteSocketAddr) const;
  /*
   * Populate BgpPeerActiveSessionInfo and FiberBgpPeer. Start fibers to observe
   * state changes and received Bgp messages.
   */
  std::shared_ptr<BgpPeerActiveSessionInfo> setupBgpPeerActiveSession(
      std::shared_ptr<BgpPeerInfoInternal> peerInfo,
      FiberSocket&& socket) noexcept;

  virtual std::unique_ptr<FiberServerSocket> makeServerSocket(
      const std::optional<folly::SocketAddress>& listenAddr) const noexcept;

  // Set necessary socket options on server socket
  void setServerSocketOptions() noexcept;

  // Waiting on accept(), handle new TCP connection.
  void passiveConnectLoop(
      folly::fibers::Semaphore& loopStartSemaphore) noexcept;

  // Helper function to check if there is a duplicate peer address configured
  bool isPeerConfigured(const folly::IPAddress& peerAddr) const noexcept;

  // Helper function to check if there is a duplicate dynamic peer configured
  bool isPeerConfigured(const folly::CIDRNetwork& peerPrefix) const noexcept;

  // Helper function to get error code when checking session up state
  folly::Expected<bool, ErrorCode> isPeerUpExpected(
      const BgpPeerId& peerId) const noexcept;

  // Given a peer address check if it falls into one of the dynamic peer groups
  std::optional<folly::CIDRNetwork> getPeerPrefix(
      const folly::IPAddress& peerAddr) const noexcept;

  // Given a peer prefix get all configured peer address within the prefix range
  std::vector<folly::IPAddress> getPeerAddrs(
      const folly::CIDRNetwork& peerPrefix) const noexcept;

  /*
   * Start when FiberBgpPeerManager::addPeer() is called and waits on
   * connect().
   */
  void setupAndRunActiveConnectFibers(
      std::shared_ptr<BgpPeerActiveConnectInfo> activeConnectInfo) noexcept;

  /*
   * Calling connect(), initiate new TCP connection. Start retry timer if
   * needed.
   */
  void activeConnect(
      std::shared_ptr<BgpPeerActiveConnectInfo> activeConnectInfo) noexcept;

  /*
   * report an error to backoff for activeConnection and start the restart
   * timer with new retry time after backoff
   */
  void restartConnRetryTimer(
      std::shared_ptr<BgpPeerActiveConnectInfo> activeConnectInfo,
      const std::chrono::milliseconds& nextRetryTime) noexcept;

  /*
   * Check if the peer is eligible for connection retry (not shutdown,
   * peer still exists, manager not shutting down).
   */
  bool canRetryConnect(const folly::IPAddress& peerAddr) const noexcept;

  /*
   * Report connect backoff error, compute jittered retry time,
   * and schedule the retry timer.
   */
  void scheduleConnRetryWithBackoff(
      const std::shared_ptr<BgpPeerActiveConnectInfo>&
          activeConnectInfo) noexcept;

  /*
   * Schedule an AsyncTimeout on evb_ that spawns a fiber to run
   * activeConnect() when it fires.
   */
  void scheduleConnectTimeout(
      std::shared_ptr<BgpPeerActiveConnectInfo> activeConnectInfo,
      std::chrono::milliseconds delay);

  /*
   * Send signal to shutdown to all fibers of FiberBgpPeerManager and
   * FiberBgpPeers
   */
  void shutdownFibers(const bool gracefulRestart) noexcept;

  /*
   * [Observer Queue Processing]
   *
   * Process observable events, which include
   *  1) state events
   *  2) messages events
   *
   * from `FiberBgpPeer` and put events to notifyQueue_.
   */
  void processObservableEventLoop(
      std::shared_ptr<BgpPeerInfoInternal> peerInfo,
      std::shared_ptr<BgpPeerActiveSessionInfo> activeSessionInfo) noexcept;

  void processObservableState(
      std::shared_ptr<BgpPeerInfoInternal> peerInfo,
      std::shared_ptr<BgpPeerActiveSessionInfo> activeSessionInfo,
      FiberBgpPeer::ObservableStateT& stateEvt);

  void processObservableMessage(
      std::shared_ptr<BgpPeerInfoInternal> peerInfo,
      std::shared_ptr<BgpPeerActiveSessionInfo> activeSessionInfo,
      FiberBgpPeer::ObservableMessageT& msgEvt);

  void writeToNotifyQueue(ObservableEventT&& event) noexcept;

  void shutdownPeerDueToCollision(std::shared_ptr<FiberBgpPeer> peer);

  // Helper to return info of a peer with established session
  BgpPeerDisplayInfo getEstablishedPeerDisplayInfoHelper(
      std::shared_ptr<BgpSessionInfo> sessionInfo,
      const bgp::PeeringParams& peeringParams);

  // Gets Session info from peer with no active bgp session
  BgpPeerDisplayInfo getIdlePeerDisplayInfoHelper(
      std::shared_ptr<BgpConnectionInfo> connectionInfo,
      const bgp::PeeringParams& peeringParams);

  /*
   * Gets active session info from peer
   * called by getAllPeerInfo and getPeerInfo
   */
  BgpPeerDisplayInfo getActivePeerDisplayInfoHelper(
      std::shared_ptr<BgpConnectionInfo> connectionInfo,
      const bgp::PeeringParams& peeringParams);

  // Local Bgp configuration
  const bgp::BgpGlobalConfig bgpGlobalConfig_;

  // indicator flag that shutdown has started
  bool shutdownInProgress_ = false;

  // Server socket waiting for incoming TCP connection request
  std::unique_ptr<FiberServerSocket> serverSocket_{nullptr};

  // Signal error from any of subfibers
  bgp::MonitoredRWQueue<BgpPeerManagerError> errorQueue_;

  /*
   * Used to notify session state changes and received messages to clients
   * Attention: provide 2 flavors of queues for fiber and coro tasks
   */
  bgp::MonitoredRWQueue<ObservableEventT> notifyQueue_;
  bgp::MonitoredMPMCQueue<ObservableEventT> notifyCoroQueue_;

 protected:
  // Set to true if manager is shutting down; false, otherwise.
  std::atomic<bool> alreadyShutdown_{false};

  folly::fibers::FiberManager& fm_;

  // Eventbase to schedule AsyncTimeout or folly::coro tasks
  folly::EventBase& evb_;

 private:
  /*
   * Task id of fiber workers created for each peer.
   * When shutting down, we wait till all these fibers complete
   */
  std::unordered_set<int> peerWorkerIds_;
  int peerWorkerId_{0};
  bgp::MonitoredRWQueue<int> stoppedPeerWorkerIdQ_;

  // Map of each peer address to BgpPeerInfoInternal
  std::unordered_map<folly::IPAddress, std::shared_ptr<BgpPeerInfoInternal>>
      allPeers_;

  // Map of dynamic peer prefix to BgpDynamicPeerGroupInfo
  std::unordered_map<
      folly::CIDRNetwork,
      std::shared_ptr<BgpDynamicPeerGroupInfo>>
      dynamicPeerGroups_;

  /*
   * Setting that controls which queue (notifyQueue_ or oqueue_) peer received
   * messages are sent to. Bgp binary sets this to false.
   * @sa FiberBgpPeerManager() constructor definition.
   */
  bool enableMessagesOverNotifyQueue_{true};

  // Flag to indicate to push to coro queue
  bool enableCoroNotifyQueue_{false};

  /**
   * Flag to enable backpressure and bounded queues in FiberBgpPeer.
   * When this flag is true, iqueue_ and sendQueue_'s bounded
   * counterparts are used to communicate between the client associated
   * with the FiberBgpPeer and the FiberBgpPeer's FiberSocket.
   *
   * These bounded queues have an explicit size limit that cannot be
   * exceeded. In order to enable this feature, the producer to a bounded
   * queue must be capable of handling the failure scenario where the queue
   * cannot accept more writes.
   *
   * The default value should NEVER be changed from false! Not all
   * users of this library are equipped to handle backpressure; only
   * users that can handle backpressure can enable this flag.
   */
  bool enableEgressQueueBackpressure_{false};

  /**
   * Flag to enable group PDU serialization for zero-copy distribution.
   * When enabled, group serializes BGP updates once and distributes
   * UpdateDescriptor to peers. I/O thread clones and mutates nexthop only.
   */
  bool enableSerializeGroupPdu_{false};

  // Advertise the RFC 8538 Notification (N) bit in the GR capability.
  bool enableGrNotification_{false};

  std::atomic<bool> isRestarting_{true};

#ifdef FiberBgpPeerManager_TEST_FRIENDS
  FiberBgpPeerManager_TEST_FRIENDS
#endif
};

/**
 * This class is for clients, which uses FiberBgpPeerManager, to observe
 * peer up/down events and receipt of Bgp updates and EoRs.
 */
struct BgpPeerManagerEventObserver {
  FiberBgpPeerCallback* callback;

  explicit BgpPeerManagerEventObserver(FiberBgpPeerCallback* callback)
      : callback(callback) {}

  void operator()(FiberBgpPeer::ObservableStateT const& stateEvt) {
    const auto& peerId = stateEvt.peerId;
    const auto& newState = stateEvt.state;

    if (newState == BgpSessionState::ESTABLISHED) {
      callback->sessionEstablished(peerId);
      return;
    }

    // We are notified only for session up/down events.
    callback->sessionTerminated(peerId);
    return;
  }

  void operator()(FiberBgpPeer::ObservableMessageT const& msgEvt) {
    const auto& peerId = msgEvt.peerId;
    const auto& newMsg = msgEvt.message;

    folly::variant_match(
        newMsg,
        [this, &peerId](
            const std::shared_ptr<const facebook::nettools::bgplib::BgpUpdate2>&
                update) { callback->bgpUpdatesReceived(peerId, *update); },
        [this, &peerId](const facebook::nettools::bgplib::BgpEndOfRib& eor) {
          callback->bgpEndOfRibReceived(peerId, eor);
        },
        [this, &peerId](const facebook::nettools::bgplib::BgpRouteRefresh& rr) {
          callback->bgpRouteRefreshReceived(peerId, rr);
        });
  }
};

// Allow std::ostream to print out the error code in text
std::ostream& operator<<(std::ostream& os, FiberBgpPeerManager::ErrorCode ec);

} // namespace bgplib
} // namespace nettools
} // namespace facebook
