// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_PAYMENTS_PAYMENT_MANAGER_H_
#define CONTENT_BROWSER_PAYMENTS_PAYMENT_MANAGER_H_

#include <string>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "components/payments/mojom/payment_app.mojom.h"
#include "content/common/content_export.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "url/gurl.h"

namespace content {

class PaymentAppContextImpl;

class CONTENT_EXPORT PaymentManager
    : public NON_EXPORTED_BASE(payments::mojom::PaymentManager) {
 public:
  PaymentManager(
      PaymentAppContextImpl* payment_app_context,
      mojo::InterfaceRequest<payments::mojom::PaymentManager> request);

  ~PaymentManager() override;

 private:
  friend class PaymentAppContentUnitTestBase;
  friend class PaymentManagerTest;

  // payments::mojom::PaymentManager methods:
  void Init(const std::string& scope) override;
  void SetManifest(payments::mojom::PaymentAppManifestPtr manifest,
                   const SetManifestCallback& callback) override;
  void GetManifest(const GetManifestCallback& callback) override;
  void DeletePaymentInstrument(
      const std::string& instrument_key,
      const DeletePaymentInstrumentCallback& callback) override;
  void SetPaymentInstrument(
      const std::string& instrument_key,
      payments::mojom::PaymentInstrumentPtr details,
      const SetPaymentInstrumentCallback& callback) override;
  void HasPaymentInstrument(
      const std::string& instrument_key,
      const HasPaymentInstrumentCallback& callback) override;
  void GetPaymentInstrument(
      const std::string& instrument_key,
      const GetPaymentInstrumentCallback& callback) override;

  // Called when an error is detected on binding_.
  void OnConnectionError();

  // PaymentAppContextImpl owns PaymentManager
  PaymentAppContextImpl* payment_app_context_;

  GURL scope_;
  mojo::Binding<payments::mojom::PaymentManager> binding_;
  base::WeakPtrFactory<PaymentManager> weak_ptr_factory_;
  DISALLOW_COPY_AND_ASSIGN(PaymentManager);
};

}  // namespace content

#endif  // CONTENT_BROWSER_PAYMENTS_PAYMENT_MANAGER_H_
