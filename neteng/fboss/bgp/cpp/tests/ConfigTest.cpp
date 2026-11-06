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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h"

#define Config_TEST_FRIENDS                                   \
  FRIEND_TEST(BgpUcmpQuantizerFixture, BgpUcmpQuantizerTest); \
  FRIEND_TEST(ConfigTestFixture, DynamicPeerLimitConfigTest); \
  FRIEND_TEST(ConfigTestFixture, StreamSubscriberLimitConfigTest);

#include <folly/logging/xlog.h>

#include <fb303/ThreadCachedServiceData.h>

#include <cmath>
#include "common/network/AddressUtil.h"
#include "configerator/structs/neteng/fboss/bgp/if/gen-cpp2/bgp_attr_types.h"
#include "neteng/fboss/bgp/cpp/common/BgpError.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/FeatureFlags.h"
#include "neteng/fboss/bgp/cpp/config/Config.h"
#include "neteng/fboss/bgp/cpp/config/ConfigBB.h"
#include "neteng/fboss/bgp/cpp/config/ConfigUtils.h"
#include "neteng/fboss/bgp/cpp/config/ThriftServerUtils.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyManager.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/ConfigTestFixture.h"
#include "neteng/fboss/bgp/cpp/tests/PolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::bgp {

using namespace facebook::nettools::bgplib;

using ::testing::UnorderedElementsAre;

using facebook::nettools::bgplib::constants::kBgpPort;
using folly::IPAddress;
using std::string;
using std::vector;

thrift::BgpNetwork createBgpNetwork(
    const string& prefix,
    const std::optional<vector<string>>& communities = std::nullopt,
    const std::optional<string>& policyName = std::nullopt) {
  thrift::BgpNetwork bgpNetwork;
  bgpNetwork.prefix() = prefix;

  if (communities) {
    bgpNetwork.communities() = *communities;
  }

  if (policyName) {
    bgpNetwork.policy_name() = *policyName;
  }
  return bgpNetwork;
}

std::optional<std::string> validateConfigGetError(
    const Config& config,
    const std::shared_ptr<const PolicyManager> policy) {
  try {
    config.verifyIfPoliciesExist(policy);
  } catch (const BgpError& error) {
    return *error.message();
  }
  return std::nullopt;
}

TEST_F(ConfigTestFixture, setConfigFromFileOldAsnTest) {
  /*
   * Test to ensure we will still use old asn field properly
   * TODO: deprecate i32 asns fields T113736668
   */
  string configFile =
      "neteng/fboss/bgp/cpp/tests/sample_configs/bgpd_old_asn_field.conf";
  auto configFilePath = getAbsoluteFilePath(configFile);
  Config config(configFilePath);
  auto myConfig = config.getConfig();
  // test i32 asn related fields
  EXPECT_EQ(64551, *myConfig.local_as());
  EXPECT_EQ(1234, *myConfig.local_confed_as());

  auto peerConf =
      config.getConfigOfAPeer(folly::IPAddress("10.46.24.42")).value();

  EXPECT_EQ(64551, peerConf.peerAsn);
}

TEST_F(ConfigTestFixture, setConfigFromFileTest) {
  string configFile = "neteng/fboss/bgp/cpp/tests/sample_configs/bgpd.conf";
  auto configFilePath = getAbsoluteFilePath(configFile);
  Config config(configFilePath);
  auto myConfig = config.getConfig();
  // test a few selected configs
  EXPECT_EQ("10.46.0.41", *myConfig.router_id());
  EXPECT_EQ(12364551, *myConfig.local_as_4_byte());
  EXPECT_EQ(10064551, *myConfig.local_confed_as_4_byte());
  EXPECT_EQ(1, myConfig.networks4()->size());
  EXPECT_EQ(2, myConfig.networks6()->size());
  EXPECT_EQ(13, myConfig.peers()->size());

  auto peer1Conf = config.getConfigOfAPeer(folly::IPAddress("::1")).value();
  auto peer2Conf = config.getConfigOfAPeer(folly::IPAddress("::2")).value();
  auto peer3Conf =
      config.getConfigOfAPeer(folly::IPAddress("10.46.24.42")).value();

  EXPECT_EQ(AddPath::RECEIVE, peer1Conf.addPath.value());
  EXPECT_EQ(AddPath::SEND, peer2Conf.addPath.value());
  EXPECT_EQ(std::nullopt, peer3Conf.addPath);

  // per peer overwrite
  EXPECT_EQ(false, peer1Conf.v4OverV6Nexthop.value());
  // peer group A config
  EXPECT_EQ(true, peer2Conf.v4OverV6Nexthop.value());
  // not set
  EXPECT_EQ(std::nullopt, peer3Conf.v4OverV6Nexthop);

  // per peer overwrite
  EXPECT_EQ(false, peer1Conf.isRedistributePeer.value());
  // peer group A config
  EXPECT_EQ(true, peer2Conf.isRedistributePeer.value());
  // not set
  EXPECT_EQ(std::nullopt, peer3Conf.isRedistributePeer);

  // Asns
  EXPECT_EQ(64551, peer1Conf.peerAsn);
  EXPECT_EQ(1064551, peer3Conf.peerAsn);
}

TEST_F(ConfigTestFixture, setConfigFromMissingFileTest) {
  // Verify that we do not do FATAL instead will throw if file is missing.
  string configFile = "non-existing.conf";
  auto configFilePath = getAbsoluteFilePath(configFile);
  EXPECT_ANY_THROW(Config config(configFilePath));
}

TEST_F(ConfigTestFixture, setConfigTest) {
  // create a new config
  thrift::BgpConfig myNewConfig;
  myNewConfig.router_id() = "1.1.1.1";
  myNewConfig.local_as_4_byte() = 100;
  auto network4_1 = createBgpNetwork("1.1.1.0/24");
  auto network4_2 =
      createBgpNetwork("1.1.2.0/24", vector<string>({"100:1", "100:2"}));
  myNewConfig.networks4()->emplace_back(network4_1);
  myNewConfig.networks4()->emplace_back(network4_2);
  auto network6_1 =
      createBgpNetwork("1::1:0/120", vector<string>({"100:1", "100:2"}));
  myNewConfig.networks6()->emplace_back(network6_1);
  Config config(myNewConfig);
  auto myConfig = config.getConfig();

  // test setConfig
  EXPECT_EQ("1.1.1.1", *myConfig.router_id());
  EXPECT_EQ(100, *myConfig.local_as_4_byte());
  EXPECT_EQ(2, myConfig.networks4()->size());
  EXPECT_THAT(
      *myConfig.networks4(), UnorderedElementsAre(network4_1, network4_2));
  EXPECT_EQ(1, myConfig.networks6()->size());
  EXPECT_THAT(*myConfig.networks6(), UnorderedElementsAre(network6_1));
  EXPECT_EQ(0, myConfig.peers()->size());
}

TEST_F(ConfigTestFixture, globalConfigTest) {
  thrift::BgpConfig thriftConfig;
  thriftConfig.router_id() = kLocalAddr1.str();

  // default settings
  {
    Config config(thriftConfig);
    auto globalConfig = config.getBgpGlobalConfig();
    /*
     * compute_ucmp_from_link_bandwidth_community is not set
     * verify computeUcmpFromLbwComm defaults to FALSE
     */
    EXPECT_FALSE(globalConfig->computeUcmpFromLbwComm);
    /*
     * count_confeds_in_as_path_len is not set
     * verify countConfedsInAsPathLen defaults to FALSE
     */
    EXPECT_FALSE(globalConfig->countConfedsInAsPathLen);
  }

  // compute_ucmp_from_link_bandwidth_community & count_confeds_in_as_path_len
  {
    // compute_ucmp_from_link_bandwidth_community = false
    thriftConfig.compute_ucmp_from_link_bandwidth_community() = false;
    // count_confeds_in_as_path_len = false
    thriftConfig.count_confeds_in_as_path_len() = false;

    Config config(thriftConfig);
    auto globalConfig = config.getBgpGlobalConfig();

    EXPECT_FALSE(globalConfig->computeUcmpFromLbwComm);
    EXPECT_FALSE(globalConfig->countConfedsInAsPathLen);
  }
  {
    // compute_ucmp_from_link_bandwidth_community = true
    thriftConfig.compute_ucmp_from_link_bandwidth_community() = true;
    // count_confeds_in_as_path_len = true
    thriftConfig.count_confeds_in_as_path_len() = true;

    Config config(thriftConfig);
    auto globalConfig = config.getBgpGlobalConfig();

    EXPECT_TRUE(globalConfig->computeUcmpFromLbwComm);
    EXPECT_TRUE(globalConfig->countConfedsInAsPathLen);
  }
}

TEST_F(ConfigTestFixture, FibOutTrackingFeature) {
  thrift::BgpConfig thriftConfig;
  thriftConfig.router_id() = kLocalAddr1.str();

  Config disabledConfig(thriftConfig);
  EXPECT_FALSE(disabledConfig.getBgpGlobalConfig()->enableFibOutTracking);

  thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
  thriftConfig.bgp_setting_config()->enable_fib_out_tracking() = false;
  Config explicitlyDisabledConfig(thriftConfig);
  EXPECT_FALSE(
      explicitlyDisabledConfig.getBgpGlobalConfig()->enableFibOutTracking);

  thriftConfig.bgp_setting_config()->enable_fib_out_tracking() = true;
  Config enabledConfig(thriftConfig);
  EXPECT_TRUE(enabledConfig.getBgpGlobalConfig()->enableFibOutTracking);
}

TEST_F(ConfigTestFixture, GrNotificationFeature) {
  thrift::BgpConfig thriftConfig;
  thriftConfig.router_id() = kLocalAddr1.str();

  Config disabledConfig(thriftConfig);
  EXPECT_FALSE(disabledConfig.getBgpGlobalConfig()->enableGrNotification);

  thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
  thriftConfig.bgp_setting_config()->enable_gr_notification() = false;
  Config explicitlyDisabledConfig(thriftConfig);
  EXPECT_FALSE(
      explicitlyDisabledConfig.getBgpGlobalConfig()->enableGrNotification);

  thriftConfig.bgp_setting_config()->enable_gr_notification() = true;
  Config enabledConfig(thriftConfig);
  EXPECT_TRUE(enabledConfig.getBgpGlobalConfig()->enableGrNotification);
}

/*
 * This test verifies the parsing of ThriftServerConfig from the raw
 * thrift::BgpConfig to BgpGlobalConfig with different thrift server field
 * settings, and tests both direct access and getter methods.
 */
TEST_F(ConfigTestFixture, enableAddPathGrReconcileTest) {
  thrift::BgpConfig thriftConfig;
  thriftConfig.router_id() = kLocalAddr1.str();

  // Default: no bgp_setting_config -> flag defaults to false.
  {
    Config config(thriftConfig);
    EXPECT_FALSE(config.getBgpGlobalConfig()->enableAddPathGrReconcile);
  }

  // bgp_setting_config present but flag unset -> still false.
  {
    thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
    Config config(thriftConfig);
    EXPECT_FALSE(config.getBgpGlobalConfig()->enableAddPathGrReconcile);
  }

  // Flag set true -> propagates to BgpGlobalConfig.
  {
    thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
    thriftConfig.bgp_setting_config()->enable_addpath_gr_reconcile() = true;
    Config config(thriftConfig);
    EXPECT_TRUE(config.getBgpGlobalConfig()->enableAddPathGrReconcile);
  }

  // Flag set false -> false.
  {
    thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
    thriftConfig.bgp_setting_config()->enable_addpath_gr_reconcile() = false;
    Config config(thriftConfig);
    EXPECT_FALSE(config.getBgpGlobalConfig()->enableAddPathGrReconcile);
  }
}

/*
 * Verifies enable_legacy_v4_nlri_encoding parses from thrift::BgpConfig into
 * BgpGlobalConfig with default-off semantics: unset (with or without
 * bgp_setting_config) stays false, an explicit true propagates, and an
 * explicit false stays false.
 */
TEST_F(ConfigTestFixture, enableLegacyV4NlriEncodingTest) {
  thrift::BgpConfig thriftConfig;
  thriftConfig.router_id() = kLocalAddr1.str();

  // Default: no bgp_setting_config -> flag defaults to false.
  {
    Config config(thriftConfig);
    EXPECT_FALSE(config.getBgpGlobalConfig()->enableLegacyV4NlriEncoding);
  }

  // bgp_setting_config present but flag unset -> still false.
  {
    thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
    Config config(thriftConfig);
    EXPECT_FALSE(config.getBgpGlobalConfig()->enableLegacyV4NlriEncoding);
  }

  // Flag set true -> propagates to BgpGlobalConfig.
  {
    thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
    thriftConfig.bgp_setting_config()->enable_legacy_v4_nlri_encoding() = true;
    Config config(thriftConfig);
    EXPECT_TRUE(config.getBgpGlobalConfig()->enableLegacyV4NlriEncoding);
  }

  // Flag set false -> false.
  {
    thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
    thriftConfig.bgp_setting_config()->enable_legacy_v4_nlri_encoding() = false;
    Config config(thriftConfig);
    EXPECT_FALSE(config.getBgpGlobalConfig()->enableLegacyV4NlriEncoding);
  }
}

/*
 * This test checks that Config reads enable_stream_subscriber_backpressure
 * from thrift::BgpConfig into BgpGlobalConfig. The result has three states.
 *
 * The field is deprecated: bgp++ always uses the bounded egress path of the
 * MP-BGP monitor and ignores the value.
 */
TEST_F(ConfigTestFixture, enableStreamSubscriberBackpressureTest) {
  thrift::BgpConfig thriftConfig;
  thriftConfig.router_id() = kLocalAddr1.str();

  // The config has no bgp_setting_config. The value stays unset.
  {
    Config config(thriftConfig);
    EXPECT_FALSE(config.getBgpGlobalConfig()
                     ->enableStreamSubscriberBackpressure.has_value());
  }

  /*
   * bgp_setting_config is present but the field is unset. The value stays
   * unset.
   */
  {
    thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
    Config config(thriftConfig);
    EXPECT_FALSE(config.getBgpGlobalConfig()
                     ->enableStreamSubscriberBackpressure.has_value());
  }

  // The config sets true. The value is set and it is true.
  {
    thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
    thriftConfig.bgp_setting_config()->enable_stream_subscriber_backpressure() =
        true;
    Config config(thriftConfig);
    const auto& backpressure =
        config.getBgpGlobalConfig()->enableStreamSubscriberBackpressure;
    ASSERT_TRUE(backpressure.has_value());
    EXPECT_TRUE(*backpressure);
  }

  /*
   * The config sets false. The value is set and it is false. This state is
   * different from the unset state above.
   */
  {
    thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
    thriftConfig.bgp_setting_config()->enable_stream_subscriber_backpressure() =
        false;
    Config config(thriftConfig);
    const auto& backpressure =
        config.getBgpGlobalConfig()->enableStreamSubscriberBackpressure;
    ASSERT_TRUE(backpressure.has_value());
    EXPECT_FALSE(*backpressure);
  }
}

TEST_F(ConfigTestFixture, ThriftServerConfigTest) {
  thrift::BgpConfig bgpConfig;
  bgpConfig.router_id() = kLocalAddr1.str();

  {
    // test 1: no thrift server config specified
    Config config(bgpConfig);
    auto globalConfig = config.getBgpGlobalConfig();

    // Test direct access
    EXPECT_FALSE(globalConfig->thriftServerConfig.has_value());

    // Test getter methods
    EXPECT_EQ(nullptr, config.getThriftServerConfig());
    EXPECT_FALSE(config.isThriftServerTlsEnabled());

    /*
     * These should return empty strings since ThriftServerConfig is not
     * configured
     */
    EXPECT_EQ("", config.getThriftServerCaPath());
    EXPECT_EQ("", config.getThriftServerCertPath());
    EXPECT_EQ("", config.getThriftServerKeyPath());
    EXPECT_EQ("", config.getThriftServerEccCurveName());

    EXPECT_EQ(
        apache::thrift::SSLPolicy::DISABLED,
        getThriftServerSSLPolicy(*config.getBgpGlobalConfig()));
  }
  {
    // test 2: thrift server config specified but TLS disabled
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = false;
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    auto globalConfig = config.getBgpGlobalConfig();

    // Test direct access
    EXPECT_TRUE(globalConfig->thriftServerConfig.has_value());
    EXPECT_FALSE(*globalConfig->thriftServerConfig->enable_tls());
    EXPECT_FALSE(globalConfig->thriftServerConfig->x509_ca_path().has_value());
    EXPECT_FALSE(
        globalConfig->thriftServerConfig->x509_cert_path().has_value());
    EXPECT_FALSE(globalConfig->thriftServerConfig->x509_key_path().has_value());

    // Test getter methods
    auto thriftConfig = config.getThriftServerConfig();
    EXPECT_NE(nullptr, thriftConfig);
    EXPECT_FALSE(*thriftConfig->enable_tls());
    EXPECT_FALSE(config.isThriftServerTlsEnabled());

    // These should return empty strings since TLS is disabled
    EXPECT_EQ("", config.getThriftServerCaPath());
    EXPECT_EQ("", config.getThriftServerCertPath());
    EXPECT_EQ("", config.getThriftServerKeyPath());
    EXPECT_EQ("", config.getThriftServerEccCurveName());

    EXPECT_EQ(
        apache::thrift::SSLPolicy::DISABLED,
        getThriftServerSSLPolicy(*config.getBgpGlobalConfig()));
  }
  {
    /*
     * test 3: thrift server config specified with TLS enabled and all required
     * fields
     */
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    thriftServerConfig.x509_ca_path() = "/path/to/ca.pem";
    thriftServerConfig.x509_cert_path() = "/path/to/cert.pem";
    thriftServerConfig.x509_key_path() = "/path/to/key.pem";
    thriftServerConfig.ecc_curve_name() = "prime256v1";
    thriftServerConfig.verify_client_type() =
        facebook::bgp::thrift::VerifyClientType::ALWAYS;
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    auto globalConfig = config.getBgpGlobalConfig();

    // Test direct access
    EXPECT_TRUE(globalConfig->thriftServerConfig.has_value());
    EXPECT_TRUE(*globalConfig->thriftServerConfig->enable_tls());
    EXPECT_EQ(
        "/path/to/ca.pem", *globalConfig->thriftServerConfig->x509_ca_path());
    EXPECT_EQ(
        "/path/to/cert.pem",
        *globalConfig->thriftServerConfig->x509_cert_path());
    EXPECT_EQ(
        "/path/to/key.pem", *globalConfig->thriftServerConfig->x509_key_path());
    EXPECT_EQ(
        "prime256v1", *globalConfig->thriftServerConfig->ecc_curve_name());
    EXPECT_EQ(
        facebook::bgp::thrift::VerifyClientType::ALWAYS,
        *globalConfig->thriftServerConfig->verify_client_type());

    // Test getter methods
    auto thriftConfig = config.getThriftServerConfig();
    EXPECT_NE(nullptr, thriftConfig);
    EXPECT_TRUE(*thriftConfig->enable_tls());
    EXPECT_TRUE(config.isThriftServerTlsEnabled());
    EXPECT_EQ("/path/to/ca.pem", config.getThriftServerCaPath());
    EXPECT_EQ("/path/to/cert.pem", config.getThriftServerCertPath());
    EXPECT_EQ("/path/to/key.pem", config.getThriftServerKeyPath());
    EXPECT_EQ("prime256v1", config.getThriftServerEccCurveName());
    EXPECT_EQ(
        apache::thrift::SSLPolicy::REQUIRED,
        getThriftServerSSLPolicy(*config.getBgpGlobalConfig()));
  }
  {
    // test 4: thrift server config with optional fields
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    thriftServerConfig.x509_ca_path() = "/path/to/ca.pem";
    thriftServerConfig.x509_cert_path() = "/path/to/cert.pem";
    thriftServerConfig.x509_key_path() = "/path/to/key.pem";
    thriftServerConfig.verify_client_type() =
        facebook::bgp::thrift::VerifyClientType::IF_PRESENTED;
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    auto globalConfig = config.getBgpGlobalConfig();

    // Test direct access
    EXPECT_TRUE(globalConfig->thriftServerConfig.has_value());
    EXPECT_TRUE(*globalConfig->thriftServerConfig->enable_tls());
    EXPECT_EQ(
        facebook::bgp::thrift::VerifyClientType::IF_PRESENTED,
        *globalConfig->thriftServerConfig->verify_client_type());
    EXPECT_FALSE(
        globalConfig->thriftServerConfig->ecc_curve_name().has_value());

    // Test getter methods
    auto thriftConfig = config.getThriftServerConfig();
    EXPECT_NE(nullptr, thriftConfig);
    EXPECT_TRUE(*thriftConfig->enable_tls());
    EXPECT_TRUE(config.isThriftServerTlsEnabled());
    EXPECT_EQ("/path/to/ca.pem", config.getThriftServerCaPath());
    EXPECT_EQ("/path/to/cert.pem", config.getThriftServerCertPath());
    EXPECT_EQ("/path/to/key.pem", config.getThriftServerKeyPath());
    EXPECT_THROW(config.getThriftServerEccCurveName(), BgpError);
    EXPECT_EQ(
        apache::thrift::SSLPolicy::PERMITTED,
        getThriftServerSSLPolicy(*config.getBgpGlobalConfig()));
  }
}

/*
 * This test verifies the getNetServiceConfig() method with different
 * net_service_config field settings.
 */
TEST_F(ConfigTestFixture, NetServiceConfigTest) {
  thrift::BgpConfig bgpConfig;
  bgpConfig.router_id() = kLocalAddr1.str();

  {
    // test 1: no net service config specified
    ConfigBB config(bgpConfig);

    // Test getter method returns nullptr when config is not present
    EXPECT_THAT(config.getNetServiceConfig(), ::testing::IsNull());
  }

  {
    // test 2: net service config specified
    thrift::BgpNetServiceThriftConfig netServiceConfig;
    bgpConfig.net_service_config() = netServiceConfig;

    ConfigBB config(bgpConfig);

    // Test getter method returns non-null shared_ptr when config is present
    std::shared_ptr<thrift::BgpNetServiceThriftConfig> netSvcConfig =
        config.getNetServiceConfig();
    EXPECT_THAT(netSvcConfig, ::testing::NotNull());
  }
}

/*
 * Test isThriftServerTlsEnabled() function.
 */
TEST_F(ConfigTestFixture, IsThriftServerTlsEnabledTest) {
  thrift::BgpConfig bgpConfig;
  bgpConfig.router_id() = kLocalAddr1.str();

  {
    // test 1: no thrift server config specified
    Config config(bgpConfig);
    EXPECT_FALSE(config.isThriftServerTlsEnabled());
  }

  {
    // test 2: thrift server config specified but TLS disabled
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = false;
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_FALSE(config.isThriftServerTlsEnabled());
  }

  {
    // test 3: thrift server config specified with TLS enabled
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_TRUE(config.isThriftServerTlsEnabled());
  }
}

/*
 * Test getThriftServerCaPath() function.
 */
TEST_F(ConfigTestFixture, GetThriftServerCaPathTest) {
  thrift::BgpConfig bgpConfig;
  bgpConfig.router_id() = kLocalAddr1.str();

  {
    // test 1: no thrift server config specified
    Config config(bgpConfig);
    EXPECT_EQ("", config.getThriftServerCaPath());
  }

  {
    // test 2: thrift server config specified but ca_path not set
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_THROW(config.getThriftServerCaPath(), BgpError);
  }

  {
    // test 3: thrift server config specified with ca_path
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    thriftServerConfig.x509_ca_path() = "/path/to/ca.pem";
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_EQ("/path/to/ca.pem", config.getThriftServerCaPath());
  }
}

/*
 * Test getThriftServerCertPath() function.
 */
TEST_F(ConfigTestFixture, GetThriftServerCertPathTest) {
  thrift::BgpConfig bgpConfig;
  bgpConfig.router_id() = kLocalAddr1.str();

  {
    // test 1: no thrift server config specified
    Config config(bgpConfig);
    EXPECT_EQ("", config.getThriftServerCertPath());
  }

  {
    // test 2: thrift server config specified but cert_path not set
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_THROW(config.getThriftServerCertPath(), BgpError);
  }

  {
    // test 3: thrift server config specified with cert_path
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    thriftServerConfig.x509_cert_path() = "/path/to/cert.pem";
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_EQ("/path/to/cert.pem", config.getThriftServerCertPath());
  }
}

/*
 * Test getThriftServerKeyPath() function.
 */
TEST_F(ConfigTestFixture, GetThriftServerKeyPathTest) {
  thrift::BgpConfig bgpConfig;
  bgpConfig.router_id() = kLocalAddr1.str();

  {
    // test 1: no thrift server config specified
    Config config(bgpConfig);
    EXPECT_EQ("", config.getThriftServerKeyPath());
  }

  {
    // test 2: thrift server config specified but key_path not set
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_THROW(config.getThriftServerKeyPath(), BgpError);
  }

  {
    // test 3: thrift server config specified with key_path
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    thriftServerConfig.x509_key_path() = "/path/to/key.pem";
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_EQ("/path/to/key.pem", config.getThriftServerKeyPath());
  }
}

/*
 * Test getThriftServerEccCurveName() function.
 */
TEST_F(ConfigTestFixture, GetThriftServerEccCurveNameTest) {
  thrift::BgpConfig bgpConfig;
  bgpConfig.router_id() = kLocalAddr1.str();

  {
    // test 1: no thrift server config specified
    Config config(bgpConfig);
    EXPECT_EQ("", config.getThriftServerEccCurveName());
  }

  {
    // test 2: thrift server config specified but ecc_curve_name not set
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_THROW(config.getThriftServerEccCurveName(), BgpError);
  }

  {
    // test 3: thrift server config specified with ecc_curve_name
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    thriftServerConfig.ecc_curve_name() = "prime256v1";
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_EQ("prime256v1", config.getThriftServerEccCurveName());
  }

  {
    // test 4: thrift server config specified with different ecc_curve_name
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    thriftServerConfig.ecc_curve_name() = "secp384r1";
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_EQ("secp384r1", config.getThriftServerEccCurveName());
  }
}

/*
 * Test getThriftServerClientVerification() function.
 */
TEST_F(ConfigTestFixture, GetThriftServerClientVerificationTest) {
  thrift::BgpConfig bgpConfig;
  bgpConfig.router_id() = kLocalAddr1.str();

  {
    // test 1: no thrift server config specified
    Config config(bgpConfig);
    EXPECT_EQ(
        folly::SSLContext::VerifyClientCertificate::DO_NOT_REQUEST,
        getThriftServerClientVerification(*config.getBgpGlobalConfig()));
  }

  {
    // test 2: thrift server config specified but verify_client_type not set
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_EQ(
        folly::SSLContext::VerifyClientCertificate::DO_NOT_REQUEST,
        getThriftServerClientVerification(*config.getBgpGlobalConfig()));
  }

  {
    // test 3: thrift server config with ALWAYS verification
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    thriftServerConfig.verify_client_type() =
        facebook::bgp::thrift::VerifyClientType::ALWAYS;
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_EQ(
        folly::SSLContext::VerifyClientCertificate::ALWAYS,
        getThriftServerClientVerification(*config.getBgpGlobalConfig()));
  }

  {
    // test 4: thrift server config with IF_PRESENTED verification
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    thriftServerConfig.verify_client_type() =
        facebook::bgp::thrift::VerifyClientType::IF_PRESENTED;
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_EQ(
        folly::SSLContext::VerifyClientCertificate::IF_PRESENTED,
        getThriftServerClientVerification(*config.getBgpGlobalConfig()));
  }

  {
    // test 5: thrift server config with NONE verification
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    thriftServerConfig.verify_client_type() =
        facebook::bgp::thrift::VerifyClientType::DO_NOT_REQUEST;
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_EQ(
        folly::SSLContext::VerifyClientCertificate::DO_NOT_REQUEST,
        getThriftServerClientVerification(*config.getBgpGlobalConfig()));
  }
}

/*
 * Test getThriftServerSSLPolicy() function.
 */
TEST_F(ConfigTestFixture, GetThriftServerSSLPolicyTest) {
  thrift::BgpConfig bgpConfig;
  bgpConfig.router_id() = kLocalAddr1.str();

  {
    // test 1: no thrift server config specified
    Config config(bgpConfig);
    EXPECT_EQ(
        apache::thrift::SSLPolicy::DISABLED,
        getThriftServerSSLPolicy(*config.getBgpGlobalConfig()));
  }

  {
    // test 2: thrift server config specified but TLS disabled
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = false;
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_EQ(
        apache::thrift::SSLPolicy::DISABLED,
        getThriftServerSSLPolicy(*config.getBgpGlobalConfig()));
  }

  {
    // test 3: thrift server config with TLS enabled and ALWAYS verification
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    thriftServerConfig.verify_client_type() =
        facebook::bgp::thrift::VerifyClientType::ALWAYS;
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_EQ(
        apache::thrift::SSLPolicy::REQUIRED,
        getThriftServerSSLPolicy(*config.getBgpGlobalConfig()));
  }

  {
    /*
     * test 4: thrift server config with TLS enabled and IF_PRESENTED
     * verification
     */
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    thriftServerConfig.verify_client_type() =
        facebook::bgp::thrift::VerifyClientType::IF_PRESENTED;
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_EQ(
        apache::thrift::SSLPolicy::PERMITTED,
        getThriftServerSSLPolicy(*config.getBgpGlobalConfig()));
  }

  {
    // test 5: thrift server config with TLS enabled and NONE verification
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    thriftServerConfig.verify_client_type() =
        facebook::bgp::thrift::VerifyClientType::DO_NOT_REQUEST;
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_EQ(
        apache::thrift::SSLPolicy::DISABLED,
        getThriftServerSSLPolicy(*config.getBgpGlobalConfig()));
  }

  {
    /*
     * test 6: thrift server config with TLS enabled but no client verification
     * set
     */
    thrift::ThriftServerConfig thriftServerConfig;
    thriftServerConfig.enable_tls() = true;
    bgpConfig.thrift_server_config() = thriftServerConfig;

    Config config(bgpConfig);
    EXPECT_EQ(
        apache::thrift::SSLPolicy::DISABLED,
        getThriftServerSSLPolicy(*config.getBgpGlobalConfig()));
  }
}

/*
 * This test verifies the parsing of SwitchLimitConfig from the raw
 * thrift::BgpConfig to BgpGlobalConfig with different limit thrift field
 * settings.
 */
TEST_F(ConfigTestFixture, SwitchLimitConfigTest) {
  thrift::BgpConfig bgpConfig;
  bgpConfig.router_id() = kLocalAddr1.str();
  const auto ingressPathLimit = 2000;
  const auto uniquePrefixLimit = 1000;
  const auto totalPathLimit = 3000;

  {
    // test 1: no switch limit config specified
    Config config(bgpConfig);
    EXPECT_EQ(nullptr, config.getBgpSwitchLimitConfig());

    EXPECT_EQ(
        0,
        fb303::ThreadCachedServiceData::get()->getCounter(
            "bgpd.unique_prefix_limit"));
    EXPECT_EQ(
        0,
        fb303::ThreadCachedServiceData::get()->getCounter(
            "bgpd.total_path_limit"));
  }
  {
    // test 2: switch limit config specified but no limit configured
    thrift::BgpSwitchLimitConfig switchLimitConfig;
    bgpConfig.switch_limit_config() = switchLimitConfig;

    Config config(bgpConfig);
    auto bgpSwitchLimitConfig = config.getBgpSwitchLimitConfig();

    EXPECT_NE(nullptr, bgpSwitchLimitConfig);
    EXPECT_EQ(std::nullopt, bgpSwitchLimitConfig->ingress_path_limit());
    EXPECT_EQ(std::nullopt, bgpSwitchLimitConfig->prefix_limit());
    EXPECT_EQ(std::nullopt, bgpSwitchLimitConfig->total_path_limit());

    EXPECT_EQ(
        0,
        fb303::ThreadCachedServiceData::get()->getCounter(
            "bgpd.unique_prefix_limit"));
    EXPECT_EQ(
        0,
        fb303::ThreadCachedServiceData::get()->getCounter(
            "bgpd.total_path_limit"));
  }
  {
    /*
     * test 3: switch limit config specified. Ingress path limit is configured
     * and unique prefix limit is not configured.
     */
    thrift::BgpSwitchLimitConfig switchLimitConfig;
    switchLimitConfig.ingress_path_limit() = ingressPathLimit;
    bgpConfig.switch_limit_config() = switchLimitConfig;

    Config config(bgpConfig);
    auto bgpSwitchLimitConfig = config.getBgpSwitchLimitConfig();

    EXPECT_NE(nullptr, bgpSwitchLimitConfig);
    EXPECT_NE(std::nullopt, bgpSwitchLimitConfig->ingress_path_limit());
    EXPECT_EQ(std::nullopt, bgpSwitchLimitConfig->prefix_limit());
    EXPECT_EQ(ingressPathLimit, *bgpSwitchLimitConfig->ingress_path_limit());
  }
  {
    /*
     * test 4: switch limit config specified. Ingress path limit is configured
     * and unique prefix limit is configured.
     */
    thrift::BgpSwitchLimitConfig switchLimitConfig;
    switchLimitConfig.ingress_path_limit() = ingressPathLimit;
    switchLimitConfig.prefix_limit() = uniquePrefixLimit;
    switchLimitConfig.total_path_limit() = totalPathLimit;
    bgpConfig.switch_limit_config() = switchLimitConfig;

    Config config(bgpConfig);
    auto bgpSwitchLimitConfig = config.getBgpSwitchLimitConfig();

    EXPECT_NE(nullptr, bgpSwitchLimitConfig);
    EXPECT_NE(std::nullopt, bgpSwitchLimitConfig->ingress_path_limit());
    EXPECT_NE(std::nullopt, bgpSwitchLimitConfig->prefix_limit());
    EXPECT_NE(std::nullopt, bgpSwitchLimitConfig->total_path_limit());
    EXPECT_EQ(ingressPathLimit, *bgpSwitchLimitConfig->ingress_path_limit());
    EXPECT_EQ(uniquePrefixLimit, *bgpSwitchLimitConfig->prefix_limit());
    EXPECT_EQ(totalPathLimit, *bgpSwitchLimitConfig->total_path_limit());

    EXPECT_EQ(
        uniquePrefixLimit,
        fb303::ThreadCachedServiceData::get()->getCounter(
            "bgpd.unique_prefix_limit"));
    EXPECT_EQ(
        totalPathLimit,
        fb303::ThreadCachedServiceData::get()->getCounter(
            "bgpd.total_path_limit"));
  }
}

// An absent cluster_id keeps the RFC 4456 default of the BGP identifier
TEST_F(ConfigTestFixture, clusterIdDefaultsToRouterIdTest) {
  ASSERT_FALSE(defaultConfig_.cluster_id().has_value());

  Config config(defaultConfig_);

  auto globalConfig = config.getBgpGlobalConfig();
  EXPECT_EQ(kLocalAddr1, globalConfig->routerId);
  EXPECT_EQ(kLocalAddr1, globalConfig->clusterId);
}

// A configured cluster_id reaches clusterId without disturbing routerId
TEST_F(ConfigTestFixture, clusterIdConfiguredIndependentlyTest) {
  defaultConfig_.cluster_id() = kLocalClusterAddr1.str();

  Config config(defaultConfig_);

  auto globalConfig = config.getBgpGlobalConfig();
  EXPECT_EQ(kLocalAddr1, globalConfig->routerId);
  EXPECT_EQ(kLocalClusterAddr1, globalConfig->clusterId);
  EXPECT_NE(globalConfig->routerId, globalConfig->clusterId);
}

TEST_F(ConfigTestFixture, clusterIdMalformedRejectedTest) {
  defaultConfig_.cluster_id() = "not-an-address";

  EXPECT_THROW(Config config(defaultConfig_), folly::IPAddressFormatException);
}

TEST_F(ConfigTestFixture, clusterIdIpv6RejectedTest) {
  defaultConfig_.cluster_id() = "2001:db8::1";

  EXPECT_THROW(Config config(defaultConfig_), folly::IPAddressFormatException);
}

// A generated config that rendered nothing must not fail the whole config load
TEST_F(ConfigTestFixture, clusterIdEmptyFallsBackToRouterIdTest) {
  defaultConfig_.cluster_id() = "";

  Config config(defaultConfig_);

  auto globalConfig = config.getBgpGlobalConfig();
  EXPECT_EQ(kLocalAddr1, globalConfig->clusterId);
}

TEST_F(ConfigTestFixture, populateConfigDatabaseTest) {
  // set config and populate config database
  Config config(defaultConfig_);

  // test config database
  auto globalConfig = config.getBgpGlobalConfig();
  EXPECT_EQ(std::nullopt, globalConfig->deviceName);
  EXPECT_EQ(kLocalAddr1, globalConfig->routerId);
  EXPECT_EQ(kAsn1, globalConfig->localAsn);

  auto dynamicPeerToConfig = config.getDynamicPeerToConfig();
  EXPECT_EQ(2, dynamicPeerToConfig.size());
  {
    // test config
    EXPECT_EQ(kPeerPrefix1, dynamicPeerToConfig.at(kPeerPrefix1)->peerPrefix);
    const auto& peerConfig =
        dynamicPeerToConfig.at(kPeerPrefix1)->commonPeerGroupConfig;
    EXPECT_EQ(kAsn2, peerConfig.peerAsn);
    EXPECT_EQ(std::nullopt, peerConfig.peerPort);
    EXPECT_EQ(std::nullopt, peerConfig.localAsn);
    auto bindAddr = folly::SocketAddress(kLocalAddr1, 0);
    ASSERT_TRUE(peerConfig.bindAddr);
    EXPECT_EQ(bindAddr, *peerConfig.bindAddr);
    EXPECT_EQ(TBgpSessionConnectMode::PASSIVE_ONLY, *peerConfig.connectMode);
    EXPECT_EQ(kNextHopV4_1, peerConfig.nexthopV4);
    EXPECT_EQ(kNextHopV6_1, peerConfig.nexthopV6);
    EXPECT_TRUE(*peerConfig.isRrClient);
    EXPECT_EQ(kPeerTypeBgpMonitor, *peerConfig.peerTag);
    EXPECT_TRUE(*peerConfig.nextHopSelf);
    EXPECT_TRUE(*peerConfig.disableIpv4Afi);
    EXPECT_TRUE(*peerConfig.disableIpv6Afi);
    EXPECT_EQ(dynamicPeer1_.peer_id().value(), *peerConfig.peerId);

    // test PeeringParams
    auto params = config.getPeeringParamsForDynamicPeer(
        *dynamicPeerToConfig.at(kPeerPrefix1));
    EXPECT_EQ(kPeerPrefix1, params.peerPrefix);
    EXPECT_EQ(kAsn1, params.localAs);
    EXPECT_EQ(kAsn2, params.remoteAs);
    EXPECT_EQ(kLocalAddr1.asV4(), params.localBgpId);
    EXPECT_EQ(kHoldTime, params.holdTime);
    EXPECT_EQ(kGrRestartTime, *params.grRestartTime);
    EXPECT_EQ(kBgpPort, params.peerPort);
    EXPECT_EQ(bindAddr, params.bindAddr);
    EXPECT_EQ(TBgpSessionConnectMode::PASSIVE_ONLY, params.connectMode);
    EXPECT_TRUE(params.isRrClient);
    EXPECT_TRUE(params.nextHopSelf);
    EXPECT_FALSE(params.isAfiIpv4Configured);
    EXPECT_FALSE(params.isAfiIpv6Configured);
    EXPECT_EQ(kDescription1, params.description);
    EXPECT_EQ(dynamicPeer1_.peer_id().value(), params.peerId);
  }
  {
    // test config
    EXPECT_EQ(kPeerPrefix2, dynamicPeerToConfig.at(kPeerPrefix2)->peerPrefix);
    const auto& peerConfig =
        dynamicPeerToConfig.at(kPeerPrefix2)->commonPeerGroupConfig;
    EXPECT_EQ(kAsn1, peerConfig.peerAsn);
    EXPECT_EQ(std::nullopt, peerConfig.peerPort);
    EXPECT_EQ(std::nullopt, peerConfig.localAsn);
    auto bindAddr = folly::SocketAddress(kLocalAddr2, 0);
    ASSERT_TRUE(peerConfig.bindAddr);
    EXPECT_EQ(bindAddr, *peerConfig.bindAddr);
    EXPECT_EQ(TBgpSessionConnectMode::PASSIVE_ONLY, *peerConfig.connectMode);
    EXPECT_EQ(kNextHopV4_2, peerConfig.nexthopV4);
    EXPECT_EQ(kNextHopV6_2, peerConfig.nexthopV6);
    EXPECT_EQ(std::nullopt, peerConfig.isRrClient);
    EXPECT_EQ(kPeerTypeBgpMonitor, *peerConfig.peerTag);
    // not setting nextHopSelf, disableIpv4Afi, disableIpv6Afi
    EXPECT_EQ(std::nullopt, peerConfig.nextHopSelf);
    EXPECT_EQ(std::nullopt, peerConfig.disableIpv4Afi);
    EXPECT_EQ(std::nullopt, peerConfig.disableIpv6Afi);
    EXPECT_EQ(std::nullopt, peerConfig.peerId);

    // test PeeringParams
    auto params = config.getPeeringParamsForDynamicPeer(
        *dynamicPeerToConfig.at(kPeerPrefix2));
    EXPECT_EQ(kPeerPrefix2, params.peerPrefix);
    EXPECT_EQ(kAsn1, params.localAs);
    EXPECT_EQ(kAsn1, params.remoteAs);
    EXPECT_EQ(kLocalAddr1.asV4(), params.localBgpId);
    EXPECT_EQ(kHoldTime, params.holdTime);
    EXPECT_EQ(kGrRestartTime, *params.grRestartTime);
    EXPECT_EQ(kBgpPort, params.peerPort);
    EXPECT_EQ(bindAddr, params.bindAddr);
    EXPECT_EQ(TBgpSessionConnectMode::PASSIVE_ONLY, params.connectMode);
    EXPECT_FALSE(params.isRrClient);
    EXPECT_FALSE(params.nextHopSelf);
    EXPECT_TRUE(params.isAfiIpv4Configured);
    EXPECT_TRUE(params.isAfiIpv6Configured);
    EXPECT_EQ(kDefaultDescription, params.description);
    EXPECT_EQ(kDefaultPeerId, params.peerId);
  }

  auto peerToConfig = config.getPeerToConfig();
  EXPECT_EQ(4, peerToConfig.size());
  {
    // test config
    EXPECT_EQ(kPeerAddr3, peerToConfig.at(kPeerAddr3)->peerAddr);
    const auto& peerConfig = peerToConfig.at(kPeerAddr3)->commonPeerGroupConfig;
    EXPECT_EQ(kAsn1, peerConfig.peerAsn);
    EXPECT_EQ(std::nullopt, peerConfig.peerPort);
    EXPECT_EQ(std::nullopt, peerConfig.localAsn);
    auto bindAddr = folly::SocketAddress(kLocalAddr3, 0);
    ASSERT_TRUE(peerConfig.bindAddr);
    EXPECT_EQ(bindAddr, *peerConfig.bindAddr);
    EXPECT_EQ(TBgpSessionConnectMode::PASSIVE_ACTIVE, *peerConfig.connectMode);
    EXPECT_EQ(kNextHopV4_3, peerConfig.nexthopV4);
    EXPECT_EQ(kNextHopV6_3, peerConfig.nexthopV6);
    EXPECT_FALSE(peerConfig.isRrClient);
    EXPECT_EQ(kPeerTypeCsw, *peerConfig.peerTag);
    EXPECT_FALSE(*peerConfig.nextHopSelf);
    EXPECT_FALSE(*peerConfig.disableIpv4Afi);
    EXPECT_FALSE(*peerConfig.disableIpv6Afi);
    EXPECT_EQ(kIngressPolicyName, *peerConfig.ingressPolicyName);
    EXPECT_EQ(kEgressPolicyName, *peerConfig.egressPolicyName);
    EXPECT_EQ(std::nullopt, peerConfig.peerId);
    EXPECT_EQ(
        AdvertiseLinkBandwidth::BEST_PATH, *peerConfig.advertiseLinkBandwidth);
    EXPECT_EQ(true, *peerConfig.removePrivateAs);
    EXPECT_EQ(kPreMaxRoutes, *peerConfig.preRouteLimit->max_routes());
    EXPECT_EQ(true, *peerConfig.preRouteLimit->warning_only());
    EXPECT_EQ(kPreWarningThreshold, *peerConfig.preRouteLimit->warning_limit());
    EXPECT_EQ(kPostMaxRoutes, *peerConfig.postRouteLimit->max_routes());
    EXPECT_EQ(true, *peerConfig.postRouteLimit->warning_only());
    EXPECT_EQ(
        kPostWarningThreshold, *peerConfig.postRouteLimit->warning_limit());
    EXPECT_TRUE(*peerConfig.enableStatefulHa);

    // test PeeringParams
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
    EXPECT_EQ(kPeerAddr3, params.peerAddr);
    EXPECT_EQ(std::nullopt, params.peerPrefix);
    EXPECT_EQ(kAsn1, params.localAs);
    EXPECT_EQ(kAsn1, params.remoteAs);
    EXPECT_EQ(kLocalAddr1.asV4(), params.localBgpId);
    EXPECT_EQ(kHoldTime, params.holdTime);
    EXPECT_EQ(kGrRestartTime, *params.grRestartTime);
    EXPECT_EQ(kBgpPort, params.peerPort);
    EXPECT_EQ(bindAddr, params.bindAddr);
    EXPECT_EQ(TBgpSessionConnectMode::PASSIVE_ACTIVE, params.connectMode);
    EXPECT_FALSE(params.isRrClient);
    EXPECT_FALSE(params.nextHopSelf);
    EXPECT_TRUE(params.isAfiIpv4Configured);
    EXPECT_TRUE(params.isAfiIpv6Configured);
    EXPECT_EQ(kDefaultDescription, params.description);
    EXPECT_EQ(kDefaultPeerId, params.peerId);
    EXPECT_EQ(AdvertiseLinkBandwidth::BEST_PATH, params.advertiseLinkBandwidth);
    EXPECT_EQ(true, params.removePrivateAs);
    EXPECT_EQ(true, params.validateRemoteAs);
    EXPECT_EQ(kPreMaxRoutes, *params.preRouteLimit->max_routes());
    EXPECT_EQ(true, *params.preRouteLimit->warning_only());
    EXPECT_EQ(kPreWarningThreshold, *params.preRouteLimit->warning_limit());
    EXPECT_EQ(kPostMaxRoutes, *params.postRouteLimit->max_routes());
    EXPECT_EQ(true, *params.postRouteLimit->warning_only());
    EXPECT_EQ(kPostWarningThreshold, *params.postRouteLimit->warning_limit());
    EXPECT_TRUE(params.enableStatefulHa);
  }
  {
    // test config
    EXPECT_EQ(kPeerAddr4, peerToConfig.at(kPeerAddr4)->peerAddr);
    const auto& peerConfig = peerToConfig.at(kPeerAddr4)->commonPeerGroupConfig;
    EXPECT_EQ(kAsn1, peerConfig.peerAsn);
    EXPECT_EQ(std::nullopt, peerConfig.peerPort);
    EXPECT_EQ(std::nullopt, peerConfig.localAsn);
    auto bindAddr = folly::SocketAddress(kLocalAddr4, 0);
    ASSERT_TRUE(peerConfig.bindAddr);
    EXPECT_EQ(bindAddr, *peerConfig.bindAddr);
    EXPECT_EQ(TBgpSessionConnectMode::PASSIVE_ACTIVE, *peerConfig.connectMode);
    EXPECT_EQ(kNextHopV4_4, peerConfig.nexthopV4);
    EXPECT_EQ(kNextHopV6_4, peerConfig.nexthopV6);
    EXPECT_FALSE(peerConfig.isRrClient);
    EXPECT_EQ(kPeerTypeCsw, *peerConfig.peerTag);
    EXPECT_EQ(std::nullopt, peerConfig.ingressPolicyName);
    EXPECT_EQ(kEgressPolicyName, *peerConfig.egressPolicyName);
    EXPECT_EQ(staticPeer2_.peer_id(), *peerConfig.peerId);
    EXPECT_EQ(
        AdvertiseLinkBandwidth::DISABLE, *peerConfig.advertiseLinkBandwidth);
    EXPECT_EQ(false, *peerConfig.removePrivateAs);
    EXPECT_FALSE(peerConfig.enableStatefulHa);

    // test PeeringParams
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr4));
    EXPECT_EQ(kPeerAddr4, params.peerAddr);
    EXPECT_EQ(std::nullopt, params.peerPrefix);
    EXPECT_EQ(kAsn1, params.localAs);
    EXPECT_EQ(kAsn1, params.remoteAs);
    EXPECT_EQ(kLocalAddr1.asV4(), params.localBgpId);
    EXPECT_EQ(kHoldTime, params.holdTime);
    EXPECT_EQ(kGrRestartTime, *params.grRestartTime);
    EXPECT_EQ(kBgpPort, params.peerPort);
    EXPECT_EQ(bindAddr, params.bindAddr);
    EXPECT_EQ(TBgpSessionConnectMode::PASSIVE_ACTIVE, params.connectMode);
    EXPECT_FALSE(params.isRrClient);
    EXPECT_EQ(kDescription2, params.description);
    EXPECT_EQ(staticPeer2_.peer_id(), params.peerId);
    EXPECT_EQ(AdvertiseLinkBandwidth::DISABLE, params.advertiseLinkBandwidth);
    EXPECT_EQ(false, params.removePrivateAs);
    EXPECT_EQ(true, params.validateRemoteAs);
    EXPECT_EQ(kPreMaxRoutes, *params.preRouteLimit->max_routes());
    EXPECT_EQ(true, *params.preRouteLimit->warning_only());
    EXPECT_EQ(kPreWarningThreshold, *params.preRouteLimit->warning_limit());
    EXPECT_EQ(std::nullopt, params.postRouteLimit);
    EXPECT_FALSE(params.enableStatefulHa);
  }
  {
    // test config
    EXPECT_EQ(kPeerAddr5, peerToConfig.at(kPeerAddr5)->peerAddr);
    const auto& peerConfig = peerToConfig.at(kPeerAddr5)->commonPeerGroupConfig;
    EXPECT_EQ(kAsn1, peerConfig.peerAsn);
    EXPECT_EQ(std::nullopt, peerConfig.peerPort);
    EXPECT_EQ(std::nullopt, peerConfig.localAsn);
    auto bindAddr = folly::SocketAddress(kLocalAddr5, 0);
    ASSERT_TRUE(peerConfig.bindAddr);
    EXPECT_EQ(bindAddr, *peerConfig.bindAddr);
    EXPECT_EQ(TBgpSessionConnectMode::PASSIVE_ONLY, *peerConfig.connectMode);
    EXPECT_EQ(kNextHopV4_5, peerConfig.nexthopV4);
    EXPECT_EQ(kNextHopV6_5, peerConfig.nexthopV6);
    EXPECT_EQ(std::nullopt, peerConfig.isRrClient);
    EXPECT_EQ(kPeerTypeCsw, *peerConfig.peerTag);
    EXPECT_TRUE(*peerConfig.nextHopSelf);
    EXPECT_EQ(std::nullopt, peerConfig.disableIpv4Afi);
    EXPECT_EQ(std::nullopt, peerConfig.disableIpv6Afi);
    EXPECT_EQ(
        *peergroup1_.ingress_policy_name(), *peerConfig.ingressPolicyName);
    EXPECT_EQ(*peergroup1_.egress_policy_name(), *peerConfig.egressPolicyName);
    EXPECT_EQ(std::nullopt, peerConfig.peerId);
    EXPECT_EQ(std::nullopt, peerConfig.advertiseLinkBandwidth);
    EXPECT_EQ(std::nullopt, peerConfig.removePrivateAs);

    // test PeeringParams
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr5));
    EXPECT_EQ(kPeerAddr5, params.peerAddr);
    EXPECT_EQ(std::nullopt, params.peerPrefix);
    EXPECT_EQ(kAsn1, params.localAs);
    EXPECT_EQ(kAsn1, params.remoteAs);
    EXPECT_EQ(kLocalAddr1.asV4(), params.localBgpId);
    EXPECT_EQ(kHoldTime, params.holdTime);
    EXPECT_EQ(kGrRestartTime, *params.grRestartTime);
    EXPECT_EQ(kBgpPort, params.peerPort);
    EXPECT_EQ(bindAddr, params.bindAddr);
    EXPECT_EQ(TBgpSessionConnectMode::PASSIVE_ONLY, params.connectMode);
    EXPECT_FALSE(params.isRrClient);
    EXPECT_TRUE(params.nextHopSelf);
    EXPECT_TRUE(params.isAfiIpv4Configured);
    EXPECT_TRUE(params.isAfiIpv6Configured);
    EXPECT_EQ(kDefaultDescription, params.description);
    EXPECT_EQ(kDefaultPeerId, params.peerId);
    EXPECT_FALSE(params.advertiseLinkBandwidth.has_value());
    EXPECT_EQ(false, params.removePrivateAs);
    EXPECT_EQ(true, params.validateRemoteAs);
    EXPECT_EQ(12000, *params.preRouteLimit->max_routes());
    EXPECT_EQ(false, *params.preRouteLimit->warning_only());
    EXPECT_EQ(0, *params.preRouteLimit->warning_limit());
    EXPECT_EQ(kPostMaxRoutes, *params.postRouteLimit->max_routes());
    EXPECT_EQ(true, *params.postRouteLimit->warning_only());
    EXPECT_EQ(kPostWarningThreshold, *params.postRouteLimit->warning_limit());
  }
  {
    // test config
    EXPECT_EQ(kPeerAddr6, peerToConfig.at(kPeerAddr6)->peerAddr);
    const auto& peerConfig = peerToConfig.at(kPeerAddr6)->commonPeerGroupConfig;
    EXPECT_EQ(kAsn1, peerConfig.peerAsn);
    EXPECT_EQ(std::nullopt, peerConfig.peerPort);
    EXPECT_EQ(std::nullopt, peerConfig.localAsn);
    auto bindAddr = folly::SocketAddress(kLocalAddr6, 0);
    ASSERT_TRUE(peerConfig.bindAddr);
    EXPECT_EQ(bindAddr, *peerConfig.bindAddr);
    EXPECT_EQ(TBgpSessionConnectMode::PASSIVE_ACTIVE, *peerConfig.connectMode);
    EXPECT_EQ(kNextHopV4_6, peerConfig.nexthopV4);
    EXPECT_EQ(kNextHopV6_6, peerConfig.nexthopV6);
    EXPECT_TRUE(*peerConfig.isRrClient);
    EXPECT_EQ(kPeerTypeCsw, *peerConfig.peerTag);
    EXPECT_TRUE(*peerConfig.nextHopSelf);
    EXPECT_EQ(std::nullopt, peerConfig.disableIpv4Afi);
    EXPECT_TRUE(*peerConfig.disableIpv6Afi);
    EXPECT_EQ(kIngressPolicyName, *peerConfig.ingressPolicyName);
    EXPECT_EQ(kEgressPolicyName, *peerConfig.egressPolicyName);
    EXPECT_EQ(std::nullopt, peerConfig.peerId);
    EXPECT_EQ(std::nullopt, peerConfig.advertiseLinkBandwidth);
    EXPECT_EQ(std::nullopt, peerConfig.removePrivateAs);

    // test PeeringParams
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr6));
    EXPECT_EQ(kPeerAddr6, params.peerAddr);
    EXPECT_EQ(std::nullopt, params.peerPrefix);
    EXPECT_EQ(kAsn1, params.localAs);
    EXPECT_EQ(kAsn1, params.remoteAs);
    EXPECT_EQ(kLocalAddr1.asV4(), params.localBgpId);
    EXPECT_EQ(kHoldTime, params.holdTime);
    EXPECT_EQ(kGrRestartTime, *params.grRestartTime);
    EXPECT_EQ(kBgpPort, params.peerPort);
    EXPECT_EQ(bindAddr, params.bindAddr);
    EXPECT_EQ(TBgpSessionConnectMode::PASSIVE_ACTIVE, params.connectMode);
    EXPECT_TRUE(params.isRrClient);
    EXPECT_TRUE(params.nextHopSelf);
    EXPECT_TRUE(params.isAfiIpv4Configured);
    EXPECT_FALSE(params.isAfiIpv6Configured);
    EXPECT_EQ(kDefaultDescription, params.description);
    EXPECT_EQ(kDefaultPeerId, params.peerId);
    EXPECT_FALSE(params.advertiseLinkBandwidth.has_value());
    EXPECT_FALSE(params.removePrivateAs);
    EXPECT_EQ(true, params.validateRemoteAs);
    EXPECT_EQ(12000, *params.preRouteLimit->max_routes());
    EXPECT_EQ(false, *params.preRouteLimit->warning_only());
    EXPECT_EQ(0, *params.preRouteLimit->warning_limit());
    EXPECT_EQ(12000, *params.postRouteLimit->max_routes());
    EXPECT_EQ(false, *params.postRouteLimit->warning_only());
    EXPECT_EQ(0, *params.postRouteLimit->warning_limit());
  }

  // Verify configured peers counter
  std::map<std::string, int64_t> counters;
  fb303::ThreadCachedServiceData::getShared()->getCounters(counters);
  EXPECT_EQ(counters.count("bgpd.configuredPeers"), 1);
  EXPECT_EQ(6, counters.at("bgpd.configuredPeers"));

  EXPECT_EQ(counters.at("bgpd.config.ucmp_enabled"), 0);
  EXPECT_EQ(counters.at("bgpd.config.peers.ucmp_advertise.aggregate_local"), 0);
  EXPECT_EQ(
      counters.at("bgpd.config.peers.ucmp_advertise.aggregate_received"), 0);
  EXPECT_EQ(counters.at("bgpd.config.peers.ucmp_advertise.best_path"), 1);
  EXPECT_EQ(counters.at("bgpd.config.peers.ucmp_advertise.disable"), 1);
  EXPECT_EQ(counters.at("bgpd.config.peers.ucmp_advertise.set_link_bps"), 0);
  EXPECT_EQ(counters.at("bgpd.config.peers.ucmp_receive.accept"), 0);
  EXPECT_EQ(counters.at("bgpd.config.peers.ucmp_receive.disable"), 0);
  EXPECT_EQ(counters.at("bgpd.config.peers.ucmp_receive.set_link_bps"), 0);
}

TEST_F(ConfigTestFixture, tunables) {
  auto tunables = BgpSettings(
      ValidateRemoteAs{false},
      SupportStatefulGr{false},
      EnableServerSocket{false});

  // set config and populate config database with tunable settings
  Config config(defaultConfig_, tunables);

  // Verify supportStatefulGr
  auto globalConfig = config.getBgpGlobalConfig();
  EXPECT_EQ(false, globalConfig->supportStatefulGr);
  EXPECT_EQ(false, globalConfig->enableServerSocket);

  auto peerToConfig = config.getPeerToConfig();
  EXPECT_EQ(4, peerToConfig.size());
  {
    // test validateRemoteAs is false for all peers
    auto peerAddrs = {kPeerAddr3, kPeerAddr4, kPeerAddr5, kPeerAddr6};
    for (const auto& peerAddr : peerAddrs) {
      auto params = config.getPeeringParamsForPeer(*peerToConfig.at(peerAddr));
      EXPECT_EQ(false, params.validateRemoteAs);
    }
  }
}

TEST_F(ConfigTestFixture, localASTest) {
  auto testConfig = defaultConfig_;

  /*
   * create Peer Group, used by peer with peerAddr kPeerAddr8 and kPeerAddr9
   * the port-group has local-as configured
   */
  thrift::PeerGroup peergroup;
  peergroup.name() = "PEERGROUP_SSW_FAUU";
  peergroup.next_hop_self() = true;
  peergroup.peer_tag() = kPeerTypeFa;
  peergroup.bgp_peer_timers() = timers1_;
  peergroup.local_as_4_byte() = kAsn5;
  auto peerGroups = testConfig.peer_groups().to_optional();
  peerGroups->emplace_back(peergroup);
  testConfig.peer_groups().from_optional(peerGroups);
  testConfig.peers()->clear();
  {
    /*
     * Add a static peer without peer-group but with local-as configured.
     * Check that local-as takes precedence over global-as
     * (globalconfig.local_as)
     */
    thrift::BgpPeer staticPeer;
    staticPeer.remote_as_4_byte() = kPeerAsn7;
    staticPeer.local_as_4_byte() = kAsn6;
    staticPeer.local_addr() = kLocalAddr7.str();
    staticPeer.peer_addr() = kPeerAddr7.str();
    staticPeer.next_hop4() = kNextHopV4_7.str();
    staticPeer.next_hop6() = kNextHopV6_7.str();
    testConfig.peers()->emplace_back(staticPeer);
  }
  {
    /*
     * Add a static peer with no local-as but with a peer-group that has
     * local-as Check that peer group's local-as overrides the global-as
     * (globalconfig.local_as)
     */
    thrift::BgpPeer staticPeer;
    staticPeer.remote_as_4_byte() = kPeerAsn8;
    staticPeer.local_addr() = kLocalAddr8.str();
    staticPeer.peer_addr() = kPeerAddr8.str();
    staticPeer.next_hop4() = kNextHopV4_8.str();
    staticPeer.next_hop6() = kNextHopV6_8.str();
    staticPeer.peer_group_name() = *peergroup.name();
    testConfig.peers()->emplace_back(staticPeer);
  }
  {
    /*
     * Add a static peer with local-as with a peer-group that also has local-as
     * Check that peer's local-as config overrides the peer-group local-as
     * config.
     */
    thrift::BgpPeer staticPeer;
    staticPeer.remote_as_4_byte() = kPeerAsn9;
    staticPeer.local_addr() = kLocalAddr9.str();
    staticPeer.peer_addr() = kPeerAddr9.str();
    staticPeer.local_as_4_byte() = kAsn3;
    staticPeer.next_hop4() = kNextHopV4_9.str();
    staticPeer.next_hop6() = kNextHopV6_9.str();
    staticPeer.peer_group_name() = *peergroup.name();
    testConfig.peers()->emplace_back(staticPeer);
  }

  Config config(testConfig);
  auto globalConfig = config.getBgpGlobalConfig();
  EXPECT_EQ(kAsn1, globalConfig->localAsn);
  auto peerToConfig = config.getPeerToConfig();
  {
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr7));
    EXPECT_EQ(kPeerAddr7, params.peerAddr);
    EXPECT_EQ(kAsn6, params.localAs);
    EXPECT_EQ(kPeerAsn7, params.remoteAs);
  }
  {
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr8));
    EXPECT_EQ(kPeerAddr8, params.peerAddr);
    EXPECT_EQ(kAsn5, params.localAs);
    EXPECT_EQ(kPeerAsn8, params.remoteAs);
  }
  {
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr9));
    EXPECT_EQ(kPeerAddr9, params.peerAddr);
    EXPECT_EQ(kAsn3, params.localAs);
    EXPECT_EQ(kPeerAsn9, params.remoteAs);
  }
}

/*
 * Verify Local-AS (RFC 7705) uses the same peer > peer-group cascade as remote
 * AS: per-peer wins as a unit (including a per-peer legacy value over a
 * peer-group 4-byte value), and within a level the 4-byte field overrides the
 * deprecated i32 field. Each peer sets a valid eBGP remote AS so the
 * Peer-Local-AS validation does not reject the config.
 */
TEST_F(ConfigTestFixture, localASCascadeTest) {
  // Utils.h only defines addresses through index 9; define 10-12 locally.
  const auto kLocalAddr10 = folly::IPAddress("127.1.0.9");
  const auto kPeerAddr10 = folly::IPAddress("127.10.0.1");
  const auto kNextHopV4_10 = folly::IPAddress("127.5.0.9");
  const auto kNextHopV6_10 = folly::IPAddress("2401:db00:e011:411:1000::31");
  const auto kLocalAddr11 = folly::IPAddress("127.1.0.10");
  const auto kPeerAddr11 = folly::IPAddress("127.11.0.1");
  const auto kNextHopV4_11 = folly::IPAddress("127.5.0.10");
  const auto kNextHopV6_11 = folly::IPAddress("2401:db00:e011:411:1000::32");
  const auto kLocalAddr12 = folly::IPAddress("127.1.0.11");
  const auto kPeerAddr12 = folly::IPAddress("127.12.0.1");
  const auto kNextHopV4_12 = folly::IPAddress("127.5.0.11");
  const auto kNextHopV6_12 = folly::IPAddress("2401:db00:e011:411:1000::33");
  const uint32_t kGroupLocalAs4Byte = 65010;
  const uint32_t kGroupLocalAsLegacy = 65011;
  const uint32_t kPeerLocalAs4ByteOverride = 65012;
  const uint32_t kPeerLocalAsLegacyOverride = 65013;

  auto testConfig = defaultConfig_;
  testConfig.peers()->clear();

  // Peer-group whose Local-AS is set via the 4-byte field (RFC 6793).
  thrift::PeerGroup pg4Byte;
  pg4Byte.name() = "PEERGROUP_LOCAL_AS_4BYTE";
  pg4Byte.next_hop_self() = true;
  pg4Byte.peer_tag() = kPeerTypeFa;
  pg4Byte.bgp_peer_timers() = timers1_;
  pg4Byte.local_as_4_byte() = kGroupLocalAs4Byte;

  // Peer-group whose Local-AS is set via the deprecated i32 field.
  thrift::PeerGroup pgLegacy;
  pgLegacy.name() = "PEERGROUP_LOCAL_AS_LEGACY";
  pgLegacy.next_hop_self() = true;
  pgLegacy.peer_tag() = kPeerTypeFa;
  pgLegacy.bgp_peer_timers() = timers1_;
  pgLegacy.local_as() = static_cast<int32_t>(kGroupLocalAsLegacy);

  auto peerGroups = testConfig.peer_groups().to_optional();
  peerGroups->emplace_back(pg4Byte);
  peerGroups->emplace_back(pgLegacy);
  testConfig.peer_groups().from_optional(peerGroups);

  // peer7: per-peer 4-byte Local-AS only, no peer-group.
  {
    thrift::BgpPeer p;
    p.remote_as_4_byte() = kPeerAsn7;
    p.local_as_4_byte() = kAsn6;
    p.local_addr() = kLocalAddr7.str();
    p.peer_addr() = kPeerAddr7.str();
    p.next_hop4() = kNextHopV4_7.str();
    p.next_hop6() = kNextHopV6_7.str();
    testConfig.peers()->emplace_back(p);
  }
  // peer8: per-peer legacy i32 Local-AS only, no peer-group.
  {
    thrift::BgpPeer p;
    p.remote_as_4_byte() = kPeerAsn8;
    p.local_as() = static_cast<int32_t>(kAsn5);
    p.local_addr() = kLocalAddr8.str();
    p.peer_addr() = kPeerAddr8.str();
    p.next_hop4() = kNextHopV4_8.str();
    p.next_hop6() = kNextHopV6_8.str();
    testConfig.peers()->emplace_back(p);
  }
  // peer9: no per-peer Local-AS, inherits peer-group's 4-byte value.
  {
    thrift::BgpPeer p;
    p.remote_as_4_byte() = kPeerAsn9;
    p.local_addr() = kLocalAddr9.str();
    p.peer_addr() = kPeerAddr9.str();
    p.next_hop4() = kNextHopV4_9.str();
    p.next_hop6() = kNextHopV6_9.str();
    p.peer_group_name() = *pg4Byte.name();
    testConfig.peers()->emplace_back(p);
  }
  // peer10: no per-peer Local-AS, inherits peer-group's legacy value.
  {
    thrift::BgpPeer p;
    p.remote_as_4_byte() = kPeerAsn7;
    p.local_addr() = kLocalAddr10.str();
    p.peer_addr() = kPeerAddr10.str();
    p.next_hop4() = kNextHopV4_10.str();
    p.next_hop6() = kNextHopV6_10.str();
    p.peer_group_name() = *pgLegacy.name();
    testConfig.peers()->emplace_back(p);
  }
  // peer11: per-peer 4-byte Local-AS overrides peer-group's 4-byte.
  {
    thrift::BgpPeer p;
    p.remote_as_4_byte() = kPeerAsn8;
    p.local_as_4_byte() = kPeerLocalAs4ByteOverride;
    p.local_addr() = kLocalAddr11.str();
    p.peer_addr() = kPeerAddr11.str();
    p.next_hop4() = kNextHopV4_11.str();
    p.next_hop6() = kNextHopV6_11.str();
    p.peer_group_name() = *pg4Byte.name();
    testConfig.peers()->emplace_back(p);
  }
  /*
   * peer12: per-peer legacy i32 Local-AS overrides peer-group's 4-byte (peer
   * wins).
   */
  {
    thrift::BgpPeer p;
    p.remote_as_4_byte() = kPeerAsn9;
    p.local_as() = static_cast<int32_t>(kPeerLocalAsLegacyOverride);
    p.local_addr() = kLocalAddr12.str();
    p.peer_addr() = kPeerAddr12.str();
    p.next_hop4() = kNextHopV4_12.str();
    p.next_hop6() = kNextHopV6_12.str();
    p.peer_group_name() = *pg4Byte.name();
    testConfig.peers()->emplace_back(p);
  }

  Config config(testConfig);
  const auto& peerToConfig = config.getPeerToConfig();

  // peer7: per-peer 4-byte resolves directly.
  EXPECT_EQ(
      kAsn6,
      config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr7)).localAs);
  // peer8: per-peer legacy resolves directly.
  EXPECT_EQ(
      kAsn5,
      config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr8)).localAs);
  // peer9: inherits the peer-group's 4-byte value.
  EXPECT_EQ(
      kGroupLocalAs4Byte,
      config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr9)).localAs);
  // peer10: inherits the peer-group's legacy value.
  EXPECT_EQ(
      kGroupLocalAsLegacy,
      config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr10)).localAs);
  // peer11: per-peer 4-byte beats the peer-group's 4-byte.
  EXPECT_EQ(
      kPeerLocalAs4ByteOverride,
      config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr11)).localAs);
  // peer12: per-peer legacy beats the peer-group's 4-byte.
  EXPECT_EQ(
      kPeerLocalAsLegacyOverride,
      config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr12)).localAs);
}

/*
 * Both Local-AS representations (i32 and 4-byte) set at the SAME config level
 * is an ambiguous config and must be rejected at config-construction time, the
 * same way remote AS is.
 */
TEST_F(ConfigTestFixture, localASBothSetNegativeTest) {
  {
    // Both set at the peer level.
    auto testConfig = defaultConfig_;
    testConfig.peers()->clear();
    thrift::BgpPeer p;
    p.remote_as_4_byte() = kPeerAsn7;
    p.local_as_4_byte() = kAsn5;
    p.local_as() = static_cast<int32_t>(kAsn6);
    p.local_addr() = kLocalAddr7.str();
    p.peer_addr() = kPeerAddr7.str();
    p.next_hop4() = kNextHopV4_7.str();
    p.next_hop6() = kNextHopV6_7.str();
    testConfig.peers()->emplace_back(p);
    EXPECT_THROW(Config config(testConfig), BgpError);
  }
  {
    // Both set at the peer-group level (peer omits Local-AS).
    auto testConfig = defaultConfig_;
    testConfig.peers()->clear();
    thrift::PeerGroup pg;
    pg.name() = "PEERGROUP_LOCAL_AS_CONFLICT";
    pg.next_hop_self() = true;
    pg.peer_tag() = kPeerTypeFa;
    pg.bgp_peer_timers() = timers1_;
    pg.local_as_4_byte() = kAsn5;
    pg.local_as() = static_cast<int32_t>(kAsn6);
    auto peerGroups = testConfig.peer_groups().to_optional();
    peerGroups->emplace_back(pg);
    testConfig.peer_groups().from_optional(peerGroups);

    thrift::BgpPeer p;
    p.remote_as_4_byte() = kPeerAsn8;
    p.local_addr() = kLocalAddr8.str();
    p.peer_addr() = kPeerAddr8.str();
    p.next_hop4() = kNextHopV4_8.str();
    p.next_hop6() = kNextHopV6_8.str();
    p.peer_group_name() = *pg.name();
    testConfig.peers()->emplace_back(p);
    EXPECT_THROW(Config config(testConfig), BgpError);
  }
}

/*
 * Verify the peer > peer-group cascade for remote AS resolution, and that
 * within a single level the 4-byte field (RFC 6793) overrides the deprecated
 * i32 field. Precedence (high -> low):
 *   peer.remote_as_4_byte > peer.remote_as
 *   > peerGroup.remote_as_4_byte > peerGroup.remote_as
 * A per-peer value always wins over the peer-group, including a per-peer legacy
 * value over a peer-group 4-byte value.
 */
TEST_F(ConfigTestFixture, remoteASTest) {
  // Utils.h only defines addresses through index 9; define 10-12 locally.
  const auto kLocalAddr10 = folly::IPAddress("127.1.0.9");
  const auto kPeerAddr10 = folly::IPAddress("127.10.0.1");
  const auto kNextHopV4_10 = folly::IPAddress("127.5.0.9");
  const auto kNextHopV6_10 = folly::IPAddress("2401:db00:e011:411:1000::31");
  const auto kLocalAddr11 = folly::IPAddress("127.1.0.10");
  const auto kPeerAddr11 = folly::IPAddress("127.11.0.1");
  const auto kNextHopV4_11 = folly::IPAddress("127.5.0.10");
  const auto kNextHopV6_11 = folly::IPAddress("2401:db00:e011:411:1000::32");
  const auto kLocalAddr12 = folly::IPAddress("127.1.0.11");
  const auto kPeerAddr12 = folly::IPAddress("127.12.0.1");
  const auto kNextHopV4_12 = folly::IPAddress("127.5.0.11");
  const auto kNextHopV6_12 = folly::IPAddress("2401:db00:e011:411:1000::33");
  const uint32_t kGroupRemoteAs4Byte = 64600;
  const uint32_t kGroupRemoteAsLegacy = 64601;
  const uint32_t kPeerOverrideAsn = 64548;

  auto testConfig = defaultConfig_;
  testConfig.peers()->clear();

  // Peer-group whose remote AS is set via the 4-byte field (RFC 6793).
  thrift::PeerGroup pg4Byte;
  pg4Byte.name() = "PEERGROUP_REMOTE_AS_4BYTE";
  pg4Byte.next_hop_self() = true;
  pg4Byte.peer_tag() = kPeerTypeFa;
  pg4Byte.bgp_peer_timers() = timers1_;
  pg4Byte.remote_as_4_byte() = kGroupRemoteAs4Byte;

  // Peer-group whose remote AS is set via the deprecated i32 field.
  thrift::PeerGroup pgLegacy;
  pgLegacy.name() = "PEERGROUP_REMOTE_AS_LEGACY";
  pgLegacy.next_hop_self() = true;
  pgLegacy.peer_tag() = kPeerTypeFa;
  pgLegacy.bgp_peer_timers() = timers1_;
  pgLegacy.remote_as() = static_cast<int32_t>(kGroupRemoteAsLegacy);

  auto peerGroups = testConfig.peer_groups().to_optional();
  peerGroups->emplace_back(pg4Byte);
  peerGroups->emplace_back(pgLegacy);
  testConfig.peer_groups().from_optional(peerGroups);

  // peer7: per-peer 4-byte only, no peer-group.
  {
    thrift::BgpPeer p;
    p.remote_as_4_byte() = kPeerAsn7;
    p.local_addr() = kLocalAddr7.str();
    p.peer_addr() = kPeerAddr7.str();
    p.next_hop4() = kNextHopV4_7.str();
    p.next_hop6() = kNextHopV6_7.str();
    testConfig.peers()->emplace_back(p);
  }
  // peer8: per-peer legacy i32 only, no peer-group.
  {
    thrift::BgpPeer p;
    p.remote_as() = static_cast<int32_t>(kPeerAsn8);
    p.local_addr() = kLocalAddr8.str();
    p.peer_addr() = kPeerAddr8.str();
    p.next_hop4() = kNextHopV4_8.str();
    p.next_hop6() = kNextHopV6_8.str();
    testConfig.peers()->emplace_back(p);
  }
  // peer9: no per-peer remote AS, inherits peer-group's 4-byte value.
  {
    thrift::BgpPeer p;
    p.local_addr() = kLocalAddr9.str();
    p.peer_addr() = kPeerAddr9.str();
    p.next_hop4() = kNextHopV4_9.str();
    p.next_hop6() = kNextHopV6_9.str();
    p.peer_group_name() = *pg4Byte.name();
    testConfig.peers()->emplace_back(p);
  }
  // peer10: no per-peer remote AS, inherits peer-group's legacy value.
  {
    thrift::BgpPeer p;
    p.local_addr() = kLocalAddr10.str();
    p.peer_addr() = kPeerAddr10.str();
    p.next_hop4() = kNextHopV4_10.str();
    p.next_hop6() = kNextHopV6_10.str();
    p.peer_group_name() = *pgLegacy.name();
    testConfig.peers()->emplace_back(p);
  }
  // peer11: per-peer 4-byte overrides peer-group's 4-byte.
  {
    thrift::BgpPeer p;
    p.remote_as_4_byte() = kPeerOverrideAsn;
    p.local_addr() = kLocalAddr11.str();
    p.peer_addr() = kPeerAddr11.str();
    p.next_hop4() = kNextHopV4_11.str();
    p.next_hop6() = kNextHopV6_11.str();
    p.peer_group_name() = *pg4Byte.name();
    testConfig.peers()->emplace_back(p);
  }
  // peer12: per-peer legacy i32 overrides peer-group's 4-byte (peer wins).
  {
    thrift::BgpPeer p;
    p.remote_as() = static_cast<int32_t>(kPeerAsn9);
    p.local_addr() = kLocalAddr12.str();
    p.peer_addr() = kPeerAddr12.str();
    p.next_hop4() = kNextHopV4_12.str();
    p.next_hop6() = kNextHopV6_12.str();
    p.peer_group_name() = *pg4Byte.name();
    testConfig.peers()->emplace_back(p);
  }

  Config config(testConfig);
  const auto& peerToConfig = config.getPeerToConfig();

  // peer7: per-peer 4-byte resolves directly.
  EXPECT_EQ(
      kPeerAsn7,
      config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr7)).remoteAs);
  // peer8: per-peer legacy resolves directly.
  EXPECT_EQ(
      kPeerAsn8,
      config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr8)).remoteAs);
  // peer9: inherits the peer-group's 4-byte value.
  EXPECT_EQ(
      kGroupRemoteAs4Byte,
      config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr9)).remoteAs);
  // peer10: inherits the peer-group's legacy value.
  EXPECT_EQ(
      kGroupRemoteAsLegacy,
      config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr10)).remoteAs);
  // peer11: per-peer 4-byte beats the peer-group's 4-byte.
  EXPECT_EQ(
      kPeerOverrideAsn,
      config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr11)).remoteAs);
  // peer12: per-peer legacy beats the peer-group's 4-byte.
  EXPECT_EQ(
      kPeerAsn9,
      config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr12)).remoteAs);
}

TEST_F(ConfigTestFixture, additionalRemoteAsTest) {
  constexpr uint32_t kGroupAdditionalRemoteAs = 4200000000;
  constexpr uint32_t kPeerAdditionalRemoteAs = 4200000001;
  constexpr size_t kDynamicPeer1Index = 0;
  constexpr size_t kStaticPeer4Index = 5;

  auto testConfig = defaultConfig_;
  auto peerGroups = testConfig.peer_groups().to_optional();
  peerGroups->at(0).additional_remote_as_4_byte() = kGroupAdditionalRemoteAs;
  testConfig.peer_groups().from_optional(peerGroups);

  testConfig.peers()->at(kDynamicPeer1Index).additional_remote_as_4_byte() =
      kPeerAdditionalRemoteAs;
  testConfig.peers()->at(kStaticPeer4Index).additional_remote_as_4_byte() =
      kPeerAdditionalRemoteAs;

  Config config(testConfig);
  const auto& dynamicPeerToConfig = config.getDynamicPeerToConfig();
  const auto& peerToConfig = config.getPeerToConfig();

  const auto dynamicParams = config.getPeeringParamsForDynamicPeer(
      *dynamicPeerToConfig.at(kPeerPrefix1));
  EXPECT_EQ(
      kPeerAdditionalRemoteAs,
      dynamicPeerToConfig.at(kPeerPrefix1)
          ->commonPeerGroupConfig.additionalRemoteAs);
  EXPECT_EQ(kPeerAdditionalRemoteAs, dynamicParams.additionalRemoteAs);

  const auto inheritedParams =
      config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr5));
  EXPECT_EQ(
      kGroupAdditionalRemoteAs,
      peerToConfig.at(kPeerAddr5)->commonPeerGroupConfig.additionalRemoteAs);
  EXPECT_EQ(kGroupAdditionalRemoteAs, inheritedParams.additionalRemoteAs);

  const auto overrideParams =
      config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr6));
  EXPECT_EQ(
      kPeerAdditionalRemoteAs,
      peerToConfig.at(kPeerAddr6)->commonPeerGroupConfig.additionalRemoteAs);
  EXPECT_EQ(kPeerAdditionalRemoteAs, overrideParams.additionalRemoteAs);

  const auto unsetParams =
      config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
  EXPECT_EQ(std::nullopt, unsetParams.additionalRemoteAs);
}

TEST_F(ConfigTestFixture, additionalRemoteAsNegativeTest) {
  constexpr size_t kStaticPeer1Index = 2;
  for (const int64_t invalidAsn : {0LL, -1LL, 4294967296LL}) {
    auto testConfig = defaultConfig_;
    testConfig.peers()->at(kStaticPeer1Index).additional_remote_as_4_byte() =
        invalidAsn;
    EXPECT_THROW(Config config(testConfig), BgpError);
  }
}

/*
 * Both remote-AS representations (i32 and 4-byte) set at the SAME config level
 * is an ambiguous config and must be rejected at config-construction time.
 */
TEST_F(ConfigTestFixture, remoteASNegativeTest) {
  {
    // Both set at the peer level.
    auto testConfig = defaultConfig_;
    testConfig.peers()->clear();
    thrift::BgpPeer p;
    p.remote_as_4_byte() = kPeerAsn7;
    p.remote_as() = static_cast<int32_t>(kPeerAsn8);
    p.local_addr() = kLocalAddr7.str();
    p.peer_addr() = kPeerAddr7.str();
    p.next_hop4() = kNextHopV4_7.str();
    p.next_hop6() = kNextHopV6_7.str();
    testConfig.peers()->emplace_back(p);
    EXPECT_THROW(Config config(testConfig), BgpError);
  }
  {
    // Both set at the peer-group level (peer omits remote AS).
    auto testConfig = defaultConfig_;
    testConfig.peers()->clear();
    thrift::PeerGroup pg;
    pg.name() = "PEERGROUP_REMOTE_AS_CONFLICT";
    pg.next_hop_self() = true;
    pg.peer_tag() = kPeerTypeFa;
    pg.bgp_peer_timers() = timers1_;
    pg.remote_as_4_byte() = kPeerAsn7;
    pg.remote_as() = static_cast<int32_t>(kPeerAsn8);
    auto peerGroups = testConfig.peer_groups().to_optional();
    peerGroups->emplace_back(pg);
    testConfig.peer_groups().from_optional(peerGroups);

    thrift::BgpPeer p;
    p.local_addr() = kLocalAddr8.str();
    p.peer_addr() = kPeerAddr8.str();
    p.next_hop4() = kNextHopV4_8.str();
    p.next_hop6() = kNextHopV6_8.str();
    p.peer_group_name() = *pg.name();
    testConfig.peers()->emplace_back(p);
    EXPECT_THROW(Config config(testConfig), BgpError);
  }
}

TEST_F(ConfigTestFixture, PeeringParamsPeerGroupName) {
  Config config(defaultConfig_);
  auto peerToConfig = config.getPeerToConfig();
  {
    // staticPeer1_ has no peer group
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
    EXPECT_EQ(params.peerGroupName, std::nullopt);
  }
  {
    // staticPeer3_ has peer group
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr5));
    EXPECT_EQ(params.peerGroupName, "PEERGROUP_RSW_CSW_V4");
  }
}

/*
 * Verify the global gate and peer > peer-group resolution hierarchy for
 * route_refresh (RFC 2918). Enhanced Route Refresh (RFC 7313) retains its
 * existing peer > peer-group > false behavior.
 *
 * The global Route Refresh setting is a kill switch. When enabled, the
 * peer/peer-group hierarchy supplies an enabled default:
 *   peer7  — neither peer nor peer-group sets it -> default true
 *   peer8  — peer-group sets true, peer doesn't override -> inherited true
 *   peer9  — peer overrides peer-group's true with false -> peer wins
 *   peer10 — per-peer sets true with no peer-group -> peer wins
 */
TEST_F(ConfigTestFixture, RouteRefreshConfigHierarchy) {
  // Local constants for peer10 (Utils.h only defines through index 9).
  const auto kLocalAddr10 = folly::IPAddress("127.1.0.9");
  const auto kPeerAddr10 = folly::IPAddress("127.10.0.1");
  const auto kNextHopV4_10 = folly::IPAddress("127.5.0.9");
  const auto kNextHopV6_10 = folly::IPAddress("2401:db00:e011:411:1000::31");
  const uint32_t kPeerAsn10 = 64547;

  auto testConfig = defaultConfig_;
  testConfig.peers()->clear();

  // Peer-group sets BOTH flags to true. Peer-level overrides cascade per-peer.
  thrift::PeerGroup peergroup;
  peergroup.name() = "PEERGROUP_RR_HIERARCHY";
  peergroup.next_hop_self() = true;
  peergroup.peer_tag() = kPeerTypeFa;
  peergroup.bgp_peer_timers() = timers1_;
  peergroup.route_refresh() = true;
  peergroup.enhanced_route_refresh() = true;
  auto peerGroups = testConfig.peer_groups().to_optional();
  peerGroups->emplace_back(peergroup);
  testConfig.peer_groups().from_optional(peerGroups);

  // peer7: no peer-group and no per-peer flag.
  {
    thrift::BgpPeer p;
    p.remote_as_4_byte() = kPeerAsn7;
    p.local_addr() = kLocalAddr7.str();
    p.peer_addr() = kPeerAddr7.str();
    p.next_hop4() = kNextHopV4_7.str();
    p.next_hop6() = kNextHopV6_7.str();
    testConfig.peers()->emplace_back(p);
  }
  // peer8: peer-group sets both true, peer doesn't override -> inherits true.
  {
    thrift::BgpPeer p;
    p.remote_as_4_byte() = kPeerAsn8;
    p.local_addr() = kLocalAddr8.str();
    p.peer_addr() = kPeerAddr8.str();
    p.next_hop4() = kNextHopV4_8.str();
    p.next_hop6() = kNextHopV6_8.str();
    p.peer_group_name() = *peergroup.name();
    testConfig.peers()->emplace_back(p);
  }
  // peer9: peer-group sets both true, peer overrides both with false -> false.
  {
    thrift::BgpPeer p;
    p.remote_as_4_byte() = kPeerAsn9;
    p.local_addr() = kLocalAddr9.str();
    p.peer_addr() = kPeerAddr9.str();
    p.next_hop4() = kNextHopV4_9.str();
    p.next_hop6() = kNextHopV6_9.str();
    p.peer_group_name() = *peergroup.name();
    p.route_refresh() = false;
    p.enhanced_route_refresh() = false;
    testConfig.peers()->emplace_back(p);
  }
  // peer10: no peer-group, per-peer sets both true -> per-peer cascade wins.
  {
    thrift::BgpPeer p;
    p.remote_as_4_byte() = kPeerAsn10;
    p.local_addr() = kLocalAddr10.str();
    p.peer_addr() = kPeerAddr10.str();
    p.next_hop4() = kNextHopV4_10.str();
    p.next_hop6() = kNextHopV6_10.str();
    p.route_refresh() = true;
    p.enhanced_route_refresh() = true;
    testConfig.peers()->emplace_back(p);
  }

  // The global setting defaults false and masks even an explicit peer enable.
  {
    Config config(testConfig);
    const auto& peerToConfig = config.getPeerToConfig();
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr10));
    EXPECT_FALSE(params.isRouteRefreshConfigured);
  }

  testConfig.bgp_setting_config() = thrift::BgpSettingConfig();
  testConfig.bgp_setting_config()->enable_route_refresh() = true;
  Config config(testConfig);
  const auto& peerToConfig = config.getPeerToConfig();

  // peer7: global setting supplies the enabled default.
  {
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr7));
    EXPECT_TRUE(params.isRouteRefreshConfigured);
    EXPECT_FALSE(params.isEnhancedRouteRefreshConfigured);
  }
  // peer8: inherits from peer-group
  {
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr8));
    EXPECT_TRUE(params.isRouteRefreshConfigured);
    EXPECT_TRUE(params.isEnhancedRouteRefreshConfigured);
  }
  // peer9: per-peer override beats peer-group
  {
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr9));
    EXPECT_FALSE(params.isRouteRefreshConfigured);
    EXPECT_FALSE(params.isEnhancedRouteRefreshConfigured);
  }
  // peer10: per-peer-only true cascades to params with no peer-group
  {
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr10));
    EXPECT_TRUE(params.isRouteRefreshConfigured);
    EXPECT_TRUE(params.isEnhancedRouteRefreshConfigured);
  }
}

TEST_F(ConfigTestFixture, localASNegativeTest) {
  {
    // Local-AS configured as same as remote-as.
    auto testConfig = defaultConfig_;
    testConfig.peers()->clear();
    thrift::BgpPeer staticPeer;
    staticPeer.remote_as_4_byte() = kAsn6;
    staticPeer.local_as_4_byte() = kAsn6;
    staticPeer.local_addr() = kLocalAddr7.str();
    staticPeer.peer_addr() = kPeerAddr7.str();
    staticPeer.next_hop4() = kNextHopV4_7.str();
    staticPeer.next_hop6() = kNextHopV6_7.str();
    testConfig.peers()->emplace_back(staticPeer);
    EXPECT_ANY_THROW(Config{testConfig});
  }
  {
    // Local-AS configured on iBGP peer.
    auto testConfig = defaultConfig_;
    testConfig.peers()->clear();
    thrift::BgpPeer staticPeer;
    staticPeer.local_as_4_byte() = kAsn6;
    staticPeer.remote_as_4_byte() = *defaultConfig_.local_as_4_byte();
    staticPeer.local_addr() = kLocalAddr7.str();
    staticPeer.peer_addr() = kPeerAddr7.str();
    staticPeer.next_hop4() = kNextHopV4_7.str();
    staticPeer.next_hop6() = kNextHopV6_7.str();
    testConfig.peers()->emplace_back(staticPeer);
    EXPECT_ANY_THROW(Config{testConfig});
  }
}

TEST_F(ConfigTestFixture, lbwConfigTest) {
  {
    /*
     * Negative test.  Advertise_link_bandwidth is SET_LINK_BPS
     * but we don't have LBW in peer config and we don't have any peer group
     * configured.
     */
    XLOG(
        INFO,
        "Negative test1 to ensure that LBW is configured if "
        "advertise_link_bandwidth is SET_LINK_BPS");
    thrift::BgpConfig myNewConfig;
    myNewConfig.router_id() = kLocalAddr1.str();
    myNewConfig.local_as_4_byte() = kAsn1;
    myNewConfig.hold_time() = kHoldTime.count();
    auto peer = createDefaultBgpPeer();
    peer.advertise_link_bandwidth() = AdvertiseLinkBandwidth::SET_LINK_BPS;
    std::vector<thrift::BgpPeer> myPeers;
    myPeers.push_back(peer);
    myNewConfig.peers() = myPeers;
    EXPECT_ANY_THROW(Config config(myNewConfig));
  }
  {
    /*
     * Another negative test.  Advertise_link_bandwidth is SET_LINK_BPS
     * but we don't have LBW in peer config. We do have some peer group
     * configured, but no LBW in that either.
     */
    XLOG(
        INFO,
        "Negative test2 to ensure that LBW is configured if "
        "advertise_link_bandwidth is SET_LINK_BPS");
    thrift::BgpConfig myNewConfig;
    myNewConfig.router_id() = kLocalAddr1.str();
    myNewConfig.local_as_4_byte() = kAsn1;
    myNewConfig.hold_time() = kHoldTime.count();

    thrift::PeerGroup peergroup;
    peergroup.name() = "PEERGROUP_TEST";
    peergroup.advertise_link_bandwidth() = AdvertiseLinkBandwidth::SET_LINK_BPS;
    std::vector<thrift::PeerGroup> peerGroups;
    peerGroups.push_back(peergroup);
    myNewConfig.peer_groups() = peerGroups;

    auto peer = createDefaultBgpPeer();
    peer.peer_group_name() = *peergroup.name();
    std::vector<thrift::BgpPeer> myPeers;
    myPeers.push_back(peer);
    myNewConfig.peers() = myPeers;

    EXPECT_ANY_THROW(Config config(myNewConfig));
  }
  {
    // Test SET_LINK_BPS in peer config
    XLOG(INFO, "Test LBW setting in peer config");
    thrift::BgpConfig myNewConfig;
    myNewConfig.router_id() = kLocalAddr1.str();
    myNewConfig.local_as_4_byte() = kAsn1;
    myNewConfig.hold_time() = kHoldTime.count();
    auto peer = createDefaultBgpPeer();
    peer.advertise_link_bandwidth() = AdvertiseLinkBandwidth::SET_LINK_BPS;
    peer.link_bandwidth_bps() = "100G";
    std::vector<thrift::BgpPeer> myPeers;
    myPeers.push_back(peer);
    myNewConfig.peers() = myPeers;
    // set config and populate config database
    Config config(myNewConfig);

    // test config database
    auto globalConfig = config.getBgpGlobalConfig();
    auto peerToConfig = config.getPeerToConfig();
    EXPECT_EQ(1, peerToConfig.size());
    EXPECT_EQ(kPeerAddr1, peerToConfig.at(kPeerAddr1)->peerAddr);
    const auto& peerConfig = peerToConfig.at(kPeerAddr1)->commonPeerGroupConfig;
    EXPECT_EQ(
        AdvertiseLinkBandwidth::SET_LINK_BPS,
        peerConfig.advertiseLinkBandwidth.value());
    EXPECT_EQ(kLbw100G, peerConfig.linkBandwidthBps.value());

    // test PeeringParams
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr1));
    EXPECT_EQ(kPeerAddr1, params.peerAddr);
    EXPECT_EQ(kAsn1, params.localAs);
    EXPECT_EQ(
        AdvertiseLinkBandwidth::SET_LINK_BPS, params.advertiseLinkBandwidth);
    EXPECT_EQ(kLbw100G, params.linkBandwidthBps.value());
  }
  {
    /*
     * link_bandwidth_bps is resolved whenever it is configured, even when
     * neither advertise nor receive LBW is SET_LINK_BPS. The value is consumed
     * independently of per-peer config by the policy engine (SET_LINK_BPS route
     * action) and AGGREGATE_LOCAL; skipping resolution here CHECK-crashed peers
     * that pull LBW in via a route policy.
     */
    XLOG(INFO, "Test LBW is resolved even when advertise/receive LBW disabled");
    thrift::BgpConfig myNewConfig;
    myNewConfig.router_id() = kLocalAddr1.str();
    myNewConfig.local_as_4_byte() = kAsn1;
    myNewConfig.hold_time() = kHoldTime.count();
    auto peer = createDefaultBgpPeer();
    peer.advertise_link_bandwidth() = AdvertiseLinkBandwidth::DISABLE;
    peer.receive_link_bandwidth() = ReceiveLinkBandwidth::DISABLE;
    peer.link_bandwidth_bps() = "100G";
    std::vector<thrift::BgpPeer> myPeers;
    myPeers.push_back(peer);
    myNewConfig.peers() = myPeers;
    // set config and populate config database
    Config config(myNewConfig);

    // test config database
    auto globalConfig = config.getBgpGlobalConfig();
    auto peerToConfig = config.getPeerToConfig();
    EXPECT_EQ(1, peerToConfig.size());
    EXPECT_EQ(kPeerAddr1, peerToConfig.at(kPeerAddr1)->peerAddr);
    const auto& peerConfig = peerToConfig.at(kPeerAddr1)->commonPeerGroupConfig;
    EXPECT_EQ(
        AdvertiseLinkBandwidth::DISABLE,
        peerConfig.advertiseLinkBandwidth.value());
    EXPECT_EQ(
        ReceiveLinkBandwidth::DISABLE, peerConfig.receiveLinkBandwidth.value());
    EXPECT_EQ(kLbw100G, peerConfig.linkBandwidthBps.value());

    // test PeeringParams
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr1));
    EXPECT_EQ(kPeerAddr1, params.peerAddr);
    EXPECT_EQ(kAsn1, params.localAs);
    EXPECT_EQ(AdvertiseLinkBandwidth::DISABLE, params.advertiseLinkBandwidth);
    EXPECT_EQ(ReceiveLinkBandwidth::DISABLE, params.receiveLinkBandwidth);
    EXPECT_EQ(kLbw100G, params.linkBandwidthBps.value());
  }
  {
    /*
     * link_bandwidth_bps = "auto" but advertise/receive LBW are left unset and
     * no peer-subnet LBW map is provided. Resolution is attempted but "auto"
     * has no FSDB data to resolve against, so linkBandwidthBps stays unset --
     * and with LBW not advertised/received there is no throw.
     */
    XLOG(INFO, "Test lbw=auto with no FSDB data resolves to unset, no throw");
    thrift::BgpConfig myNewConfig;
    myNewConfig.router_id() = kLocalAddr1.str();
    myNewConfig.local_as_4_byte() = kAsn1;
    myNewConfig.hold_time() = kHoldTime.count();
    auto peer = createDefaultBgpPeer();
    peer.link_bandwidth_bps() = "auto";
    std::vector<thrift::BgpPeer> myPeers;
    myPeers.push_back(peer);
    myNewConfig.peers() = myPeers;
    Config config(myNewConfig);

    const auto& peerToConfig = config.getPeerToConfig();
    EXPECT_EQ(1, peerToConfig.size());
    const auto& peerConfig = peerToConfig.at(kPeerAddr1)->commonPeerGroupConfig;
    EXPECT_FALSE(peerConfig.linkBandwidthBps.has_value());

    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr1));
    EXPECT_FALSE(params.linkBandwidthBps.has_value());
  }
  {
    // Test SET_LINK_BPS in peer-group config
    XLOG(INFO, "Test LBW setting in peer group config");
    thrift::BgpConfig myNewConfig;
    myNewConfig.router_id() = kLocalAddr1.str();
    myNewConfig.local_as_4_byte() = kAsn1;
    myNewConfig.hold_time() = kHoldTime.count();

    thrift::PeerGroup peergroup;
    peergroup.name() = "PEERGROUP_TEST";
    peergroup.advertise_link_bandwidth() = AdvertiseLinkBandwidth::SET_LINK_BPS;
    // test that we trim spaces around the string
    peergroup.link_bandwidth_bps() = " 150g ";
    std::vector<thrift::PeerGroup> peerGroups;
    peerGroups.push_back(peergroup);
    myNewConfig.peer_groups() = peerGroups;

    auto peer = createDefaultBgpPeer();
    peer.peer_group_name() = *peergroup.name();
    std::vector<thrift::BgpPeer> myPeers;
    myPeers.push_back(peer);
    myNewConfig.peers() = myPeers;

    // set config and populate config database
    Config config(myNewConfig);
    auto globalConfig = config.getBgpGlobalConfig();
    auto peerToConfig = config.getPeerToConfig();
    EXPECT_EQ(1, peerToConfig.size());
    EXPECT_EQ(kPeerAddr1, peerToConfig.at(kPeerAddr1)->peerAddr);
    const auto& peerConfig = peerToConfig.at(kPeerAddr1)->commonPeerGroupConfig;
    EXPECT_EQ(
        AdvertiseLinkBandwidth::SET_LINK_BPS,
        peerConfig.advertiseLinkBandwidth.value());
    EXPECT_EQ(kLbw150G, peerConfig.linkBandwidthBps.value());

    // test PeeringParams
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr1));
    EXPECT_EQ(kPeerAddr1, params.peerAddr);
    EXPECT_EQ(kAsn1, params.localAs);
    EXPECT_EQ(
        AdvertiseLinkBandwidth::SET_LINK_BPS, params.advertiseLinkBandwidth);
    EXPECT_EQ(kLbw150G, params.linkBandwidthBps.value());
  }
  {
    // Test that we prefer value configured in peer vs peer-group
    XLOG(INFO, "Test that we prefer LBW setting peer vw peer group");
    thrift::BgpConfig myNewConfig;
    myNewConfig.router_id() = kLocalAddr1.str();
    myNewConfig.local_as_4_byte() = kAsn1;
    myNewConfig.hold_time() = kHoldTime.count();

    thrift::PeerGroup peergroup;
    peergroup.name() = "PEERGROUP_TEST";
    peergroup.advertise_link_bandwidth() = AdvertiseLinkBandwidth::SET_LINK_BPS;
    // Set LBW in peer group to 150G
    peergroup.link_bandwidth_bps() = "150G";
    std::vector<thrift::PeerGroup> peerGroups;
    peerGroups.push_back(peergroup);
    myNewConfig.peer_groups() = peerGroups;

    auto peer = createDefaultBgpPeer();
    peer.peer_group_name() = *peergroup.name();
    // Set LBW in peer to 100G
    peer.link_bandwidth_bps() = "100G";
    std::vector<thrift::BgpPeer> myPeers;
    myPeers.push_back(peer);
    myNewConfig.peers() = myPeers;

    // set config and populate config database
    Config config(myNewConfig);
    auto globalConfig = config.getBgpGlobalConfig();
    auto peerToConfig = config.getPeerToConfig();
    EXPECT_EQ(1, peerToConfig.size());
    EXPECT_EQ(kPeerAddr1, peerToConfig.at(kPeerAddr1)->peerAddr);
    const auto& peerConfig = peerToConfig.at(kPeerAddr1)->commonPeerGroupConfig;
    EXPECT_EQ(
        AdvertiseLinkBandwidth::SET_LINK_BPS,
        peerConfig.advertiseLinkBandwidth.value());
    // Expect LBW to be that configured in peer, not in peer group
    EXPECT_EQ(kLbw100G, peerConfig.linkBandwidthBps.value());

    // test PeeringParams
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr1));
    EXPECT_EQ(kPeerAddr1, params.peerAddr);
    EXPECT_EQ(kAsn1, params.localAs);
    EXPECT_EQ(
        AdvertiseLinkBandwidth::SET_LINK_BPS, params.advertiseLinkBandwidth);
    // Expect LBW to be that configured in peer, not in peer group
    EXPECT_EQ(kLbw100G, params.linkBandwidthBps.value());
  }
}

TEST_F(ConfigTestFixture, AutoLbwConfigTest) {
  {
    XLOG(
        INFO,
        "test that when PeerSubnetLbwMap is not set "
        "linkbandwidthBps will be std::nullopt");
    thrift::BgpConfig myNewConfig;
    myNewConfig.router_id() = kLocalAddr1.str();
    myNewConfig.local_as_4_byte() = kAsn1;
    myNewConfig.hold_time() = kHoldTime.count();
    auto peer = createDefaultBgpPeer();
    peer.advertise_link_bandwidth() = AdvertiseLinkBandwidth::SET_LINK_BPS;
    peer.link_bandwidth_bps() = "auto";
    std::vector<thrift::BgpPeer> myPeers;
    myPeers.push_back(peer);
    myNewConfig.peers() = myPeers;
    Config config(myNewConfig);

    // test config database
    auto globalConfig = config.getBgpGlobalConfig();
    auto peerToConfig = config.getPeerToConfig();
    EXPECT_EQ(1, peerToConfig.size());
    EXPECT_EQ(kPeerAddr1, peerToConfig.at(kPeerAddr1)->peerAddr);
    const auto& peerConfig = peerToConfig.at(kPeerAddr1)->commonPeerGroupConfig;
    EXPECT_EQ(
        AdvertiseLinkBandwidth::SET_LINK_BPS,
        peerConfig.advertiseLinkBandwidth.value());
    EXPECT_FALSE(peerConfig.linkBandwidthBps.has_value());

    // test PeeringParams
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr1));
    EXPECT_EQ(kPeerAddr1, params.peerAddr);
    EXPECT_EQ(kAsn1, params.localAs);
    EXPECT_EQ(
        AdvertiseLinkBandwidth::SET_LINK_BPS, params.advertiseLinkBandwidth);
    EXPECT_FALSE(params.linkBandwidthBps.has_value());
  }
  {
    XLOG(
        INFO,
        "test that static peer not mapping to peerSubnetLbwMap gets "
        "std::nullopt as link_bandwidth_bps");

    folly::F14NodeMap<folly::CIDRNetwork, int64_t> peerSubnetLbwMap;
    peerSubnetLbwMap.emplace(
        folly::CIDRNetwork(kPeerAddr2, 31), 100000 /* 100Gbps */);

    thrift::BgpConfig myNewConfig;
    myNewConfig.router_id() = kLocalAddr1.str();
    myNewConfig.local_as_4_byte() = kAsn1;
    myNewConfig.hold_time() = kHoldTime.count();
    auto peer = createDefaultBgpPeer();
    peer.advertise_link_bandwidth() = AdvertiseLinkBandwidth::SET_LINK_BPS;
    peer.link_bandwidth_bps() = "auto";
    std::vector<thrift::BgpPeer> myPeers;
    myPeers.push_back(peer);
    myNewConfig.peers() = myPeers;
    Config config(myNewConfig, std::move(peerSubnetLbwMap));

    // test config database
    auto globalConfig = config.getBgpGlobalConfig();
    auto peerToConfig = config.getPeerToConfig();
    EXPECT_EQ(1, peerToConfig.size());
    EXPECT_EQ(kPeerAddr1, peerToConfig.at(kPeerAddr1)->peerAddr);
    const auto& peerConfig = peerToConfig.at(kPeerAddr1)->commonPeerGroupConfig;
    EXPECT_EQ(
        AdvertiseLinkBandwidth::SET_LINK_BPS,
        peerConfig.advertiseLinkBandwidth.value());
    EXPECT_FALSE(peerConfig.linkBandwidthBps.has_value());

    // test PeeringParams
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr1));
    EXPECT_EQ(kPeerAddr1, params.peerAddr);
    EXPECT_EQ(kAsn1, params.localAs);
    EXPECT_EQ(
        AdvertiseLinkBandwidth::SET_LINK_BPS, params.advertiseLinkBandwidth);
    EXPECT_FALSE(params.linkBandwidthBps.has_value());
  }
  {
    XLOG(
        INFO,
        "test that dynamic peer gets std::nullopt as link_bandwidth_bps in auto mode");

    folly::F14NodeMap<folly::CIDRNetwork, int64_t> peerSubnetLbwMap;
    const auto kPeerAddrXYZ = folly::CIDRNetwork("2401:db00:e01f:701:1::", 80);

    peerSubnetLbwMap.emplace(kPeerAddrXYZ, 100000 /* 100Gbps */);

    thrift::BgpConfig myNewConfig;
    myNewConfig.router_id() = kLocalAddr1.str();
    myNewConfig.local_as_4_byte() = kAsn1;
    myNewConfig.hold_time() = kHoldTime.count();
    auto peer = createBgpPeer(
        kAsn1, // remoteAsn
        kLocalAddr1,
        kPeerAddrXYZ,
        kNextHopV4_1,
        kNextHopV6_1,
        false, // isPassive
        kPeerTypeCsw);

    peer.advertise_link_bandwidth() = AdvertiseLinkBandwidth::SET_LINK_BPS;
    peer.link_bandwidth_bps() = "auto";
    std::vector<thrift::BgpPeer> myPeers;
    myPeers.push_back(peer);
    myNewConfig.peers() = myPeers;
    Config config(myNewConfig, std::move(peerSubnetLbwMap));

    // test config database
    auto globalConfig = config.getBgpGlobalConfig();
    auto peerToConfig = config.getDynamicPeerToConfig();
    EXPECT_EQ(1, peerToConfig.size());
    EXPECT_EQ(kPeerAddrXYZ, peerToConfig.at(kPeerAddrXYZ)->peerPrefix);
    const auto& peerConfig =
        peerToConfig.at(kPeerAddrXYZ)->commonPeerGroupConfig;
    EXPECT_EQ(
        AdvertiseLinkBandwidth::SET_LINK_BPS,
        peerConfig.advertiseLinkBandwidth.value());
    EXPECT_FALSE(peerConfig.linkBandwidthBps.has_value());

    // test PeeringParams
    auto params =
        config.getPeeringParamsForDynamicPeer(*peerToConfig.at(kPeerAddrXYZ));
    EXPECT_EQ(kPeerAddrXYZ, params.peerPrefix);
    EXPECT_EQ(kAsn1, params.localAs);
    EXPECT_EQ(
        AdvertiseLinkBandwidth::SET_LINK_BPS, params.advertiseLinkBandwidth);
    EXPECT_FALSE(params.linkBandwidthBps.has_value());
  }
  {
    // Test SET_LINK_BPS in peer config
    XLOG(INFO, "Test ALBW setting in peer config");

    folly::F14NodeMap<folly::CIDRNetwork, int64_t> peerSubnetLbwMap;
    peerSubnetLbwMap.emplace(
        folly::CIDRNetwork(kPeerAddr1, 31), 100000 /* 100Gbps */);
    peerSubnetLbwMap.emplace(
        folly::CIDRNetwork("1.1.0.1", 31), 150000 /* 150Gbps */
    );

    thrift::BgpConfig myNewConfig;
    myNewConfig.router_id() = kLocalAddr1.str();
    myNewConfig.local_as_4_byte() = kAsn1;
    myNewConfig.hold_time() = kHoldTime.count();
    auto peer = createDefaultBgpPeer();
    peer.advertise_link_bandwidth() = AdvertiseLinkBandwidth::SET_LINK_BPS;
    peer.link_bandwidth_bps() = "auto";
    std::vector<thrift::BgpPeer> myPeers;
    myPeers.push_back(peer);
    myNewConfig.peers() = myPeers;
    // set config and populate config database
    Config config(myNewConfig, std::move(peerSubnetLbwMap));

    // test config database
    auto globalConfig = config.getBgpGlobalConfig();
    auto peerToConfig = config.getPeerToConfig();
    EXPECT_EQ(1, peerToConfig.size());
    EXPECT_EQ(kPeerAddr1, peerToConfig.at(kPeerAddr1)->peerAddr);
    const auto& peerConfig = peerToConfig.at(kPeerAddr1)->commonPeerGroupConfig;
    EXPECT_EQ(
        AdvertiseLinkBandwidth::SET_LINK_BPS,
        peerConfig.advertiseLinkBandwidth.value());
    EXPECT_EQ(kLbw100G, peerConfig.linkBandwidthBps.value());

    // test PeeringParams
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr1));
    EXPECT_EQ(kPeerAddr1, params.peerAddr);
    EXPECT_EQ(kAsn1, params.localAs);
    EXPECT_EQ(
        AdvertiseLinkBandwidth::SET_LINK_BPS, params.advertiseLinkBandwidth);
    EXPECT_EQ(kLbw100G, params.linkBandwidthBps.value());
  }
  {
    // Test SET_LINK_BPS in peer-group config
    XLOG(INFO, "Test LBW setting in peer group config");

    folly::F14NodeMap<folly::CIDRNetwork, int64_t> peerSubnetLbwMap;
    peerSubnetLbwMap.emplace(
        folly::CIDRNetwork(kPeerAddr1, 31), 100000 /* 100Gbps */);
    peerSubnetLbwMap.emplace(
        folly::CIDRNetwork("1.1.0.1", 31), 150000 /* 150Gbps */
    );

    thrift::BgpConfig myNewConfig;
    myNewConfig.router_id() = kLocalAddr1.str();
    myNewConfig.local_as_4_byte() = kAsn1;
    myNewConfig.hold_time() = kHoldTime.count();

    thrift::PeerGroup peergroup;
    peergroup.name() = "PEERGROUP_TEST";
    peergroup.advertise_link_bandwidth() = AdvertiseLinkBandwidth::SET_LINK_BPS;
    // test that we trim spaces around the string
    peergroup.link_bandwidth_bps() = "auto";
    std::vector<thrift::PeerGroup> peerGroups;
    peerGroups.push_back(peergroup);
    myNewConfig.peer_groups() = peerGroups;

    auto peer = createDefaultBgpPeer();
    peer.peer_group_name() = *peergroup.name();
    std::vector<thrift::BgpPeer> myPeers;
    myPeers.push_back(peer);
    myNewConfig.peers() = myPeers;

    // set config and populate config database
    Config config(myNewConfig, std::move(peerSubnetLbwMap));
    auto globalConfig = config.getBgpGlobalConfig();
    auto peerToConfig = config.getPeerToConfig();
    EXPECT_EQ(1, peerToConfig.size());
    EXPECT_EQ(kPeerAddr1, peerToConfig.at(kPeerAddr1)->peerAddr);
    const auto& peerConfig = peerToConfig.at(kPeerAddr1)->commonPeerGroupConfig;
    EXPECT_EQ(
        AdvertiseLinkBandwidth::SET_LINK_BPS,
        peerConfig.advertiseLinkBandwidth.value());
    EXPECT_EQ(kLbw100G, peerConfig.linkBandwidthBps.value());

    // test PeeringParams
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr1));
    EXPECT_EQ(kPeerAddr1, params.peerAddr);
    EXPECT_EQ(kAsn1, params.localAs);
    EXPECT_EQ(
        AdvertiseLinkBandwidth::SET_LINK_BPS, params.advertiseLinkBandwidth);
    EXPECT_EQ(kLbw100G, params.linkBandwidthBps.value());
  }
}

TEST_F(ConfigTestFixture, populateConfigDatabaseConfedTest) {
  {
    // valid confed configuration with local_confed_as and is_confed_peer
    thrift::BgpConfig myNewConfig;
    myNewConfig.router_id() = kLocalAddr1.str();
    myNewConfig.local_as_4_byte() = kAsn1;
    myNewConfig.hold_time() = kHoldTime.count();
    myNewConfig.local_confed_as_4_byte() = kAsn3;
    auto peer = createBgpPeer(
        kAsn1,
        kLocalAddr1,
        kPeerAddr1,
        kNextHopV4_1,
        kNextHopV6_1,
        false,
        kPeerTypeCsw);
    peer.is_confed_peer() = true;
    std::vector<thrift::BgpPeer> myPeers;
    myPeers.push_back(peer);
    myNewConfig.peers() = myPeers;

    // set config and populate config database
    Config config(myNewConfig);

    // test config database
    auto globalConfig = config.getBgpGlobalConfig();
    EXPECT_EQ(kLocalAddr1, globalConfig->routerId);
    EXPECT_EQ(kAsn1, globalConfig->localAsn);
    EXPECT_EQ(kAsn3, globalConfig->localConfedAsn);

    auto peerToConfig = config.getPeerToConfig();
    EXPECT_EQ(1, peerToConfig.size());
    EXPECT_EQ(kPeerAddr1, peerToConfig.at(kPeerAddr1)->peerAddr);
    const auto& peerConfig = peerToConfig.at(kPeerAddr1)->commonPeerGroupConfig;
    EXPECT_EQ(kAsn1, peerConfig.peerAsn);
    EXPECT_TRUE(*peerConfig.isConfedPeer);

    // test PeeringParams
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr1));
    EXPECT_EQ(kPeerAddr1, params.peerAddr);
    EXPECT_EQ(kAsn3, params.localAs);
    EXPECT_EQ(kAsn1, params.remoteAs);
    EXPECT_TRUE(params.isConfedPeer);
    EXPECT_EQ(kAsn3, params.localConfedAs);
    EXPECT_EQ(kAsn1, params.asConfedId);
  }
  {
    // is_confed_peer config without local_confed_as config
    thrift::BgpConfig myNewConfig;
    myNewConfig.router_id() = kLocalAddr1.str();
    myNewConfig.local_as_4_byte() = kAsn1;
    myNewConfig.hold_time() = kHoldTime.count();
    auto peer = createBgpPeer(
        kAsn1,
        kLocalAddr1,
        kPeerAddr1,
        kNextHopV4_1,
        kNextHopV6_1,
        false,
        kPeerTypeCsw);
    peer.is_confed_peer() = true;
    std::vector<thrift::BgpPeer> myPeers;
    myPeers.push_back(peer);
    myNewConfig.peers() = myPeers;

    // set config and populate config database
    Config config(myNewConfig);

    // test config database
    auto peerToConfig = config.getPeerToConfig();
    EXPECT_EQ(1, peerToConfig.size());
    EXPECT_EQ(kPeerAddr1, peerToConfig.at(kPeerAddr1)->peerAddr);
    const auto& peerConfig = peerToConfig.at(kPeerAddr1)->commonPeerGroupConfig;
    EXPECT_EQ(kAsn1, peerConfig.peerAsn);
    EXPECT_TRUE(*peerConfig.isConfedPeer);

    // test PeeringParams
    auto expectedMessage =
        "is_confed_peer is configured, but local_confed_as is missing";
    try {
      auto param = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr1));
    } catch (const BgpError& error) {
      EXPECT_EQ(expectedMessage, *error.message());
    }
  }
}

TEST_F(ConfigTestFixture, getLocalRouteTest) {
  // set input config
  thrift::BgpConfig myNewConfig;
  myNewConfig.router_id() = kIPAddr1.str();
  myNewConfig.local_as_4_byte() = kDefaultAsn;
  vector<string> communities = {"100:1", "100:2"};
  const string policyName = "SET-ATTR";
  auto network4_1 = createBgpNetwork(IPAddress::networkToString(kV4Prefix1));
  auto network4_2 =
      createBgpNetwork(IPAddress::networkToString(kV4Prefix2), communities);
  auto network4_3 = createBgpNetwork(
      IPAddress::networkToString(kV4Prefix3),
      std::nullopt, // communities
      policyName);
  myNewConfig.networks4()->emplace_back(network4_1);
  myNewConfig.networks4()->emplace_back(network4_2);
  myNewConfig.networks4()->emplace_back(network4_3);
  auto network6_1 =
      createBgpNetwork(IPAddress::networkToString(kV6Prefix1), communities);
  auto network6_2 = createBgpNetwork(
      IPAddress::networkToString(kV6Prefix2),
      std::nullopt, // communities
      policyName);
  myNewConfig.networks6()->emplace_back(network6_1);
  myNewConfig.networks6()->emplace_back(network6_2);
  Config config(myNewConfig);

  // create expected communities
  vector<BgpAttrCommunity> expectedEmptyCommunities;
  vector<BgpAttrCommunity> expectedCommunities =
      createBgpAttrCommunityVec(communities);

  // get local routes
  auto localRoutes = config.getLocalRoutes();
  ASSERT_EQ(5, localRoutes.size());
  EXPECT_TRUE(localRoutes.find(kV4Prefix1) != localRoutes.end());
  EXPECT_TRUE(localRoutes.find(kV4Prefix2) != localRoutes.end());
  EXPECT_TRUE(localRoutes.find(kV4Prefix3) != localRoutes.end());
  EXPECT_TRUE(localRoutes.find(kV6Prefix1) != localRoutes.end());
  EXPECT_TRUE(localRoutes.find(kV6Prefix2) != localRoutes.end());
  EXPECT_EQ(network4_1, localRoutes[kV4Prefix1]);
  EXPECT_EQ(network4_2, localRoutes[kV4Prefix2]);
  EXPECT_EQ(network4_3, localRoutes[kV4Prefix3]);
  EXPECT_EQ(policyName, localRoutes[kV4Prefix3].policy_name());
  EXPECT_EQ(network6_1, localRoutes[kV6Prefix1]);
  EXPECT_EQ(network6_2, localRoutes[kV6Prefix2]);
  EXPECT_EQ(policyName, localRoutes[kV6Prefix2].policy_name());
}

TEST_F(ConfigTestFixture, verifyPopulatingPolicyFromConfigFile) {
  string configFile = "neteng/fboss/bgp/cpp/tests/sample_configs/bgpd.conf";
  auto configFilePath = getAbsoluteFilePath(configFile);
  Config config(configFilePath);
  auto myConfig = config.getConfig();

  // Verify that policy names are loaded correctly from config file
  {
    auto peerConfig = config.getConfigOfAPeer(folly::IPAddress("10.46.0.40"));
    EXPECT_EQ(kIngressPolicyName, *peerConfig->ingressPolicyName);
    EXPECT_EQ(kEgressPolicyName, *peerConfig->egressPolicyName);
  }
  // Sanity check policies are populated correctly
  auto policies = config.getPolicies();
  auto policyManager =
      std::make_shared<PolicyManager>(*policies, createTestBgpGlobalConfig());
  {
    const auto& policy = policyManager->getPolicyFromName(kIngressPolicyName);
    EXPECT_EQ(kIngressPolicyName, policy->getPolicyName());
    const auto& terms = policy->getPolicyTerms();
    EXPECT_EQ(1, terms.size());
  }
  {
    const auto& policy = policyManager->getPolicyFromName(kEgressPolicyName);
    EXPECT_EQ(kEgressPolicyName, policy->getPolicyName());
    const auto& terms = policy->getPolicyTerms();
    EXPECT_EQ(1, terms.size());
  }

  // Verify that there are no exceptions with valid config
  try {
    config.verifyIfPoliciesExist(policyManager);
  } catch (const std::exception&) {
    FAIL();
  }
}

TEST_F(ConfigTestFixture, verifyPolicyConfigValidation) {
  auto lambdaValidatePolicyConfig =
      [&](const std::optional<std::string>& ingressPolicy,
          const std::optional<std::string>& egressPolicy,
          const std::shared_ptr<const PolicyManager>& policyManager,
          const std::string& expectedStr) {
        thrift::BgpConfig myNewConfig;
        myNewConfig.router_id() = kLocalAddr1.str();
        auto peer = createBgpPeer(
            kAsn1,
            kLocalAddr3,
            kPeerAddr3,
            kNextHopV4_3,
            kNextHopV6_3,
            false,
            kPeerTypeCsw,
            ingressPolicy,
            egressPolicy);
        std::vector<thrift::BgpPeer> myPeers;
        myPeers.push_back(peer);
        myNewConfig.peers() = myPeers;

        Config config(myNewConfig);

        const auto& receivedStr = validateConfigGetError(config, policyManager);
        ASSERT_TRUE(receivedStr);
        EXPECT_EQ(expectedStr, *receivedStr);
      };

  {
    /*
     * Verify we do not accept peers with policy if no policies are configured
     * Test for ingress policy
     */
    std::string expectedStr(
        "Missing ingress policy (Ingress) needed for peer (127.3.0.1)");
    // nullptr indicates no policies i.e no PolicyManager created
    lambdaValidatePolicyConfig(
        kIngressPolicyName,
        std::nullopt, // egressPolicy
        nullptr,
        expectedStr);
  }
  // Create policy config which doesn't have necessary policies
  bgp_policy::BgpPolicyStatement bgpStatement;
  bgpStatement.name() = "NOT_MATCHING_POLICY";
  bgpStatement.description() = "";
  bgpStatement.policy_version() = "0";
  bgp_policy::BgpPolicies policies;
  policies.bgp_policy_statements()->emplace_back(bgpStatement);

  auto policyManager =
      std::make_shared<PolicyManager>(policies, createTestBgpGlobalConfig());
  /*
   * Verify we do not accept peers if policy config doesn't have matching
   * policy.
   */
  {
    // CASE: Test for egress policy missing
    std::string expectedStr(
        "Missing egress policy (Egress) needed for peer (127.3.0.1)");
    lambdaValidatePolicyConfig(
        std::nullopt, // ingressPolicy
        kEgressPolicyName,
        policyManager,
        expectedStr);
  }
  {
    // CASE: Test for ingress policy missing
    std::string expectedStr(
        "Missing ingress policy (Ingress) needed for peer (127.3.0.1)");
    lambdaValidatePolicyConfig(
        kIngressPolicyName,
        std::nullopt, // egressPolicy
        policyManager,
        expectedStr);
  }
}

TEST(PeeringParams, getUniquePeerIdTest) {
  PeeringParams p;

  p.peerId = kDefaultPeerId;
  toLower(p.peerId);
  EXPECT_EQ(p.peerId, p.getUniquePeerId());

  auto peerAddr = "10.127.240.0";
  p.peerId = peerAddr;
  EXPECT_EQ(peerAddr, p.getUniquePeerId());

  auto lc = "fsw003-lc101.p007.f01.prn3:1:v4";
  auto lcPeerId = "fsw003-lc101:1:v4";
  p.peerId = lc;
  EXPECT_EQ(lcPeerId, p.getUniquePeerId());

  auto fc = "fsw003-fc001.p007.f01.prn3:1:v6";
  auto fcPeerId = "fsw003-fc001:1:v6";
  p.peerId = fc;
  EXPECT_EQ(fcPeerId, p.getUniquePeerId());

  auto ssw = "ssw038.s003.f01.prn3:1:v4";
  auto sswPeerId = "ssw038:1:v4";
  p.peerId = ssw;
  EXPECT_EQ(sswPeerId, p.getUniquePeerId());

  auto rsw = "rsw024.p007.f01.prn3:1:v6";
  auto rswPeerId = "rsw024:1:v6";
  p.peerId = rsw;
  EXPECT_EQ(rswPeerId, p.getUniquePeerId());

  auto csw = "csw12d.ash1:1:v4";
  auto cswPeerId = "csw12d:1:v4";
  p.peerId = csw;
  EXPECT_EQ(cswPeerId, p.getUniquePeerId());

  auto fsw = "fsw003.p067.f01.prn3:1:v6";
  auto fswPeerId = "fsw003:1:v6";
  p.peerId = fsw;
  EXPECT_EQ(fswPeerId, p.getUniquePeerId());

  // Examing the default cap routes values.
  EXPECT_EQ(std::nullopt, p.preRouteLimit);
  EXPECT_EQ(std::nullopt, p.postRouteLimit);
}

TEST(PeeringParams, AcceptsRemoteAsTest) {
  constexpr uint32_t kPrimaryAs = 65001;
  constexpr uint32_t kAdditionalAs = 65002;
  constexpr uint32_t kUnexpectedAs = 65003;
  PeeringParams params;
  params.remoteAs = kPrimaryAs;
  params.additionalRemoteAs = kAdditionalAs;

  EXPECT_TRUE(params.acceptsRemoteAs(kPrimaryAs));
  EXPECT_TRUE(params.acceptsRemoteAs(kAdditionalAs));
  EXPECT_FALSE(params.acceptsRemoteAs(kUnexpectedAs));

  params.additionalRemoteAs.reset();
  EXPECT_FALSE(params.acceptsRemoteAs(kAdditionalAs));
}

TEST(Config, GetLinkBandwidthBpsTest) {
  {
    auto linkBps = Config::getLinkBandwidthBps("123");
    EXPECT_TRUE(linkBps.has_value());
    EXPECT_EQ(linkBps.value(), 123);
  }
  {
    auto linkBps = Config::getLinkBandwidthBps("123K");
    EXPECT_TRUE(linkBps.has_value());
    EXPECT_EQ(linkBps.value(), 123000);
  }
  {
    auto linkBps = Config::getLinkBandwidthBps("123M");
    EXPECT_TRUE(linkBps.has_value());
    EXPECT_EQ(linkBps.value(), 123000000);
  }
  {
    auto linkBps = Config::getLinkBandwidthBps("123G");
    EXPECT_TRUE(linkBps.has_value());
    EXPECT_EQ(linkBps.value(), 123000000000);
  }
  {
    auto linkBps = Config::getLinkBandwidthBps("12.5G");
    EXPECT_TRUE(linkBps.has_value());
    EXPECT_EQ(linkBps.value(), 12500000000);
  }
  {
    auto linkBps = Config::getLinkBandwidthBps("1.23456G");
    EXPECT_TRUE(linkBps.has_value());
    EXPECT_EQ(linkBps.value(), 1234560000);
  }
  // add few white spaces
  {
    auto linkBps = Config::getLinkBandwidthBps(" 123K ");
    EXPECT_TRUE(linkBps.has_value());
    EXPECT_EQ(linkBps.value(), 123000);
  }
  // invalid suffix
  {
    auto linkBps = Config::getLinkBandwidthBps("123H");
    EXPECT_FALSE(linkBps.has_value());
  }
  // Test "auto" - getLinkBandwidthBps does NOT handle "auto" specially
  {
    auto linkBps = Config::getLinkBandwidthBps("auto");
    EXPECT_FALSE(linkBps.has_value());
  }
  // Test "AUTO" (uppercase) - should also not be handled
  {
    auto linkBps = Config::getLinkBandwidthBps("AUTO");
    EXPECT_FALSE(linkBps.has_value());
  }
}

TEST(Config, GetLinkBandwidthBytesPerSecTest) {
  string configFile = "neteng/fboss/bgp/cpp/tests/sample_configs/bgpd.conf";
  auto configFilePath = getAbsoluteFilePath(configFile);
  Config config(configFilePath);
  auto peer = createDefaultBgpPeer();
  {
    auto linkBytesPerSec = config.getLinkBandwidthBytesPerSec("123", peer);
    EXPECT_TRUE(linkBytesPerSec.has_value());
    EXPECT_EQ(linkBytesPerSec.value(), 123.0f / 8);
  }
  {
    auto linkBytesPerSec = config.getLinkBandwidthBytesPerSec("123K", peer);
    EXPECT_TRUE(linkBytesPerSec.has_value());
    EXPECT_EQ(linkBytesPerSec.value(), 123.0f * 1000 / 8);
  }
  {
    auto linkBytesPerSec = config.getLinkBandwidthBytesPerSec("123M", peer);
    EXPECT_TRUE(linkBytesPerSec.has_value());
    EXPECT_EQ(linkBytesPerSec.value(), 123.0f * 1000 * 1000 / 8);
  }
  {
    auto linkBytesPerSec = config.getLinkBandwidthBytesPerSec("123G", peer);
    EXPECT_TRUE(linkBytesPerSec.has_value());
    EXPECT_EQ(linkBytesPerSec.value(), 123.0f * 1000 * 1000 * 1000 / 8);
  }
  {
    auto linkBytesPerSec = config.getLinkBandwidthBytesPerSec("12.5G", peer);
    EXPECT_TRUE(linkBytesPerSec.has_value());
    EXPECT_EQ(linkBytesPerSec.value(), 12.5f * 1000 * 1000 * 1000 / 8);
  }
  {
    auto linkBytesPerSec = config.getLinkBandwidthBytesPerSec("1.23456G", peer);
    EXPECT_TRUE(linkBytesPerSec.has_value());
    EXPECT_EQ(linkBytesPerSec.value(), 1.23456f * 1000 * 1000 * 1000 / 8);
  }
  {
    folly::F14NodeMap<folly::CIDRNetwork, int64_t> peerSubnetLbwMap;
    peerSubnetLbwMap.emplace(
        folly::CIDRNetwork(*peer.peer_addr(), 31), 123000 /* 123Gbps */);
    Config configWithLbwMap(configFilePath, peerSubnetLbwMap);
    auto linkBytesPerSec =
        configWithLbwMap.getLinkBandwidthBytesPerSec("auto", peer);
    EXPECT_TRUE(linkBytesPerSec.has_value());
    EXPECT_EQ(linkBytesPerSec.value(), 123.0f * 1000 * 1000 * 1000 / 8);
  }
  {
    // Test lambda function getting invoked
    folly::F14NodeMap<folly::CIDRNetwork, int64_t> peerSubnetLbwMap;
    peerSubnetLbwMap.emplace(
        folly::CIDRNetwork(*peer.peer_addr(), 31), 123000 /* 123Gbps */);
    auto invoked = 0;
    Config configWithLambdaFunc(configFilePath, [&]() {
      invoked++;
      return peerSubnetLbwMap;
    });
    auto linkBytesPerSec =
        configWithLambdaFunc.getLinkBandwidthBytesPerSec("auto", peer);
    EXPECT_TRUE(linkBytesPerSec.has_value());
    EXPECT_EQ(linkBytesPerSec.value(), 123.0f * 1000 * 1000 * 1000 / 8);
    EXPECT_EQ(invoked, 1);
  }

  {
    /*
     * test lambda function only invoked once even though map fetch
     * failed(std::nullopt)
     */
    auto invoked = 0;
    folly::F14NodeMap<folly::CIDRNetwork, int64_t> peerSubnetLbwMap;
    peerSubnetLbwMap.emplace(
        folly::CIDRNetwork(*peer.peer_addr(), 31), 123000 /* 123Gbps */);
    Config configWithLambdaFunc(configFilePath, [&]() {
      invoked++;
      return std::nullopt;
    });
    auto linkBytesPerSec =
        configWithLambdaFunc.getLinkBandwidthBytesPerSec("auto", peer);
    EXPECT_FALSE(linkBytesPerSec.has_value());
    auto linkBytesPerSec2 =
        configWithLambdaFunc.getLinkBandwidthBytesPerSec("auto", peer);
    EXPECT_FALSE(linkBytesPerSec2.has_value());
    EXPECT_EQ(invoked, 1);
  }
  {
    // test lambda function not invoked if not "auto"
    auto invoked = 0;
    folly::F14NodeMap<folly::CIDRNetwork, int64_t> peerSubnetLbwMap;
    peerSubnetLbwMap.emplace(
        folly::CIDRNetwork(*peer.peer_addr(), 31), 123000 /* 123Gbps */);
    Config configWithLambdaFunc(configFilePath, [&]() {
      invoked++;
      return std::nullopt;
    });
    auto linkBytesPerSec = config.getLinkBandwidthBytesPerSec("123", peer);
    EXPECT_TRUE(linkBytesPerSec.has_value());
    EXPECT_EQ(linkBytesPerSec.value(), 123.0f / 8);
    EXPECT_EQ(invoked, 0);
  }

  // Additional "auto" test cases
  {
    // Test "auto" when peer is not in the subnet map
    folly::F14NodeMap<folly::CIDRNetwork, int64_t> peerSubnetLbwMap;
    // Add a different subnet, not matching the peer
    peerSubnetLbwMap.emplace(
        folly::CIDRNetwork(folly::IPAddress("192.168.100.0"), 24),
        50000 /* 50Gbps */);
    Config configWithDifferentSubnet(configFilePath, peerSubnetLbwMap);
    auto linkBytesPerSec =
        configWithDifferentSubnet.getLinkBandwidthBytesPerSec("auto", peer);
    EXPECT_FALSE(linkBytesPerSec.has_value());
  }
  {
    // Test "AUTO" (uppercase) - should be case sensitive and NOT match
    folly::F14NodeMap<folly::CIDRNetwork, int64_t> peerSubnetLbwMap;
    peerSubnetLbwMap.emplace(
        folly::CIDRNetwork(*peer.peer_addr(), 31), 100000 /* 100Gbps */);
    Config configWithLbwMap(configFilePath, peerSubnetLbwMap);
    auto linkBytesPerSec =
        configWithLbwMap.getLinkBandwidthBytesPerSec("AUTO", peer);
    /*
     * "AUTO" should not be treated as "auto", so it should try to parse and
     * fail
     */
    EXPECT_FALSE(linkBytesPerSec.has_value());
  }
  {
    // Test "auto" with empty peerSubnetLbwMap
    folly::F14NodeMap<folly::CIDRNetwork, int64_t> emptyMap;
    Config configWithEmptyMap(configFilePath, emptyMap);
    auto linkBytesPerSec =
        configWithEmptyMap.getLinkBandwidthBytesPerSec("auto", peer);
    EXPECT_FALSE(linkBytesPerSec.has_value());
  }
  {
    // Test "auto" with peer that has zero bandwidth in map
    folly::F14NodeMap<folly::CIDRNetwork, int64_t> peerSubnetLbwMap;
    peerSubnetLbwMap.emplace(
        folly::CIDRNetwork(*peer.peer_addr(), 31), 0 /* 0 Mbps */);
    Config configWithZeroBw(configFilePath, peerSubnetLbwMap);
    auto linkBytesPerSec =
        configWithZeroBw.getLinkBandwidthBytesPerSec("auto", peer);
    EXPECT_TRUE(linkBytesPerSec.has_value());
    EXPECT_EQ(linkBytesPerSec.value(), 0.0f);
  }
  {
    /*
     * Test "auto" when peer has multiple matching subnets (should use first
     * match)
     */
    folly::F14NodeMap<folly::CIDRNetwork, int64_t> peerSubnetLbwMap;
    // Add overlapping subnets
    peerSubnetLbwMap.emplace(
        folly::CIDRNetwork(*peer.peer_addr(), 31), 50000 /* 50Gbps */);
    peerSubnetLbwMap.emplace(
        folly::CIDRNetwork(*peer.peer_addr(), 30), 100000 /* 100Gbps */);
    Config configWithOverlap(configFilePath, peerSubnetLbwMap);
    auto linkBytesPerSec =
        configWithOverlap.getLinkBandwidthBytesPerSec("auto", peer);
    EXPECT_TRUE(linkBytesPerSec.has_value());
    /*
     * Should match one of them (implementation dependent on map iteration
     * order)
     */
    float expected50G = 50.0f * 1000 * 1000 * 1000 / 8;
    float expected100G = 100.0f * 1000 * 1000 * 1000 / 8;
    EXPECT_TRUE(
        linkBytesPerSec.value() == expected50G ||
        linkBytesPerSec.value() == expected100G);
  }
  {
    // Test "auto" with whitespace should NOT match
    folly::F14NodeMap<folly::CIDRNetwork, int64_t> peerSubnetLbwMap;
    peerSubnetLbwMap.emplace(
        folly::CIDRNetwork(*peer.peer_addr(), 31), 100000 /* 100Gbps */);
    Config configWithLbwMap(configFilePath, peerSubnetLbwMap);
    auto linkBytesPerSec =
        configWithLbwMap.getLinkBandwidthBytesPerSec(" auto ", peer);
    // " auto " with whitespace should not match "auto"
    EXPECT_FALSE(linkBytesPerSec.has_value());
  }
}

struct TestParam {
  float errorPctThreshold;
  explicit TestParam(float errorPctThreshold)
      : errorPctThreshold(errorPctThreshold) {}
};

class BgpUcmpQuantizerFixture
    : public ::testing::Test,
      public ::testing::WithParamInterface<TestParam> {};

// Test following cases with different error-pct-threshold
INSTANTIATE_TEST_CASE_P(
    BgpUcmpQuantizerTests,
    BgpUcmpQuantizerFixture,
    ::testing::Values(
        TestParam(0.1f),
        TestParam(0.15f),
        TestParam(0.2f),
        TestParam(0.25f),
        TestParam(0.3f)));

TEST_P(BgpUcmpQuantizerFixture, BgpUcmpQuantizerTest) {
  const auto& param = GetParam();

  thrift::BgpUcmpQuantizerConfig config;
  config.min_step_bps() = "100G";
  config.error_pct_threshold() = param.errorPctThreshold;
  config.fixed_quantized_bps_list() = {"2400G", "3600G"};

  auto quantizer = Config::createBgpUcmpQuantizer(config);
  EXPECT_EQ(quantizer.minStepBps, 100e9);
  EXPECT_EQ(quantizer.errorPctThreshold, *config.error_pct_threshold());
  EXPECT_EQ(quantizer.fixedQuantizedBpsList.size(), 2);
  EXPECT_EQ(quantizer.fixedQuantizedBpsList.at(0), 2400e9);
  EXPECT_EQ(quantizer.fixedQuantizedBpsList.at(1), 3600e9);

  /*
   * due to precision loss between float <-> uint64_t cast, give it 5% error
   * margin
   * ensure all input/output pair yield error < thrshold * kErrorMargin
   */
  const float kErrorMargin = 1.05f;

  for (uint64_t inputBps = 3600e9; inputBps > 0; inputBps -= 100e9) {
    float inputBytesPerSec = static_cast<float>(inputBps) / 8;
    float outputBytesPerSec = quantizer.quantize(inputBytesPerSec);
    float error = fabs(outputBytesPerSec - inputBytesPerSec) / inputBytesPerSec;
    XLOGF(
        DBG4,
        "{}, {} -> {}: {}",
        inputBps,
        inputBytesPerSec,
        outputBytesPerSec,
        error);
    EXPECT_LT(error, *config.error_pct_threshold() * kErrorMargin);
  }

  // verify first link failures will be masked off
  auto output3600 = quantizer.quantize(3600e9 / 8);
  auto output3601 = quantizer.quantize((3600e9 + 50) / 8);
  auto output3599 = quantizer.quantize((3600e9 - 50) / 8);
  auto output3500 = quantizer.quantize(3500e9 / 8);
  auto output3400 = quantizer.quantize(3400e9 / 8);
  auto output3300 = quantizer.quantize(3300e9 / 8);
  EXPECT_EQ(output3600, output3601);
  EXPECT_EQ(output3600, output3599);
  EXPECT_EQ(output3600, output3500);
  EXPECT_EQ(output3600, output3400);
  EXPECT_EQ(output3600, output3300);

  auto output2400 = quantizer.quantize(2400e9 / 8);
  auto output2401 = quantizer.quantize((2400e9 + 50) / 8);
  auto output2399 = quantizer.quantize((2400e9 - 50) / 8);
  auto output2300 = quantizer.quantize(2300e9 / 8);
  auto output2200 = quantizer.quantize(2200e9 / 8);
  EXPECT_EQ(output2400, output2300);
  EXPECT_EQ(output2400, output2401);
  EXPECT_EQ(output2400, output2399);
  EXPECT_EQ(output2400, output2200);

  /*
   * Verify that last few failures will yield the same value. 1/10e6 of error
   * margin because of float rounding.
   */
  EXPECT_NEAR(quantizer.quantize(100e9 / 8), 100e9 / 8, 1000);
  EXPECT_NEAR(quantizer.quantize(200e9 / 8), 200e9 / 8, 2000);
  EXPECT_NEAR(quantizer.quantize(300e9 / 8), 300e9 / 8, 3000);
  EXPECT_NEAR(quantizer.quantize(400e9 / 8), 400e9 / 8, 4000);
}

/*
 * Reproduce P1: BgpUcmpQuantizer underflow when max bps is not evenly
 * divisible by minStepBps. With max=3600 and step=1000, the sequence is
 * 3600, 2600, 1600, 600 — then 600-1000 wraps to UINT64_MAX, causing
 * an infinite loop or OOM filling quantizedBpsMap.
 */
TEST(ConfigTest, BgpUcmpQuantizerUnderflow) {
  /* Must complete within a reasonable time.
   * Without fix this hangs or OOMs. */
  BgpUcmpQuantizer quantizer(1000, 0.1, {3600});
  EXPECT_GT(quantizer.quantizedBpsMap.size(), 0);
  EXPECT_LE(quantizer.quantizedBpsMap.size(), 4);
}

/*
 * Verify the parsing of dynamicPeerLimit from raw thrift::BgpConfig to
 * BgpGlobalConfig.
 */
TEST_F(ConfigTestFixture, DynamicPeerLimitConfigTest) {
  // Create a thrift::BgpConfig object
  thrift::BgpConfig thriftConfig;
  thriftConfig.router_id() = kLocalAddr1.str();
  const auto kDynamicPeerLimit = "dynamic_peer_limit";

  // Test case 1: no limit specified
  {
    FeatureFlags::LoadFromThriftConfig(thriftConfig);
    EXPECT_FALSE(FeatureFlags::IsFeatureEnabled(kDynamicPeerLimit));

    Config config(thriftConfig);
    auto globalConfig = config.getBgpGlobalConfig();
    // Expect that dynamicPeerLimit has no value
    EXPECT_FALSE(globalConfig->dynamicPeerLimit.has_value());
  }

  // Test case 2: Limit specified but FeatureFlags initialized after Config
  {
    thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
    std::set<std::string> features = {kDynamicPeerLimit};
    thriftConfig.bgp_setting_config()->features() = std::move(features);
    Config config(thriftConfig);

    FeatureFlags::LoadFromThriftConfig(thriftConfig);
    EXPECT_TRUE(FeatureFlags::IsFeatureEnabled(kDynamicPeerLimit));

    auto globalConfig = config.getBgpGlobalConfig();
    EXPECT_FALSE(globalConfig->dynamicPeerLimit.has_value());
  }

  // Test case 3: Limit specified and FeatureFlags enabled first
  {
    thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
    std::set<std::string> features = {kDynamicPeerLimit};
    thriftConfig.bgp_setting_config()->features() = std::move(features);

    FeatureFlags::LoadFromThriftConfig(thriftConfig);
    EXPECT_TRUE(FeatureFlags::IsFeatureEnabled(kDynamicPeerLimit));

    Config config(thriftConfig);
    auto globalConfig = config.getBgpGlobalConfig();
    // Expect that dynamicPeerLimit has a value and it's equal to 100
    EXPECT_TRUE(globalConfig->dynamicPeerLimit.has_value());
    EXPECT_EQ(globalConfig->dynamicPeerLimit.value(), config.dynamicPeerLimit_);
  }
}

/**
 * @brief streamSubscriberLimit is always set: the compiled default, replaced
 * by the bgp_setting value only when the "stream_subscriber_limit" feature is
 * enabled and the value is positive.
 *
 * @details Test steps:
 * 1. No feature, no value: the compiled default is enforced, not "no limit".
 * 2. Value present, feature absent: the value is ignored, default enforced.
 * 3. Feature and value present, FeatureFlags loaded after Config: default
 *    enforced, because Config saw no features when it was built. The value is
 *    set so this case differs from step 4.
 * 4. Feature present, no value: the compiled default is enforced.
 * 5. Feature present with an explicit value: that value is used. The value is
 *    the compiled default plus one, so it cannot coincide with the default.
 * 6. Feature present with 0: the default is kept, so the device does not
 *    lock out every monitor.
 * 7. Feature present with a negative value: the default is kept. The field
 *    is a signed i32, so an unguarded cast would wrap to a huge unsigned
 *    limit and enforce nothing.
 */
TEST_F(ConfigTestFixture, StreamSubscriberLimitConfigTest) {
  // Create a thrift::BgpConfig object
  thrift::BgpConfig thriftConfig;
  thriftConfig.router_id() = kLocalAddr1.str();
  const auto kStreamSubscriberLimit = "stream_subscriber_limit";

  // Test case 1: no feature, no value -> compiled default.
  {
    FeatureFlags::LoadFromThriftConfig(thriftConfig);
    EXPECT_FALSE(FeatureFlags::IsFeatureEnabled(kStreamSubscriberLimit));

    Config config(thriftConfig);
    auto globalConfig = config.getBgpGlobalConfig();
    EXPECT_TRUE(globalConfig->streamSubscriberLimit.has_value());
    EXPECT_EQ(
        globalConfig->streamSubscriberLimit.value(),
        config.streamSubscriberLimit_);
  }

  // Test case 2: value present but feature absent -> value ignored.
  // Must stay ahead of every case that enables the feature: FeatureFlags is a
  // process-global that never clears, so a load cannot be undone.
  {
    thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
    thriftConfig.bgp_setting_config()->features() = std::set<std::string>{};
    thriftConfig.bgp_setting_config()->stream_subscriber_limit() =
        static_cast<int32_t>(Config(thriftConfig).streamSubscriberLimit_ + 1);

    FeatureFlags::LoadFromThriftConfig(thriftConfig);
    EXPECT_FALSE(FeatureFlags::IsFeatureEnabled(kStreamSubscriberLimit));

    Config config(thriftConfig);
    auto globalConfig = config.getBgpGlobalConfig();
    EXPECT_TRUE(globalConfig->streamSubscriberLimit.has_value());
    EXPECT_EQ(
        globalConfig->streamSubscriberLimit.value(),
        config.streamSubscriberLimit_);
  }

  // Test case 3: feature and value present, but FeatureFlags initialized
  // after Config. Config reads the global FeatureFlags, not the thrift struct
  // it is handed, so a Config built before the load sees no features.
  {
    thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
    std::set<std::string> features = {kStreamSubscriberLimit};
    thriftConfig.bgp_setting_config()->features() = std::move(features);
    thriftConfig.bgp_setting_config()->stream_subscriber_limit() =
        static_cast<int32_t>(Config(thriftConfig).streamSubscriberLimit_ + 1);
    Config config(thriftConfig);

    FeatureFlags::LoadFromThriftConfig(thriftConfig);
    EXPECT_TRUE(FeatureFlags::IsFeatureEnabled(kStreamSubscriberLimit));

    auto globalConfig = config.getBgpGlobalConfig();
    EXPECT_TRUE(globalConfig->streamSubscriberLimit.has_value());
    EXPECT_EQ(
        globalConfig->streamSubscriberLimit.value(),
        config.streamSubscriberLimit_);
  }

  // Test case 4: feature enabled, no value -> compiled default.
  {
    thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
    std::set<std::string> features = {kStreamSubscriberLimit};
    thriftConfig.bgp_setting_config()->features() = std::move(features);

    FeatureFlags::LoadFromThriftConfig(thriftConfig);
    EXPECT_TRUE(FeatureFlags::IsFeatureEnabled(kStreamSubscriberLimit));

    Config config(thriftConfig);
    auto globalConfig = config.getBgpGlobalConfig();
    EXPECT_TRUE(globalConfig->streamSubscriberLimit.has_value());
    EXPECT_EQ(
        globalConfig->streamSubscriberLimit.value(),
        config.streamSubscriberLimit_);
  }

  // Test case 5: feature enabled with an explicit value -> that value wins.
  // The value is derived from the compiled default rather than hardcoded, so
  // it can never coincide with it -- a literal would silently stop proving
  // anything if the default were ever changed to match.
  {
    thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
    thriftConfig.bgp_setting_config()->features() =
        std::set<std::string>{kStreamSubscriberLimit};

    FeatureFlags::LoadFromThriftConfig(thriftConfig);

    const uint32_t configuredLimit =
        Config(thriftConfig).streamSubscriberLimit_ + 1;
    thriftConfig.bgp_setting_config()->stream_subscriber_limit() =
        static_cast<int32_t>(configuredLimit);

    Config config(thriftConfig);
    auto globalConfig = config.getBgpGlobalConfig();
    EXPECT_TRUE(globalConfig->streamSubscriberLimit.has_value());
    EXPECT_EQ(configuredLimit, globalConfig->streamSubscriberLimit.value());
  }

  // Test case 6: explicit 0 -> rejected, compiled default kept.
  {
    thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
    thriftConfig.bgp_setting_config()->features() =
        std::set<std::string>{kStreamSubscriberLimit};
    thriftConfig.bgp_setting_config()->stream_subscriber_limit() = 0;

    FeatureFlags::LoadFromThriftConfig(thriftConfig);

    Config config(thriftConfig);
    auto globalConfig = config.getBgpGlobalConfig();
    EXPECT_TRUE(globalConfig->streamSubscriberLimit.has_value());
    EXPECT_EQ(
        globalConfig->streamSubscriberLimit.value(),
        config.streamSubscriberLimit_);
  }

  // Test case 7: negative value -> rejected, compiled default kept.
  // Without the guard the cast wraps and the limit becomes ~4 billion, which
  // enforces nothing at all.
  {
    thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();
    thriftConfig.bgp_setting_config()->features() =
        std::set<std::string>{kStreamSubscriberLimit};
    thriftConfig.bgp_setting_config()->stream_subscriber_limit() = -1;

    FeatureFlags::LoadFromThriftConfig(thriftConfig);

    Config config(thriftConfig);
    auto globalConfig = config.getBgpGlobalConfig();
    EXPECT_TRUE(globalConfig->streamSubscriberLimit.has_value());
    EXPECT_EQ(
        globalConfig->streamSubscriberLimit.value(),
        config.streamSubscriberLimit_);
  }
}

TEST_F(ConfigTestFixture, IncludeInterfaceRegexesTest) {
  // Create a thrift::BgpConfig object
  thrift::BgpConfig thriftConfig;
  thriftConfig.router_id() = kLocalAddr1.str();
  thriftConfig.local_as_4_byte() = kAsn1;
  thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();

  // Test case 1: include_interface_regexes is not set (default value)
  {
    EXPECT_FALSE(thriftConfig.bgp_setting_config()
                     ->include_interface_regexes()
                     .has_value());

    Config config(thriftConfig);
    auto globalConfig = config.getBgpGlobalConfig();
    // Expect that includeInterfaceRegexes is empty by default
    EXPECT_TRUE(globalConfig->includeInterfaceRegexes.empty());
  }

  // Test case 2: include_interface_regexes is set to empty list
  {
    std::vector<std::string> emptyRegexes;
    thriftConfig.bgp_setting_config()->include_interface_regexes() =
        emptyRegexes;
    EXPECT_TRUE(thriftConfig.bgp_setting_config()
                    ->include_interface_regexes()
                    .has_value());
    EXPECT_TRUE(thriftConfig.bgp_setting_config()
                    ->include_interface_regexes()
                    ->empty());

    Config config(thriftConfig);
    auto globalConfig = config.getBgpGlobalConfig();
    EXPECT_TRUE(globalConfig->includeInterfaceRegexes.empty());
  }

  // Test case 3: include_interface_regexes is set with interface patterns
  {
    std::vector<std::string> interfaceRegexes = {
        "eth[0-9]+", "port-channel.*", "fboss.*"};
    thriftConfig.bgp_setting_config()->include_interface_regexes() =
        interfaceRegexes;
    EXPECT_TRUE(thriftConfig.bgp_setting_config()
                    ->include_interface_regexes()
                    .has_value());
    EXPECT_EQ(
        interfaceRegexes.size(),
        thriftConfig.bgp_setting_config()->include_interface_regexes()->size());

    Config config(thriftConfig);
    auto globalConfig = config.getBgpGlobalConfig();
    EXPECT_EQ(3, globalConfig->includeInterfaceRegexes.size());
    EXPECT_EQ(interfaceRegexes, globalConfig->includeInterfaceRegexes);

    // Verify specific patterns
    EXPECT_EQ("eth[0-9]+", globalConfig->includeInterfaceRegexes[0]);
    EXPECT_EQ("port-channel.*", globalConfig->includeInterfaceRegexes[1]);
    EXPECT_EQ("fboss.*", globalConfig->includeInterfaceRegexes[2]);
  }

  /*
   * Test case 4: include_interface_regexes combined with
   * enable_next_hop_tracking
   */
  {
    std::vector<std::string> interfaceRegexes = {"loopback0", "ethernet.*"};
    thriftConfig.bgp_setting_config()->include_interface_regexes() =
        interfaceRegexes;
    thriftConfig.bgp_setting_config()->enable_next_hop_tracking() = true;

    Config config(thriftConfig);
    auto globalConfig = config.getBgpGlobalConfig();
    EXPECT_EQ(2, globalConfig->includeInterfaceRegexes.size());
    EXPECT_EQ(interfaceRegexes, globalConfig->includeInterfaceRegexes);
    EXPECT_TRUE(globalConfig->enableNextHopTracking);
  }
}

TEST_F(ConfigTestFixture, BgpSettingConfigTest) {
  // Create a thrift::BgpConfig object
  thrift::BgpConfig thriftConfig;
  thriftConfig.router_id() = kLocalAddr1.str();
  thriftConfig.local_as_4_byte() = kAsn1;
  thriftConfig.bgp_setting_config() = thrift::BgpSettingConfig();

  {
    // Verify the default flag
    Config config(thriftConfig);
    auto globalConfig = config.getBgpGlobalConfig();

    // Egress queue backpressure is always enabled in bgp++.
    EXPECT_TRUE(globalConfig->enableEgressQueueBackpressure);
    EXPECT_FALSE(globalConfig->enableNextHopTracking);
    EXPECT_FALSE(globalConfig->enableUpdateGroup);
    EXPECT_FALSE(globalConfig->enableOptimizedGR);
    EXPECT_FALSE(globalConfig->enableRouteRefresh);

    std::map<std::string, int64_t> counters;
    fb303::ThreadCachedServiceData::getShared()->getCounters(counters);
    ASSERT_EQ(1, counters.count(BgpStats::kUpdateGroupEnabled));
    EXPECT_EQ(0, counters.at(BgpStats::kUpdateGroupEnabled));
  }

  {
    // Verify the non-default flag
    thriftConfig.bgp_setting_config()->enable_egress_queue_backpressure() =
        true;
    thriftConfig.bgp_setting_config()->enable_update_group() = true;
    thriftConfig.bgp_setting_config()->enable_next_hop_tracking() = true;
    thriftConfig.bgp_setting_config()->enable_optimized_GR() = true;
    thriftConfig.bgp_setting_config()->enable_route_refresh() = true;

    Config config(thriftConfig);
    auto globalConfig = config.getBgpGlobalConfig();

    EXPECT_TRUE(globalConfig->enableEgressQueueBackpressure);
    EXPECT_TRUE(globalConfig->enableNextHopTracking);
    EXPECT_TRUE(globalConfig->enableUpdateGroup);
    EXPECT_TRUE(globalConfig->enableOptimizedGR);
    EXPECT_TRUE(globalConfig->enableRouteRefresh);

    std::map<std::string, int64_t> counters;
    fb303::ThreadCachedServiceData::getShared()->getCounters(counters);
    ASSERT_EQ(1, counters.count(BgpStats::kUpdateGroupEnabled));
    EXPECT_EQ(1, counters.at(BgpStats::kUpdateGroupEnabled));
  }
}

// Test validatePeerExists function with static peers
TEST_F(ConfigTestFixture, ValidatePeerExistsStaticPeerTest) {
  Config config(defaultConfig_);

  // Test with existing static peer
  EXPECT_TRUE(config.validatePeerExists(kPeerAddr3.str()));
  EXPECT_TRUE(config.validatePeerExists(kPeerAddr4.str()));
  EXPECT_TRUE(config.validatePeerExists(kPeerAddr5.str()));
  EXPECT_TRUE(config.validatePeerExists(kPeerAddr6.str()));

  // Test with non-existent static peer
  EXPECT_FALSE(config.validatePeerExists("192.168.1.1"));
  EXPECT_FALSE(config.validatePeerExists("2001:db8::1"));
}

// Test validatePeerExists function with dynamic peers
TEST_F(ConfigTestFixture, ValidatePeerExistsDynamicPeerTest) {
  Config config(defaultConfig_);

  // Test with existing dynamic peer prefixes
  EXPECT_TRUE(config.validatePeerExists(
      folly::to<std::string>(kPeerPrefix1.first, "/", kPeerPrefix1.second)));
  EXPECT_TRUE(config.validatePeerExists(
      folly::to<std::string>(kPeerPrefix2.first, "/", kPeerPrefix2.second)));

  // Test with non-existent dynamic peer prefix
  EXPECT_FALSE(config.validatePeerExists("192.168.0.0/24"));
  EXPECT_FALSE(config.validatePeerExists("2001:db8::/64"));
}

// Test validatePeerExists function with invalid peer address format
TEST_F(ConfigTestFixture, ValidatePeerExistsInvalidFormatTest) {
  Config config(defaultConfig_);

  // Test with invalid IP address formats
  EXPECT_FALSE(config.validatePeerExists("invalid-ip"));
  EXPECT_FALSE(config.validatePeerExists("256.256.256.256"));
  EXPECT_FALSE(config.validatePeerExists(""));
  EXPECT_FALSE(config.validatePeerExists("192.168.1"));
}

// Test validatePeerExists function edge cases
TEST_F(ConfigTestFixture, ValidatePeerExistsEdgeCasesTest) {
  Config config(defaultConfig_);

  /*
   * Test with IPv4-mapped IPv6 addresses if applicable
   * Test with loopback addresses
   */
  EXPECT_FALSE(config.validatePeerExists("127.0.0.1"));
  EXPECT_FALSE(config.validatePeerExists("::1"));
}

// Test validatePeerGroupExists function
TEST_F(ConfigTestFixture, ValidatePeerGroupExistsTest) {
  Config config(defaultConfig_);

  // Test with existing peer group
  EXPECT_TRUE(config.validatePeerGroupExists("PEERGROUP_RSW_CSW_V4"));

  // Test with non-existent peer group
  EXPECT_FALSE(config.validatePeerGroupExists("NON_EXISTENT_PEER_GROUP"));
  EXPECT_FALSE(config.validatePeerGroupExists(""));
}

// Test validatePeerGroupExists function case sensitivity
TEST_F(ConfigTestFixture, ValidatePeerGroupExistsCaseSensitiveTest) {
  Config config(defaultConfig_);

  // Test case sensitivity - should be exact match
  EXPECT_TRUE(config.validatePeerGroupExists("PEERGROUP_RSW_CSW_V4"));
  EXPECT_FALSE(config.validatePeerGroupExists("peergroup_rsw_csw_v4"));
  EXPECT_FALSE(config.validatePeerGroupExists("PEERGROUP_RSW_CSW_v4"));
}

// Test validatePeerExists with mixed peer configurations
TEST_F(ConfigTestFixture, ValidatePeerExistsMixedConfigTest) {
  // Create a custom config with both static and dynamic peers
  thrift::BgpConfig customConfig;
  customConfig.router_id() = kLocalAddr1.str();
  customConfig.local_as_4_byte() = kAsn1;
  customConfig.hold_time() = kHoldTime.count();
  customConfig.graceful_restart_convergence_seconds() = kGrRestartTime.count();
  customConfig.listen_addr() = kLocalAddr1.str();
  customConfig.listen_port() = kBgpPort;

  // Add some static and dynamic peers
  std::vector<thrift::BgpPeer> peers;

  // Static peer
  auto staticPeer = createBgpPeer(
      kAsn1,
      kLocalAddr1,
      kPeerAddr1,
      kNextHopV4_1,
      kNextHopV6_1,
      false,
      kPeerTypeCsw);
  peers.push_back(staticPeer);

  // Dynamic peer
  auto dynamicPeer = createBgpPeer(
      kAsn2,
      kLocalAddr2,
      kPeerPrefix1,
      kNextHopV4_2,
      kNextHopV6_2,
      true,
      kPeerTypeBgpMonitor);
  peers.push_back(dynamicPeer);

  customConfig.peers() = peers;

  Config config(customConfig);

  // Test static peer validation
  EXPECT_TRUE(config.validatePeerExists(kPeerAddr1.str()));

  // Test dynamic peer validation
  EXPECT_TRUE(config.validatePeerExists(
      folly::to<std::string>(kPeerPrefix1.first, "/", kPeerPrefix1.second)));

  // Test non-existent peers
  EXPECT_FALSE(config.validatePeerExists("10.10.10.10"));
  EXPECT_FALSE(config.validatePeerExists("192.168.0.0/16"));
}

// Test validatePeerExists with empty peer configuration
TEST_F(ConfigTestFixture, ValidatePeerExistsEmptyConfigTest) {
  // Create config with no peers
  thrift::BgpConfig emptyConfig;
  emptyConfig.router_id() = kLocalAddr1.str();
  emptyConfig.local_as_4_byte() = kAsn1;
  emptyConfig.hold_time() = kHoldTime.count();
  emptyConfig.listen_addr() = kLocalAddr1.str();
  emptyConfig.listen_port() = kBgpPort;
  // Don't add any peers

  Config config(emptyConfig);

  // All peer validations should fail when no peers are configured
  EXPECT_FALSE(config.validatePeerExists(kPeerAddr1.str()));
  EXPECT_FALSE(config.validatePeerExists(
      folly::to<std::string>(kPeerPrefix1.first, "/", kPeerPrefix1.second)));
  EXPECT_FALSE(config.validatePeerExists("192.168.1.1"));
}

// Test validatePeerGroupExists with empty peer group configuration
TEST_F(ConfigTestFixture, ValidatePeerGroupExistsEmptyConfigTest) {
  // Create config with no peer groups
  thrift::BgpConfig emptyConfig;
  emptyConfig.router_id() = kLocalAddr1.str();
  emptyConfig.local_as_4_byte() = kAsn1;
  emptyConfig.hold_time() = kHoldTime.count();
  emptyConfig.listen_addr() = kLocalAddr1.str();
  emptyConfig.listen_port() = kBgpPort;
  // Don't add any peer groups

  Config config(emptyConfig);

  // All peer group validations should fail when no peer groups are configured
  EXPECT_FALSE(config.validatePeerGroupExists("ANY_PEER_GROUP"));
  EXPECT_FALSE(config.validatePeerGroupExists("PEERGROUP_RSW_CSW_V4"));
}

// Test validatePeerExists with IPv6 addresses
TEST_F(ConfigTestFixture, ValidatePeerExistsIPv6Test) {
  // Create config with IPv6 peers
  thrift::BgpConfig ipv6Config;
  ipv6Config.router_id() = kLocalAddr1.str();
  ipv6Config.local_as_4_byte() = kAsn1;
  ipv6Config.hold_time() = kHoldTime.count();
  ipv6Config.listen_addr() = kLocalAddr1.str();
  ipv6Config.listen_port() = kBgpPort;

  std::vector<thrift::BgpPeer> peers;

  // Add IPv6 static peer
  auto ipv6Peer = createBgpPeer(
      kAsn1,
      folly::IPAddress("2001:db8::1"),
      folly::IPAddress("2001:db8::2"),
      folly::IPAddress("10.0.0.1"),
      folly::IPAddress("2001:db8::3"),
      false,
      kPeerTypeCsw);
  peers.push_back(ipv6Peer);

  ipv6Config.peers() = peers;

  Config config(ipv6Config);

  // Test IPv6 peer validation
  EXPECT_TRUE(config.validatePeerExists("2001:db8::2"));
  EXPECT_FALSE(config.validatePeerExists("2001:db8::999"));
}

// Test validatePeerExists with subnet matching for dynamic peers
TEST_F(ConfigTestFixture, ValidatePeerExistsSubnetMatchingTest) {
  // Create config with dynamic peer using subnet
  thrift::BgpConfig subnetConfig;
  subnetConfig.router_id() = kLocalAddr1.str();
  subnetConfig.local_as_4_byte() = kAsn1;
  subnetConfig.hold_time() = kHoldTime.count();
  subnetConfig.listen_addr() = kLocalAddr1.str();
  subnetConfig.listen_port() = kBgpPort;

  std::vector<thrift::BgpPeer> peers;

  // Add dynamic peer with /24 subnet
  auto dynamicPeer = createBgpPeer(
      kAsn1,
      kLocalAddr1,
      folly::CIDRNetwork("192.168.1.0", 24),
      kNextHopV4_1,
      kNextHopV6_1,
      true,
      kPeerTypeBgpMonitor);
  peers.push_back(dynamicPeer);

  subnetConfig.peers() = peers;

  Config config(subnetConfig);

  // Test subnet validation - should match the exact subnet configured
  EXPECT_TRUE(config.validatePeerExists("192.168.1.0/24"));

  /*
   * Test individual IPs within the subnet - this should not match
   * as validatePeerExists is looking for exact peer/prefix matches
   */
  EXPECT_FALSE(config.validatePeerExists("192.168.1.1"));
  EXPECT_FALSE(config.validatePeerExists("192.168.1.100"));

  // Test different subnets
  EXPECT_FALSE(config.validatePeerExists("192.168.2.0/24"));
  EXPECT_FALSE(config.validatePeerExists("192.168.1.0/16"));
}

// Test ttlSecurityHops config parsing and validation
TEST_F(ConfigTestFixture, TtlSecurityHopsNotSetTest) {
  // When ttl_security_hops is not set, it should default to nullopt
  Config config(defaultConfig_);
  const auto& peerToConfig = config.getPeerToConfig();

  // staticPeer1_ has no ttl_security_hops set
  const auto& peerConfig = peerToConfig.at(kPeerAddr3)->commonPeerGroupConfig;
  EXPECT_EQ(std::nullopt, peerConfig.ttlSecurityHops);

  auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
  EXPECT_EQ(std::nullopt, params.ttlSecurityHops);
}

TEST_F(ConfigTestFixture, TtlSecurityHopsValidValueTest) {
  // Valid ttl_security_hops values (1, 64, 255) should propagate correctly
  thrift::BgpConfig bgpConfig;
  bgpConfig.router_id() = kLocalAddr1.str();
  bgpConfig.local_as_4_byte() = kAsn1;

  {
    // Lower boundary: ttl_security_hops = 1
    auto peer = createBgpPeer(
        kAsn1,
        kLocalAddr3,
        kPeerAddr3,
        kNextHopV4_3,
        kNextHopV6_3,
        false,
        kPeerTypeCsw);
    peer.ttl_security_hops() = 1;
    bgpConfig.peers() = {peer};
    Config config(bgpConfig);

    auto peerConf = config.getConfigOfAPeer(kPeerAddr3).value();
    EXPECT_EQ(1, peerConf.ttlSecurityHops.value());

    const auto& peerToConfig = config.getPeerToConfig();
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
    EXPECT_EQ(1, params.ttlSecurityHops.value());
  }
  {
    // Mid-range: ttl_security_hops = 64
    auto peer = createBgpPeer(
        kAsn1,
        kLocalAddr3,
        kPeerAddr3,
        kNextHopV4_3,
        kNextHopV6_3,
        false,
        kPeerTypeCsw);
    peer.ttl_security_hops() = 64;
    bgpConfig.peers() = {peer};
    Config config(bgpConfig);

    auto peerConf = config.getConfigOfAPeer(kPeerAddr3).value();
    EXPECT_EQ(64, peerConf.ttlSecurityHops.value());

    const auto& peerToConfig = config.getPeerToConfig();
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
    EXPECT_EQ(64, params.ttlSecurityHops.value());
  }
  {
    // Upper boundary: ttl_security_hops = 255
    auto peer = createBgpPeer(
        kAsn1,
        kLocalAddr3,
        kPeerAddr3,
        kNextHopV4_3,
        kNextHopV6_3,
        false,
        kPeerTypeCsw);
    peer.ttl_security_hops() = 255;
    bgpConfig.peers() = {peer};
    Config config(bgpConfig);

    auto peerConf = config.getConfigOfAPeer(kPeerAddr3).value();
    EXPECT_EQ(255, peerConf.ttlSecurityHops.value());

    const auto& peerToConfig = config.getPeerToConfig();
    auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
    EXPECT_EQ(255, params.ttlSecurityHops.value());
  }
}

TEST_F(ConfigTestFixture, TtlSecurityHopsInvalidValueTest) {
  // Invalid ttl_security_hops values should throw BgpError
  thrift::BgpConfig bgpConfig;
  bgpConfig.router_id() = kLocalAddr1.str();
  bgpConfig.local_as_4_byte() = kAsn1;

  auto makeConfig = [&](int32_t hops) {
    auto peer = createBgpPeer(
        kAsn1,
        kLocalAddr3,
        kPeerAddr3,
        kNextHopV4_3,
        kNextHopV6_3,
        false,
        kPeerTypeCsw);
    peer.ttl_security_hops() = hops;
    bgpConfig.peers() = {peer};
    Config config(bgpConfig);
    const auto& peerToConfig = config.getPeerToConfig();
    config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
  };

  // Below range: ttl_security_hops = 0
  EXPECT_THROW(makeConfig(0), BgpError);
  // Above range: ttl_security_hops = 256
  EXPECT_THROW(makeConfig(256), BgpError);
  // Negative value: ttl_security_hops = -1
  EXPECT_THROW(makeConfig(-1), BgpError);
}

TEST_F(ConfigTestFixture, TtlSecurityHopsPeerGroupInheritanceTest) {
  // Peer should inherit ttl_security_hops from peer group
  thrift::BgpConfig bgpConfig;
  bgpConfig.router_id() = kLocalAddr1.str();
  bgpConfig.local_as_4_byte() = kAsn1;

  // Set ttl_security_hops on peer group
  thrift::PeerGroup peerGroup;
  peerGroup.name() = "TTL_TEST_GROUP";
  peerGroup.peer_tag() = kPeerTypeCsw;
  peerGroup.ttl_security_hops() = 1;
  bgpConfig.peer_groups() = {peerGroup};

  // Peer references the group but does NOT set ttl_security_hops
  thrift::BgpPeer peer;
  peer.remote_as_4_byte() = kAsn1;
  peer.local_addr() = kLocalAddr3.str();
  peer.peer_addr() = kPeerAddr3.str();
  peer.next_hop4() = kNextHopV4_3.str();
  peer.next_hop6() = kNextHopV6_3.str();
  peer.peer_group_name() = "TTL_TEST_GROUP";
  bgpConfig.peers() = {peer};

  Config config(bgpConfig);

  auto peerConf = config.getConfigOfAPeer(kPeerAddr3).value();
  EXPECT_EQ(1, peerConf.ttlSecurityHops.value());

  const auto& peerToConfig = config.getPeerToConfig();
  auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
  EXPECT_EQ(1, params.ttlSecurityHops.value());
}

TEST_F(ConfigTestFixture, TtlSecurityHopsPeerOverridesPeerGroupTest) {
  // Peer's ttl_security_hops should override peer group's value
  thrift::BgpConfig bgpConfig;
  bgpConfig.router_id() = kLocalAddr1.str();
  bgpConfig.local_as_4_byte() = kAsn1;

  // Set ttl_security_hops on peer group
  thrift::PeerGroup peerGroup;
  peerGroup.name() = "TTL_TEST_GROUP";
  peerGroup.peer_tag() = kPeerTypeCsw;
  peerGroup.ttl_security_hops() = 1;
  bgpConfig.peer_groups() = {peerGroup};

  // Peer overrides with its own value
  thrift::BgpPeer peer;
  peer.remote_as_4_byte() = kAsn1;
  peer.local_addr() = kLocalAddr3.str();
  peer.peer_addr() = kPeerAddr3.str();
  peer.next_hop4() = kNextHopV4_3.str();
  peer.next_hop6() = kNextHopV6_3.str();
  peer.peer_group_name() = "TTL_TEST_GROUP";
  peer.ttl_security_hops() = 64;
  bgpConfig.peers() = {peer};

  Config config(bgpConfig);

  auto peerConf = config.getConfigOfAPeer(kPeerAddr3).value();
  EXPECT_EQ(64, peerConf.ttlSecurityHops.value());

  const auto& peerToConfig = config.getPeerToConfig();
  auto params = config.getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
  EXPECT_EQ(64, params.ttlSecurityHops.value());
}

} // namespace facebook::bgp
