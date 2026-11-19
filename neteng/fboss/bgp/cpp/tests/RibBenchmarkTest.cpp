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

#include <folly/Benchmark.h>
#include <folly/init/Init.h>
#include <folly/io/async/EventBase.h>

#include <neteng/fboss/bgp/cpp/common/RibMessage.h>
#include <neteng/fboss/bgp/cpp/tests/RibUtils.h>
#include "fboss/agent/Constants.h"

using namespace facebook::bgp;

class RibBenchmarkFixture {
 public:
  explicit RibBenchmarkFixture() {
    // Create global config
    facebook::bgp::BgpGlobalConfig bgpGlobalConfig(
        kAsn1, // localAsn
        kLocalAddr1, // routerId
        kPeerAddr3, // clusterId
        kHoldTime, // holdTime
        std::nullopt, // listenAddr
        kGrRestartTime, // grRestartTime
        {}, // networksV4
        {} // networksV6
    );

    // Create RIB
    rib_ = std::make_unique<MockRib>(
        std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>(),
        bgpGlobalConfig,
        std::nullopt, /* policy config */
        ribInQ_,
        ribOutQ_,
        kDevPlatform,
        nullptr /* fsdb syncer */);

    // Create RIB thread
    ribThread_ = std::thread([this]() { rib_->run(); });
    rib_->getEventBase().waitUntilRunning();
  }

  // Helper function to generate RibInAnnouncement with different prefixes
  std::vector<RibInMessage> generateRibInMessages(
      uint64_t count,
      bool isWithdrawal) {
    std::vector<RibInMessage> res;

    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs->publish();
    TinyPeerInfo peer(
        kLocalV6RoutePeerAddr, kLocalRouteAs, 0, BgpSessionType::IBGP, false);

    for (int i = 0; i < count; i++) {
      // Generate unique prefixes
      auto prefix =
          folly::CIDRNetwork(folly::IPAddress::fromLong(0x01000000 + i), 24);
      PrefixPathIds pfxPathIds;
      pfxPathIds.emplace_back(prefix, kDefaultPathID);
      if (isWithdrawal) {
        res.emplace_back(RibInWithdrawal(
            facebook::fboss::kDefaultVrf, peer, std::move(pfxPathIds)));
      } else {
        res.emplace_back(RibInAnnouncement(
            facebook::fboss::kDefaultVrf, peer, std::move(pfxPathIds), attrs));
      }
    }
    return res;
  }

  // Member variables
  std::unique_ptr<MockRib> rib_;
  std::thread ribThread_;
  facebook::nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ_{
      facebook::nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<RibOutMessage> ribOutQ_;
};

// Benchmark the RIB message processing loop with different numbers of prefixes
void BM_ProcessRibInMsgLoop(
    uint32_t iters,
    size_t numPrefixes,
    bool isWithdrawal) {
  auto suspender = folly::BenchmarkSuspender();
  RibBenchmarkFixture fixture;

  while (iters--) {
    // Generate announcement with specified number of prefixes
    auto messages = fixture.generateRibInMessages(numPrefixes, isWithdrawal);

    // Start benchmark timing
    suspender.dismiss();

    for (auto& message : messages) {
      // Send announcement to RIB
      fixture.ribInQ_.fiberPush(std::move(message));
    }

    // Wait until all messages are consumed from the queue
    while (fixture.ribInQ_.size() > 0) {
      std::this_thread::yield();
    }

    // Stop benchmark timing
    suspender.rehire();
  }

  // stop the thread and tearing down
  fixture.rib_->stop();
  fixture.ribThread_.join();
}

// Benchmark with different numbers of announcements
BENCHMARK_NAMED_PARAM(BM_ProcessRibInMsgLoop, Announcement_100, 100, false);
BENCHMARK_NAMED_PARAM(BM_ProcessRibInMsgLoop, Announcement_500, 500, false);
BENCHMARK_NAMED_PARAM(BM_ProcessRibInMsgLoop, Announcement_1000, 1000, false);
BENCHMARK_NAMED_PARAM(BM_ProcessRibInMsgLoop, Announcement_5000, 5000, false);
BENCHMARK_NAMED_PARAM(BM_ProcessRibInMsgLoop, Announcement_10000, 10000, false);
BENCHMARK_NAMED_PARAM(BM_ProcessRibInMsgLoop, Announcement_50000, 50000, false);
BENCHMARK_NAMED_PARAM(
    BM_ProcessRibInMsgLoop,
    Announcement_100000,
    100000,
    false);
BENCHMARK_NAMED_PARAM(
    BM_ProcessRibInMsgLoop,
    Announcement_500000,
    500000,
    false);

// Benchmark with different numbers of withdrawals
BENCHMARK_NAMED_PARAM(BM_ProcessRibInMsgLoop, Withdrawal_100, 100, true);
BENCHMARK_NAMED_PARAM(BM_ProcessRibInMsgLoop, Withdrawal_500, 500, true);
BENCHMARK_NAMED_PARAM(BM_ProcessRibInMsgLoop, Withdrawal_1000, 1000, true);
BENCHMARK_NAMED_PARAM(BM_ProcessRibInMsgLoop, Withdrawal_5000, 5000, true);
BENCHMARK_NAMED_PARAM(BM_ProcessRibInMsgLoop, Withdrawal_10000, 10000, true);
BENCHMARK_NAMED_PARAM(BM_ProcessRibInMsgLoop, Withdrawal_20000, 20000, true);

int main(int argc, char** argv) {
  // Initialize folly
  const folly::Init init(&argc, &argv);

  // Run the benchmarks
  folly::runBenchmarks();
  return 0;
}
