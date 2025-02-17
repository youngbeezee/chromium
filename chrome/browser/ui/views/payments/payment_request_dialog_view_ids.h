// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_PAYMENTS_PAYMENT_REQUEST_DIALOG_VIEW_IDS_H_
#define CHROME_BROWSER_UI_VIEWS_PAYMENTS_PAYMENT_REQUEST_DIALOG_VIEW_IDS_H_

#include "components/autofill/core/browser/field_types.h"

// This defines an enumeration of IDs that can uniquely identify a view within
// the scope of the Payment Request Dialog.

namespace payments {

enum class DialogViewID : int {
  VIEW_ID_NONE = autofill::MAX_VALID_FIELD_TYPE,

  // The following are views::Button (clickable).
  PAYMENT_SHEET_CONTACT_INFO_SECTION,
  PAYMENT_SHEET_SUMMARY_SECTION,
  PAYMENT_SHEET_PAYMENT_METHOD_SECTION,
  PAYMENT_SHEET_SHIPPING_ADDRESS_SECTION,
  PAYMENT_SHEET_SHIPPING_OPTION_SECTION,
  PAYMENT_METHOD_ADD_CARD_BUTTON,
  PAYMENT_METHOD_ADD_SHIPPING_BUTTON,
  PAYMENT_METHOD_ADD_CONTACT_BUTTON,
  EDITOR_SAVE_BUTTON,
  PAY_BUTTON,
  CANCEL_BUTTON,
  BACK_BUTTON,
  CVC_PROMPT_CONFIRM_BUTTON,

  // The following are buttons that are displayed inline in the Payment Sheet
  // sections when no option is selected or available.
  PAYMENT_SHEET_CONTACT_INFO_SECTION_BUTTON,
  PAYMENT_SHEET_PAYMENT_METHOD_SECTION_BUTTON,
  PAYMENT_SHEET_SHIPPING_ADDRESS_SECTION_BUTTON,
  PAYMENT_SHEET_SHIPPING_OPTION_SECTION_BUTTON,

  // The following are Label objects.
  ORDER_SUMMARY_TOTAL_AMOUNT_LABEL,
  ORDER_SUMMARY_LINE_ITEM_1,
  ORDER_SUMMARY_LINE_ITEM_2,
  ORDER_SUMMARY_LINE_ITEM_3,
  SHIPPING_OPTION_DESCRIPTION,
  SHIPPING_OPTION_AMOUNT,

  // Used in profile labels to annotate each line of the grouping.
  PROFILE_LABEL_LINE_1,
  PROFILE_LABEL_LINE_2,
  PROFILE_LABEL_LINE_3,
  PROFILE_LABEL_ERROR,

  // The following are views contained within the Payment Method Sheet.
  CONTACT_INFO_SHEET_LIST_VIEW,
  PAYMENT_METHOD_SHEET_LIST_VIEW,
  SHIPPING_ADDRESS_SHEET_LIST_VIEW,

  // Used in selectable rows. Each row in a view reuses this ID, but the ID is
  // unique at the scope of the parent row.
  CHECKMARK_VIEW,

  // Used to label the error labels with an offset, which gets added to
  // the Autofill type value they represent (for tests).
  ERROR_LABEL_OFFSET,

  // The CVC text field in the unmask sheet.
  CVC_PROMPT_TEXT_FIELD,
};

}  // namespace payments

#endif  // CHROME_BROWSER_UI_VIEWS_PAYMENTS_PAYMENT_REQUEST_DIALOG_VIEW_IDS_H_
