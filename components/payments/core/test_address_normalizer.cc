// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/payments/core/test_address_normalizer.h"

namespace payments {

bool TestAddressNormalizer::AreRulesLoadedForRegion(
    const std::string& region_code) {
  return true;
}

void TestAddressNormalizer::StartAddressNormalization(
    const autofill::AutofillProfile& profile,
    const std::string& region_code,
    int timeout_seconds,
    AddressNormalizer::Delegate* requester) {
  if (instantaneous_normalization_) {
    requester->OnAddressNormalized(profile);
    return;
  }

  // Setup the necessary variables for the delayed normalization.
  profile_ = profile;
  requester_ = requester;
}

void TestAddressNormalizer::DelayNormalization() {
  instantaneous_normalization_ = false;
}

void TestAddressNormalizer::CompleteAddressNormalization() {
  requester_->OnAddressNormalized(profile_);
}

}  // namespace payments
