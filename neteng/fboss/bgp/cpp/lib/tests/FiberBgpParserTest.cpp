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

#include <gtest/gtest.h>

#define BackPressuredQueue_TEST_FRIENDS \
  friend class FiberManagerFixture;     \
  FRIEND_TEST(FiberManagerFixture, FiberBgpParserPartialPacket);

#include "neteng/fboss/bgp/cpp/lib/BgpException.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberBgpParser.h"
#include "neteng/fboss/bgp/cpp/lib/tests/BgpMessageParserTestData.h"

#include <folly/Random.h>
#include <folly/Unit.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Collect.h>
#include <folly/fibers/EventBaseLoopController.h>
#include <folly/io/Cursor.h>
#include <folly/io/async/EventBase.h>
#include <folly/logging/xlog.h>

using namespace facebook::nettools::bgplib;
using namespace folly::fibers;

namespace {
// OPEN, KEEPALIVE + 3xUPDATES in a row. All messages are valid
// OPEN indicates only 2 byte AS capability, sends AS paths which are two bytes.
// clang-format off
std::vector<uint8_t> const kMultipleBgpMessages = {
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0x00, 0x2d,
  0x01,
  0x04, 0xff, 0x14, 0x00, 0xb4, 0x03, 0x03, 0x03,
  0x03, 0x10, 0x02, 0x06, 0x01, 0x04, 0x00, 0x01,
  0x00, 0x01, 0x02, 0x02, 0x80, 0x00, 0x02, 0x02,
  0x02, 0x00,
  //
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0x00, 0x13,
  0x04,
  //
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0x00, 0x3e,
  0x02,
  0x00, 0x00,
  0x00, 0x22,
  0x40, 0x01, 0x01, 0x02,
  0x40, 0x02, 0x06, 0x02, 0x02, 0xfe, 0x4c, 0xfe, 0xb0,
  0x40, 0x03, 0x04, 0x01, 0x01, 0x01, 0x01,
  0x80, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00,
  0x40, 0x05, 0x04, 0x00, 0x00, 0x00, 0x64,
  0x1e, 0xac, 0x10, 0x00, 0x08,
  //
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0x00, 0x45,
  0x02,
  0x00, 0x00,
  0x00, 0x22,
  0x40, 0x01, 0x01, 0x00,
  0x40, 0x02, 0x06, 0x02, 0x02, 0xfe, 0x4c, 0xfe, 0xb0,
  0x40, 0x03, 0x04, 0x01, 0x01, 0x01, 0x01,
  0x80, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00,
  0x40, 0x05, 0x04, 0x00, 0x00, 0x00, 0x64,
  0x18, 0x0a, 0x14, 0x03,
  0x18, 0x0a, 0x14, 0x02,
  0x18, 0x0a, 0x14, 0x01,
   //
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0x00, 0x43,
  0x02,
  0x00, 0x00,
  0x00, 0x20,
  0x40, 0x01, 0x01, 0x00,
  0x40, 0x02, 0x04, 0x02, 0x01, 0xfe, 0x4c,
  0x40, 0x03, 0x04, 0x01, 0x01, 0x01, 0x01,
  0x80, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00,
  0x40, 0x05, 0x04, 0x00, 0x00, 0x00, 0x64,
  0x18, 0x0a, 0x0a, 0x03,
  0x18, 0x0a, 0x0a, 0x02,
  0x18, 0x0a, 0x0a, 0x01
};
// clang-format on

// kMultipleBgpMessages and a bit more.
std::vector<uint8_t> kMultipleBgpMessagesPlus(kMultipleBgpMessages);

} // namespace

/*
 * The fixture provides fiber manager and evb for the tests
 */
class FiberManagerFixture : public ::testing::Test {
 public:
  FiberManagerFixture() = default;
  ~FiberManagerFixture() override = default;

  void SetUp() override {
    FiberManager::Options options;
    /*
     * this is needed due to nested recursion with large stack
     * due to installed exception handlers...
     */
    options.stackSize = 64 * 1024;
    manager = std::make_unique<FiberManager>(
        std::make_unique<EventBaseLoopController>(), options);

    static_cast<EventBaseLoopController&>(manager->loopController())
        .attachEventBase(evb);

    kMultipleBgpMessagesPlus.insert(
        std::end(kMultipleBgpMessagesPlus),
        std::begin(kUpdateMessage2ByteAs),
        std::end(kUpdateMessage2ByteAs));
  }

  void TearDown() override {}

  void chunkSendAndValidate(
      const std::vector<uint8_t>& dataStream,
      uint32_t openCount,
      uint32_t keepAliveCount,
      uint32_t updateCount,
      uint32_t notificationCount,
      uint32_t eorCount,
      uint32_t routeRefreshCount);

  std::unique_ptr<FiberManager> manager;
  folly::EventBase evb;
};

/*
 * This method chunks any data buffer, sends it to parser and
 * validates that parser has processed the type of packet sent
 */
void FiberManagerFixture::chunkSendAndValidate(
    const std::vector<uint8_t>& dataStream,
    uint32_t openCount,
    uint32_t keepAliveCount,
    uint32_t updateCount,
    uint32_t notificationCount,
    uint32_t eorCount,
    uint32_t routeRefreshCount) {
  // child fibers created for this peer
  std::vector<folly::Future<folly::Unit>> workers;

  MonitoredBackPressuredQueue<
      std::optional<folly::Try<FiberBgpParser::BgpMessageT>>>
      rcvdQueue;

  // generate and write BGP messages in random pieces
  {
    auto fiber =
        manager->addTaskFuture([&rcvdQueue, &dataStream]() mutable noexcept {
          FiberBgpParser bgpParser(BgpCapabilities(), rcvdQueue);

          // Expected capacity after processing all chunks is zero
          EXPECT_EQ(0, bgpParser.getBufCapaticy());

          // grab range, slice it in random pieces, send those as buffers
          folly::ByteRange range(dataStream.data(), dataStream.size());

          while (range.size()) {
            auto blockSize = std::min(
                folly::Random::rand32() %
                    (static_cast<uint32_t>(dataStream.size()) / 2),
                static_cast<uint32_t>(range.size()));

            if (blockSize == 0) {
              continue;
            }

            XLOGF(
                DBG4,
                "Writing block of size {} remaining {}",
                blockSize,
                (range.size() - blockSize));

            folly::ByteRange subRange(range.data(), blockSize);

            bgpParser.processBgpMsgBuf(folly::IOBuf::wrapBuffer(subRange));
            range.advance(blockSize);
          }

          // gracefully shut down coro task
          rcvdQueue.fiberPush(std::nullopt);
        });
    workers.emplace_back(std::move(fiber));
  }

  // the task to consume parser messages
  {
    auto coro = folly::coro::co_invoke(
        [&rcvdQueue,
         openCount,
         keepAliveCount,
         updateCount,
         notificationCount,
         eorCount,
         routeRefreshCount]() -> folly::coro::Task<void> {
          // record the statistics of the message
          uint32_t openCnt{0};
          uint32_t updateCnt{0};
          uint32_t keepAliveCnt{0};
          uint32_t eorCnt{0};
          uint32_t notifCnt{0};
          uint32_t routeRefreshCnt{0};

          // collect all messages emitted by the parser
          do {
            auto bgpMsg = co_await co_awaitTry(rcvdQueue.pop());
            folly::variant_match(
                /*
                 * triple star to unpack:
                 *  1. folly::Try(MPMCQueue)
                 *  2. std::optional(for termination signal)
                 *  3. folly::Try(BgpMessageT)
                 */
                ***bgpMsg,
                [&](const BgpOpenMsg&) { openCnt++; },
                [&](std::shared_ptr<const BgpUpdate2> const&) { updateCnt++; },
                [&](const BgpKeepAlive&) { keepAliveCnt++; },
                [&](const BgpEndOfRib&) { eorCnt++; },
                [&](const BgpNotification&) { notifCnt++; },
                [&](const BgpRouteRefresh&) { routeRefreshCnt++; },
                [&](const FiberSocketError&) { /* unused */ },
                [&](const UpdateDescriptor&) {
                  /* UpdateDescriptor never comes from parser, only goes to
                   * serializer */
                  XLOG(ERR, "Unexpected UpdateDescriptor in parser test!");
                });

            if (openCnt == openCount && keepAliveCnt == keepAliveCount &&
                eorCnt == eorCount && updateCnt == updateCount &&
                notifCnt == notificationCount &&
                routeRefreshCnt == routeRefreshCount) {
              XLOG(DBG1, "Message consumer has finished its loop");
              break;
            }
          } while (true);
        });
    /*
     * Start the coroutine immediately so it runs concurrently with the fiber
     * Convert SemiFuture to Future by attaching to event base
     */
    auto semiFuture =
        folly::coro::co_withExecutor(&evb, std::move(coro)).start();
    workers.emplace_back(std::move(semiFuture).via(&evb));
  }

  // Wait for all tasks (both fibers and coroutines) to complete concurrently
  folly::collectAll(workers.begin(), workers.end()).get();
}

namespace {
/*
 * A 19-byte BGP header with an invalid marker (all zeros instead of all
 * 0xff). parseBgpMsgHdr rejects this with BgpHeaderException
 * ("Synchronization error"), exercising the header-error path in
 * processBgpMsgBuf.
 */
std::vector<uint8_t> const kBgpHeaderBadMarker = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // (bad) marker
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // (bad) marker
    0x00, 0x13, // Length (19)
    0x04, // Type (KEEPALIVE) -- marker check fails first
};

struct DrainResult {
  bool sawError{false};
  uint32_t goodCount{0};
  folly::exception_wrapper error; // the last error delivered
};

/*
 * Drives FiberBgpParser on a fiber, feeding `wire` into a capacity-1 queue.
 * The first (valid) message fills the queue, so the second message's push
 * hits backpressure and the producer fiber must suspend. If that second
 * message fails to parse, the suspend happens from inside the parser's catch
 * block -- which trips folly's Fiber::preempt CHECK and aborts (the bug under
 * test). A consumer coroutine drains the queue and records whether an error
 * Try was delivered.
 */
DrainResult runParserUnderBackpressure(
    FiberManager& manager,
    folly::EventBase& evb,
    const std::vector<uint8_t>& wire) {
  DrainResult result;
  manager.addTask([&]() mutable noexcept {
    // Capacity 1: the first message fills the queue so the second push blocks.
    MonitoredBackPressuredQueue<
        std::optional<folly::Try<FiberBgpParser::BgpMessageT>>>
        rcvdQueue(1);

    std::vector<folly::Future<folly::Unit>> workers;

    // Producer fiber: parse the wire, then signal stop.
    workers.emplace_back(
        manager.addTaskFuture([&rcvdQueue, &wire]() mutable noexcept {
          FiberBgpParser parser(BgpCapabilities(), rcvdQueue);
          parser.processBgpMsgBuf(
              folly::IOBuf::wrapBuffer(wire.data(), wire.size()));
          rcvdQueue.fiberPush(std::nullopt);
        }));

    // Consumer coroutine: drain until the stop signal, recording deliveries.
    auto coro = folly::coro::co_invoke(
        [&rcvdQueue, &result]() -> folly::coro::Task<void> {
          while (true) {
            auto popped = co_await co_awaitTry(rcvdQueue.pop());
            if (popped.hasException() || !popped->has_value()) {
              break; // queue cancelled, or stop signal (nullopt)
            }
            if ((**popped).hasException()) {
              result.sawError = true;
              result.error = (**popped).exception();
            } else {
              ++result.goodCount;
            }
          }
        });
    workers.emplace_back(
        folly::coro::co_withExecutor(&evb, std::move(coro)).start().via(&evb));

    folly::collectAll(workers.begin(), workers.end()).get();
  });
  evb.loop();
  return result;
}
} // namespace

/*
 * Regression for P2359392421: a malformed UPDATE arriving while the ingress
 * queue is full must not crash. Pre-fix, handleBgpUpdateMsgException did the
 * backpressured fiberPush from inside parseBgpMessage's catch block, so the
 * fiber suspended with an exception in flight and folly's Fiber::preempt CHECK
 * aborted the process.
 */
TEST_F(FiberManagerFixture, MalformedUpdateUnderBackpressureDoesNotCrash) {
  std::vector<uint8_t> wire(kBgpKeepaliveMessage);
  wire.insert(
      wire.end(),
      kBgpUpdateMessageWithInvalidAsPathAttrsLength.begin(),
      kBgpUpdateMessageWithInvalidAsPathAttrsLength.end());

  auto result = runParserUnderBackpressure(*manager, evb, wire);

  // The parse error is delivered downstream, not dropped, and we do not crash.
  EXPECT_TRUE(result.sawError);
  EXPECT_EQ(1u, result.goodCount); // the leading keepalive
}

/*
 * Same hazard, but via the header-error path in processBgpMsgBuf (a malformed
 * header is caught there and pushed from inside that catch block).
 */
TEST_F(FiberManagerFixture, MalformedHeaderUnderBackpressureDoesNotCrash) {
  std::vector<uint8_t> wire(kBgpKeepaliveMessage);
  wire.insert(
      wire.end(), kBgpHeaderBadMarker.begin(), kBgpHeaderBadMarker.end());

  auto result = runParserUnderBackpressure(*manager, evb, wire);

  EXPECT_TRUE(result.sawError);
  EXPECT_EQ(1u, result.goodCount); // the leading keepalive
}

/*
 * A 19-byte ROUTE-REFRESH (header only) at the end of the read buffer has no
 * AFI, subtype or SAFI. It must end this session with a Bad Message Length
 * header error, not escape processBgpMsgBuf into the noexcept read loop.
 */
TEST_F(FiberManagerFixture, ShortRouteRefreshDoesNotCrash) {
  std::vector<uint8_t> wire(kBgpKeepaliveMessage);
  std::vector<uint8_t> shortRouteRefresh(kBgpKeepaliveMessage);
  shortRouteRefresh[18] = 0x05; // Type (ROUTE-REFRESH), Length stays 19
  wire.insert(wire.end(), shortRouteRefresh.begin(), shortRouteRefresh.end());

  auto result = runParserUnderBackpressure(*manager, evb, wire);

  EXPECT_TRUE(result.sawError);
  EXPECT_EQ(1u, result.goodCount); // the leading keepalive
  const auto* error = result.error.get_exception<BgpHeaderException>();
  ASSERT_NE(nullptr, error);
  EXPECT_EQ(BgpNotifMsgHdrErrSubCode::BN_MH_BAD_MSG_LEN, error->getSubCode());
}

TEST_F(FiberManagerFixture, FiberBgpParserGoodMessages) {
  for (int i = 0; i < 64; i++) {
    manager->addTask([this]() mutable noexcept {
      chunkSendAndValidate(kMultipleBgpMessages, 1, 1, 3, 0, 0, 0);
    });
  }
  evb.loop();
}

TEST_F(FiberManagerFixture, VerifyNegotiatedAsSizeIsUsed) {
  manager->addTask([&]() mutable noexcept {
    // child fibers created for this peer
    std::vector<folly::Future<folly::Unit>> workers;

    MonitoredBackPressuredQueue<
        std::optional<folly::Try<FiberBgpParser::BgpMessageT>>>
        rcvdQueue;

    {
      auto fiber = manager->addTaskFuture([&rcvdQueue]() mutable noexcept {
        /*
         * Set configured capabilities as AS 4 byte.
         * Peer is capable of only 2 byte AS numbers and sends the same.
         */
        auto caps = BgpCapabilities();
        caps.as4byte() = true;
        FiberBgpParser bgpParser(caps, rcvdQueue);

        folly::ByteRange range(
            kMultipleBgpMessagesPlus.data(), kMultipleBgpMessagesPlus.size());

        // process one msg buffer
        bgpParser.processBgpMsgBuf(folly::IOBuf::wrapBuffer(range));

        // gracefully shut down coro task
        rcvdQueue.fiberPush(std::nullopt);
      });
      workers.emplace_back(std::move(fiber));
    }
    {
      // the task to consume parser messages
      auto coro =
          folly::coro::co_invoke([&rcvdQueue]() -> folly::coro::Task<void> {
            uint32_t openCnt{0};
            uint32_t updateCnt{0};
            uint32_t notifCnt{0};

            do {
              auto bgpMsg = co_await co_awaitTry(rcvdQueue.pop());
              folly::variant_match(
                  /*
                   * triple star to unpack:
                   *  1. folly::Try(MPMCQueue)
                   *  2. std::optional(for termination signal)
                   *  3. folly::Try(BgpMessageT)
                   */
                  ***bgpMsg,
                  [&](const BgpOpenMsg&) { openCnt++; },
                  [&](std::shared_ptr<const BgpUpdate2> const& update) {
                    updateCnt++;

                    // Verify that all AS paths are interpreted as 2 byte
                    const auto asPath = *update->attrs()->asPath();
                    EXPECT_EQ(1, asPath.size()); // Number of segments
                    switch (updateCnt) {
                      case 1:
                      case 2: {
                        /*
                         * first and 2nd updates have 2 AS numbers in AS
                         * sequence.
                         */
                        EXPECT_EQ(2, asPath[0].asSequence()->size());
                        EXPECT_EQ(0xfe4c, asPath[0].asSequence()[0]);
                        EXPECT_EQ(0xfeb0, asPath[0].asSequence()[1]);
                      } break;
                      case 3: {
                        // 3rd update has 1 AS number in AS sequence.
                        EXPECT_EQ(1, asPath[0].asSequence()->size());
                        EXPECT_EQ(0xfe4c, asPath[0].asSequence()[0]);
                      } break;
                      case 4: {
                        // the UPDATE from kUpdateMessage2ByteAs
                        EXPECT_EQ(1, asPath[0].asSequence()->size());
                        EXPECT_EQ(0xfde8, asPath[0].asSequence()[0]);
                      } break;
                    }
                  },
                  [&](const BgpKeepAlive&) { /* unused */ },
                  [&](const BgpEndOfRib&) { /* unused */ },
                  [&](const BgpNotification&) { notifCnt++; },
                  [&](const BgpRouteRefresh&) { /* unused */ },
                  [&](const FiberSocketError&) { /* unused */ },
                  [&](const UpdateDescriptor&) {
                    /* UpdateDescriptor never comes from parser, only goes to
                     * serializer */
                    XLOG(ERR, "Unexpected UpdateDescriptor in parser test!");
                  });

              if (openCnt == 1 && updateCnt == 4 && notifCnt == 0) {
                XLOG(DBG1, "Message consumer has finished its loop");
                break;
              }
            } while (true);
          });

      /*
       * Start the coroutine immediately so it runs concurrently with the fiber
       * Convert SemiFuture to Future by attaching to event base
       */
      auto semiFuture =
          folly::coro::co_withExecutor(&evb, std::move(coro)).start();
      workers.emplace_back(std::move(semiFuture).via(&evb));
    }

    // Wait for all tasks (both fibers and coroutines) to complete concurrently
    folly::collectAll(workers.begin(), workers.end()).get();
  });
  evb.loop();
}

/*
 * Verify partial packet processing
 * 1. Verify that any type of packet can be received in chunks
 * 2. Verify that any type of packet will be processed immediately when all
 * chunks are received
 * 3. Verify that capacity of the buffer used to keep track of chunks is
 * 0 after processing of all chunks
 */
TEST_F(FiberManagerFixture, FiberBgpParserPartialPacket) {
  manager->addTask([&]() mutable noexcept {
    // Verify for open message
    chunkSendAndValidate(kBgpOpenMessage, 1, 0, 0, 0, 0, 0);
    // Verify for keep alive message
    chunkSendAndValidate(kBgpKeepaliveMessage, 0, 1, 0, 0, 0, 0);
    // Verify for update message
    chunkSendAndValidate(kBgpUpdateMessage, 0, 0, 1, 0, 0, 0);
    // Verify for notification message
    chunkSendAndValidate(kBgpNoticationMessage, 0, 0, 0, 1, 0, 0);
    // Verify for eor message
    chunkSendAndValidate(kBgpEorMessage, 0, 0, 0, 0, 1, 0);
    // Verify for route refresh message
    chunkSendAndValidate(kBgpRouteRefreshMsg, 0, 0, 0, 0, 0, 1);
  });
  evb.loop();
}
