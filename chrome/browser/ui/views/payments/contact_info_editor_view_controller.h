// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_PAYMENTS_CONTACT_INFO_EDITOR_VIEW_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_PAYMENTS_CONTACT_INFO_EDITOR_VIEW_CONTROLLER_H_

#include <memory>
#include <vector>

#include "base/macros.h"
#include "base/strings/string16.h"
#include "chrome/browser/ui/views/payments/editor_view_controller.h"
#include "chrome/browser/ui/views/payments/validation_delegate.h"

namespace autofill {
class AutofillProfile;
}  // namespace autofill

namespace payments {

class ContactInfoEditorViewController : public EditorViewController {
 public:
  // Does not take ownership of the arguments, which should outlive this object.
  // Passing nullptr as |profile| indicates that we are editing a new profile;
  // other arguments should never be null.
  ContactInfoEditorViewController(PaymentRequestSpec* spec,
                                  PaymentRequestState* state,
                                  PaymentRequestDialogView* dialog,
                                  autofill::AutofillProfile* profile);
  ~ContactInfoEditorViewController() override;

  // EditorViewController:
  std::unique_ptr<views::View> CreateHeaderView() override;
  std::vector<EditorField> GetFieldDefinitions() override;
  base::string16 GetInitialValueForType(
      autofill::ServerFieldType type) override;
  bool ValidateModelAndSave() override;
  std::unique_ptr<ValidationDelegate> CreateValidationDelegate(
      const EditorField& field) override;
  std::unique_ptr<ui::ComboboxModel> GetComboboxModelForType(
      const autofill::ServerFieldType& type) override;

 protected:
  // PaymentRequestSheetController:
  base::string16 GetSheetTitle() override;

 private:
  bool ValidateModel();
  // Uses the values in the UI fields to populate the corresponding values in
  // |profile|.
  void PopulateProfile(autofill::AutofillProfile* profile);

  autofill::AutofillProfile* profile_to_edit_;

  class ContactInfoValidationDelegate : public ValidationDelegate {
   public:
    ContactInfoValidationDelegate(const EditorField& field,
                                  const std::string& locale,
                                  EditorViewController* controller);
    ~ContactInfoValidationDelegate() override;

    // ValidationDelegate:
    bool ValidateTextfield(views::Textfield* textfield) override;
    bool ValidateCombobox(views::Combobox* combobox) override;
    void ComboboxModelChanged(views::Combobox* combobox) override {}

   private:
    EditorField field_;
    // Outlives this class. Never null.
    EditorViewController* controller_;
    const std::string& locale_;
  };
};

}  // namespace payments

#endif  // CHROME_BROWSER_UI_VIEWS_PAYMENTS_CONTACT_INFO_EDITOR_VIEW_CONTROLLER_H_
