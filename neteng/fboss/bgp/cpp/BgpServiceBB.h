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

#include "neteng/fboss/bgp/cpp/BgpServiceBase.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NetlinkWrapper.h"

namespace facebook::bgp {

/**
 * BgpServiceBB — BB-specific (Express Backbone) Thrift service handler.
 *
 * Owns BB-specific peer management RPCs (addPeers / delPeers). DC-only RPCs are
 * not declared here; Thrift's default behavior applies if a client calls them
 * on the BB binary.
 *
 * BB also owns the NetlinkWrapper dependency: the base ctor builds a
 * HealthValidator without NetlinkWrapper, and BB's ctor body replaces it
 * with a NetlinkWrapper-aware HealthValidator. BB-specific augmentations to
 * Thrift handlers (e.g. NetlinkWrapper kernel-FIB counters in
 * co_getEntryStats) are layered via the BgpServiceBase virtual hooks rather
 * than by directly overriding the handler — this preserves request-dampening
 * semantics and avoids the facebook-thrift-handler-direct-call lint.
 */
class BgpServiceBB : public BgpServiceBase {
 public:
  BgpServiceBB(
      PeerManagerBase& peerMgr,
      std::shared_ptr<ConfigManager> configManager,
      RibBase& rib,
      Watchdog& watchdog,
      std::shared_ptr<NetlinkWrapper> nlWrapper,
      bool enable_thrift_protection);
  ~BgpServiceBB() override = default;

  /**
   * [Peer management] BB-only RPCs
   */
  folly::coro::Task<facebook::neteng::fboss::bgp::thrift::BgpConfigChangeResult>
  co_addPeers(
      std::unique_ptr<std::vector<facebook::bgp::thrift::BgpPeer>> peers)
      override;

  folly::coro::Task<facebook::neteng::fboss::bgp::thrift::BgpConfigChangeResult>
  co_delPeers(std::unique_ptr<std::vector<std::string>> peerAddrs) override;

 protected:
  /**
   * Layers NetlinkWrapper-derived kernel-FIB counters on top of the rib-
   * derived TEntryStats produced by BgpServiceBase::co_getEntryStats.
   * Called only when the base's request-dampening guards have passed.
   */
  void augmentEntryStatsForPlatform(
      facebook::neteng::fboss::bgp::thrift::TEntryStats& stats) override;

  /**
   * Set the link-up hold times without a restart. The netlink fiber reads the
   * new values at the next link-down.
   */
  void setNetlinkLinkUpHold(
      facebook::neteng::fboss::bgp::thrift::TResult& result,
      int32_t initialMs,
      int32_t maxMs) override;

  /** Show the link-up hold times that bgpd uses now. */
  void getNetlinkLinkUpHold(
      facebook::neteng::fboss::bgp::thrift::TNetlinkLinkUpHold& result)
      override;

 private:
  // Shared: reject CIDR and invalid IP addresses
  std::optional<folly::IPAddress> validatePeerAddress(
      const std::string& addrStr);

  /*
   * True when the peer is already configured exactly as requested, which lets
   * a retried addPeers succeed instead of failing validateAddedPeer.
   */
  bool isExistingIdenticalPeer(
      const facebook::bgp::thrift::BgpPeer& peer,
      const folly::F14FastSet<std::string_view>& allowedFields,
      const Config& currentConfig) const;

  bool validateAddedPeer(
      const facebook::bgp::thrift::BgpPeer& peer,
      const folly::F14FastSet<std::string_view>& allowedFields,
      const Config& currentConfig,
      std::set<folly::IPAddress>& newPeerAddrs);

  bool validateDeletedPeer(
      const std::string& addrStr,
      std::vector<folly::IPAddress>& validatedAddrs);

  // BB-only: owned NetlinkWrapper for kernel-FIB queries.
  std::shared_ptr<NetlinkWrapper> nlWrapper_;

#ifdef BgpServiceBB_TEST_FRIENDS
  BgpServiceBB_TEST_FRIENDS
#endif
};

} // namespace facebook::bgp
