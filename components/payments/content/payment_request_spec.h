// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PAYMENTS_CONTENT_PAYMENT_REQUEST_SPEC_H_
#define COMPONENTS_PAYMENTS_CONTENT_PAYMENT_REQUEST_SPEC_H_

#include <set>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/observer_list.h"
#include "components/payments/core/currency_formatter.h"
#include "components/payments/core/payment_options_provider.h"
#include "components/payments/mojom/payment_request.mojom.h"

namespace payments {

// Identifier for the basic card payment method in the PaymentMethodData.
extern const char kBasicCardMethodName[];

// The spec contains all the options that the merchant has specified about this
// Payment Request. It's a (mostly) read-only view, which can be updated in
// certain occasions by the merchant (see API).
class PaymentRequestSpec : public PaymentOptionsProvider {
 public:
  // This enum represents which bit of information was changed to trigger an
  // update roundtrip with the website.
  enum class UpdateReason {
    NONE,
    SHIPPING_OPTION,
    SHIPPING_ADDRESS,
  };

  // Any class call add itself as Observer via AddObserver() and receive
  // notification about spec events.
  class Observer {
   public:
    // Called when the website is notified that the user selected shipping
    // options or a shipping address. This will be followed by a call to
    // OnSpecUpdated or the PaymentRequest being aborted due to a timeout.
    virtual void OnStartUpdating(UpdateReason reason) {}

    // Called when the provided spec has changed.
    virtual void OnSpecUpdated() = 0;

   protected:
    virtual ~Observer() {}
  };

  PaymentRequestSpec(mojom::PaymentOptionsPtr options,
                     mojom::PaymentDetailsPtr details,
                     std::vector<mojom::PaymentMethodDataPtr> method_data,
                     PaymentRequestSpec::Observer* observer,
                     const std::string& app_locale);
  ~PaymentRequestSpec() override;

  // Called when the merchant has new PaymentDetails. Will recompute every spec
  // state that depends on |details|.
  void UpdateWith(mojom::PaymentDetailsPtr details);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // PaymentOptionsProvider:
  bool request_shipping() const override;
  bool request_payer_name() const override;
  bool request_payer_phone() const override;
  bool request_payer_email() const override;
  PaymentShippingType shipping_type() const override;

  const std::vector<std::string>& supported_card_networks() const {
    return supported_card_networks_;
  }
  const std::set<std::string>& supported_card_networks_set() const {
    return supported_card_networks_set_;
  }
  // Returns whether the |method_name| was specified as supported through the
  // "basic-card" payment method. If false, it means either the |method_name| is
  // not supported at all, or specified directly in supportedMethods.
  bool IsMethodSupportedThroughBasicCard(const std::string& method_name);

  // Uses CurrencyFormatter to format |amount| with the currency symbol for this
  // request's currency. Will use currency of the "total" display item, because
  // all items are supposed to have the same currency in a given request.
  base::string16 GetFormattedCurrencyAmount(const std::string& amount);

  // Uses CurrencyFormatter to get the formatted currency code for this
  // request's currency. Will use currency of the "total" display item, because
  // all items are supposed to have the same currency in a given request.
  std::string GetFormattedCurrencyCode();

  mojom::PaymentShippingOption* selected_shipping_option() const {
    return selected_shipping_option_;
  }

  const mojom::PaymentDetails& details() const { return *details_.get(); }

  void StartWaitingForUpdateWith(UpdateReason reason);

 private:
  friend class PaymentRequestDialogView;
  void add_observer_for_testing(Observer* observer_for_testing) {
    observer_for_testing_ = observer_for_testing;
  }

  // Validates the |method_data| and fills |supported_card_networks_|,
  // |supported_card_networks_set_| and |basic_card_specified_networks_|.
  void PopulateValidatedMethodData(
      const std::vector<mojom::PaymentMethodDataPtr>& method_data);

  // Updates the selected_shipping_option based on the data passed to this
  // payment request by the website. This will set selected_shipping_option_ to
  // the last option marked selected in the options array.
  void UpdateSelectedShippingOption();

  // Will notify all observers that the spec has changed.
  void NotifyOnSpecUpdated();

  // Returns the CurrencyFormatter instance for this PaymentRequest.
  // |locale_name| should be the result of the browser's GetApplicationLocale().
  // Note: Having multiple currencies per PaymentRequest is not supported; hence
  // the CurrencyFormatter is cached here.
  CurrencyFormatter* GetOrCreateCurrencyFormatter(
      const std::string& currency_code,
      const std::string& currency_system,
      const std::string& locale_name);

  mojom::PaymentOptionsPtr options_;
  mojom::PaymentDetailsPtr details_;
  const std::string app_locale_;
  // The currently shipping option as specified by the merchant.
  mojom::PaymentShippingOption* selected_shipping_option_;

  std::unique_ptr<CurrencyFormatter> currency_formatter_;

  // A list/set of supported basic card networks. The list is used to keep the
  // order in which they were specified by the merchant. The set is used for
  // fast lookup of supported methods.
  std::vector<std::string> supported_card_networks_;
  std::set<std::string> supported_card_networks_set_;

  // Only the set of basic-card specified networks. NOTE: callers should use
  // |supported_card_networks_set_| to check merchant support.
  std::set<std::string> basic_card_specified_networks_;

  // The |observer_for_testing_| will fire after all the |observers_| have been
  // notified.
  base::ObserverList<Observer> observers_;
  Observer* observer_for_testing_;

  DISALLOW_COPY_AND_ASSIGN(PaymentRequestSpec);
};

}  // namespace payments

#endif  // COMPONENTS_PAYMENTS_CONTENT_PAYMENT_REQUEST_SPEC_H_
