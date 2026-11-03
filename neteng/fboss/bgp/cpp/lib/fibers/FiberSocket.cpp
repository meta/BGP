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

#include "neteng/fboss/bgp/cpp/lib/fibers/FiberSocket.h"

#include <folly/ExceptionWrapper.h>
#include <folly/fibers/FiberManagerMap.h>
#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/cpp/lib/fibers/Utils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

using folly::fibers::await;
using folly::fibers::Promise;

namespace {

/*
 * Wrapper class to handle the connect callback
 */
class ConnectCallback : public folly::AsyncSocket::ConnectCallback {
 public:
  explicit ConnectCallback(std::shared_ptr<folly::AsyncSocket> socket)
      : socket_(std::move(socket)) {}
  ~ConnectCallback() override = default;

  void setPromise(Promise<void>&& promise) noexcept {
    promise_ = std::move(promise);
  }

 private:
  // we need the socket to cancel connecting on errors
  const std::shared_ptr<folly::AsyncSocket> socket_;

  // this promise is used to pass the result to the caller
  std::optional<Promise<void>> promise_;

  /*
   * ConnectCallbcack implementation
   */
  void connectSuccess() noexcept override {
    XLOG(DBG5, "connectSuccess()");
    socket_->cancelConnect();
    promise_->setValue();
  }

  void connectErr(const folly::AsyncSocketException& ex) noexcept override {
    XLOG(DBG5, "connectErr()");
    socket_->cancelConnect();
    promise_->setException(folly::exception_wrapper(ex));
  }
};

/*
 * Wrapper class to handle the read callbacks
 */
class ReadCallback : public folly::AsyncSocket::ReadCallback,
                     public folly::AsyncTimeout {
 public:
  /*
   * we need to pass the socket into ReadCallback so we can clear the callback
   * pointer in the socket, thus preventing multiple callbacks from happening
   * in one run of event loop. This may happen, for example, when one fiber
   * writes and immediately closes the socket - this would cause the async
   * socket to call readDataAvailable and readEOF in sequence, causing the
   * promise to be fulfilled twice (oops!)
   */
  ReadCallback(
      std::shared_ptr<folly::AsyncSocket> socket,
      folly::IOBuf* buf,
      uint64_t maxLen,
      std::chrono::milliseconds readTimeout)
      : AsyncTimeout(socket->getEventBase()),
        socket_{socket},
        buf_{buf},
        maxLen_{maxLen} {
    if (readTimeout.count() > 0) {
      scheduleTimeout(readTimeout.count());
    }
  }
  ~ReadCallback() override = default;

  void setPromise(Promise<size_t>&& promise) noexcept {
    promise_ = std::move(promise);
  }

 private:
  // we need the socket to be able to manipulate callback
  const std::shared_ptr<folly::AsyncSocket> socket_;

  // pass the result back to the caller via this promise
  std::optional<Promise<size_t>> promise_;

  // the read buffer we store to hand off to callback - obtained from user
  folly::IOBuf* const buf_;

  /*
   * how much to consume in a single read. We cannot use IOBuf's capacity
   * since it might be slightly higher than requested value.
   */
  const uint64_t maxLen_{0};

  /*
   * ReadCallback methods
   */

  /*
   * this is called right before readDataAvailable(), always
   * in the same sequence
   */
  void getReadBuffer(void** buf, size_t* len) override {
    XLOGF(DBG5, "getReadBuffer, allowing len: {}", buf_->capacity());
    *buf = buf_->writableData();
    *len = maxLen_;
  }

  // once we get actual data, uninstall callback and clear timeout
  void readDataAvailable(size_t len) noexcept override {
    XLOGF(DBG5, "readDataAvailable: {} bytes", len);
    // disable callbacks
    socket_->setReadCB(nullptr);
    cancelTimeout();
    promise_->setValue(len);
  }

  void readEOF() noexcept override {
    XLOG(DBG5, "readEOF()");
    // disable callbacks
    socket_->setReadCB(nullptr);
    cancelTimeout();
    promise_->setValue(0);
  }

  void readErr(const folly::AsyncSocketException& ex) noexcept override {
    XLOG(DBG5, "readErr()");
    // disable callbacks
    socket_->setReadCB(nullptr);
    cancelTimeout();
    promise_->setException(ex);
  }

  /*
   * AsyncTimeout method
   */
  void timeoutExpired() noexcept override {
    XLOG(DBG5, "timeout expired");

    // uninstall read callback. it takes another read to bring it back.
    socket_->setReadCB(nullptr);
    promise_->setException(
        folly::AsyncSocketException(
            folly::AsyncSocketException::AsyncSocketExceptionType::TIMED_OUT,
            "Timed out waiting for data",
            errno));
  }
};

/*
 * Wrapper class to handle the write callbacks
 */
class WriteCallback : public folly::AsyncSocket::WriteCallback {
 public:
  WriteCallback() = default;
  ~WriteCallback() override = default;

  void setPromise(Promise<void>&& promise) noexcept {
    promise_ = std::move(promise);
  }

 private:
  // the promise to pass result back to the caller
  std::optional<Promise<void>> promise_;

  /*
   * Methods of WriteCallback
   */
  void writeSuccess() noexcept override {
    XLOG(DBG5, "writeSuccess");
    promise_->setValue();
  }

  /*
   * TODO: right now we do not differentiate partial failures - e.g.
   * when some bytes were sent and some not. This is pretty rare,
   * but may happen. Right now we would throw exception on any error,
   * even if some data made it through
   */
  void writeErr(
      size_t bytesWritten,
      const folly::AsyncSocketException& ex) noexcept override {
    XLOGF(DBG5, "writeErr, wrote {} bytes", bytesWritten);
    promise_->setException(ex);
  }
};

} // namespace

namespace facebook {
namespace nettools {
namespace bgplib {

/*
 * FiberSocketBufferCallback implementation
 */
void FiberSocketBufferCallback::onEgressBuffered() {
  totalBufferedEvents_++;
  lastBufferedTimeMs_ = getCurrentTimeMs();
  if (!peerIdOdsStr_.empty()) {
    facebook::bgp::PeerStats::incrSocketEgressBuffered(peerIdOdsStr_);
  }
}

void FiberSocketBufferCallback::setPeerIdOdsStr(std::string_view peerIdOdsStr) {
  peerIdOdsStr_ = peerIdOdsStr;
}

void FiberSocketBufferCallback::onEgressBufferCleared() {}

uint32_t FiberSocketBufferCallback::getTotalBufferedEvents() const {
  return totalBufferedEvents_;
}

uint64_t FiberSocketBufferCallback::getLastBufferedTimeMs() const {
  return lastBufferedTimeMs_;
}

/*
 * Public API
 */
FiberSocket::FiberSocket()
    : socket_{folly::AsyncSocket::newSocket(getFiberEventBase())},
      closed_(true) {}

FiberSocket::FiberSocket(std::shared_ptr<folly::AsyncSocket> socket)
    : FiberSocket(std::move(socket), getFiberEventBase()) {}

FiberSocket::FiberSocket(
    std::shared_ptr<folly::AsyncSocket> socket,
    folly::EventBase* evb)
    : socket_{std::move(socket)} {
  CHECK(socket_);
  CHECK_EQ(evb, socket_->getEventBase())
      << "Attempt to construct FiberSocket from AsyncSocket created in "
         "different event loop";
  CHECK(socket_->good());

  try {
    // save the socket's peer address
    socket_->getPeerAddress(&peerAddress_);
    // ... and our own address
    socket_->getLocalAddress(&localAddress_);
  } catch (const std::exception& ex) {
    throw folly::AsyncSocketException(
        folly::AsyncSocketException::INTERNAL_ERROR,
        std::string("Failed to get socket addresses: ") + ex.what(),
        errno);
  }
}

FiberSocket::~FiberSocket() {
  // Clear the buffer callback to avoid dangling pointer
  if (socket_) {
    socket_->setBufferCallback(nullptr);
  }
}

// static
folly::Expected<FiberSocket, FiberSocketError> FiberSocket::makeConnectedSocket(
    const folly::SocketAddress& destAddr,
    std::chrono::milliseconds connectTimeout,
    const folly::SocketAddress& bindAddr,
    std::optional<RQueue<FiberSocketInputMessageT>> iqueue,
    bool disableTSocks,
    std::shared_ptr<folly::SSLContext> ctx) {
  // get the event base driving fibers on the current thread
  auto evb = getFiberEventBase();

  // create socket, but do not ask to connect (no addr specified)
  auto socket = ctx ? folly::AsyncSSLSocket::newSocket(std::move(ctx), evb)
                    : folly::to_shared_ptr(folly::AsyncSocket::newSocket(evb));

  // disable read callbacks for now
  socket->setReadCB(nullptr);

  if (disableTSocks) {
    socket->disableTSocks();
  }

  // start a fiber listening on the input queue
  if (iqueue) {
    folly::fibers::getFiberManager(*evb).addTask(
        [iqueue = std::move(iqueue), socket, destAddr]() mutable {
          auto msg = iqueue->get();
          if (!msg) {
            XLOGF(DBG2, "closing socket to {}", destAddr.getAddressStr());
            socket->cancelConnect();
          }
        }); // addTask
  }

  /*
   * Unlike the await in FiberSocket::connect(), this one needs no
   * in-progress tracking by the caller. This is a static factory: the
   * FiberSocket does not exist until it is constructed from `socket` below, so
   * nothing can replace or destroy it while the fiber is suspended here, and
   * the code after the await touches only locals.
   */
  ConnectCallback cb(socket);
  try {
    await([&cb, &socket, destAddr, bindAddr, connectTimeout](
              Promise<void> promise) mutable {
      cb.setPromise(std::move(promise));
      socket->connect(
          &cb,
          destAddr,
          connectTimeout.count(),
          // TODO: add support for socket options on connect
          folly::emptySocketOptionMap,
          bindAddr);
    });
  } catch (folly::AsyncSocketException const& ex) {
    // abort due to null input message
    if (ex.getType() ==
        folly::AsyncSocketException::AsyncSocketExceptionType::END_OF_FILE) {
      return folly::makeUnexpected<FiberSocketError>(FiberGenericSocketError{
          FiberGenericSocketErrorType::CONNECT_STOPPED, "connect() stopped"});
    }
    return folly::makeUnexpected<FiberSocketError>(ex);
  }
  return FiberSocket(std::move(socket));
}

folly::Expected<folly::Unit, FiberSocketError> FiberSocket::connect(
    const folly::SocketAddress& destAddr,
    std::chrono::milliseconds connectTimeout,
    const folly::SocketAddress& bindAddr,
    const FiberOptionMap& options) {
  if (!closed_) {
    return folly::makeUnexpected<FiberSocketError>(FiberGenericSocketError{
        FiberGenericSocketErrorType::CONNECT_ALREADY,
        "connect() called on socket already connected"});
  }

  /*
   * The await() below suspends the fiber, and the code after it dereferences
   * members of `this`. Owners must therefore not replace or destroy this
   * socket until connect() returns; FiberBgpPeerManager tracks that with
   * BgpPeerActiveConnectInfo::connectInProgress.
   */

  // disable read callbacks for now
  socket_->setReadCB(nullptr);

  ConnectCallback cb(socket_);
  try {
    await([&cb, socket = socket_, destAddr, bindAddr, connectTimeout, options](
              Promise<void> promise) mutable {
      cb.setPromise(std::move(promise));
      socket->connect(&cb, destAddr, connectTimeout.count(), options, bindAddr);
    });
    // save the socket's peer address
    socket_->getPeerAddress(&peerAddress_);
    // ... and our own address
    socket_->getLocalAddress(&localAddress_);
    closed_ = false;
    return folly::Unit();
  } catch (folly::AsyncSocketException const& ex) {
    return folly::makeUnexpected<FiberSocketError>(ex);
  }
}

/*
 * IO methods. Notice all of those are invoked inside event loop, so
 * we can safely modify all other objects accessed inside the same loop
 */
folly::Expected<std::unique_ptr<folly::IOBuf>, FiberSocketError>
FiberSocket::read(
    uint64_t maxSize,
    std::chrono::milliseconds readTimeout) noexcept {
  if (closed_) {
    return folly::makeUnexpected<FiberSocketError>(folly::AsyncSocketException{
        folly::AsyncSocketException::AsyncSocketExceptionType::NOT_OPEN,
        "Attempted to read from closed fiber socket",
        0 /* errno */});
  }

  auto buf = folly::IOBuf::createCombined(maxSize);

  XLOGF(DBG5, "FiberSocket::read(), expecting max len {}", maxSize);

  ReadCallback cb{socket_, buf.get(), maxSize, readTimeout};
  try {
    auto bytesRead = await([this, &cb](Promise<size_t> promise) {
      cb.setPromise(std::move(promise));
      socket_->setReadCB(&cb);
    });

    if (bytesRead > 0) {
      buf->append(bytesRead);
    } else {
      closed_ = true;
    }

    return std::move(buf);
  } catch (folly::AsyncSocketException const& ex) {
    closed_ = true;
    return folly::makeUnexpected<FiberSocketError>(ex);
  }
}

folly::Expected<size_t, FiberSocketError> FiberSocket::write(
    std::unique_ptr<folly::IOBuf> buf) noexcept {
  if (closed_) {
    return folly::makeUnexpected<FiberSocketError>(folly::AsyncSocketException{
        folly::AsyncSocketException::AsyncSocketExceptionType::NOT_OPEN,
        "Attempted to write to closed fiber socket",
        0 /* errno */});
  }

  // writeChain is expect to write all buffers using iovec
  auto length = buf->computeChainDataLength();
  XLOGF(DBG5, "FiberSocket::write(), len {}", length);

  // TODO: add code to handle write without installing callback first

  WriteCallback cb;
  try {
    await([this, &cb, buf = std::move(buf)](Promise<void> promise) mutable {
      cb.setPromise(std::move(promise));
      socket_->writeChain(&cb, std::move(buf));
    });
  } catch (folly::AsyncSocketException const& ex) {
    closed_ = true;
    return folly::makeUnexpected<FiberSocketError>(ex);
  }
  return length;
}

void FiberSocket::close() noexcept {
  if (closed_) {
    return;
  }
  /*
   * socket_ might be null if this object has
   * been moved from
   */
  if (socket_) {
    socket_->close();
  }
  closed_ = true;
}

void FiberSocket::closeWithReset() noexcept {
  if (closed_) {
    return;
  }
  /*
   * socket_ might be null if this object has
   * been moved from
   */
  if (socket_) {
    socket_->closeWithReset();
  }
  closed_ = true;
}

void FiberSocket::shutdownWrite() noexcept {
  if (closed_) {
    return;
  }
  if (socket_) {
    socket_->shutdownWrite();
  }
  // we do not mark socket as closed
}

int FiberSocket::getFd() const noexcept {
  if (socket_) {
    return socket_->getNetworkSocket().toFd();
  }
  return -1;
}

folly::SocketAddress FiberSocket::getLocalAddress() const noexcept {
  return localAddress_;
}

folly::SocketAddress FiberSocket::getPeerAddress() const noexcept {
  return peerAddress_;
}

void FiberSocket::setBufferCallback(std::string_view peerIdOdsStr) noexcept {
  bufferCallback_.setPeerIdOdsStr(peerIdOdsStr);
  if (socket_) {
    socket_->setBufferCallback(&bufferCallback_);
  }
}

uint32_t FiberSocket::getTotalAsyncSocketBufferedEvents() const noexcept {
  return bufferCallback_.getTotalBufferedEvents();
}

uint64_t FiberSocket::getLastSocketBufferedTimeMs() const noexcept {
  return bufferCallback_.getLastBufferedTimeMs();
}
} // namespace bgplib
} // namespace nettools
} // namespace facebook
