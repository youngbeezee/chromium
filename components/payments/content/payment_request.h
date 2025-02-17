// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PAYMENTS_CONTENT_PAYMENT_REQUEST_H_
#define COMPONENTS_PAYMENTS_CONTENT_PAYMENT_REQUEST_H_

#include <memory>
#include <vector>

#include "base/macros.h"
#include "components/payments/content/payment_request_spec.h"
#include "components/payments/content/payment_request_state.h"
#include "components/payments/core/payment_request_delegate.h"
#include "components/payments/mojom/payment_request.mojom.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/interface_request.h"

namespace content {
class WebContents;
}

namespace payments {

class PaymentRequestWebContentsManager;

// This class manages the interaction between the renderer (through the
// PaymentRequestClient and Mojo stub implementation) and the UI (through the
// PaymentRequestDelegate). The API user (merchant) specification (supported
// payment methods, required information, order details) is stored in
// PaymentRequestSpec, and the current user selection state (and related data)
// is stored in PaymentRequestSpec.
class PaymentRequest : public mojom::PaymentRequest,
                       public PaymentRequestSpec::Observer,
                       public PaymentRequestState::Delegate {
 public:
  class ObserverForTest {
   public:
    virtual void OnCanMakePaymentCalled() = 0;
    virtual void OnNotSupportedError() = 0;

   protected:
    virtual ~ObserverForTest() {}
  };

  PaymentRequest(content::WebContents* web_contents,
                 std::unique_ptr<PaymentRequestDelegate> delegate,
                 PaymentRequestWebContentsManager* manager,
                 mojo::InterfaceRequest<mojom::PaymentRequest> request,
                 ObserverForTest* observer_for_testing);
  ~PaymentRequest() override;

  // mojom::PaymentRequest
  void Init(mojom::PaymentRequestClientPtr client,
            std::vector<mojom::PaymentMethodDataPtr> method_data,
            mojom::PaymentDetailsPtr details,
            mojom::PaymentOptionsPtr options) override;
  void Show() override;
  void UpdateWith(mojom::PaymentDetailsPtr details) override;
  void Abort() override;
  void Complete(mojom::PaymentComplete result) override;
  void CanMakePayment() override;

  // PaymentRequestSpec::Observer:
  void OnSpecUpdated() override {}

  // PaymentRequestState::Delegate:
  void OnPaymentResponseAvailable(mojom::PaymentResponsePtr response) override;
  void OnShippingOptionIdSelected(std::string shipping_option_id) override;
  void OnShippingAddressSelected(mojom::PaymentAddressPtr address) override;

  // Called when the user explicitely cancelled the flow. Will send a message
  // to the renderer which will indirectly destroy this object (through
  // OnConnectionTerminated).
  void UserCancelled();

  // As a result of a browser-side error or renderer-initiated mojo channel
  // closure (e.g. there was an error on the renderer side, or payment was
  // successful), this method is called. It is responsible for cleaning up,
  // such as possibly closing the dialog.
  void OnConnectionTerminated();

  // Called when the user clicks on the "Pay" button.
  void Pay();

  content::WebContents* web_contents() { return web_contents_; }

  PaymentRequestSpec* spec() { return spec_.get(); }
  PaymentRequestState* state() { return state_.get(); }

 private:
  content::WebContents* web_contents_;
  std::unique_ptr<PaymentRequestDelegate> delegate_;
  // |manager_| owns this PaymentRequest.
  PaymentRequestWebContentsManager* manager_;
  mojo::Binding<mojom::PaymentRequest> binding_;
  mojom::PaymentRequestClientPtr client_;

  std::unique_ptr<PaymentRequestSpec> spec_;
  std::unique_ptr<PaymentRequestState> state_;

  // May be null, must outlive this object.
  ObserverForTest* observer_for_testing_;

  DISALLOW_COPY_AND_ASSIGN(PaymentRequest);
};

}  // namespace payments

#endif  // COMPONENTS_PAYMENTS_CONTENT_PAYMENT_REQUEST_H_
