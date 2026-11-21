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

#include "configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h"
#include "neteng/fboss/bgp/cpp/common/Structs.h"
#include "neteng/fboss/bgp/cpp/config/ConfigStructs.h"
#include "neteng/fboss/bgp/if/gen-cpp2/BgpStructs_types.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::bgp {

class PolicyManager;

class Config {
 public:
  virtual ~Config() = default;

  explicit Config(
      const std::string& configFileName,
      std::function<
          std::optional<folly::F14NodeMap<folly::CIDRNetwork, int64_t>>()>
          fetchPeerSubnetLbwMap,
      bool populateConfigDb = true)
      : fetchPeerSubnetLbwMap_(std::move(fetchPeerSubnetLbwMap)) {
    // Validates JSON syntax via folly::parseJson() first
    setConfigFromFile(configFileName);
    if (populateConfigDb) {
      populateConfigDatabase();
    }
  }

  explicit Config(
      const std::string& configFileName,
      std::optional<folly::F14NodeMap<folly::CIDRNetwork, int64_t>>
          peerSubnetLbwMap = std::nullopt,
      bool populateConfigDb = true)
      : peerSubnetLbwMap_(std::move(peerSubnetLbwMap)) {
    // Validates JSON syntax via folly::parseJson() first
    setConfigFromFile(configFileName);
    if (populateConfigDb) {
      populateConfigDatabase();
    }
  }

  explicit Config(
      thrift::BgpConfig config,
      std::optional<folly::F14NodeMap<folly::CIDRNetwork, int64_t>>
          peerSubnetLbwMap = std::nullopt,
      bool splitConfigPolicy = false)
      : config_(std::move(config)),
        splitConfigPolicy_(splitConfigPolicy),
        peerSubnetLbwMap_(std::move(peerSubnetLbwMap)) {
    populateConfigDatabase();
  }

  explicit Config(
      const std::string& configFileName,
      const BgpSettings& tunables) {
    // Validates JSON syntax via folly::parseJson() first
    setConfigFromFile(configFileName);
    populateConfigDatabase(tunables);
  }

  explicit Config(thrift::BgpConfig config, const BgpSettings& tunables)
      : config_(std::move(config)) {
    populateConfigDatabase(tunables);
  }

  static void populatePeerConfigCounters(
      const folly::F14NodeMap<folly::IPAddress, std::shared_ptr<BgpPeerConfig>>&
          peerToConfig);

  PeeringParams getPeeringParamsForPeer(const BgpPeerConfig& peerConfig) const;
  PeeringParams getPeeringParamsForDynamicPeer(
      const BgpDynamicPeerConfig& dynamicPeerConfig) const;

  /*
   * Local routes. Only VRF 0 supports local routes today: RIB originates every
   * configured local route into the default VRF.
   */
  std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork> createLocalRoutes(
      const std::vector<thrift::BgpNetwork>& networks);
  std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork> getLocalRoutes();

  /*
   * Methods of BGP config accessor
   */

  // return flag indicating if BGP daemon running with split config input
  bool splitConfigPolicy() const {
    return splitConfigPolicy_;
  }

  // return raw bgp config thrift structure
  const thrift::BgpConfig& getConfig() const {
    return config_;
  }

  // return global settings of bgp config
  const std::shared_ptr<BgpGlobalConfig> getBgpGlobalConfig() const {
    return globalConfig_;
  }

  bool isUpdateGroupEnabled() const {
    return globalConfig_->enableUpdateGroup;
  }

  // return normal, aka, static peer -> config map
  const folly::F14NodeMap<
      folly::IPAddress /* peerAddr */,
      std::shared_ptr<BgpPeerConfig>>&
  getPeerToConfig() const {
    return peerToConfig_;
  }

  // return subnet range, aka, dynamic peer -> config map
  const folly::F14NodeMap<
      folly::CIDRNetwork /* peerPrefix */,
      std::shared_ptr<BgpDynamicPeerConfig>>&
  getDynamicPeerToConfig() const {
    return dynamicPeerToConfig_;
  }

  // return switch limit config
  const std::shared_ptr<thrift::BgpSwitchLimitConfig> getBgpSwitchLimitConfig()
      const {
    if (!globalConfig_->switchLimitConfig.has_value()) {
      return nullptr;
    }
    return std::make_shared<thrift::BgpSwitchLimitConfig>(
        *globalConfig_->switchLimitConfig);
  }

  // return thrift server config
  const std::shared_ptr<thrift::ThriftServerConfig> getThriftServerConfig()
      const {
    if (!globalConfig_->thriftServerConfig.has_value()) {
      return nullptr;
    }
    return std::make_shared<thrift::ThriftServerConfig>(
        *globalConfig_->thriftServerConfig);
  }

  const std::shared_ptr<thrift::MemoryProfilingConfig>
  getMemoryProfilingConfig() const {
    if (auto setting = config_.bgp_setting_config()) {
      if (auto profilingConfig = setting->memory_profiling_config()) {
        return std::make_shared<thrift::MemoryProfilingConfig>(
            *profilingConfig);
      }
    }
    // memory config not configured
    return nullptr;
  }

  /**
   * Returns whether TLS is enabled for the thrift server.
   *
   * @return true if thrift server config is present and enable_tls is true,
   *         false otherwise (including when no thrift server config is present)
   */
  bool isThriftServerTlsEnabled() const;

  /**
   * Returns the path to the CA certificate file for thrift server TLS.
   *
   * @return CA certificate file path if TLS is enabled and ca_path is
   * configured, empty string if TLS is disabled or no thrift server config
   * present
   * @throws BgpError if TLS is enabled but x509_ca_path is not configured
   */
  const std::string getThriftServerCaPath() const;

  /**
   * Returns the path to the server certificate file for thrift server TLS.
   *
   * @return Server certificate file path if TLS is enabled and cert_path is
   * configured, empty string if TLS is disabled or no thrift server config
   * present
   * @throws BgpError if TLS is enabled but x509_cert_path is not configured
   */
  const std::string getThriftServerCertPath() const;

  /**
   * Returns the path to the private key file for thrift server TLS.
   *
   * @return Private key file path if TLS is enabled and key_path is configured,
   *         empty string if TLS is disabled or no thrift server config present
   * @throws BgpError if TLS is enabled but x509_key_path is not configured
   */
  const std::string getThriftServerKeyPath() const;

  /**
   * Returns the ECC curve name for thrift server TLS.
   *
   * @return ECC curve name (e.g., "prime256v1", "secp384r1") if TLS is enabled
   *         and ecc_curve_name is configured,
   *         empty string if TLS is disabled or no thrift server config present
   * @throws BgpError if TLS is enabled but ecc_curve_name is not configured
   */
  const std::string getThriftServerEccCurveName() const;

  // return running config for display purpose
  std::string getRunningConfig() const;

  // return running policy config for display in JSON format
  std::string getPolicyConfig() const;

  /*
   * Methods of BGP Policy Config Operation
   */

  // Set policy content from a file to config variable
  void setPolicyConfigFromFile(const std::string& configFile);

  // reset policy fields from BgpConfig struct
  static void resetPolicyConfig(thrift::BgpConfig& config);

  // Determine if there is policy configuration
  bool arePoliciesConfigured() const;

  // Return the policy config
  std::optional<bgp_policy::BgpPolicies> getPolicies() const;

  // Return the peer groups map
  const folly::F14NodeMap<std::string, thrift::PeerGroup>& getPeerGroups()
      const {
    return peerGroups_;
  }

  /*
   * Get the common config of a peer address
   * (could be either static peer or dynamic peer)
   */
  std::optional<const BgpCommonPeerGroupConfig> getConfigOfAPeer(
      const folly::IPAddress& peerAddr) const;

  // populate internal config database from the BgpConfig thrift
  void populateConfigDatabase(
      const std::optional<const BgpSettings>& tunables = std::nullopt);

  // Verify policy names configured for each peer exist in policy configuration
  void verifyIfPoliciesExist(
      const std::shared_ptr<const PolicyManager>& policy) const;

  // Preserve the platform-specific config type during validation and updates.
  virtual std::shared_ptr<Config> createConfigFromFile(
      const std::string& configFileName) const;
  virtual std::shared_ptr<const Config> createConfig(
      thrift::BgpConfig config) const;

  static std::shared_ptr<PolicyManager> createPolicyManager(
      const std::shared_ptr<facebook::bgp::Config>& config);

  /*
   * helper functions to parse link bandwidth string with optional
   * suffix (K, M, G) to number, support following two falvors. return none if
   * parsing failure
   *
   * 1) return link speed in bits per second as int64_t
   * e.g "1G" -> 1,000,000,000
   */
  static std::optional<int64_t> getLinkBandwidthBps(const std::string& lbwStr);

  /*
   * 2) per RFC, lbw will be represented as float in Bytes Per Sec
   * "1G" -> 1.25e+8
   */
  std::optional<float> getLinkBandwidthBytesPerSec(
      const std::string& lbwStr,
      const thrift::BgpPeer& peer);

  // Check if a peer group exists in the configuration
  bool isPeerGroupConfigured(const std::string& peerGroupName) const;

  // Helper methods for validation - kept simple for internal validation needs
  bool validatePeerExists(const std::string& peerAddr) const;
  bool validatePeerGroupExists(const std::string& peerGroupName) const;

 protected:
  virtual void verifyPlatformPolicies(
      const std::shared_ptr<const PolicyManager>& /*policy*/) const {}

  thrift::BgpConfig config_;

 private:
  void setConfigFromFile(const std::string& configFile);

  PeeringParams getPeeringParamsHelper(
      const BgpCommonPeerGroupConfig& config) const;

  BgpCommonPeerGroupConfig createCommonPeerGroupConfig(
      const thrift::BgpPeer& peer);

  // create BgpUcmpQuantizer, throw if bad arguments were given
  static BgpUcmpQuantizer createBgpUcmpQuantizer(
      const thrift::BgpUcmpQuantizerConfig& quantizerConfig);

  std::optional<std::string> getDeviceName() const;

  folly::F14NodeMap<std::string, thrift::PeerGroup> peerGroups_;

  // If policies configured with separate input from config settings
  bool splitConfigPolicy_{false};

  /*
   * Config for global BGP setting/feature knob/etc.
   */
  std::shared_ptr<BgpGlobalConfig> globalConfig_;

  /*
   * Config for statically configured peers. Mapped by a peer's IPAddress.
   */
  folly::F14NodeMap<
      folly::IPAddress /* peerAddr */,
      std::shared_ptr<BgpPeerConfig>>
      peerToConfig_;

  /*
   * Config for dynamically configured peers. Mapped by a range of subnet.
   */
  folly::F14NodeMap<
      folly::CIDRNetwork /* peerPrefix */,
      std::shared_ptr<BgpDynamicPeerConfig>>
      dynamicPeerToConfig_;

  /*
   * UCMP auto link bandwidth calculation. Mapped the peer address subnet to
   * the aggregated link bandwidth.
   */
  std::optional<folly::F14NodeMap<
      folly::CIDRNetwork /* peer subnet */,
      int64_t /* link bandwidth mbps */>>
      peerSubnetLbwMap_;

  std::function<std::optional<folly::F14NodeMap<folly::CIDRNetwork, int64_t>>()>
      fetchPeerSubnetLbwMap_ = [this]() {
        peerSubnetLbwMapFetched_ = true;
        return std::nullopt;
      };

  bool peerSubnetLbwMapFetched_{false};

  /**
   * Maximum number of dynamic peers allowed
   */
  uint32_t dynamicPeerLimit_{1000};

  /**
   * Default maximum number of stream subscribers. Replaced only when the
   * stream_subscriber_limit feature is enabled and the bgp_setting layer
   * carries a positive stream_subscriber_limit value.
   */
  uint32_t streamSubscriberLimit_{2};

#ifdef Config_TEST_FRIENDS
  Config_TEST_FRIENDS
#endif
#ifdef StreamSubscriber_TEST_FRIENDS
      StreamSubscriber_TEST_FRIENDS
#endif
};

} // namespace facebook::bgp
