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

#include <folly/FileUtil.h>
#include <folly/Singleton.h>
#include <folly/init/Init.h>
#include <folly/system/ThreadName.h>
#include <thrift/lib/cpp2/async/MultiplexAsyncProcessor.h>

#include <fb303/BaseService.h>
#include <fb303/ThreadCachedServiceData.h>

#include <fboss/lib/AlertLogger.h>
#include "magic_enum/magic_enum.hpp"
#include "neteng/fboss/bgp/cpp/BgpServiceDC.h"
#include "neteng/fboss/bgp/cpp/BgpServiceEventHandler.h"
#include "neteng/fboss/bgp/cpp/BgpServiceStream.h"
#include "neteng/fboss/bgp/cpp/BgpServiceUtil.h"
#include "neteng/fboss/bgp/cpp/MainUtil.h"
#include "neteng/fboss/bgp/cpp/MainUtilDC.h"
#include "neteng/fboss/bgp/cpp/common/BuildInfo.h"
#include "neteng/fboss/bgp/cpp/common/FeatureFlags.h"
#include "neteng/fboss/bgp/cpp/config/Config.h"
#include "neteng/fboss/bgp/cpp/config/ConfigManager.h"
#include "neteng/fboss/bgp/cpp/lib/coro/BackPressuredQueue.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopCache.h"
#include "neteng/fboss/bgp/cpp/peer/NeighborWatcher.h"
#include "neteng/fboss/bgp/cpp/peer/PeerManagerBase.h"
#include "neteng/fboss/bgp/cpp/peer/SessionManager.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyManager.h"
#include "neteng/fboss/bgp/cpp/rib/RibDC.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/stats/StatsDC.h"
#include "neteng/fboss/bgp/cpp/watchdog/MonitoredQueue.h"
#include "neteng/fboss/bgp/cpp/watchdog/Watchdog.h"

using namespace facebook::bgp;

using facebook::neteng::fboss::bgp::thrift::BgpInitializationEvent;

/*
 * Gflags in int32_t/int64_t
 */
DECLARE_int32(agent_thrift_port);
DECLARE_int32(agent_thrift_recv_timeout_ms);

DEFINE_int32(
    thrift_port,
    6909, // FBOSS Bgp thrift port number
    "port for thrift service");
DEFINE_int32(
    stream_port,
    6910, // FBOSS Bgp thrift stream port number
    "port for thrift streaming service");
DEFINE_int32(
    num_thrift_io_threads,
    1,
    "Number of I/O threads to be used by the Thrift server");
DEFINE_int32(
    num_thrift_stream_io_threads,
    1,
    "Number of I/O threads to be used by the Thrift stream server");
DEFINE_int32(bgp_policy_cache_size, 0, "Policy cache maximum size");
DEFINE_int32(
    max_thrift_requests,
    20,
    "Set the number of maximum active thrift requests");
DEFINE_int32(
    max_thrift_listen_backlog,
    20,
    "Set the number of maximum incoming connections that can queue up");
DEFINE_int32(
    max_thrift_connections,
    256,
    "Set the number of maximum incoming connections that can be active");
DEFINE_int32(
    fd_soft_limit,
    0, // if set to greater than 0, will override fd soft limit
    "Set the soft limit for file descriptors for this process");
/*
 * Gflags in string
 */
DEFINE_string(
    platform,
    kFbossPlatform,
    "The platform name. Supported platforms are: fboss, dev (for testing only)");
DEFINE_string(config, "", "File name of initial bgp configuration");
DEFINE_string(policy, "", "File name of initial bgp policy configuration");

namespace {
using BgpSignalHandler = facebook::bgp::BgpSignalHandler;

// fb303::BaseService has a protected constructor.
class BgpFb303Handler : public facebook::fb303::BaseService {
 public:
  BgpFb303Handler() : BaseService("bgpd") {}
};
} // namespace

int main(int argc, char** argv) {
  /*
   * [Version]
   *
   * Set version string for `bgpd_cpp --version`
   * NOTE: Must set the version info before any other functions which will touch
   * the command line args.
   */
  gflags::SetVersionString(facebook::bgp::BuildInfo::toDebugString());

  // parse command line flags, but do not remove them.
  gflags::ParseCommandLineFlags(&argc, &argv, false);

  // export buildInfo
  facebook::bgp::BuildInfo::exportBuildInfo();

  /*
   * existence of FLAGS_policy file must mean split config
   */
  bool splitConfigPolicy = false;
  if (FLAGS_policy != "") {
    int fd = folly::openNoInt(FLAGS_policy.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd != -1) {
      folly::closeNoInt(fd);
      splitConfigPolicy = true;
    }
  }

  /**
   *  Inside initFlagsFromConfig as soon as we create the Config object, it
   *  Causes a folly::Singleton to be vivified which causes an abort.
   *  This was not happening before because we were linked against libpython
   *  which caused the following behavior, now we do it explicitly here.
   */
  folly::SingletonVault::singleton()->setType(
      folly::SingletonVault::Type::Relaxed);

  /*
   * emit a signal for whether previous exit was planned or potentially a crash.
   * requires SingletonVault type to be Relaxed, and also should preferably be
   * as early as possible to avoid a crash before handling previous exit signal.
   */
  BgpStats::handlePreviousExit();

  // Override default values of gflags with optional value in config file
  const std::string emptyPolicy;
  facebook::bgp::initFlagsFromConfig(
      FLAGS_config, splitConfigPolicy ? FLAGS_policy : emptyPolicy);

  /*
   * Call folly::Init only after the call to initFlagsFromConfig().  This
   * will ensure that flags specified in command line override values
   * specified in config file
   */
  folly::Init init(&argc, &argv);

  // Set main thread name
  folly::setThreadName("bgpd_main");

  // Set file descriptor soft limit
  if (FLAGS_fd_soft_limit > 0) {
    facebook::bgp::setFileDescriptorSoftLimit(FLAGS_fd_soft_limit);
  }

  // Log BGP++ initialization event
  BgpStats::logInitializationEvent(
      "Main", BgpInitializationEvent::INITIALIZING);

  // Initialize counters
  initStatsBase();
  initStatsDC();
  if (splitConfigPolicy) {
    BgpStats::setPolicySymlink(1);
  } else {
    BgpStats::setPolicySymlink(0);
  }

  /*
   * Start the publish thread to periodically flush thread-cached stats.
   * ServiceFramework::go() normally starts this via ThriftStatsModuleLight.
   * Without ServiceFramework, we start it explicitly.
   */
  facebook::fb303::ThreadCachedServiceData::get()->startPublishThread(
      std::chrono::milliseconds{1000});

  /*
   * [Signal Handler]
   *
   * Register the signals to handle before anything else. This guarantees that
   * any threads created below will inherit the signal mask.
   */
  folly::EventBase signalHandlerEvb;
  auto signalHandler = std::make_unique<BgpSignalHandler>(&signalHandlerEvb);

  auto signalHandlerEvbThread =
      std::thread([&]() { signalHandlerEvb.loopForever(); });
  signalHandlerEvb.waitUntilRunning();

  /*
   * [Fib Service Waiting]
   *
   * BGP++ requires underneath platform to be ready to accept thrift request to
   * program routes.
   */
  if (FLAGS_platform == kFbossPlatform) {
    if (!facebook::bgp::waitForFibService(
            signalHandlerEvb,
            FLAGS_agent_thrift_port,
            FLAGS_agent_thrift_recv_timeout_ms)) {
      signalHandlerEvbThread.join();
      XLOGF(INFO, "Stopping BGP++ daemon: pid = {}", getpid());
      return 0;
    }
    XLOG(INFO, "Bgp FIB Agent is ready");
  } else if (FLAGS_platform == kDevPlatform) {
    // With kDevPlatform, no need to wait on platform agent for testing purpose.
  } else {
    throw BgpError("Unsupported platform, '", FLAGS_platform, "'");
  }
  BgpStats::logInitializationEvent(
      "Main", BgpInitializationEvent::AGENT_CONFIGURED);

  // Start config before creating the watchers so their optional next-hop
  // tracking dependencies can be derived from the configured global settings.
  std::shared_ptr<Config> config = std::make_shared<Config>(FLAGS_config);

  std::shared_ptr<NexthopCache> nexthopCache;
  std::shared_ptr<fboss::fsdb::FsdbCowStateSubManager> sharedFsdbSubMgr;
  if (config->getBgpGlobalConfig()->enableNextHopTracking) {
    nexthopCache = std::make_shared<NexthopCache>();
    /*
     * FsdbFibWatcher and NeighborWatcher share one COW subscription so that
     * interface neighbor updates and FIB host-route updates are processed by
     * the same event stream. This is required before enabling next-hop
     * tracking; without it the RIB receives a null cache and aborts on the
     * first route update.
     */
    sharedFsdbSubMgr =
        std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
            fboss::fsdb::SubscriptionOptions("bgpd"),
            fboss::utils::ConnectionOptions("::1", FLAGS_fsdbPort));
  }

  auto neighborEventQ =
      std::make_optional<MonitoredMPMCQueue<NeighborWatcherMessage>>();
  facebook::nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ{
      facebook::nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<RibOutMessage> ribOutQ;

  auto neighborWatcher = std::make_shared<NeighborWatcher>(
      *neighborEventQ,
      ribInQ,
      false, /* enableDsfFastTearDown */
      sharedFsdbSubMgr);
  if (nexthopCache) {
    neighborWatcher->learnConnectedNeighbors(nexthopCache, ribInQ);
    std::vector<folly::IPAddress> peerAddresses;
    peerAddresses.reserve(config->getPeerToConfig().size());
    for (const auto& [peerAddress, _] : config->getPeerToConfig()) {
      peerAddresses.push_back(peerAddress);
    }
    neighborWatcher->startFibWatcher(nexthopCache, ribInQ, peerAddresses);
  }
  auto neighborWatcherThread = neighborWatcher->runInThread();
  neighborWatcher->subscribe();

  Watchdog watchdog(config);
  auto watchdogThread = watchdog.runInThread();

  // Create ConfigManager with the config and config file path
  auto configManager = std::make_shared<ConfigManager>(config, FLAGS_config);

  // load configuration
  if (splitConfigPolicy) {
    XLOG(INFO, "Explicit BGP policy config input");
    // Validates JSON syntax via folly::parseJson() first
    config->setPolicyConfigFromFile(FLAGS_policy);
  }
  const auto& myConfig = config->getConfig();
  XLOGF(
      INFO,
      "{}Start bgp with router_id = {}, local_as = {}",
      facebook::fboss::BGPAlert().str(),
      *myConfig.router_id(),
      config->getBgpGlobalConfig()->localAsn);

  auto policyManager = Config::createPolicyManager(config);

  XLOGF(INFO, "Setting Policy Cache size to {}", FLAGS_bgp_policy_cache_size);
  AdjRibPolicyCache::get()->setCacheSize(FLAGS_bgp_policy_cache_size);

  /*
   * Start rib thread before peer thread
   * We restore RIB Policy from file in RIB ctor
   */
  RibDC rib(
      config->getLocalRoutes(),
      *(config->getBgpGlobalConfig()),
      config->getPolicies(),
      ribInQ, /* read from and write to ribInQ */
      ribOutQ, /* write to ribOutQ */
      FLAGS_platform,
      nullptr, /* no FSDB syncer for OSS */
      nexthopCache,
      FLAGS_agent_thrift_port,
      FLAGS_agent_thrift_recv_timeout_ms,
      neighborWatcher);
  watchdog.monitorModule(rib.getModuleName(), rib);
  auto ribThread = rib.runInThread();

  auto sessionMgr = std::make_shared<SessionManager>(
      *(config->getBgpGlobalConfig()),
      false, /* enableMessagesOverNotifyQueue */
      true); /* enableCoroNotifyQueue */

  // Start peer thread (plain PeerManagerBase, not PeerManagerDC)
  PeerManagerBase peerMgr(
      configManager,
      policyManager,
      ribInQ, /* write to ribInQ */
      ribOutQ, /* read from ribOutQ */
      neighborEventQ);
  peerMgr.setSessionManager(sessionMgr);
  watchdog.monitorModule(peerMgr.getModuleName(), peerMgr);

  /*
   * now that RIB Policy is loaded in RIB, we pass the route filter policy to
   * PeerManagerBase before AdjRibs are created
   */
  auto tRouteFilterPolicy = rib.getRouteFilterPolicy();

  // Validate peer group configuration in route filter policy
  PeerGroupValidationResult validationResult =
      isPeerGroupConfigValid(tRouteFilterPolicy, config->getPeerGroups());

  if (validationResult != PeerGroupValidationResult::SUCCESS) {
    throw BgpError(
        "Route filter policy validation failed: ",
        std::string(magic_enum::enum_name(validationResult)));
  }

  XLOG(INFO, "Route filter policy validation passed");

  peerMgr.setRouteFilterPolicy(
      std::make_unique<RouteFilterPolicy>(tRouteFilterPolicy));
  auto peerMgrThread = peerMgr.runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  // Setup the SSL policy
  std::shared_ptr<wangle::SSLContextConfig> sslContext;
  if (config->isThriftServerTlsEnabled()) {
    sslContext = std::make_shared<wangle::SSLContextConfig>();
    sslContext->setCertificate(
        config->getThriftServerCertPath(),
        config->getThriftServerKeyPath(),
        "");
    sslContext->clientCAFiles =
        std::vector<std::string>{config->getThriftServerCaPath()};
    sslContext->sessionContext = "bgpd";
    sslContext->setNextProtocols(
        **apache::thrift::ThriftServer::defaultNextProtocols());
    sslContext->clientVerification =
        getThriftServerClientVerification(*config->getBgpGlobalConfig());
    sslContext->eccCurveName = config->getThriftServerEccCurveName();
  }

  // Create BgpService ThriftServer directly (no ServiceFramework)
  auto bgpServer = facebook::bgp::makeThriftServer(
      "bgpd",
      sslContext,
      config->isThriftServerTlsEnabled(),
      getThriftServerSSLPolicy(*config->getBgpGlobalConfig()));
  auto processorEventHandler = std::make_shared<BgpServiceEventHandler>();
  apache::thrift::TProcessorBase::addProcessorEventHandler_deprecated(
      std::move(processorEventHandler));
  auto bgpHandler = std::make_shared<BgpServiceDC>(
      peerMgr, configManager, rib, neighborWatcher, watchdog, false);
  /*
   * Internally ServiceFramework serves fb303 for bgpd; this bare server has
   * none, so getCounters/getRegexCounters would land on the FacebookService
   * stubs TBgpService inherits and fail as unimplemented. The multiplex routes
   * each method to the first processor that declares it, so the fb303 handler
   * must come before bgpHandler, which declares those methods but implements
   * none of them.
   */
  std::vector<std::shared_ptr<apache::thrift::AsyncProcessorFactory>>
      bgpHandlers{std::make_shared<BgpFb303Handler>(), std::move(bgpHandler)};
  bgpServer->setInterface(
      std::make_shared<apache::thrift::MultiplexAsyncProcessorFactory>(
          std::move(bgpHandlers)));
  bgpServer->setPort(FLAGS_thrift_port);
  bgpServer->setMaxRequests(FLAGS_max_thrift_requests);
  bgpServer->setListenBacklog(FLAGS_max_thrift_listen_backlog);
  bgpServer->setMaxConnections(FLAGS_max_thrift_connections);
  if (FLAGS_num_thrift_io_threads > 0) {
    bgpServer->setNumIOWorkerThreads(FLAGS_num_thrift_io_threads);
  }

  // Create BgpServiceStream ThriftServer directly (no ServiceFramework)
  auto streamServer = facebook::bgp::makeThriftServer(
      "BgpStreamService",
      sslContext,
      config->isThriftServerTlsEnabled(),
      getThriftServerSSLPolicy(*config->getBgpGlobalConfig()));
  auto streamHandler = std::make_shared<BgpServiceStream>(&peerMgr);
  streamServer->setInterface(std::move(streamHandler));
  streamServer->setPort(FLAGS_stream_port);
  if (FLAGS_num_thrift_stream_io_threads > 0) {
    streamServer->setNumIOWorkerThreads(FLAGS_num_thrift_stream_io_threads);
  }

  // Start thrift servers directly in threads (no ServiceFramework)
  std::thread thriftServiceThread([&]() {
    XLOG(INFO, "Starting Thrift service...");
    bgpServer->serve();
  });

  std::thread streamServiceThread([&]() {
    XLOG(INFO, "Starting Stream service...");
    streamServer->serve();
  });

  // Main thread will be blocked waiting for signal to terminate
  signalHandlerEvbThread.join();

  XLOGF(INFO, "Stopping BGP++ daemon: pid = {}", getpid());

  // Stop thrift servers
  bgpServer->stop();
  streamServer->stop();

  /*
   * Queue has been closed by injecting null object
   * inside stop() method.
   */
  rib.stop();
  neighborWatcher->stop();

  /*
   * Save GR state BEFORE stopping SessionManager, which triggers
   * sessionTerminated() and clears establishedGrPeers_.
   */
  peerMgr.markDaemonShutdown();
  peerMgr.saveGrState();

  sessionMgr->stop();
  peerMgr.stop();
  watchdog.stop();

  // Wait for all threads to finish
  thriftServiceThread.join();
  streamServiceThread.join();
  ribThread.join();
  neighborWatcherThread.join();
  neighborWatcher.reset();
  peerMgrThread.join();
  sessionMgrThread.join();
  watchdogThread.join();

  XLOGF(INFO, "Successfully stopped BGP++ daemon: pid = {}", getpid());

  return 0;
}
