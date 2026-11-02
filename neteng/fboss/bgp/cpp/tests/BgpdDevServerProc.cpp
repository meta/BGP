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

#include <fmt/format.h>
#include <folly/synchronization/Baton.h>
#include <folly/system/Shell.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/EventBaseThread.h>
#include <folly/io/async/SSLContext.h>
#include <folly/io/async/SSLOptions.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp2/async/RocketClientChannel.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include "security/ca/lib/certpathpicker/CertPathPicker.h"

#include "configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h"
#include "neteng/fboss/bgp/cpp/tests/BgpdDevServerProc.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

using namespace std::chrono_literals;

namespace {
const auto kThriftConnTimeout = std::chrono::seconds(1);
const auto kThriftSendTimeout = std::chrono::seconds(5);
const auto kThriftRecvTimeout = std::chrono::seconds(5);
} // namespace

DEFINE_int32(
    bgpd_waiting_timeout_s,
    120,
    "The default waiting upper bound for bgpd turning on");

/*
 * This budget covers how long bgpd's startup lines take to surface through its
 * asynchronous logger, not just how long bgpd takes to initialize. On a loaded
 * host the two diverge sharply: bgpd has been observed ready in 13s with its
 * startup lines still surfacing 33s in. 60s leaves ~1.8x headroom over that
 * worst measured lag while keeping a genuine failure quick to report.
 */
DEFINE_int32(
    bgpd_initializing_timeout_s,
    60,
    "The default waiting upper bound for bgpd finishing initialization");

DEFINE_int64(
    bgpd_max_rss_size_mb,
    5000,
    "Set rlimit for bgpd RSS in MB for dev tests, default 5GB.");

// Anonymous namespace defining some related constants and util functions
namespace {
// The bgpd LOG print out the BGP thrift port
const std::string kBgpdThriftPortPrint{"The bgpd thrift port is "};

// The bgpd LOG print out the BGP protocol port
const std::string kBgpdProtocolPortPrint{"Running acceptor on "};

// The bgpd LOG print out when the bgpd starts initialization.
const std::string kBgpdStartInitializationPrint{
    apache::thrift::util::enumNameSafe(
        facebook::neteng::fboss::bgp::thrift::BgpInitializationEvent::
            INITIALIZING)};

// The bgpd LOG print out when the bgpd is ready.
const std::string kBgpdReadyPrint{apache::thrift::util::enumNameSafe(
    facebook::neteng::fboss::bgp::thrift::BgpInitializationEvent::EOR_SENT)};

// Util method to return an unexpected when bgpd is not started yet.
folly::Unexpected<std::string> makeUnexpectedBgpdIsNotStarted() {
  return folly::makeUnexpected("Bgpd is not started yet.");
}

} // namespace

namespace facebook::bgp {

BgpdDevServerProc::BgpdDevServerProc(
    const std::string& config,
    const std::string& policy,
    const std::string& bgpdLoc)
    : config_{config}, policy_{policy}, bgpdLoc_{bgpdLoc} {}

bool BgpdDevServerProc::allStartupSignalsCaptured() const noexcept {
  return isStarted_ && bgpPortParsed_ && thriftPortParsed_;
}

bool BgpdDevServerProc::run() {
  XLOGF(
      INFO, "Starting bgpd with config {} and policy {} ...", config_, policy_);

  auto bgpdCmd = fmt::format(
      "{} --config {} --policy {}"
      " --platform=dev --route_watcher=false "
      "--logging='.=DBG1;default:async=true' --thrift_port=0 "
      "--stream_port=0 --max_rss_size_mb_dev_test {}",
      bgpdLoc_,
      config_,
      ((policy_ != "") ? policy_ : "non-existent"),
      FLAGS_bgpd_max_rss_size_mb);

  bgpdProc_ = std::make_unique<folly::Subprocess>(
      std::move(bgpdCmd),
      folly::Subprocess::Options().pipeStdout().pipeStderr());

  folly::Baton<> bgpdStartInitializationBaton;
  folly::Baton<> bgpdReadyBaton;

  bgpdThread_ = std::make_unique<std::thread>(
      [this, &bgpdStartInitializationBaton, &bgpdReadyBaton] {
        bgpdProc_->communicate(
            folly::Subprocess::readLinesCallback(
                [this, &bgpdStartInitializationBaton, &bgpdReadyBaton](
                    int /*fd*/, folly::StringPiece s) {
                  // Printout the subprocess (bgpd) logs
                  XLOGF(INFO, "[bgpd] {}", s);
                  if (this->allStartupSignalsCaptured()) {
                    return false;
                  }

                  // Try to get the thrift port number
                  if (!this->thriftPortParsed_) {
                    auto pos = s.find(kBgpdThriftPortPrint);
                    if (pos != std::string::npos) {
                      thriftPort_ = folly::to<decltype(thriftPort_)>(
                          s.subpiece(pos + kBgpdThriftPortPrint.length()));
                      this->thriftPortParsed_ = true;
                      XLOGF(INFO, "Thrift port: {}", thriftPort_);
                    }
                  }

                  // Try to get the BGP protocol port number
                  if (!this->bgpPortParsed_) {
                    auto pos = s.find(kBgpdProtocolPortPrint);
                    if (pos != std::string::npos) {
                      auto portPos = s.rfind(':');
                      bgpProtocolPort_ = folly::to<decltype(bgpProtocolPort_)>(
                          s.subpiece(portPos + 1));
                      this->bgpPortParsed_ = true;
                      XLOGF(INFO, "BGP protocol port: {}", bgpProtocolPort_);
                    }
                  }

                  // Try to get the bgpd ready message
                  if (!this->isStarted_) {
                    if (s.contains(kBgpdStartInitializationPrint)) {
                      XLOG(INFO, "bgpd starts initialization.");
                      bgpdStartInitializationBaton.post();
                    }
                    if (s.contains(kBgpdReadyPrint)) {
                      this->isStarted_ = true;
                      XLOG(INFO, "bgpd is ready.");
                    }
                  }

                  // this needs to be at the end in case no more logs
                  if (this->allStartupSignalsCaptured()) {
                    XLOG(INFO, "All ports parsed and bgpd ready, returning.");
                    bgpdReadyBaton.post();
                  }

                  return false; // continue reading
                }),
            [](int /*pdf*/, int /*cfd*/) {
              return true; // No need to write
            });
      });

  /*
   * It would take some time to load bgpd into memory, which could take a long
   * time when the server stress-runs the experiments. To make fair comparison,
   * we should consider the two timeouts separately
   */
  if (!bgpdStartInitializationBaton.try_wait_for(
          std::chrono::seconds(FLAGS_bgpd_waiting_timeout_s))) {
    XLOGF(
        ERR,
        "BGP Dev Server fails to load in {} s.",
        FLAGS_bgpd_waiting_timeout_s);

    return false;
  }
  XLOG(INFO, "BGP Dev Server is loaded and starts initialization.");

  /*
   * We just use one baton for thrift port, bgp protocol port ready and bgpd
   * is ready, since the bgpd ready message would be printed out after thrift
   * and bgp protocol ports are ready. All three signals are indicated by
   * variable isStarted_.
   */
  if (!bgpdReadyBaton.try_wait_for(
          std::chrono::seconds(FLAGS_bgpd_initializing_timeout_s))) {
    XLOGF(
        ERR,
        "BGP Dev Server fails to initialize in {} s.",
        FLAGS_bgpd_initializing_timeout_s);

    return false;
  }

  XLOGF(INFO, "BGP Dev Server has started: {}", isStarted_.load());

  folly::EventBase socketEvb;
  auto thriftClient = getThriftClient(
      "::1", thriftPort_ /*thriftPort*/, &socketEvb, "localhost");

  auto hasPolicySymlink = thriftClient->sync_hasPolicySymlink();
  XLOGF(INFO, "Policy Symlink : {}", hasPolicySymlink);

  std::string configStr;
  thriftClient->sync_getPolicyConfig(configStr);

  matchPolicyConfig(policy_, configStr);
  if (policyMatched_) {
    XLOGF(INFO, "getPolicyConfig() matched : {}", policyMatched_.load());
  }

  /*
   * While at it, validate validateConfigAndPolicy service API
   */
  facebook::neteng::fboss::bgp::thrift::TResult result;
  bool hasPolicy = (policy_ != "");
  bool policySymlinkMatched = (hasPolicy == hasPolicySymlink);
  if (hasPolicy) {
    /*
     * Negative test: validating a policy-bearing config against an empty
     * policy must be rejected.
     */
    thriftClient->sync_validateConfigAndPolicy(result, config_, "");
    if (*result.success()) {
      XLOG(ERR, "validateConfigAndPolicy accepted an empty policy.");
      return false;
    }
    thriftClient->sync_validateConfigAndPolicy(result, config_, policy_);
  } else {
    thriftClient->sync_validateConfig(result, config_);
  }
  validateAPIPassed_ = *result.success();
  XLOGF(INFO, "validateConfigAndPolicy passed : {}", validateAPIPassed_.load());

  return (
      isStarted_ && policySymlinkMatched && policyMatched_ &&
      validateAPIPassed_);
}

folly::Expected<int, std::string> BgpdDevServerProc::getThriftPort() const {
  if (!isStarted_) {
    return makeUnexpectedBgpdIsNotStarted();
  }
  return folly::makeExpected<std::string>(folly::copy(thriftPort_));
}

folly::Expected<int, std::string> BgpdDevServerProc::getBgpProtocolPort()
    const {
  if (!isStarted_) {
    return makeUnexpectedBgpdIsNotStarted();
  }
  return folly::makeExpected<std::string>(folly::copy(bgpProtocolPort_));
}

/**
 * @brief  Create a necessary secure thrift client to BgpService.
 *         This client is used to make thrift calls to BGP daemon
 *
 * @param  ipv6_str IPv6 address (localhost ipv6 address in this case)
 *                  of the thrift server
 *
 * @param  thriftPort  thrift port of the server
 *
 * @param  evb  Temporary EVB base to bind thrift sockets to
 *
 * @param  devicename  Name of the device (in this case case "localhost"
 *
 * @return unique_ptr of thrift client
 */
std::unique_ptr<
    apache::thrift::Client<facebook::neteng::fboss::bgp::thrift::TBgpService>>
BgpdDevServerProc::getThriftClient(
    const std::string& ipv6_str,
    int thriftPort,
    folly::EventBase* evb,
    const std::string& devicename) {
  folly::SocketAddress sockAddr(ipv6_str, thriftPort);
  // secure client
  auto [certPath, keyPath] =
      facebook::security::CertPathPicker::getClientCredentialPaths(true);
  if (certPath.empty() || keyPath.empty()) {
    XLOGF(ERR, "empty cert or key => cert: {}, key: {}", certPath, keyPath);
    return nullptr;
  }
  auto ctx = std::make_shared<folly::SSLContext>();
  folly::ssl::SSLCommonOptions::setClientOptions(*ctx);
  ctx->loadCertificate(certPath.c_str());
  ctx->loadPrivateKey(keyPath.c_str());
  // Thrift's Rocket transport requires an ALPN
  ctx->setAdvertisedNextProtocols({"rs"});

  auto socket = folly::AsyncSSLSocket::UniquePtr(
      new folly::AsyncSSLSocket(std::move(ctx), evb));

  XLOGF(INFO, "Connecting to {}:{}  {}", ipv6_str, thriftPort, devicename);
  socket->connect(
      nullptr,
      sockAddr,
      std::chrono::duration_cast<std::chrono::milliseconds>(kThriftConnTimeout)
          .count());
  socket->setSendTimeout(
      std::chrono::duration_cast<std::chrono::milliseconds>(kThriftSendTimeout)
          .count());
  auto channel =
      apache::thrift::RocketClientChannel::newChannel(std::move(socket));
  channel->setTimeout(
      std::chrono::duration_cast<std::chrono::milliseconds>(kThriftRecvTimeout)
          .count());
  auto thriftClient = make_unique<apache::thrift::Client<
      facebook::neteng::fboss::bgp::thrift::TBgpService>>(std::move(channel));

  XLOGF(
      INFO,
      "Thrift client created {}:{}  {}",
      ipv6_str,
      thriftPort,
      devicename);
  return thriftClient;
}

/**
 * @brief  Make a thrift API call to bgp daemon to get the Policy Config
 *         it is running with and check if policy-names match with the
 *         policy config that was input to the BGP daemon
 *
 * @param  policy  A policy config file that is input to the BGP
 *                 daemon
 *
 * @param  configStr current configuration in JSON string format
 *
 * @return void
 */
void BgpdDevServerProc::matchPolicyConfig(
    const std::string& policy,
    const std::string& configStr) {
  if (policy == "") {
    this->policyMatched_ = true;
  }

  std::string contents;
  facebook::bgp::thrift::BgpConfig config;
  if (folly::readFile(policy_.c_str(), contents)) {
    auto jsonSerializer = apache::thrift::SimpleJSONSerializer();

    try {
      jsonSerializer.deserialize(contents, config);
    } catch (const std::exception& ex) {
      XLOGF(
          ERR,
          "Could not parse policy config file : {}",
          folly::exceptionStr(ex));
      throw;
    }

    if (config.policies().has_value() &&
        apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
            config.policies()->bgp_policy_statements())) {
      for (const auto& stmt : *config.policies()->bgp_policy_statements()) {
        auto pos = configStr.find(*stmt.name());
        if (pos != std::string::npos) {
          this->policyMatched_ = true;
        } else {
          XLOGF(INFO, " Do not find a match for: {}", *stmt.name());
          this->policyMatched_ = false;
          break;
        }
      }
    }
  }

  return;
}

void BgpdDevServerProc::stop() {
  CHECK(!stopFlag_);
  // signal all coroutines to stop
  stopFlag_.store(true, std::memory_order_relaxed);
  if (bgpdProc_) {
    bgpdProc_->sendSignal(SIGTERM);
    /*
     * To avoid data race, don't wait in thread.
     * bgpdProc_ will terminate when SIGTERM is called, then the stop will go
     * through.
     */
    bgpdProc_->wait();
  }
  if (bgpdThread_) {
    bgpdThread_->join();
  }
}
} // namespace facebook::bgp
