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

#include <optional>
#include <utility>

#include <folly/ExceptionWrapper.h>
#include <folly/io/Cursor.h>
#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/cpp/lib/BgpException.h"
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"
#include "neteng/fboss/bgp/cpp/lib/BgpUtil.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberBgpParser.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

namespace facebook {
namespace nettools {
namespace bgplib {

void FiberBgpParser::FiberParserCallbacks::rcvdBgpOpenMsg(BgpOpenMsg msg) {
  bgp::PeerStats::incrMessageRecvOpen();
  caps_ = negotiateCapabilities(caps_, *msg.capabilities());
  wqueue_.fiberPush(folly::Try<BgpMessageT>(std::move(msg)));
}

void FiberBgpParser::FiberParserCallbacks::rcvdBgpNotification(
    BgpNotification msg) {
  bgp::PeerStats::incrMessageRecvNotification();
  wqueue_.fiberPush(folly::Try<BgpMessageT>(msg));
}

void FiberBgpParser::FiberParserCallbacks::rcvdBgpKeepAlive() {
  bgp::PeerStats::incrMessageRecvKeepAlive();
  BgpKeepAlive msg;
  wqueue_.fiberPush(folly::Try<BgpMessageT>(std::move(msg)));
}

void FiberBgpParser::FiberParserCallbacks::rcvdBgpUpdate(BgpUpdate2 msg) {
  bgp::PeerStats::incrMessageRecvUpdate();
  auto p = std::make_shared<BgpUpdate2>(msg);
  wqueue_.fiberPush(folly::Try<BgpMessageT>(std::move(p)));
}

void FiberBgpParser::FiberParserCallbacks::rcvdBgpEndOfRib(BgpEndOfRib msg) {
  bgp::PeerStats::incrMessageRecvUpdate();
  wqueue_.fiberPush(folly::Try<BgpMessageT>(std::move(msg)));
}

void FiberBgpParser::FiberParserCallbacks::rcvdBgpRouteRefresh(
    BgpRouteRefresh msg) {
  bgp::PeerStats::incrMessageRecvRouteRefresh();
  wqueue_.fiberPush(folly::Try<BgpMessageT>(msg));
}

void FiberBgpParser::FiberParserCallbacks::handleBgpException(
    const BgpException& ex) {
  XLOGF(ERR, "Error parsing BGP message: {}", folly::exceptionStr(ex));

  pendingError_ = folly::Try<BgpMessageT>(
      folly::exception_wrapper(std::make_exception_ptr(ex)));
}

void FiberBgpParser::FiberParserCallbacks::handleBgpFsmException(
    const BgpFsmException& ex) {
  XLOGF(ERR, "FSM error with exception: {}", folly::exceptionStr(ex));

  pendingError_ = folly::Try<BgpMessageT>(
      folly::exception_wrapper(std::make_exception_ptr(ex)));
}

void FiberBgpParser::FiberParserCallbacks::handleBgpHeaderException(
    const BgpHeaderException& ex) {
  XLOGF(ERR, "Header error with exception: {}", folly::exceptionStr(ex));

  pendingError_ = folly::Try<BgpMessageT>(
      folly::exception_wrapper(std::make_exception_ptr(ex)));
}

void FiberBgpParser::FiberParserCallbacks::handleBgpOpenMsgException(
    const BgpOpenMsgException& ex) {
  XLOGF(ERR, "Error parsing BGP OPEN message: {}", folly::exceptionStr(ex));

  pendingError_ = folly::Try<BgpMessageT>(
      folly::exception_wrapper(std::make_exception_ptr(ex)));
}

void FiberBgpParser::FiberParserCallbacks::handleBgpUpdateMsgException(
    const BgpUpdateMsgException& ex) {
  XLOGF(ERR, "Error parsing BGP UPDATE message: {}", folly::exceptionStr(ex));

  pendingError_ = folly::Try<BgpMessageT>(
      folly::exception_wrapper(std::make_exception_ptr(ex)));
}

void FiberBgpParser::FiberParserCallbacks::handleBgpRouteRefreshMsgException(
    const BgpRouteRefreshMsgException& ex) {
  XLOGF(
      ERR,
      "Error parsing BGP ROUTE_REFRESH message: {}",
      folly::exceptionStr(ex));

  pendingError_ = folly::Try<BgpMessageT>(
      folly::exception_wrapper(std::make_exception_ptr(ex)));
}

FiberBgpParser::FiberParserCallbacks::FiberParserCallbacks(
    BgpCapabilities& caps,
    MonitoredBackPressuredQueue<std::optional<folly::Try<BgpMessageT>>>& wqueue)
    : caps_{caps}, wqueue_{wqueue} {}

FiberBgpParser::FiberBgpParser(
    const BgpCapabilities& myCaps,
    MonitoredBackPressuredQueue<std::optional<folly::Try<BgpMessageT>>>& wqueue)
    : myCaps_(myCaps), wqueue_(wqueue), parserCb_(negotiatedCaps_, wqueue_) {
  negotiatedCaps_ = myCaps;
}

void FiberBgpParser::processBgpMsgBuf(std::unique_ptr<folly::IOBuf> buf) {
  XLOGF(DBG4, "Got another buffer of size {}", buf->length());

  if (buf_) {
    // We have data from previous read call, append this IOBuf
    buf_->appendChain(std::move(buf));
  } else {
    buf_ = std::move(buf);
  }

  /*
   * NOTE: Majority of cases IOBuf will have only 1 element in chain
   *       this call will bailout without leading to any allocation and
   *       copying of buffer.
   */
  buf_->coalesce();
  XLOGF(DBG4, "Total buffer size accumulated to {}", buf_->length());

  folly::io::Cursor cur{buf_.get()};
  /*
   * TODO : potential issue when we open source, if we have a message <
   * kBgpMsgHeaderLen we won't be able to terminate correctly
   */
  while (cur.length() >= kBgpMsgHeaderLen) {
    XLOGF(DBG4, "Parsing loop, cursor length {}", cur.length());
    BgpMessageHeader msgHdr;

    /*
     * try parsing header. if we fail, stop parsing since the stream
     * of data is likely damaged and sessions needs to re-synchronize.
     * Record the error here and push it *after* the catch unwinds: a fiber
     * must never suspend (e.g. on a backpressured fiberPush) while a C++
     * exception is in flight -- folly::fibers::Fiber::preempt CHECK-fails.
     */
    std::optional<folly::Try<BgpMessageT>> headerError;
    try {
      msgHdr = BgpMessageParser2::parseBgpMsgHdr(cur);
      XLOGF(DBG3, "BGP header, message size {}", msgHdr.length);
    } catch (const BgpHeaderException& ex) {
      XLOG(ERR, "Error parsing BGP header message");
      headerError = folly::Try<BgpMessageT>(
          folly::exception_wrapper(std::make_exception_ptr(ex)));
    } catch (...) {
      XLOG(ERR, "Incorrect BGP message header");
      headerError = folly::Try<BgpMessageT>(
          folly::exception_wrapper(std::current_exception()));
    }
    if (headerError) {
      wqueue_.fiberPush(std::move(*headerError));
      return;
    }

    // can we parse full message now?
    if (cur.length() < msgHdr.length) {
      // nope!
      XLOGF(
          DBG4,
          "Have  {}, need {}, wait for full message",
          cur.length(),
          msgHdr.length);
      break;
    }

    /*
     * parseBgpMessage handles the Bgp* exceptions itself. Any other exception
     * would reach the noexcept FiberBgpPeer::readSocketLoop and terminate
     * bgpd, so report it as a parse error of this session instead. As with the
     * header error above, push only after the catch unwinds.
     */
    std::optional<folly::Try<BgpMessageT>> bodyError;
    try {
      BgpMessageParser2::parseBgpMessage(&parserCb_, *buf_, negotiatedCaps_);
    } catch (...) {
      auto ew = folly::exception_wrapper(std::current_exception());
      XLOGF(
          ERR,
          "Unexpected error parsing BGP message: {}",
          folly::exceptionStr(ew));
      bodyError = folly::Try<BgpMessageT>(std::move(ew));
    }
    if (bodyError) {
      wqueue_.fiberPush(std::move(*bodyError));
      return;
    }

    /*
     * The parser's handle*Exception callbacks record any parse error rather
     * than pushing it directly (they run inside the parser's catch block, where
     * suspending a fiber would trip folly::fibers::Fiber::preempt). Push it
     * here, after the catch has unwound, so the fiber never suspends
     * mid-flight.
     */
    if (auto err = parserCb_.takePendingError()) {
      wqueue_.fiberPush(std::move(*err));
    }

    XLOGF(DBG4, "Skipping {}", msgHdr.length);
    buf_->trimStart(msgHdr.length);
    cur.reset(buf_.get());
  }

  if (buf_->empty()) {
    buf_.reset();
  }
}

} // namespace bgplib
} // namespace nettools
} // namespace facebook
