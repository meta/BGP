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

#include <string_view>

#include <folly/Expected.h>
#include <folly/SocketAddress.h>
#include <folly/String.h>
#include <folly/fibers/FiberManager.h>
#include <folly/io/IOBuf.h>
#include <folly/io/SocketOptionMap.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/AsyncSocket.h>

#include "neteng/fboss/bgp/cpp/lib/fibers/Queue.h"

namespace facebook {
namespace nettools {
namespace bgplib {

enum class FiberGenericSocketErrorType : uint8_t {
  UNKNOWN = 255,
  ACCEPT_STOPPED = 1,
  CONNECT_STOPPED = 2,
  CONNECT_ALREADY = 3
};

/*
 * Wrap socket errors occur in FiberSocket, FiberServerSocket, and
 * FiberUDPSocket layer
 */
struct FiberGenericSocketError {
  explicit FiberGenericSocketError(std::string const& msg) : msg_(msg) {
    type_ = FiberGenericSocketErrorType::UNKNOWN;
  }
  FiberGenericSocketError(
      FiberGenericSocketErrorType type,
      std::string const& msg)
      : type_(type), msg_(msg) {}
  FiberGenericSocketErrorType type_;
  std::string msg_;
};

using FiberSocketError =
    std::variant<folly::AsyncSocketException, FiberGenericSocketError>;

struct FiberSocketErrorVisitor {
  std::string operator()(folly::AsyncSocketException const& error) {
    return error.what();
  }

  std::string operator()(FiberGenericSocketError const& error) {
    return error.msg_;
  }
};

struct FiberSocketInputMessageT {};

/*
 * Wrapper on AsyncSocket Option map
 */
using FiberOptionKey = folly::SocketOptionKey;
using FiberOptionMap = folly::SocketOptionMap;

/*
 * BufferCallback to track AsyncSocket buffering events
 */
class FiberSocketBufferCallback : public folly::AsyncTransport::BufferCallback {
 public:
  FiberSocketBufferCallback() = default;

  void onEgressBuffered() override;
  void onEgressBufferCleared() override;

  uint32_t getTotalBufferedEvents() const;
  uint64_t getLastBufferedTimeMs() const;

  void setPeerIdOdsStr(std::string_view peerIdOdsStr);

 private:
  uint32_t totalBufferedEvents_{0};
  uint64_t lastBufferedTimeMs_{0};
  /*
   * Per-peer ODS counter key component; empty means no per-peer counter.
   * Non-owning: the referenced string must outlive the socket.
   */
  std::string_view peerIdOdsStr_;
};

/*
 * Wraps AsyncSocket and provides basic read/write/close operations
 */
class FiberSocket {
 public:
  // movable
  FiberSocket(FiberSocket&&) = default;
  FiberSocket& operator=(FiberSocket&&) = default;

  FiberSocket();
  explicit FiberSocket(std::shared_ptr<folly::AsyncSocket> socket);
  FiberSocket(
      std::shared_ptr<folly::AsyncSocket> socket,
      folly::EventBase* evb);

  /*
   * Factory method to produce FiberSocket by connecting
   * to the remote side
   */
  static folly::Expected<FiberSocket, FiberSocketError> makeConnectedSocket(
      const folly::SocketAddress& destAddr,
      std::chrono::milliseconds connectTimeout = std::chrono::milliseconds(0),
      const folly::SocketAddress& bindAddr = folly::AsyncSocket::anyAddress(),
      std::optional<RQueue<FiberSocketInputMessageT>> iqueue = std::nullopt,
      bool disableTSocks = false,
      std::shared_ptr<folly::SSLContext> ctx = nullptr);

  /*
   * notice that we do not close the socket on destruction.
   * we assume that this has to be done by the caller
   * who wrapped AsyncSocket in FiberSocket initially
   */
  ~FiberSocket();

  // connect socket to remote address
  folly::Expected<folly::Unit, FiberSocketError> connect(
      const folly::SocketAddress& destAddr,
      std::chrono::milliseconds connectTimeout = std::chrono::milliseconds(0),
      const folly::SocketAddress& bindAddr = folly::AsyncSocket::anyAddress(),
      const FiberOptionMap& options = folly::emptySocketOptionMap);

  /*
   * read data and return new buffer or error. Notice that we sacrifice
   * performance on purpose: we allocate new buffer on every read and
   * pass it to the user. This streamlines the processing, and allows
   * the logic to be easily "chained"
   */
  folly::Expected<std::unique_ptr<folly::IOBuf>, FiberSocketError> read(
      uint64_t maxSize,
      std::chrono::milliseconds timeout =
          std::chrono::milliseconds(0)) noexcept;

  /*
   * write the supplied buffer; returns the size written on
   * success, and actual bytes written on failure. It's up to
   * the caller to retry write on failure
   */
  folly::Expected<size_t, FiberSocketError> write(
      std::unique_ptr<folly::IOBuf> buf) noexcept;

  /*
   * FiberServerSocket may produce FiberSockets without exposing the
   * underlying AsyncSocket. This method is a convenience handler to
   * close such socket.
   */
  void close() noexcept;

  /*
   * This will close local end without lingering on any unsent data
   */
  void closeWithReset() noexcept;

  /*
   * Shutdown the write side of the socket
   */
  void shutdownWrite() noexcept;

  /*
   * Get the underlying FD
   */
  int getFd() const noexcept;

  /**
   * Generic API for reading a socket option.
   *
   * @param level     same as the "level" parameter in getsockopt().
   * @param optname   same as the "optname" parameter in getsockopt().
   * @param optval    pointer to the variable in which the option value should
   *                  be returned.
   * @param optlen    value-result argument, initially containing the size of
   *                  the buffer pointed to by optval, and modified on return
   *                  to indicate the actual size of the value returned.
   * @return          same as the return value of getsockopt().
   */
  template <typename T>
  int getSockOpt(int level, int optname, T* optval, socklen_t* optlen) {
    return socket_->getSockOpt(level, optname, (void*)optval, optlen);
  }

  /**
   * Generic API for setting a socket option.
   *
   * @param level     same as the "level" parameter in getsockopt().
   * @param optname   same as the "optname" parameter in getsockopt().
   * @param optval    the option value to set.
   * @return          same as the return value of setsockopt().
   */
  template <typename T>
  int setSockOpt(int level, int optname, const T* optval) {
    return socket_->setSockOpt(level, optname, optval);
  }

  // return our's and peer's address
  folly::SocketAddress getLocalAddress() const noexcept;
  folly::SocketAddress getPeerAddress() const noexcept;

  void setBufferCallback(std::string_view peerIdOdsStr = {}) noexcept;

  // get buffer statistics
  uint32_t getTotalAsyncSocketBufferedEvents() const noexcept;
  uint64_t getLastSocketBufferedTimeMs() const noexcept;

  inline bool readable() const {
    return socket_->readable();
  }

 private:
  // non-copyable
  FiberSocket(const FiberSocket&) = delete;
  FiberSocket& operator=(const FiberSocket&) = delete;

  /*
   * Class invariants
   */

  // Async socket we are working with
  const std::shared_ptr<folly::AsyncSocket> socket_;

  /*
   * address information for the socket: local + remote
   * both should be set since we are being passed a
   * connected socket. No disconnected bullshit.
   */
  folly::SocketAddress localAddress_;
  folly::SocketAddress peerAddress_;

  // set to true if socket hit an error, e.g. EOF or RST
  bool closed_{false};

  // BufferCallback to track AsyncSocket buffering events
  FiberSocketBufferCallback bufferCallback_;
};

} // namespace bgplib
} // namespace nettools
} // namespace facebook
