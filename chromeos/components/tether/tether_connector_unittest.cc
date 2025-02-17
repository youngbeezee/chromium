// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/components/tether/tether_connector.h"

#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "chromeos/components/tether/connect_tethering_operation.h"
#include "chromeos/components/tether/device_id_tether_network_guid_map.h"
#include "chromeos/components/tether/fake_active_host.h"
#include "chromeos/components/tether/fake_ble_connection_manager.h"
#include "chromeos/components/tether/fake_tether_host_fetcher.h"
#include "chromeos/components/tether/fake_wifi_hotspot_connector.h"
#include "chromeos/components/tether/mock_tether_host_response_recorder.h"
#include "chromeos/components/tether/tether_connector.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/network/network_connection_handler.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "chromeos/network/network_state_test.h"
#include "components/cryptauth/remote_device.h"
#include "components/cryptauth/remote_device_test_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/shill/dbus-constants.h"

namespace chromeos {

namespace tether {

namespace {

const char kSuccessResult[] = "success";

const char kSsid[] = "ssid";
const char kPassword[] = "password";

const char kWifiNetworkGuid[] = "wifiNetworkGuid";

std::string CreateWifiConfigurationJsonString() {
  std::stringstream ss;
  ss << "{"
     << "  \"GUID\": \"" << kWifiNetworkGuid << "\","
     << "  \"Type\": \"" << shill::kTypeWifi << "\","
     << "  \"State\": \"" << shill::kStateIdle << "\""
     << "}";
  return ss.str();
}

class TestNetworkConnectionHandler : public NetworkConnectionHandler {
 public:
  TestNetworkConnectionHandler() : NetworkConnectionHandler() {}
  ~TestNetworkConnectionHandler() override {}

  void CallTetherDelegate(
      const std::string& tether_network_guid,
      const base::Closure& success_callback,
      const network_handler::ErrorCallback& error_callback) {
    InitiateTetherNetworkConnection(tether_network_guid, success_callback,
                                    error_callback);
  }
};

class FakeConnectTetheringOperation : public ConnectTetheringOperation {
 public:
  FakeConnectTetheringOperation(
      const cryptauth::RemoteDevice& device_to_connect,
      BleConnectionManager* connection_manager,
      TetherHostResponseRecorder* tether_host_response_recorder)
      : ConnectTetheringOperation(device_to_connect,
                                  connection_manager,
                                  tether_host_response_recorder) {}

  ~FakeConnectTetheringOperation() override {}

  void SendSuccessfulResponse(const std::string& ssid,
                              const std::string& password) {
    NotifyObserversOfSuccessfulResponse(ssid, password);
  }

  void SendFailedResponse(ConnectTetheringResponse_ResponseCode error_code) {
    NotifyObserversOfConnectionFailure(error_code);
  }

  cryptauth::RemoteDevice GetRemoteDevice() {
    EXPECT_EQ(1u, remote_devices().size());
    return remote_devices()[0];
  }
};

class FakeConnectTetheringOperationFactory
    : public ConnectTetheringOperation::Factory {
 public:
  FakeConnectTetheringOperationFactory() {}
  virtual ~FakeConnectTetheringOperationFactory() {}

  std::vector<FakeConnectTetheringOperation*>& created_operations() {
    return created_operations_;
  }

 protected:
  // ConnectTetheringOperation::Factory:
  std::unique_ptr<ConnectTetheringOperation> BuildInstance(
      const cryptauth::RemoteDevice& device_to_connect,
      BleConnectionManager* connection_manager,
      TetherHostResponseRecorder* tether_host_response_recorder) override {
    FakeConnectTetheringOperation* operation =
        new FakeConnectTetheringOperation(device_to_connect, connection_manager,
                                          tether_host_response_recorder);
    created_operations_.push_back(operation);
    return base::WrapUnique(operation);
  }

 private:
  std::vector<FakeConnectTetheringOperation*> created_operations_;
};

}  // namespace

class TetherConnectorTest : public NetworkStateTest {
 public:
  TetherConnectorTest()
      : test_devices_(cryptauth::GenerateTestRemoteDevices(2u)) {}
  ~TetherConnectorTest() override {}

  void SetUp() override {
    DBusThreadManager::Initialize();
    NetworkStateTest::SetUp();
    network_state_handler()->SetTetherTechnologyState(
        NetworkStateHandler::TECHNOLOGY_ENABLED);

    fake_operation_factory_ =
        base::WrapUnique(new FakeConnectTetheringOperationFactory());
    ConnectTetheringOperation::Factory::SetInstanceForTesting(
        fake_operation_factory_.get());

    test_network_connection_handler_ =
        base::WrapUnique(new TestNetworkConnectionHandler());
    fake_wifi_hotspot_connector_ =
        base::MakeUnique<FakeWifiHotspotConnector>(network_state_handler());
    fake_active_host_ = base::MakeUnique<FakeActiveHost>();
    fake_tether_host_fetcher_ = base::MakeUnique<FakeTetherHostFetcher>(
        test_devices_, false /* synchronously_reply_with_results */);
    fake_ble_connection_manager_ = base::MakeUnique<FakeBleConnectionManager>();
    mock_tether_host_response_recorder_ =
        base::MakeUnique<MockTetherHostResponseRecorder>();
    device_id_tether_network_guid_map_ =
        base::MakeUnique<DeviceIdTetherNetworkGuidMap>();

    result_.clear();

    tether_connector_ = base::WrapUnique(new TetherConnector(
        test_network_connection_handler_.get(), network_state_handler(),
        fake_wifi_hotspot_connector_.get(), fake_active_host_.get(),
        fake_tether_host_fetcher_.get(), fake_ble_connection_manager_.get(),
        mock_tether_host_response_recorder_.get(),
        device_id_tether_network_guid_map_.get()));

    SetUpTetherNetworks();
  }

  void TearDown() override {
    // Must delete |fake_wifi_hotspot_connector_| before NetworkStateHandler is
    // destroyed to ensure that NetworkStateHandler has zero observers by the
    // time it reaches its destructor.
    fake_wifi_hotspot_connector_.reset();

    ShutdownNetworkState();
    NetworkStateTest::TearDown();
    DBusThreadManager::Shutdown();
  }

  std::string GetTetherNetworkGuid(const std::string& device_id) {
    return device_id_tether_network_guid_map_->GetTetherNetworkGuidForDeviceId(
        device_id);
  }

  void SetUpTetherNetworks() {
    // Add a tether network corresponding to both of the test devices. These
    // networks are expected to be added already before TetherConnector receives
    // its ConnectToNetwork() callback.
    network_state_handler()->AddTetherNetworkState(
        GetTetherNetworkGuid(test_devices_[0].GetDeviceId()),
        "TetherNetworkName1", "TetherNetworkCarrier1",
        85 /* battery_percentage */, 75 /* signal_strength */);
    network_state_handler()->AddTetherNetworkState(
        GetTetherNetworkGuid(test_devices_[1].GetDeviceId()),
        "TetherNetworkName2", "TetherNetworkCarrier2",
        90 /* battery_percentage */, 50 /* signal_strength */);
  }

  void SuccessfullyJoinWifiNetwork() {
    ConfigureService(CreateWifiConfigurationJsonString());
    fake_wifi_hotspot_connector_->CallMostRecentCallback(kWifiNetworkGuid);
  }

  void VerifyTetherAndWifiNetworkAssociation(
      const std::string& tether_network_guid) {
    const NetworkState* tether_network_state =
        network_state_handler()->GetNetworkStateFromGuid(tether_network_guid);
    EXPECT_TRUE(tether_network_state);
    EXPECT_EQ(kWifiNetworkGuid, tether_network_state->tether_guid());

    const NetworkState* wifi_network_state =
        network_state_handler()->GetNetworkStateFromGuid(kWifiNetworkGuid);
    EXPECT_TRUE(wifi_network_state);
    EXPECT_EQ(tether_network_guid, wifi_network_state->tether_guid());
  }

  void SuccessCallback() { result_ = kSuccessResult; }

  void ErrorCallback(const std::string& error_name,
                     std::unique_ptr<base::DictionaryValue> error_data) {
    result_ = error_name;
  }

  void CallTetherDelegate(const std::string& tether_network_guid) {
    test_network_connection_handler_->CallTetherDelegate(
        tether_network_guid,
        base::Bind(&TetherConnectorTest::SuccessCallback,
                   base::Unretained(this)),
        base::Bind(&TetherConnectorTest::ErrorCallback,
                   base::Unretained(this)));
  }

  std::string GetResultAndReset() {
    std::string result;
    result.swap(result_);
    return result;
  }

  const std::vector<cryptauth::RemoteDevice> test_devices_;
  const base::MessageLoop message_loop_;

  std::unique_ptr<FakeConnectTetheringOperationFactory> fake_operation_factory_;
  std::unique_ptr<TestNetworkConnectionHandler>
      test_network_connection_handler_;
  std::unique_ptr<FakeWifiHotspotConnector> fake_wifi_hotspot_connector_;
  std::unique_ptr<FakeActiveHost> fake_active_host_;
  std::unique_ptr<FakeTetherHostFetcher> fake_tether_host_fetcher_;
  std::unique_ptr<FakeBleConnectionManager> fake_ble_connection_manager_;
  std::unique_ptr<MockTetherHostResponseRecorder>
      mock_tether_host_response_recorder_;
  // TODO(hansberry): Use a fake for this when a real mapping scheme is created.
  std::unique_ptr<DeviceIdTetherNetworkGuidMap>
      device_id_tether_network_guid_map_;

  std::string result_;

  std::unique_ptr<TetherConnector> tether_connector_;

 private:
  DISALLOW_COPY_AND_ASSIGN(TetherConnectorTest);
};

TEST_F(TetherConnectorTest, TestCannotFetchDevice) {
  // Base64-encoded version of "nonexistentDeviceId".
  const char kNonexistentDeviceId[] = "bm9uZXhpc3RlbnREZXZpY2VJZA==";

  CallTetherDelegate(GetTetherNetworkGuid(kNonexistentDeviceId));
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(kNonexistentDeviceId, fake_active_host_->GetActiveHostDeviceId());
  EXPECT_EQ(GetTetherNetworkGuid(kNonexistentDeviceId),
            fake_active_host_->GetTetherNetworkGuid());
  EXPECT_TRUE(fake_active_host_->GetWifiNetworkGuid().empty());

  fake_tether_host_fetcher_->InvokePendingCallbacks();

  // Since an invalid device ID was used, no connection should have been
  // started.
  EXPECT_EQ(ActiveHost::ActiveHostStatus::DISCONNECTED,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(NetworkConnectionHandler::kErrorConnectFailed, GetResultAndReset());
}

TEST_F(TetherConnectorTest, TestConnectTetheringOperationFails) {
  CallTetherDelegate(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()));
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(test_devices_[0].GetDeviceId(),
            fake_active_host_->GetActiveHostDeviceId());
  EXPECT_EQ(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()),
            fake_active_host_->GetTetherNetworkGuid());
  EXPECT_TRUE(fake_active_host_->GetWifiNetworkGuid().empty());

  fake_tether_host_fetcher_->InvokePendingCallbacks();

  // Simulate a failed connection attempt (either the host cannot provide
  // tethering at this time or a timeout occurs).
  EXPECT_EQ(1u, fake_operation_factory_->created_operations().size());
  fake_operation_factory_->created_operations()[0]->SendFailedResponse(
      ConnectTetheringResponse_ResponseCode::
          ConnectTetheringResponse_ResponseCode_UNKNOWN_ERROR);

  // The failure should have resulted in the host being disconnected.
  EXPECT_EQ(ActiveHost::ActiveHostStatus::DISCONNECTED,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(NetworkConnectionHandler::kErrorConnectFailed, GetResultAndReset());
}

TEST_F(TetherConnectorTest, TestConnectingToWifiFails) {
  CallTetherDelegate(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()));
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(test_devices_[0].GetDeviceId(),
            fake_active_host_->GetActiveHostDeviceId());
  EXPECT_EQ(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()),
            fake_active_host_->GetTetherNetworkGuid());
  EXPECT_TRUE(fake_active_host_->GetWifiNetworkGuid().empty());

  fake_tether_host_fetcher_->InvokePendingCallbacks();

  // Receive a successful response. We should still be connecting.
  EXPECT_EQ(1u, fake_operation_factory_->created_operations().size());
  fake_operation_factory_->created_operations()[0]->SendSuccessfulResponse(
      kSsid, kPassword);
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());

  // |fake_wifi_hotspot_connector_| should have received the SSID and password
  // above. Verify this, then return an empty string, signaling a failure to
  // connect.
  EXPECT_EQ(kSsid, fake_wifi_hotspot_connector_->most_recent_ssid());
  EXPECT_EQ(kPassword, fake_wifi_hotspot_connector_->most_recent_password());
  fake_wifi_hotspot_connector_->CallMostRecentCallback("");

  // The failure should have resulted in the host being disconnected.
  EXPECT_EQ(ActiveHost::ActiveHostStatus::DISCONNECTED,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(NetworkConnectionHandler::kErrorConnectFailed, GetResultAndReset());
}

TEST_F(TetherConnectorTest, TestSuccessfulConnection) {
  CallTetherDelegate(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()));
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(test_devices_[0].GetDeviceId(),
            fake_active_host_->GetActiveHostDeviceId());
  EXPECT_EQ(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()),
            fake_active_host_->GetTetherNetworkGuid());
  EXPECT_TRUE(fake_active_host_->GetWifiNetworkGuid().empty());

  fake_tether_host_fetcher_->InvokePendingCallbacks();

  // Receive a successful response. We should still be connecting.
  EXPECT_EQ(1u, fake_operation_factory_->created_operations().size());
  fake_operation_factory_->created_operations()[0]->SendSuccessfulResponse(
      kSsid, kPassword);
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());

  // |fake_wifi_hotspot_connector_| should have received the SSID and password
  // above. Verify this, then return the GUID corresponding to the connected
  // Wi-Fi network.
  EXPECT_EQ(kSsid, fake_wifi_hotspot_connector_->most_recent_ssid());
  EXPECT_EQ(kPassword, fake_wifi_hotspot_connector_->most_recent_password());
  SuccessfullyJoinWifiNetwork();

  // The active host should now be connected, and the tether and Wi-Fi networks
  // should be associated.
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTED,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(test_devices_[0].GetDeviceId(),
            fake_active_host_->GetActiveHostDeviceId());
  EXPECT_EQ(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()),
            fake_active_host_->GetTetherNetworkGuid());
  EXPECT_EQ(kWifiNetworkGuid, fake_active_host_->GetWifiNetworkGuid());
  VerifyTetherAndWifiNetworkAssociation(
      GetTetherNetworkGuid(test_devices_[0].GetDeviceId()));
  EXPECT_EQ(kSuccessResult, GetResultAndReset());
}

TEST_F(TetherConnectorTest,
       TestNewConnectionAttemptDuringFetch_DifferentDevice) {
  CallTetherDelegate(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()));

  // Instead of invoking the pending callbacks on |fake_tether_host_fetcher_|,
  // attempt another connection attempt, this time to another device.
  CallTetherDelegate(GetTetherNetworkGuid(test_devices_[1].GetDeviceId()));
  // The first connection attempt should have resulted in a connect canceled
  // error.
  EXPECT_EQ(NetworkConnectionHandler::kErrorConnectCanceled,
            GetResultAndReset());

  // Now invoke the callbacks. An operation should have been created for the
  // device 1, not device 0.
  fake_tether_host_fetcher_->InvokePendingCallbacks();
  EXPECT_EQ(1u, fake_operation_factory_->created_operations().size());
  EXPECT_EQ(
      test_devices_[1],
      fake_operation_factory_->created_operations()[0]->GetRemoteDevice());
}

TEST_F(TetherConnectorTest,
       TestNewConnectionAttemptDuringOperation_DifferentDevice) {
  CallTetherDelegate(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()));
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(test_devices_[0].GetDeviceId(),
            fake_active_host_->GetActiveHostDeviceId());
  EXPECT_EQ(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()),
            fake_active_host_->GetTetherNetworkGuid());
  EXPECT_TRUE(fake_active_host_->GetWifiNetworkGuid().empty());

  fake_tether_host_fetcher_->InvokePendingCallbacks();

  // An operation should have been created.
  EXPECT_EQ(1u, fake_operation_factory_->created_operations().size());

  // Before the created operation replies, start a new connection to device 1.
  CallTetherDelegate(GetTetherNetworkGuid(test_devices_[1].GetDeviceId()));
  // The first connection attempt should have resulted in a connect canceled
  // error.
  EXPECT_EQ(NetworkConnectionHandler::kErrorConnectCanceled,
            GetResultAndReset());
  fake_tether_host_fetcher_->InvokePendingCallbacks();

  // Now, the active host should be the second device.
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(test_devices_[1].GetDeviceId(),
            fake_active_host_->GetActiveHostDeviceId());
  EXPECT_EQ(GetTetherNetworkGuid(test_devices_[1].GetDeviceId()),
            fake_active_host_->GetTetherNetworkGuid());
  EXPECT_TRUE(fake_active_host_->GetWifiNetworkGuid().empty());

  // A second operation should have been created.
  EXPECT_EQ(2u, fake_operation_factory_->created_operations().size());

  // No connection should have been started.
  EXPECT_TRUE(fake_wifi_hotspot_connector_->most_recent_ssid().empty());
  EXPECT_TRUE(fake_wifi_hotspot_connector_->most_recent_password().empty());

  // The second operation replies successfully, and this response should
  // result in a Wi-Fi connection attempt.
  fake_operation_factory_->created_operations()[1]->SendSuccessfulResponse(
      kSsid, kPassword);
  EXPECT_EQ(kSsid, fake_wifi_hotspot_connector_->most_recent_ssid());
  EXPECT_EQ(kPassword, fake_wifi_hotspot_connector_->most_recent_password());
}

TEST_F(TetherConnectorTest,
       TestNewConnectionAttemptDuringWifiConnection_DifferentDevice) {
  CallTetherDelegate(GetTetherNetworkGuid(test_devices_[0].GetDeviceId()));
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(test_devices_[0].GetDeviceId(),
            fake_active_host_->GetActiveHostDeviceId());

  fake_tether_host_fetcher_->InvokePendingCallbacks();

  EXPECT_EQ(1u, fake_operation_factory_->created_operations().size());
  fake_operation_factory_->created_operations()[0]->SendSuccessfulResponse(
      kSsid, kPassword);
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(kSsid, fake_wifi_hotspot_connector_->most_recent_ssid());
  EXPECT_EQ(kPassword, fake_wifi_hotspot_connector_->most_recent_password());

  // While the connection to the Wi-Fi network is in progress, start a new
  // connection attempt.
  CallTetherDelegate(GetTetherNetworkGuid(test_devices_[1].GetDeviceId()));
  // The first connection attempt should have resulted in a connect canceled
  // error.
  EXPECT_EQ(NetworkConnectionHandler::kErrorConnectCanceled,
            GetResultAndReset());
  fake_tether_host_fetcher_->InvokePendingCallbacks();

  // Connect successfully to the first Wi-Fi network. Even though a temporary
  // connection has succeeded, the active host should be CONNECTING to device 1.
  SuccessfullyJoinWifiNetwork();
  EXPECT_EQ(ActiveHost::ActiveHostStatus::CONNECTING,
            fake_active_host_->GetActiveHostStatus());
  EXPECT_EQ(test_devices_[1].GetDeviceId(),
            fake_active_host_->GetActiveHostDeviceId());
  EXPECT_EQ(GetTetherNetworkGuid(test_devices_[1].GetDeviceId()),
            fake_active_host_->GetTetherNetworkGuid());
  EXPECT_TRUE(fake_active_host_->GetWifiNetworkGuid().empty());
}

}  // namespace tether

}  // namespace chromeos
