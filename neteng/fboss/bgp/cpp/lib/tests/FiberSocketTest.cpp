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

#include <folly/Overload.h>
#include <folly/Random.h>
#include <folly/fibers/EventBaseLoopController.h>
#include <folly/io/Cursor.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/test/MockAsyncSocket.h>
#include <folly/io/async/test/ScopedBoundPort.h>
#include <folly/io/async/test/SocketPair.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

#include "neteng/fboss/bgp/cpp/lib/fibers/FiberServerSocket.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberSocket.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/Utils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

using namespace facebook::nettools::bgplib;
using namespace folly::fibers;
using namespace std::chrono_literals;

namespace {
// static so we don't need to capture in lambda

// the block size to use for transfers
static constexpr auto kBlockSize = 1024;
static constexpr auto kLargeBlockSize = 1024 * 1024;

static constexpr auto kNumClients = 128;
} // namespace

/*
 * The fixture provides fiber manager and evb for the tests
 */
class FiberSocketFixture : public ::testing::Test {
 public:
  FiberSocketFixture() = default;
  ~FiberSocketFixture() override = default;

  void SetUp() override {
    manager = std::make_unique<FiberManager>(
        std::make_unique<EventBaseLoopController>());

    static_cast<EventBaseLoopController&>(manager->loopController())
        .attachEventBase(evb);
  }

  void TearDown() override {}

  std::unique_ptr<FiberManager> manager;
  folly::EventBase evb;
};

/*
 * Use socket pair to create socket connection, test single read/write
 */
TEST_F(FiberSocketFixture, ReadWritePair) {
  // grab the socket pair
  folly::SocketPair sp;
  auto fd0 = sp.extractFD0();
  auto fd1 = sp.extractFD1();

  auto as0 = folly::to_shared_ptr(
      folly::AsyncSocket::newSocket(&evb, folly::NetworkSocket::fromFd(fd0)));
  auto fs0 = FiberSocket(as0, &evb);
  manager->addTask([fs0 = std::move(fs0)]() mutable {
    std::array<uint8_t, kBlockSize> data{{'a'}};
    uint32_t offset{0};
    while (offset < kBlockSize) {
      auto result = fs0.write(
          folly::IOBuf::wrapBuffer(
              data.begin() + offset, data.size() - offset));
      ASSERT_TRUE(result.hasValue());
      EXPECT_EQ(kBlockSize, result.value());
      offset += *result;
    }
    fs0.close();
  });

  auto as1 = folly::to_shared_ptr(
      folly::AsyncSocket::newSocket(&evb, folly::NetworkSocket::fromFd(fd1)));
  manager->addTask([as1 = std::move(as1)]() mutable {
    auto fs1 = FiberSocket(as1);
    uint64_t bytesRead = 0;
    // we are going to append returned IOBufs to this chain
    folly::IOBuf buf;
    while (bytesRead < kBlockSize) {
      // result is folly expected
      auto result = fs1.read(kBlockSize).then([&buf, &bytesRead](auto bufRead) {
        bytesRead += bufRead->length();
        buf.appendChain(std::move(bufRead));
      });

      if (result.hasError()) {
        auto error = result.error();
        FiberSocketErrorVisitor errProcessor;
        auto errorStr = std::visit(errProcessor, error);
        XLOGF(ERR, "{}", errorStr);
      }
      ASSERT_TRUE(result.hasValue());
    }
    // assemble all buffers in contiguous block
    buf.coalesce();
    std::array<uint8_t, kBlockSize> expected{{'a'}};
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), buf.data()));
    fs1.close();
  });

  evb.loop();
}

/*
 * Test BufferCallback initial state
 */
TEST_F(FiberSocketFixture, BufferCallbackInitialState) {
  manager->addTask([this]() mutable {
    /* Create a socket pair */
    folly::SocketPair sp;
    auto fd0 = sp.extractFD0();

    auto as0 = folly::to_shared_ptr(
        folly::AsyncSocket::newSocket(&evb, folly::NetworkSocket::fromFd(fd0)));
    auto fs0 = FiberSocket(as0, &evb);
    fs0.setBufferCallback();

    /* Initially, buffer stats should be zero */
    EXPECT_EQ(0, fs0.getTotalAsyncSocketBufferedEvents());
    EXPECT_EQ(0, fs0.getLastSocketBufferedTimeMs());

    fs0.close();
  });

  evb.loop();
}

/*
 * Test BufferCallback is properly set and cleared
 */
TEST_F(FiberSocketFixture, BufferCallbackLifecycle) {
  manager->addTask([this]() mutable {
    folly::SocketPair sp;
    auto fd0 = sp.extractFD0();
    auto fd1 = sp.extractFD1();

    auto as0 = folly::to_shared_ptr(
        folly::AsyncSocket::newSocket(&evb, folly::NetworkSocket::fromFd(fd0)));
    auto as1 = folly::to_shared_ptr(
        folly::AsyncSocket::newSocket(&evb, folly::NetworkSocket::fromFd(fd1)));

    int bufSize = 1024;
    as0->setSockOpt(SOL_SOCKET, SO_SNDBUF, &bufSize);

    auto fs0 = std::make_shared<FiberSocket>(as0, &evb);
    fs0->setBufferCallback();

    /* Verify initial state */
    EXPECT_EQ(0, fs0->getTotalAsyncSocketBufferedEvents());
    EXPECT_EQ(0, fs0->getLastSocketBufferedTimeMs());

    /* Fill the send buffer by writing synchronously before destroying
     * FiberSocket. Don't read from the other end, so the buffer fills up
     * completely. */
    std::vector<uint8_t> largeData(kLargeBlockSize, 'x');
    for (int i = 0; i < 10; i++) {
      as0->write(nullptr, largeData.data(), largeData.size());
    }

    fs0.reset();

    /* After FiberSocket, trigger onEgressBuffered() callback.
     * If the callback wasn't cleared, this will crash with dangling pointer. */
    for (int i = 0; i < 10; i++) {
      as0->write(nullptr, largeData.data(), largeData.size());
    }
    SUCCEED();
  });
  evb.loop();
}

/*
 * Test BufferCallback correctly tracks buffering events with drain cycle.
 * Uses FiberSocket::write() which triggers partial writes internally.
 */
TEST_F(FiberSocketFixture, BufferCallbackTracksEvents) {
  manager->addTask([this]() mutable {
    folly::SocketPair sp;
    auto fd0 = sp.extractFD0();
    auto fd1 = sp.extractFD1();

    auto as0 = folly::to_shared_ptr(
        folly::AsyncSocket::newSocket(&evb, folly::NetworkSocket::fromFd(fd0)));
    auto as1 = folly::to_shared_ptr(
        folly::AsyncSocket::newSocket(&evb, folly::NetworkSocket::fromFd(fd1)));

    /* Set very small send and receive buffer sizes to force partial writes */
    int bufSize = 64;
    as0->setSockOpt(SOL_SOCKET, SO_SNDBUF, &bufSize);
    as1->setSockOpt(SOL_SOCKET, SO_RCVBUF, &bufSize);

    /* Record time before triggering buffering */
    auto startTime = getCurrentTimeMs();

    auto fs0 = std::make_shared<FiberSocket>(as0, &evb);
    auto fs1 = std::make_shared<FiberSocket>(as1, &evb);

    const std::string peerIdOdsStr = "fiber-socket-test-peer";
    fs0->setBufferCallback(peerIdOdsStr);

    /* Start writer fiber that will block on FiberSocket::write().
     * With a 64-byte buffer and 1MB write, this will cause partial writes
     * internally, triggering onEgressBuffered() callback. */
    auto writeFuture = manager->addTaskFuture([fs0]() mutable {
      auto result = fs0->write(
          folly::IOBuf::copyBuffer(std::string(kLargeBlockSize, 'x')));
      return result.hasValue() ? result.value() : 0;
    });

    /* Start reader fiber to drain the socket, allowing writes to complete */
    auto readFuture = manager->addTaskFuture([fs1]() mutable {
      size_t totalRead = 0;
      folly::IOBuf receivedData;
      while (totalRead < kLargeBlockSize) {
        auto result = fs1->read(kBlockSize);
        if (result.hasError()) {
          break;
        }
        totalRead += result.value()->length();
        receivedData.appendChain(std::move(result.value()));
      }
      return std::make_pair(totalRead, std::move(receivedData));
    });

    /* Wait for both fibers to complete */
    auto totalWritten = std::move(writeFuture).get();
    auto [totalRead, receivedData] = std::move(readFuture).get();

    /* Verify buffering was triggered during the write */
    auto bufferedEvents = fs0->getTotalAsyncSocketBufferedEvents();
    ASSERT_GT(bufferedEvents, 0u);

    /* Verify the per-peer ODS counter matches the socket's own count */
    facebook::fb303::ThreadCachedServiceData::get()->publishStats();
    EXPECT_EQ(
        static_cast<int64_t>(bufferedEvents),
        facebook::fb303::ThreadCachedServiceData::get()->getCounter(
            fmt::format(
                facebook::bgp::PeerStats::kPeerSocketEgressBuffered,
                facebook::bgp::kEbbPlatform,
                facebook::bgp::kBgpcppTag,
                peerIdOdsStr)));

    /* Verify timestamp is reasonable */
    auto lastBufferedTime = fs0->getLastSocketBufferedTimeMs();
    EXPECT_GT(lastBufferedTime, 0u);
    EXPECT_GE(lastBufferedTime, startTime);

    /* Verify all data was written and read */
    EXPECT_EQ(kLargeBlockSize, totalWritten);
    EXPECT_EQ(kLargeBlockSize, totalRead);

    /* Verify the data content is correct - all 'x' characters */
    receivedData.coalesce();
    std::string expected(kLargeBlockSize, 'x');
    EXPECT_EQ(expected.size(), receivedData.length());
    EXPECT_TRUE(
        std::equal(expected.begin(), expected.end(), receivedData.data()));

    fs0->close();
    fs1->close();
  });

  evb.loop();
}

/*
 * The buffer callback holds a non-owning view of the peer ID. Destroying the
 * FiberSocket must detach the callback, so freeing the peer ID afterwards is
 * safe even though the underlying AsyncSocket keeps buffering writes. Under
 * ASAN, a dangling callback or peer ID view is reported as heap-use-after-free.
 */
TEST_F(FiberSocketFixture, BufferCallbackSafeAfterSocketAndPeerIdFreed) {
  manager->addTask([this]() mutable {
    folly::SocketPair sp;
    auto fd0 = sp.extractFD0();
    auto fd1 = sp.extractFD1();

    auto as0 = folly::to_shared_ptr(
        folly::AsyncSocket::newSocket(&evb, folly::NetworkSocket::fromFd(fd0)));
    auto as1 = folly::to_shared_ptr(
        folly::AsyncSocket::newSocket(&evb, folly::NetworkSocket::fromFd(fd1)));

    int bufSize = 64;
    as0->setSockOpt(SOL_SOCKET, SO_SNDBUF, &bufSize);
    as1->setSockOpt(SOL_SOCKET, SO_RCVBUF, &bufSize);

    /* Longer than the small-string buffer so the peer ID is heap allocated */
    const std::string peerIdCopy = "fiber-socket-freed-peer-id-heap-allocated";
    auto peerIdOdsStr = std::make_unique<std::string>(peerIdCopy);

    auto fs0 = std::make_unique<FiberSocket>(as0, &evb);
    fs0->setBufferCallback(*peerIdOdsStr);
    fs0.reset();
    peerIdOdsStr.reset();

    /* Drive partial writes on the same AsyncSocket after both are freed */
    auto writer = std::make_shared<FiberSocket>(as0, &evb);
    auto reader = std::make_shared<FiberSocket>(as1, &evb);

    auto writeFuture = manager->addTaskFuture([writer]() mutable {
      auto result = writer->write(
          folly::IOBuf::copyBuffer(std::string(kLargeBlockSize, 'x')));
      return result.hasValue() ? result.value() : 0;
    });

    auto readFuture = manager->addTaskFuture([reader]() mutable {
      size_t totalRead = 0;
      while (totalRead < kLargeBlockSize) {
        auto result = reader->read(kBlockSize);
        if (result.hasError()) {
          break;
        }
        totalRead += result.value()->length();
      }
      return totalRead;
    });

    EXPECT_EQ(kLargeBlockSize, std::move(writeFuture).get());
    EXPECT_EQ(kLargeBlockSize, std::move(readFuture).get());

    /* The detached callback never counted the writes for the freed peer */
    facebook::fb303::ThreadCachedServiceData::get()->publishStats();
    EXPECT_FALSE(
        facebook::fb303::ThreadCachedServiceData::get()->hasCounter(
            fmt::format(
                facebook::bgp::PeerStats::kPeerSocketEgressBuffered,
                facebook::bgp::kEbbPlatform,
                facebook::bgp::kBgpcppTag,
                peerIdCopy)));

    writer->close();
    reader->close();
  });

  evb.loop();
}

/*
 * Close socket while trying to read
 */
TEST_F(FiberSocketFixture, CloseWhileReading) {
  // grab the socket pair
  folly::SocketPair sp;
  auto fd1 = sp.extractFD1();

  manager->addTask([this, fd1]() mutable {
    auto as1 = folly::to_shared_ptr(
        folly::AsyncSocket::newSocket(&evb, folly::NetworkSocket::fromFd(fd1)));
    auto fs1 = std::make_shared<FiberSocket>(as1);
    manager->addTask([fs1]() mutable {
      /*
       * first callback installed before we close
       * result is folly expected
       */
      {
        auto result = fs1->read(kBlockSize).then([](auto /*bufRead*/) {});
        EXPECT_TRUE(result.hasValue());
      }

      // result is folly expected
      {
        auto result = fs1->read(kBlockSize).then([](auto /*bufRead*/) {});
        EXPECT_TRUE(result.hasError());
        auto err = std::get<folly::AsyncSocketException>(result.error());
        EXPECT_EQ(
            folly::AsyncSocketException::AsyncSocketExceptionType::NOT_OPEN,
            err.getType());
      }
    });

    manager->addTask([fs1]() mutable {
      fiberSleepFor(100ms);
      fs1->close();
    });
  });

  evb.loop();
}

/*
 * Create connection, test read timeout (one side reads, another never
 * writes)
 */
TEST_F(FiberSocketFixture, ReadTimeout) {
  // grab the socket pair
  folly::SocketPair sp;
  auto fd1 = sp.extractFD1();

  auto as1 = folly::to_shared_ptr(
      folly::AsyncSocket::newSocket(&evb, folly::NetworkSocket::fromFd(fd1)));
  manager->addTask([as1 = std::move(as1)]() mutable {
    auto fs1 = FiberSocket(as1);
    folly::IOBuf buf;
    auto result = fs1.read(kBlockSize, 300ms).then([&buf](auto newBuf) {
      buf.appendChain(std::move(newBuf));
    });
    ASSERT_FALSE(result.hasValue());
    auto error = std::get<folly::AsyncSocketException>(result.error());
    EXPECT_EQ(
        folly::AsyncSocketException::AsyncSocketExceptionType::TIMED_OUT,
        error.getType());
    fs1.close();
  });

  evb.loop();

  // the fd's will be closed by destructor
}

/*
 * Use a pair of sockets, send random blocks, use 4-byte
 * frame length to communicate this to the other side
 */
TEST_F(FiberSocketFixture, ReadWritePairRandom) {
  // grab the socket pair
  folly::SocketPair sp;
  auto fd0 = sp.extractFD0();
  auto fd1 = sp.extractFD1();

  auto as0 = folly::to_shared_ptr(
      folly::AsyncSocket::newSocket(&evb, folly::NetworkSocket::fromFd(fd0)));
  /*
   * notice that we capture socket by copy, so we can retain it for
   * the duration of both fiber lifetimes
   */
  manager->addTask([as0]() mutable {
    auto fs0 = FiberSocket(as0);
    // the array to send data
    std::vector<uint8_t> data;

    // send N rounds of data; send frame size first
    for (auto i = 0; i < 128; i++) {
      data.resize(sizeof(uint32_t) + folly::Random::rand32() % 1280000);
      std::fill(data.begin(), data.end(), i);

      uint32_t length = data.size();

      auto headerBuf = folly::IOBuf::wrapBuffer(data.data(), sizeof(uint32_t));
      // could have just jused htonl here but you know...
      folly::io::RWPrivateCursor headerCursor{headerBuf.get()};
      headerCursor.writeBE<uint32_t>(length);

      XLOGF(DBG4, "Round {} preparing to write {} bytes", i, length);

      auto result =
          fs0.write(folly::IOBuf::wrapBuffer(data.data(), data.size()));

      ASSERT_TRUE(result.hasValue());
      XLOGF(DBG4, "Round {} wrote {} bytes", i, result.value());
      EXPECT_EQ(data.size(), result.value());
    }
  });

  auto as1 = folly::to_shared_ptr(
      folly::AsyncSocket::newSocket(&evb, folly::NetworkSocket::fromFd(fd1)));
  manager->addTask([as1]() mutable {
    auto fs1 = FiberSocket(as1);

    for (auto i = 0; i < 128; i++) {
      // apend all data read to this buffer chain
      folly::IOBuf buf;

      // read and parse frame length
      {
        auto result = fs1.read(sizeof(uint32_t)).then([&buf](auto newBuf) {
          buf.appendChain(std::move(newBuf));
        });
        ASSERT_TRUE(result.hasValue());
      }

      auto length = folly::io::Cursor(&buf).readBE<uint32_t>();
      XLOGF(DBG4, "Received frame length {}", length);
      ASSERT_GT(length, sizeof(uint32_t));

      // read the frame
      auto bytesRead = sizeof(uint32_t);
      while (bytesRead < length) {
        auto result =
            fs1.read(length - bytesRead).then([&buf, &bytesRead](auto newBuf) {
              bytesRead += newBuf->length();
              XLOGF(DBG4, "Partial read of {} bytes", newBuf->length());
              buf.appendChain(std::move(newBuf));
            });
        ASSERT_TRUE(result.hasValue());
        XLOGF(DBG4, "Remaining bytes {}", length - bytesRead);
      }

      XLOGF(DBG4, "Read round {} completed with {} bytes", i, length);
      // make sure the data matches
      buf.coalesce();
      EXPECT_EQ(length, buf.length());
      std::vector<uint8_t> expected;
      expected.resize(length - sizeof(uint32_t));
      std::fill(expected.begin(), expected.end(), i);
      EXPECT_TRUE(std::equal(expected.begin(), expected.end(), buf.buffer()));
    }
  });

  evb.loop();
}

/*
 * Test socket closing; we send very small blocks here,
 * and do not handle partiar read/writes
 */
TEST_F(FiberSocketFixture, ReadWriteClose) {
  // grab the socket pair
  folly::SocketPair sp;
  auto fd0 = sp.extractFD0();
  auto fd1 = sp.extractFD1();

  auto as0 = folly::to_shared_ptr(
      folly::AsyncSocket::newSocket(&evb, folly::NetworkSocket::fromFd(fd0)));
  manager->addTask([as0]() mutable {
    auto fs0 = FiberSocket(as0);
    std::array<uint8_t, 16> data{{'a'}};
    auto result =
        fs0.write(folly::IOBuf::wrapBuffer(data.begin(), data.size()));
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(16, result.value());
    fs0.close();
  });

  auto as1 = folly::to_shared_ptr(
      folly::AsyncSocket::newSocket(&evb, folly::NetworkSocket::fromFd(fd1)));
  manager->addTask([as1]() mutable {
    auto fs1 = FiberSocket(as1);
    std::array<uint8_t, 16> expected{{'a'}};

    // read data
    {
      auto result = fs1.read(16);
      EXPECT_TRUE(result.hasValue());
      EXPECT_EQ(16, result.value()->length());
      EXPECT_TRUE(
          std::equal(expected.begin(), expected.end(), result.value()->data()));
    }

    // read EOF
    {
      auto result = fs1.read(16);
      // zero bytes read
      EXPECT_EQ(0, result.value()->length());
    }
  });

  evb.loop();
}

/*
 * Test connection failure.
 */
TEST_F(FiberSocketFixture, ConnectFail) {
  // spins new thread, binds port
  folly::ScopedBoundPort ph;

  auto closedAddress = ph.getAddress();
  // Add a task that tries connecting to a non-responding port
  manager->addTask([closedAddress]() mutable {
    auto socket = FiberSocket::makeConnectedSocket(closedAddress, 100ms);
    ASSERT_TRUE(socket.hasError());
    auto error = std::get<folly::AsyncSocketException>(socket.error());
    EXPECT_EQ(
        folly::AsyncSocketException::AsyncSocketExceptionType::NOT_OPEN,
        error.getType());
    EXPECT_EQ(ECONNREFUSED, error.getErrno());
  });

  evb.loop();
}

/*
 * Close socket on client causing reset
 */
TEST_F(FiberSocketFixture, CloseWithReset) {
  manager->addTask([]() mutable {
    FiberServerSocket serverSocket(std::optional<folly::SocketAddress>(), 256);

    auto serverAddress = serverSocket.getListenAddress();
    XLOGF(
        DBG4,
        "Running acceptor on [{}]:{}",
        serverAddress.getAddressStr(),
        serverAddress.getPort());

    // accept connection, read smaller chunks, expect reset
    addTask([serverSocket = std::move(serverSocket)]() mutable {
      SCOPE_EXIT {
        serverSocket.close();
      };
      serverSocket.accept().then([](auto socket) {
        while (true) {
          auto ret = socket.read(256);
          if (ret.hasError()) {
            auto err = ret.error();
            folly::variant_match(
                err,
                [](FiberGenericSocketError const&) { FAIL(); },
                [](folly::AsyncSocketException const& errVal) {
                  EXPECT_EQ(ECONNRESET, errVal.getErrno());
                });
            break;
          }
        } // while
      }); // then
    }); // addTask

    // connect, write larger block, close with reset
    addTask([serverAddress]() mutable {
      FiberSocket::makeConnectedSocket(serverAddress, 1s)
          .then([serverAddress](auto sock) {
            SCOPE_EXIT {
              sock.close();
            };
            char buf[1024];
            sock.write(folly::IOBuf::wrapBuffer(&buf[0], sizeof(buf)));
            sock.closeWithReset();
          });
    });
  });

  evb.loop();
}

/*
 * Close connection using half-duplex shutdown
 */
TEST_F(FiberSocketFixture, ShutdownWrite) {
  manager->addTask([]() mutable {
    FiberServerSocket serverSocket(std::optional<folly::SocketAddress>(), 256);

    auto serverAddress = serverSocket.getListenAddress();
    XLOGF(
        DBG4,
        "Running acceptor on [{}]:{}",
        serverAddress.getAddressStr(),
        serverAddress.getPort());

    // accept connection, read smaller chunks, expect reset
    addTask([serverSocket = std::move(serverSocket)]() mutable {
      SCOPE_EXIT {
        serverSocket.close();
      };
      serverSocket.accept().then([](auto socket) {
        while (true) {
          auto ret = socket.read(256);
          ASSERT_FALSE(ret.hasError());
          if (ret.value()->length() == 0) {
            socket.close();
            break;
          }
        } // while
      }); // then
    }); // addTask

    // connect, write larger block, close with reset
    addTask([serverAddress]() mutable {
      FiberSocket::makeConnectedSocket(serverAddress, 1s)
          .then([serverAddress](auto sock) {
            SCOPE_EXIT {
              sock.close();
            };
            char buf[1024];
            sock.write(folly::IOBuf::wrapBuffer(&buf[0], sizeof(buf)));
            sock.shutdownWrite();
            auto ret = sock.read(1024);
            ASSERT_TRUE(ret.hasValue());
            EXPECT_EQ(0, ret.value()->length());
          });
    });
  });

  evb.loop();
}

/*
 * Server-client exchange, single thread, multiple fibers.
 * We run one fiber with acceptor - on each accept we
 * create another fiber that writes into the connection.
 *
 * We also create equal number of fibers for clients using factory methods:
 * makeConnectedSocket() - those read the data published by the server fibers.
 */
TEST_F(FiberSocketFixture, ClientServerSingleThread) {
  /*
   * create wrapper task that will kick in more tasks afterwards
   * this is mainly needed because server sockets needs to be
   * created inside a fiber to grab the reference to fiber manager
   */
  manager->addTask([]() mutable {
    /*
     * hold this in the scope, so we won't close accepting socket
     * too soon, hence the shared ptr
     */
    FiberServerSocket serverSocket(std::optional<folly::SocketAddress>(), 256);

    auto serverAddress = serverSocket.getListenAddress();
    XLOGF(
        DBG4,
        "Running acceptor on [{}]:{}",
        serverAddress.getAddressStr(),
        serverAddress.getPort());

    /*
     * the server task accepts new connections and then starts another
     * task that writes a random block of data back
     */
    addTask([serverSocket = std::move(serverSocket)]() mutable {
      SCOPE_EXIT {
        serverSocket.close();
      };
      for (int i = 0; i < kNumClients; i++) {
        serverSocket.accept().then([i](auto socket) {
          XLOGF(DBG4, "Accepted {} th connection, starting worker", i);
          /*
           * start new task to handle the data; we are inside fiber, so
           * we can use implicit call to addTask
           */
          addTask([i, socket = std::move(socket)]() mutable {
            SCOPE_EXIT {
              socket.close();
            };
            std::vector<uint8_t> buf;
            buf.resize(kLargeBlockSize);
            std::fill(buf.begin(), buf.end(), i);
            folly::ByteRange range(buf.data(), buf.size());

            while (range.size()) {
              auto result = socket.write(
                  folly::IOBuf::wrapBuffer(range.start(), range.size()));
              ASSERT_TRUE(result.hasValue());
              range.advance(result.value());
              XLOGF(
                  DBG4,
                  "Wrote {} bytes, remaining {}",
                  result.value(),
                  range.size());
            }
            XLOGF(DBG4, "Server connection {} has finised, closing", i);
          });
        }); // then
      } // for
    }); // addTask

    // add clients that connect to the server
    for (int i = 0; i < kNumClients; i++) {
      XLOGF(DBG4, "Adding client number {}", i);
      addTask([i, serverAddress]() mutable {
        FiberSocket::makeConnectedSocket(serverAddress, 1s)
            .then([i, serverAddress](auto sock) {
              SCOPE_EXIT {
                sock.close();
              };
              folly::IOBuf buf;
              while (buf.computeChainDataLength() < kLargeBlockSize) {
                sock.read(kLargeBlockSize - buf.computeChainDataLength())
                    .then([&buf, i](auto newBuf) {
                      XLOGF(
                          DBG4, "Client {} read {} bytes", i, newBuf->length());
                      EXPECT_GE(kLargeBlockSize, newBuf->length());
                      buf.appendChain(std::move(newBuf));
                    });
              }
              XLOGF(DBG4, "Client {} has finished, closing socket", i);
            });
      });
    }
  });

  evb.loop();
}

/*
 * Server-client exchange, single thread, multiple fibers.
 * We run one fiber with acceptor - on each accept we
 * create another fiber that writes into the connection.
 *
 * We also create equal number of fibers for clients using FiberSocket() ->
 * connect() - those read the data published by the server fibers.
 */
TEST_F(FiberSocketFixture, ClientServerSingleThread2) {
  /*
   * create wrapper task that will kick in more tasks afterwards
   * this is mainly needed because server sockets needs to be
   * created inside a fiber to grab the reference to fiber manager
   */
  manager->addTask([]() mutable {
    /*
     * hold this in the scope, so we won't close accepting socket
     * too soon, hence the shared ptr
     */
    FiberServerSocket serverSocket(std::optional<folly::SocketAddress>(), 256);

    auto serverAddress = serverSocket.getListenAddress();
    XLOGF(
        DBG4,
        "Running acceptor on [{}]:{}",
        serverAddress.getAddressStr(),
        serverAddress.getPort());

    /*
     * the server task accepts new connections and then starts another
     * task that writes a random block of data back
     */
    addTask([serverSocket = std::move(serverSocket)]() mutable {
      SCOPE_EXIT {
        serverSocket.close();
      };
      for (int i = 0; i < kNumClients; i++) {
        serverSocket.accept().then([i](auto socket) {
          XLOGF(DBG4, "Accepted {} th connection, starting worker", i);
          /*
           * start new task to handle the data; we are inside fiber, so
           * we can use implicit call to addTask
           */
          addTask([i, socket = std::move(socket)]() mutable {
            SCOPE_EXIT {
              socket.close();
            };
            std::vector<uint8_t> buf;
            buf.resize(kLargeBlockSize);
            std::fill(buf.begin(), buf.end(), i);
            folly::ByteRange range(buf.data(), buf.size());

            while (range.size()) {
              auto result = socket.write(
                  folly::IOBuf::wrapBuffer(range.start(), range.size()));
              ASSERT_TRUE(result.hasValue());
              range.advance(result.value());
              XLOGF(
                  DBG4,
                  "Wrote {} bytes, remaining {}",
                  result.value(),
                  range.size());
            }
            XLOGF(DBG4, "Server connection {} has finised, closing", i);
          });
        }); // then
      } // for
    }); // addTask

    // add clients that connect to the server
    for (int i = 0; i < kNumClients; i++) {
      XLOGF(DBG4, "Adding client number {}", i);
      addTask([i, serverAddress]() mutable {
        auto sock = FiberSocket();
        sock.connect(serverAddress, 1s).then([&sock, i](auto /* ret */) {
          SCOPE_EXIT {
            sock.close();
          };
          folly::IOBuf buf;
          while (buf.computeChainDataLength() < kLargeBlockSize) {
            sock.read(kLargeBlockSize - buf.computeChainDataLength())
                .then([&buf, i](auto newBuf) {
                  XLOGF(DBG4, "Client {} read {} bytes", i, newBuf->length());
                  EXPECT_GE(kLargeBlockSize, newBuf->length());
                  buf.appendChain(std::move(newBuf));
                });
          }
          XLOGF(DBG4, "Client {} has finished, closing socket", i);
        });
      });
    }
  });

  evb.loop();
}

/*
 * This test demonstrates inter-thread communications. It does
 * not directly relate to the async IO, but demonstrates how
 * one can talk among fibers running in different managers
 */
TEST_F(FiberSocketFixture, RemoteTaskExecution) {
  // this is the guy we schedule locally in this thread
  manager->addTask([this]() {
    for (int i = 0; i < 100; i++) {
      XLOGF(DBG4, "First task, iteration {}", i);
      yield();
    }
  });

  auto t = std::thread([this]() {
    std::vector<folly::Future<int>> futs;

    for (int i = 0; i < 64; i++) {
      /*
       * the below schedules a task in another thread (which
       * runs fiber manager driven by event base). We collect
       * all the futures from submitted tasks and then grab
       * their values. Notice that this pattern is still
       * synchronous: we "wait" for the result of computation
       * happening in the other thread in this thread, instead
       * of letting callback be invoked in another thread...
       */
      auto fut = manager->addTaskRemoteFuture([i]() -> int {
        XLOG(DBG4, "Task invoked");
        return i;
      });
      futs.emplace_back(std::move(fut));
    }

    for (auto& fut : futs) {
      // this will yield to the scheduler
      int i = std::move(fut).get();
      XLOGF(DBG4, "GOT {}", i);
    }
    // this is needed since we do loopForever below
    evb.terminateLoopSoon();
  });

  /*
   * this is needed over simple loop() because otherwise the
   * other thread may submit something AFTER we did loop()
   */
  evb.loopForever();
  t.join();
}

/*
 * Have on acceptor fiber accepting, reading and closing
 * incoming connections. We want to test that accept happens
 * in the order of arriving connections.
 */
TEST_F(FiberSocketFixture, AcceptInOrder) {
  manager->addTask([]() mutable {
    FiberServerSocket serverSocket(
        std::optional<folly::SocketAddress>(), kNumClients);

    auto serverAddress = serverSocket.getListenAddress();
    XLOGF(
        DBG4,
        "Running acceptor on [{}]:{}",
        serverAddress.getAddressStr(),
        serverAddress.getPort());

    // the server accepts connections and reads client id
    addTask([serverSocket = std::move(serverSocket)]() mutable {
      for (int i = 0; i < kNumClients; i++) {
        serverSocket.accept().then([i = i](auto socket) {
          folly::IOBuf buf;
          socket.read(sizeof(uint32_t)).then([&buf](auto newBuf) {
            buf.appendChain(std::move(newBuf));
            buf.coalesce();
          });
          auto connId = folly::io::Cursor(&buf).readBE<uint32_t>();
          EXPECT_EQ(i + 1, connId);
          socket.close();
        }); // then
      } // for
      // stop accepting
      serverSocket.close();
    });

    /*
     * add clients that connect to the server. Notice that we run all
     * connections in single fiber to avoid re-ordering by scheduler
     */
    addTask([serverAddress]() mutable {
      for (int i = 0; i < kNumClients; i++) {
        XLOGF(DBG4, "Adding client number {}", i);
        FiberSocket::makeConnectedSocket(serverAddress, 1s)
            .then([i](auto sock) {
              // we avoid writing zero
              uint32_t connId = htonl(i + 1);
              sock.write(folly::IOBuf::wrapBuffer(&connId, sizeof(connId)));
              XLOGF(DBG4, "Client {} has finished, closing socket", i);
              sock.close();
            });
      }
    });
  });

  evb.loop();
}

/*
 * This is stupid unittest to test binding to a given address
 */
TEST_F(FiberSocketFixture, BindToAddr) {
  // bind to "any" port on localhost
  manager->addTask([]() {
    FiberServerSocket serverSocket(folly::SocketAddress("::1", 0), 1);
    serverSocket.close();
  });
  evb.loop();
}

/*
 * Test exception handling when getPeerAddress() fails
 */
TEST_F(FiberSocketFixture, FiberSocketWithDisconnectedPeer) {
  manager->addTask([this]() mutable {
    auto mockSocket = std::make_shared<folly::test::MockAsyncSocket>(&evb);
    EXPECT_CALL(*mockSocket, good()).WillOnce(testing::Return(true));
    EXPECT_CALL(*mockSocket, getPeerAddress(testing::_))
        .WillOnce(
            testing::Throw(
                std::system_error(
                    std::make_error_code(std::errc::not_connected),
                    "Transport endpoint is not connected")));

    try {
      // This constructor should throw AsyncSocketException
      auto fs1 = FiberSocket(mockSocket, &evb);
      FAIL()
          << "Expected exception when creating FiberSocket with disconnected peer";
    } catch (const folly::AsyncSocketException& ex) {
      EXPECT_EQ(folly::AsyncSocketException::INTERNAL_ERROR, ex.getType());
      EXPECT_THAT(
          ex.what(), testing::HasSubstr("Failed to get socket addresses"));
    }
  });

  evb.loop();
}

/*
 * Test the sleep via AsyncTimeout
 */
TEST_F(FiberSocketFixture, SleepFor) {
  auto start = std::chrono::steady_clock::now();
  manager->addTask([]() { fiberSleepFor(100ms); });
  evb.loop();
  auto finish = std::chrono::steady_clock::now();
  EXPECT_GE(
      std::chrono::duration_cast<std::chrono::milliseconds>(finish - start),
      100ms);
  EXPECT_LE(
      std::chrono::duration_cast<std::chrono::milliseconds>(finish - start),
      200ms);
}

/*
 * Test that main fiber kills acceptor fiber by closing socket.
 */
TEST_F(FiberSocketFixture, KillAcceptorFiber) {
  // main fiber
  manager->addTask([]() mutable {
    std::shared_ptr<FiberServerSocket> serverSocket =
        std::make_shared<FiberServerSocket>(
            std::optional<folly::SocketAddress>(), 256);

    // acceptor fiber
    addTask([serverSocket]() mutable {
      auto serverAddress = serverSocket->getListenAddress();
      XLOGF(
          DBG4,
          "Running acceptor on [{}]:{}",
          serverAddress.getAddressStr(),
          serverAddress.getPort());
      while (true) {
        auto result = serverSocket->accept().then([](auto /* socket */) {
          XLOG(DBG4, "Accepted connection, starting/stopping worker");
        });
        if (result.hasError()) {
          SUCCEED();
          break;
        }
      } // while
      XLOG(DBG4, "Stopping acceptor");
      SUCCEED();
    }); // addTask

    // make sure that acceptor fiber has started accepting
    fiberSleepFor(100ms);
    // signal acceptor fiber to shut down
    serverSocket->close();
  }); // addTask

  evb.loop();
}

/*
 * Test that main fiber kills connector fiber by passing null msg to input
 * queue.
 */
TEST_F(FiberSocketFixture, KillConnectorFiber) {
  // test setup to handle FiberSocketError
  struct TestFiberSocketErrorVisitor {
    bool operator()(folly::AsyncSocketException const&) {
      // do nothing
      return false;
    }

    bool operator()(FiberGenericSocketError const& error) {
      if (error.type_ == FiberGenericSocketErrorType::CONNECT_STOPPED) {
        // bail out
        return true;
      }
      // do nothing
      return false;
    }
  };

  // test setup to handle combined events
  struct ConnRetryTimeout {};
  using CombinedType = std::variant<FiberSocketInputMessageT, ConnRetryTimeout>;
  struct CombinedVisitor {
    std::function<void(void)> fnConnRetryTimeout_;
    explicit CombinedVisitor(std::function<void(void)> fnConnRetryTimeout)
        : fnConnRetryTimeout_(std::move(fnConnRetryTimeout)) {}

    void operator()(const FiberSocketInputMessageT&) {}

    void operator()(const ConnRetryTimeout&) {
      fnConnRetryTimeout_();
    }
  };

  // main fiber
  manager->addTask([this]() mutable {
    RWQueue<FiberSocketInputMessageT> errQueue;
    std::shared_ptr<Timer<ConnRetryTimeout>> connRetryTimer =
        std::make_shared<Timer<ConnRetryTimeout>>(
            std::chrono::milliseconds(50));
    auto errReader = errQueue.getReader();

    // connect retry fiber
    addTask([this,
             errReaderFromMain = std::move(errReader),
             connRetryTimer]() mutable {
      // this is the queue where we put merged data
      RWQueue<CombinedType> combinedQueue;
      auto connRetryTimerReader = connRetryTimer->getQueue();

      // this fiber merges queue
      addTask([&combinedQueue,
               &errReaderFromMain,
               &connRetryTimerReader]() mutable {
        auto writer = combinedQueue.getWriter();
        mergeQueuesStatic(
            writer,
            std::move(errReaderFromMain),
            std::move(connRetryTimerReader));
      }); // addTask

      // the visitor handles connection retry on timeout events.
      RWQueue<FiberSocketInputMessageT> errQueueForConnect;
      auto errWriterForConnect = errQueueForConnect.getWriter();
      auto visitor = CombinedVisitor([this,
                                      &errQueueForConnect,
                                      connRetryTimer]() mutable {
        // this fiber calls FiberSocket::connect()
        manager->addTask([&errQueueForConnect,
                          timer = std::move(connRetryTimer)]() mutable {
          // spins new thread, binds port
          folly::ScopedBoundPort ph;
          auto closedAddress = ph.getAddress();
          XLOGF(
              DBG4, "Running connector to [{}]", closedAddress.getAddressStr());
          auto result =
              FiberSocket::makeConnectedSocket(
                  closedAddress,
                  std::chrono::milliseconds(0),
                  folly::AsyncSocket::anyAddress(),
                  errQueueForConnect.getReader())
                  .then([](auto /* socket */) {
                    XLOG(
                        DBG4,
                        "Connection established, starting/stopping worker");
                  }); // then
          if (result.hasError()) {
            TestFiberSocketErrorVisitor errProcessor;
            auto bailout = std::visit(errProcessor, result.error());
            if (bailout) {
              XLOG(DBG4, "Bail out due to error msg from main fiber");
              return;
            }
            // start a fiber running conn retry timer
            addTask([t = std::move(timer)] {
              XLOG(DBG4, "Starting conn retry timer");
              t->run();
            }); // addTask
          } // if
        }); // addTask
      });

      auto combinedReader = combinedQueue.getReader();

      // this fiber consumes error from main fiber and timeout events
      auto consumer = manager->addTaskFuture(
          [&visitor, &errWriterForConnect, &combinedReader]() mutable {
            while (true) {
              auto val = combinedReader.get();
              if (!val) {
                errWriterForConnect.putNull();
                break;
              }
              std::visit(visitor, *val);
            }
          }); // addTask

      // start a fiber running conn retry timer
      addTask([connRetryTimer] {
        XLOG(DBG4, "Starting conn retry timer");
        connRetryTimer->run();
      }); // addTask

      // wait for the consumer to finish
      std::move(consumer).get();
      XLOG(DBG4, "Connect retry fiber terminates");
    }); // addTask

    // make sure that connector fiber has started connecting
    fiberSleepFor(100ms);
    XLOG(DBG4, "Main fiber signals to stop the connector fibers");
    // signal connector fiber to shut down
    auto errWriter = errQueue.getWriter();
    errWriter.putNull();
    connRetryTimer->stop();
    XLOG(DBG4, "Main fiber terminates");
  }); // addTask

  evb.loop();
}

/*
 * Test that main fiber kills connector fiber by closing socket
 */
TEST_F(FiberSocketFixture, KillConnectorFiber2) {
  /*
   * create wrapper task that will kick in more tasks afterwards
   * this is mainly needed because server sockets needs to be
   * created inside a fiber to grab the reference to fiber manager
   */
  manager->addTask([]() mutable {
    /*
     * hold this in the scope, so we won't close accepting socket
     * too soon, hence the shared ptr
     */
    FiberServerSocket serverSocket(std::optional<folly::SocketAddress>(), 256);

    auto serverAddress = serverSocket.getListenAddress();
    XLOGF(
        DBG4,
        "Running acceptor on [{}]:{}",
        serverAddress.getAddressStr(),
        serverAddress.getPort());

    /*
     * the server task accepts new connections and then starts another
     * task that writes a random block of data back
     */
    addTask([serverSocket = std::move(serverSocket)]() mutable {
      SCOPE_EXIT {
        serverSocket.close();
      };
      // do nothing: let client connect hang
    }); // addTask

    auto clientSocket = FiberSocket();

    // add client that connect to the server
    XLOG(DBG4, "Adding client");
    addTask([&]() mutable { clientSocket.connect(serverAddress, 2s); });

    fiberSleepFor(100ms);
    XLOG(DBG4, "Main fiber signals to stop the connector fibers");
    clientSocket.close();
  });
  evb.loop();
}

/*
 * Call second connect on connect socket
 */
TEST_F(FiberSocketFixture, DoubleConnect) {
  /*
   * create wrapper task that will kick in more tasks afterwards
   * this is mainly needed because server sockets needs to be
   * created inside a fiber to grab the reference to fiber manager
   */
  manager->addTask([]() mutable {
    /*
     * hold this in the scope, so we won't close accepting socket
     * too soon, hence the shared ptr
     */
    FiberServerSocket serverSocket(std::optional<folly::SocketAddress>(), 256);

    auto serverAddress = serverSocket.getListenAddress();
    XLOGF(
        DBG4,
        "Running acceptor on [{}]:{}",
        serverAddress.getAddressStr(),
        serverAddress.getPort());

    /*
     * the server task accepts new connections and then starts another
     * task that writes a random block of data back
     */
    addTask([serverSocket = std::move(serverSocket)]() mutable {
      SCOPE_EXIT {
        serverSocket.close();
      };
      serverSocket.accept();
    }); // addTask

    auto clientSocket = FiberSocket();

    // add client that connect to the server
    XLOG(DBG4, "Adding client");
    addTask([serverAddress, clientSocket = std::move(clientSocket)]() mutable {
      SCOPE_EXIT {
        clientSocket.close();
      };
      clientSocket.connect(serverAddress, 1s)
          .then([&clientSocket, serverAddress](auto /* folly::Unit */) {
            auto ret = clientSocket.connect(serverAddress, 1s);
            ASSERT_TRUE(ret.hasError());
            folly::variant_match(
                ret.error(),
                [](FiberGenericSocketError const& error) {
                  EXPECT_EQ(
                      error.type_,
                      FiberGenericSocketErrorType::CONNECT_ALREADY);
                },
                [](folly::AsyncSocketException const& /*error*/) {
                  FAIL() << "seconnd connect() call fail for "
                            "folly::AsyncSocketException.";
                });
          });
    });
  });
  evb.loop();
}
