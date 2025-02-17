// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_DEVICE_FINGERPRINT_FINGERPRINT_CHROMEOS_H_
#define SERVICES_DEVICE_FINGERPRINT_FINGERPRINT_CHROMEOS_H_

#include <stdint.h>

#include "base/macros.h"
#include "chromeos/dbus/biod/biod_client.h"
#include "dbus/object_path.h"
#include "services/device/fingerprint/fingerprint_export.h"
#include "services/device/public/interfaces/fingerprint.mojom.h"

namespace device {

// Implementation of Fingerprint interface for ChromeOS platform.
// This is used to connect to biod(through dbus) and perform fingerprint related
// operations. It observes signals from biod.
class SERVICES_DEVICE_FINGERPRINT_EXPORT FingerprintChromeOS
    : public NON_EXPORTED_BASE(mojom::Fingerprint),
      public chromeos::BiodClient::Observer {
 public:
  explicit FingerprintChromeOS();
  ~FingerprintChromeOS() override;

  // mojom::Fingerprint:
  void GetRecordsForUser(const std::string& user_id,
                         const GetRecordsForUserCallback& callback) override;
  void StartEnrollSession(const std::string& user_id,
                          const std::string& label) override;
  void CancelCurrentEnrollSession(
      const CancelCurrentEnrollSessionCallback& callback) override;
  void RequestRecordLabel(const std::string& record_path,
                          const RequestRecordLabelCallback& callback) override;
  void SetRecordLabel(const std::string& record_path,
                      const std::string& new_label,
                      const SetRecordLabelCallback& callback) override;
  void RemoveRecord(const std::string& record_path,
                    const RemoveRecordCallback& callback) override;
  void StartAuthSession() override;
  void EndCurrentAuthSession(
      const EndCurrentAuthSessionCallback& callback) override;
  void DestroyAllRecords(const DestroyAllRecordsCallback& callback) override;
  void RequestType(const RequestTypeCallback& callback) override;
  void AddFingerprintObserver(mojom::FingerprintObserverPtr observer) override;

 private:
  friend class FingerprintChromeOSTest;

  // chromeos::BiodClient::Observer:
  void BiodServiceRestarted() override;
  void BiodEnrollScanDoneReceived(biod::ScanResult scan_result,
                                  bool enroll_session_complete) override;
  void BiodAuthScanDoneReceived(
      biod::ScanResult scan_result,
      const chromeos::AuthScanMatches& matches) override;
  void BiodSessionFailedReceived() override;

  void OnFingerprintObserverDisconnected(mojom::FingerprintObserver* observer);
  void OnStartEnrollSession(const dbus::ObjectPath& enroll_path);
  void OnStartAuthSession(const dbus::ObjectPath& auth_path);
  void OnGetRecordsForUser(const GetRecordsForUserCallback& callback,
                           const std::vector<dbus::ObjectPath>& record_paths);
  void OnGetLabelFromRecordPath(const GetRecordsForUserCallback& callback,
                                size_t num_records,
                                const dbus::ObjectPath& record_path,
                                const std::string& label);

  std::vector<mojom::FingerprintObserverPtr> observers_;
  std::unique_ptr<dbus::ObjectPath> current_enroll_session_path_;
  std::unique_ptr<dbus::ObjectPath> current_auth_session_path_;
  std::unordered_map<std::string, std::string> records_path_to_label_;

  base::WeakPtrFactory<FingerprintChromeOS> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(FingerprintChromeOS);
};

}  // namespace device

#endif  // SERVICES_DEVICE_FINGERPRINT_FINGERPRINT_CHROMEOS_H_
