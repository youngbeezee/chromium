// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include "base/guid.h"
#include "base/macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "components/autofill/core/browser/autofill_experiments.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/autofill_type.h"
#include "components/autofill/core/browser/credit_card.h"
#include "components/autofill/core/browser/validation.h"
#include "components/autofill/core/common/autofill_constants.h"
#include "components/autofill/core/common/form_field_data.h"
#include "components/grit/components_scaled_resources.h"
#include "components/variations/variations_params_manager.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::ASCIIToUTF16;
using base::UTF8ToUTF16;

namespace autofill {

const CreditCard::RecordType LOCAL_CARD = CreditCard::LOCAL_CARD;
const CreditCard::RecordType MASKED_SERVER_CARD =
    CreditCard::MASKED_SERVER_CARD;
const CreditCard::RecordType FULL_SERVER_CARD = CreditCard::FULL_SERVER_CARD;

namespace {

// From https://www.paypalobjects.com/en_US/vhelp/paypalmanager_help/credit_card_numbers.htm
const char* const kValidNumbers[] = {
  "378282246310005",
  "3714 4963 5398 431",
  "3787-3449-3671-000",
  "5610591081018250",
  "3056 9309 0259 04",
  "3852-0000-0232-37",
  "6011111111111117",
  "6011 0009 9013 9424",
  "3530-1113-3330-0000",
  "3566002020360505",
  "5555 5555 5555 4444",
  "5105-1051-0510-5100",
  "4111111111111111",
  "4012 8888 8888 1881",
  "4222-2222-2222-2",
  "5019717010103742",
  "6331101999990016",
  "6247130048162403",
};
const char* const kInvalidNumbers[] = {
  "4111 1111 112", /* too short */
  "41111111111111111115", /* too long */
  "4111-1111-1111-1110", /* wrong Luhn checksum */
  "3056 9309 0259 04aa", /* non-digit characters */
};

const std::string kUTF8MidlineEllipsis =
    "  "
    "\xE2\x80\xA2\xE2\x80\x86"
    "\xE2\x80\xA2\xE2\x80\x86"
    "\xE2\x80\xA2\xE2\x80\x86"
    "\xE2\x80\xA2\xE2\x80\x86";

}  // namespace

// Tests credit card summary string generation.  This test simulates a variety
// of different possible summary strings.  Variations occur based on the
// existence of credit card number, month, and year fields.
TEST(CreditCardTest, PreviewSummaryAndTypeAndLastFourDigitsStrings) {
  // Case 0: empty credit card.
  CreditCard credit_card0(base::GenerateGUID(), "https://www.example.com/");
  base::string16 summary0 = credit_card0.Label();
  EXPECT_EQ(base::string16(), summary0);
  base::string16 obfuscated0 = credit_card0.TypeAndLastFourDigits();
  EXPECT_EQ(ASCIIToUTF16("Card"), obfuscated0);

  // Case 00: Empty credit card with empty strings.
  CreditCard credit_card00(base::GenerateGUID(), "https://www.example.com/");
  test::SetCreditCardInfo(&credit_card00,"John Dillinger", "", "", "");
  base::string16 summary00 = credit_card00.Label();
  EXPECT_EQ(base::string16(ASCIIToUTF16("John Dillinger")), summary00);
  base::string16 obfuscated00 = credit_card00.TypeAndLastFourDigits();
  EXPECT_EQ(ASCIIToUTF16("Card"), obfuscated00);

  // Case 1: No credit card number.
  CreditCard credit_card1(base::GenerateGUID(), "https://www.example.com/");
  test::SetCreditCardInfo(&credit_card1,"John Dillinger", "", "01", "2010");
  base::string16 summary1 = credit_card1.Label();
  EXPECT_EQ(base::string16(ASCIIToUTF16("John Dillinger")), summary1);
  base::string16 obfuscated1 = credit_card1.TypeAndLastFourDigits();
  EXPECT_EQ(ASCIIToUTF16("Card"), obfuscated1);

  // Case 2: No month.
  CreditCard credit_card2(base::GenerateGUID(), "https://www.example.com/");
  test::SetCreditCardInfo(
      &credit_card2, "John Dillinger", "5105 1051 0510 5100", "", "2010");
  base::string16 summary2 = credit_card2.Label();
  EXPECT_EQ(UTF8ToUTF16("MasterCard" + kUTF8MidlineEllipsis + "5100"),
            summary2);
  base::string16 obfuscated2 = credit_card2.TypeAndLastFourDigits();
  EXPECT_EQ(UTF8ToUTF16("MasterCard" + kUTF8MidlineEllipsis + "5100"),
            obfuscated2);

  // Case 3: No year.
  CreditCard credit_card3(base::GenerateGUID(), "https://www.example.com/");
  test::SetCreditCardInfo(
      &credit_card3, "John Dillinger", "5105 1051 0510 5100", "01", "");
  base::string16 summary3 = credit_card3.Label();
  EXPECT_EQ(UTF8ToUTF16("MasterCard" + kUTF8MidlineEllipsis + "5100"),
            summary3);
  base::string16 obfuscated3 = credit_card3.TypeAndLastFourDigits();
  EXPECT_EQ(UTF8ToUTF16("MasterCard" + kUTF8MidlineEllipsis + "5100"),
            obfuscated3);

  // Case 4: Have everything.
  CreditCard credit_card4(base::GenerateGUID(), "https://www.example.com/");
  test::SetCreditCardInfo(
      &credit_card4, "John Dillinger", "5105 1051 0510 5100", "01", "2010");
  base::string16 summary4 = credit_card4.Label();
  EXPECT_EQ(UTF8ToUTF16("MasterCard" + kUTF8MidlineEllipsis + "5100, 01/2010"),
            summary4);
  base::string16 obfuscated4 = credit_card4.TypeAndLastFourDigits();
  EXPECT_EQ(UTF8ToUTF16("MasterCard" + kUTF8MidlineEllipsis + "5100"),
            obfuscated4);

  // Case 5: Very long credit card
  CreditCard credit_card5(base::GenerateGUID(), "https://www.example.com/");
  test::SetCreditCardInfo(
      &credit_card5,
      "John Dillinger",
      "0123456789 0123456789 0123456789 5105 1051 0510 5100", "01", "2010");
  base::string16 summary5 = credit_card5.Label();
  EXPECT_EQ(UTF8ToUTF16("Card" + kUTF8MidlineEllipsis + "5100, 01/2010"),
            summary5);
  base::string16 obfuscated5 = credit_card5.TypeAndLastFourDigits();
  EXPECT_EQ(UTF8ToUTF16("Card" + kUTF8MidlineEllipsis + "5100"),
            obfuscated5);
}

TEST(CreditCardTest, AssignmentOperator) {
  CreditCard a(base::GenerateGUID(), "some origin");
  test::SetCreditCardInfo(&a, "John Dillinger", "123456789012", "01", "2010");

  // Result of assignment should be logically equal to the original profile.
  CreditCard b(base::GenerateGUID(), "some other origin");
  b = a;
  EXPECT_TRUE(a == b);

  // Assignment to self should not change the profile value.
  a = a;
  EXPECT_TRUE(a == b);
}

struct SetExpirationYearFromStringTestCase {
  std::string expiration_year;
  int expected_year;
};

class SetExpirationYearFromStringTest
    : public testing::TestWithParam<SetExpirationYearFromStringTestCase> {};

TEST_P(SetExpirationYearFromStringTest, SetExpirationYearFromString) {
  auto test_case = GetParam();
  CreditCard card(base::GenerateGUID(), "some origin");
  card.SetExpirationYearFromString(ASCIIToUTF16(test_case.expiration_year));

  EXPECT_EQ(test_case.expected_year, card.expiration_year())
      << test_case.expiration_year << " " << test_case.expected_year;
}

INSTANTIATE_TEST_CASE_P(CreditCardTest,
                        SetExpirationYearFromStringTest,
                        testing::Values(
                            // Valid values.
                            SetExpirationYearFromStringTestCase{"2040", 2040},
                            SetExpirationYearFromStringTestCase{"45", 2045},
                            SetExpirationYearFromStringTestCase{"045", 2045},
                            SetExpirationYearFromStringTestCase{"9", 2009},

                            // Unrecognized year values.
                            SetExpirationYearFromStringTestCase{"052045", 0},
                            SetExpirationYearFromStringTestCase{"123", 0},
                            SetExpirationYearFromStringTestCase{"y2045", 0}));

struct SetExpirationDateFromStringTestCase {
  std::string expiration_date;
  int expected_month;
  int expected_year;
};

class SetExpirationDateFromStringTest
    : public testing::TestWithParam<SetExpirationDateFromStringTestCase> {};

TEST_P(SetExpirationDateFromStringTest, SetExpirationDateFromString) {
  auto test_case = GetParam();
  CreditCard card(base::GenerateGUID(), "some origin");
  card.SetExpirationDateFromString(ASCIIToUTF16(test_case.expiration_date));

  EXPECT_EQ(test_case.expected_month, card.expiration_month());
  EXPECT_EQ(test_case.expected_year, card.expiration_year());
}

INSTANTIATE_TEST_CASE_P(
    CreditCardTest,
    SetExpirationDateFromStringTest,
    testing::Values(
        SetExpirationDateFromStringTestCase{"10", 0, 0},       // Too small.
        SetExpirationDateFromStringTestCase{"1020451", 0, 0},  // Too long.

        // No separators.
        SetExpirationDateFromStringTestCase{"105", 0, 0},  // Too ambiguous.
        SetExpirationDateFromStringTestCase{"0545", 5, 2045},
        SetExpirationDateFromStringTestCase{"52045", 0, 0},  // Too ambiguous.
        SetExpirationDateFromStringTestCase{"052045", 5, 2045},

        // "/" separator.
        SetExpirationDateFromStringTestCase{"05/45", 5, 2045},
        SetExpirationDateFromStringTestCase{"5/2045", 5, 2045},
        SetExpirationDateFromStringTestCase{"05/2045", 5, 2045},

        // "-" separator.
        SetExpirationDateFromStringTestCase{"05-45", 5, 2045},
        SetExpirationDateFromStringTestCase{"5-2045", 5, 2045},
        SetExpirationDateFromStringTestCase{"05-2045", 5, 2045},

        // "|" separator.
        SetExpirationDateFromStringTestCase{"05|45", 5, 2045},
        SetExpirationDateFromStringTestCase{"5|2045", 5, 2045},
        SetExpirationDateFromStringTestCase{"05|2045", 5, 2045},

        // Invalid values.
        SetExpirationDateFromStringTestCase{"13/2016", 0, 2016},
        SetExpirationDateFromStringTestCase{"16/13", 0, 2013},
        SetExpirationDateFromStringTestCase{"May-2015", 0, 0},
        SetExpirationDateFromStringTestCase{"05-/2045", 0, 0},
        SetExpirationDateFromStringTestCase{"05_2045", 0, 0}));

TEST(CreditCardTest, Copy) {
  CreditCard a(base::GenerateGUID(), "https://www.example.com");
  test::SetCreditCardInfo(&a, "John Dillinger", "123456789012", "01", "2010");

  // Clone should be logically equal to the original.
  CreditCard b(a);
  EXPECT_TRUE(a == b);
}

struct IsLocalDuplicateOfServerCardTestCase {
  CreditCard::RecordType first_card_record_type;
  const char* first_card_name;
  const char* first_card_number;
  const char* first_card_exp_mo;
  const char* first_card_exp_yr;

  CreditCard::RecordType second_card_record_type;
  const char* second_card_name;
  const char* second_card_number;
  const char* second_card_exp_mo;
  const char* second_card_exp_yr;
  const char* second_card_type;

  bool is_local_duplicate;
};

class IsLocalDuplicateOfServerCardTest
    : public testing::TestWithParam<IsLocalDuplicateOfServerCardTestCase> {};

TEST_P(IsLocalDuplicateOfServerCardTest, IsLocalDuplicateOfServerCard) {
  auto test_case = GetParam();
  CreditCard a(base::GenerateGUID(), std::string());
  a.set_record_type(test_case.first_card_record_type);
  test::SetCreditCardInfo(
      &a, test_case.first_card_name, test_case.first_card_number,
      test_case.first_card_exp_mo, test_case.first_card_exp_yr);

  CreditCard b(base::GenerateGUID(), std::string());
  b.set_record_type(test_case.second_card_record_type);
  test::SetCreditCardInfo(
      &b, test_case.second_card_name, test_case.second_card_number,
      test_case.second_card_exp_mo, test_case.second_card_exp_yr);

  if (test_case.second_card_record_type == CreditCard::MASKED_SERVER_CARD)
    b.SetTypeForMaskedCard(test_case.second_card_type);

  EXPECT_EQ(test_case.is_local_duplicate, a.IsLocalDuplicateOfServerCard(b))
      << " when comparing cards " << a.Label() << " and " << b.Label();
}

INSTANTIATE_TEST_CASE_P(
    CreditCardTest,
    IsLocalDuplicateOfServerCardTest,
    testing::Values(
        IsLocalDuplicateOfServerCardTestCase{LOCAL_CARD, "", "", "", "",
                                             LOCAL_CARD, "", "", "", "",
                                             nullptr, false},
        IsLocalDuplicateOfServerCardTestCase{LOCAL_CARD, "", "", "", "",
                                             FULL_SERVER_CARD, "", "", "", "",
                                             nullptr, true},
        IsLocalDuplicateOfServerCardTestCase{FULL_SERVER_CARD, "", "", "", "",
                                             FULL_SERVER_CARD, "", "", "", "",
                                             nullptr, false},
        IsLocalDuplicateOfServerCardTestCase{
            LOCAL_CARD, "John Dillinger", "423456789012", "01", "2010",
            FULL_SERVER_CARD, "John Dillinger", "423456789012", "01", "2010",
            nullptr, true},
        IsLocalDuplicateOfServerCardTestCase{
            LOCAL_CARD, "J Dillinger", "423456789012", "01", "2010",
            FULL_SERVER_CARD, "John Dillinger", "423456789012", "01", "2010",
            nullptr, false},
        IsLocalDuplicateOfServerCardTestCase{
            LOCAL_CARD, "", "423456789012", "01", "2010", FULL_SERVER_CARD,
            "John Dillinger", "423456789012", "01", "2010", nullptr, true},
        IsLocalDuplicateOfServerCardTestCase{
            LOCAL_CARD, "", "423456789012", "", "", FULL_SERVER_CARD,
            "John Dillinger", "423456789012", "01", "2010", nullptr, true},
        IsLocalDuplicateOfServerCardTestCase{
            LOCAL_CARD, "", "423456789012", "", "", MASKED_SERVER_CARD,
            "John Dillinger", "9012", "01", "2010", kVisaCard, true},
        IsLocalDuplicateOfServerCardTestCase{
            LOCAL_CARD, "", "423456789012", "", "", MASKED_SERVER_CARD,
            "John Dillinger", "9012", "01", "2010", kMasterCard, false},
        IsLocalDuplicateOfServerCardTestCase{
            LOCAL_CARD, "John Dillinger", "4234-5678-9012", "01", "2010",
            FULL_SERVER_CARD, "John Dillinger", "423456789012", "01", "2010",
            nullptr, true}));

TEST(CreditCardTest, HasSameNumberAs) {
  CreditCard a(base::GenerateGUID(), std::string());
  CreditCard b(base::GenerateGUID(), std::string());

  // Empty cards have the same empty number.
  EXPECT_TRUE(a.HasSameNumberAs(b));
  EXPECT_TRUE(b.HasSameNumberAs(a));

  // Same number.
  a.set_record_type(CreditCard::LOCAL_CARD);
  a.SetRawInfo(CREDIT_CARD_NUMBER, ASCIIToUTF16("4111111111111111"));
  a.set_record_type(CreditCard::LOCAL_CARD);
  b.SetRawInfo(CREDIT_CARD_NUMBER, ASCIIToUTF16("4111111111111111"));
  EXPECT_TRUE(a.HasSameNumberAs(b));
  EXPECT_TRUE(b.HasSameNumberAs(a));

  // Local cards shouldn't match even if the last 4 are the same.
  a.set_record_type(CreditCard::LOCAL_CARD);
  a.SetRawInfo(CREDIT_CARD_NUMBER, ASCIIToUTF16("4111111111111111"));
  a.set_record_type(CreditCard::LOCAL_CARD);
  b.SetRawInfo(CREDIT_CARD_NUMBER, ASCIIToUTF16("4111222222221111"));
  EXPECT_FALSE(a.HasSameNumberAs(b));
  EXPECT_FALSE(b.HasSameNumberAs(a));

  // Likewise if one is an unmasked server card.
  a.set_record_type(CreditCard::FULL_SERVER_CARD);
  EXPECT_FALSE(a.HasSameNumberAs(b));
  EXPECT_FALSE(b.HasSameNumberAs(a));

  // But if one is a masked card, then they should.
  b.set_record_type(CreditCard::MASKED_SERVER_CARD);
  EXPECT_TRUE(a.HasSameNumberAs(b));
  EXPECT_TRUE(b.HasSameNumberAs(a));
}

TEST(CreditCardTest, Compare) {
  CreditCard a(base::GenerateGUID(), std::string());
  CreditCard b(base::GenerateGUID(), std::string());

  // Empty cards are the same.
  EXPECT_EQ(0, a.Compare(b));

  // GUIDs don't count.
  a.set_guid(base::GenerateGUID());
  b.set_guid(base::GenerateGUID());
  EXPECT_EQ(0, a.Compare(b));

  // Origins don't count.
  a.set_origin("apple");
  b.set_origin("banana");
  EXPECT_EQ(0, a.Compare(b));

  // Different values produce non-zero results.
  test::SetCreditCardInfo(&a, "Jimmy", NULL, NULL, NULL);
  test::SetCreditCardInfo(&b, "Ringo", NULL, NULL, NULL);
  EXPECT_GT(0, a.Compare(b));
  EXPECT_LT(0, b.Compare(a));
}

// This method is not compiled for iOS because these resources are not used and
// should not be shipped.
#if !defined(OS_IOS)
// Test we get the correct icon for each card type.
TEST(CreditCardTest, IconResourceId) {
  EXPECT_EQ(IDR_AUTOFILL_CC_AMEX,
            CreditCard::IconResourceId(kAmericanExpressCard));
  EXPECT_EQ(IDR_AUTOFILL_CC_DINERS,
            CreditCard::IconResourceId(kDinersCard));
  EXPECT_EQ(IDR_AUTOFILL_CC_DISCOVER,
            CreditCard::IconResourceId(kDiscoverCard));
  EXPECT_EQ(IDR_AUTOFILL_CC_JCB,
            CreditCard::IconResourceId(kJCBCard));
  EXPECT_EQ(IDR_AUTOFILL_CC_MASTERCARD,
            CreditCard::IconResourceId(kMasterCard));
  EXPECT_EQ(IDR_AUTOFILL_CC_MIR,
            CreditCard::IconResourceId(kMirCard));
  EXPECT_EQ(IDR_AUTOFILL_CC_UNIONPAY,
            CreditCard::IconResourceId(kUnionPay));
  EXPECT_EQ(IDR_AUTOFILL_CC_VISA,
            CreditCard::IconResourceId(kVisaCard));
}
#endif  // #if !defined(OS_IOS)

TEST(CreditCardTest, UpdateFromImportedCard) {
  CreditCard original_card(base::GenerateGUID(), "https://www.example.com");
  test::SetCreditCardInfo(
      &original_card, "John Dillinger", "123456789012", "09", "2017");

  CreditCard a = original_card;

  // The new card has a different name, expiration date, and origin.
  CreditCard b = a;
  b.set_guid(base::GenerateGUID());
  b.set_origin("https://www.example.org");
  b.SetRawInfo(CREDIT_CARD_NAME_FULL, ASCIIToUTF16("J. Dillinger"));
  b.SetRawInfo(CREDIT_CARD_EXP_MONTH, ASCIIToUTF16("08"));
  b.SetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR, ASCIIToUTF16("2019"));

  // |a| should be updated with the information from |b|.
  EXPECT_TRUE(a.UpdateFromImportedCard(b, "en-US"));
  EXPECT_EQ("https://www.example.org", a.origin());
  EXPECT_EQ(ASCIIToUTF16("J. Dillinger"), a.GetRawInfo(CREDIT_CARD_NAME_FULL));
  EXPECT_EQ(ASCIIToUTF16("08"), a.GetRawInfo(CREDIT_CARD_EXP_MONTH));
  EXPECT_EQ(ASCIIToUTF16("2019"), a.GetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR));

  // Try again, but with no name set for |b|.
  // |a| should be updated with |b|'s expiration date and keep its original
  // name.
  a = original_card;
  b.SetRawInfo(CREDIT_CARD_NAME_FULL, base::string16());

  EXPECT_TRUE(a.UpdateFromImportedCard(b, "en-US"));
  EXPECT_EQ("https://www.example.org", a.origin());
  EXPECT_EQ(ASCIIToUTF16("John Dillinger"),
            a.GetRawInfo(CREDIT_CARD_NAME_FULL));
  EXPECT_EQ(ASCIIToUTF16("08"), a.GetRawInfo(CREDIT_CARD_EXP_MONTH));
  EXPECT_EQ(ASCIIToUTF16("2019"), a.GetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR));

  // Try again, but with only the original card having a verified origin.
  // |a| should be unchanged.
  a = original_card;
  a.set_origin(kSettingsOrigin);
  b.SetRawInfo(CREDIT_CARD_NAME_FULL, ASCIIToUTF16("J. Dillinger"));

  EXPECT_TRUE(a.UpdateFromImportedCard(b, "en-US"));
  EXPECT_EQ(kSettingsOrigin, a.origin());
  EXPECT_EQ(ASCIIToUTF16("John Dillinger"),
            a.GetRawInfo(CREDIT_CARD_NAME_FULL));
  EXPECT_EQ(ASCIIToUTF16("09"), a.GetRawInfo(CREDIT_CARD_EXP_MONTH));
  EXPECT_EQ(ASCIIToUTF16("2017"), a.GetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR));

  // Try again, but with using an expired verified original card.
  // |a| should not be updated because the name on the cards are not identical.
  a = original_card;
  a.set_origin("Chrome settings");
  a.SetExpirationYear(2010);

  EXPECT_TRUE(a.UpdateFromImportedCard(b, "en-US"));
  EXPECT_EQ("Chrome settings", a.origin());
  EXPECT_EQ(ASCIIToUTF16("John Dillinger"),
            a.GetRawInfo(CREDIT_CARD_NAME_FULL));
  EXPECT_EQ(ASCIIToUTF16("09"), a.GetRawInfo(CREDIT_CARD_EXP_MONTH));
  EXPECT_EQ(ASCIIToUTF16("2010"), a.GetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR));

  // Try again, but with using identical name on the cards.
  // |a|'s expiration date should be updated.
  a = original_card;
  a.set_origin("Chrome settings");
  a.SetExpirationYear(2010);
  a.SetRawInfo(CREDIT_CARD_NAME_FULL, ASCIIToUTF16("J. Dillinger"));

  EXPECT_TRUE(a.UpdateFromImportedCard(b, "en-US"));
  EXPECT_EQ("Chrome settings", a.origin());
  EXPECT_EQ(ASCIIToUTF16("J. Dillinger"), a.GetRawInfo(CREDIT_CARD_NAME_FULL));
  EXPECT_EQ(ASCIIToUTF16("08"), a.GetRawInfo(CREDIT_CARD_EXP_MONTH));
  EXPECT_EQ(ASCIIToUTF16("2019"), a.GetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR));

  // Try again, but with |b| being expired.
  // |a|'s expiration date should not be updated.
  a = original_card;
  a.set_origin("Chrome settings");
  a.SetExpirationYear(2010);
  a.SetRawInfo(CREDIT_CARD_NAME_FULL, ASCIIToUTF16("J. Dillinger"));
  b.SetExpirationYear(2009);

  EXPECT_TRUE(a.UpdateFromImportedCard(b, "en-US"));
  EXPECT_EQ("Chrome settings", a.origin());
  EXPECT_EQ(ASCIIToUTF16("J. Dillinger"), a.GetRawInfo(CREDIT_CARD_NAME_FULL));
  EXPECT_EQ(ASCIIToUTF16("09"), a.GetRawInfo(CREDIT_CARD_EXP_MONTH));
  EXPECT_EQ(ASCIIToUTF16("2010"), a.GetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR));

  // Put back |b|'s initial expiration date.
  b.SetExpirationYear(2019);

  // Try again, but with only the new card having a verified origin.
  // |a| should be updated.
  a = original_card;
  b.set_origin(kSettingsOrigin);

  EXPECT_TRUE(a.UpdateFromImportedCard(b, "en-US"));
  EXPECT_EQ(kSettingsOrigin, a.origin());
  EXPECT_EQ(ASCIIToUTF16("J. Dillinger"), a.GetRawInfo(CREDIT_CARD_NAME_FULL));
  EXPECT_EQ(ASCIIToUTF16("08"), a.GetRawInfo(CREDIT_CARD_EXP_MONTH));
  EXPECT_EQ(ASCIIToUTF16("2019"), a.GetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR));

  // Try again, with both cards having a verified origin.
  // |a| should be updated.
  a = original_card;
  a.set_origin("Chrome Autofill dialog");
  b.set_origin(kSettingsOrigin);

  EXPECT_TRUE(a.UpdateFromImportedCard(b, "en-US"));
  EXPECT_EQ(kSettingsOrigin, a.origin());
  EXPECT_EQ(ASCIIToUTF16("J. Dillinger"), a.GetRawInfo(CREDIT_CARD_NAME_FULL));
  EXPECT_EQ(ASCIIToUTF16("08"), a.GetRawInfo(CREDIT_CARD_EXP_MONTH));
  EXPECT_EQ(ASCIIToUTF16("2019"), a.GetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR));

  // Try again, but with |b| having a different card number.
  // |a| should be unchanged.
  a = original_card;
  b.SetRawInfo(CREDIT_CARD_NUMBER, ASCIIToUTF16("4111111111111111"));

  EXPECT_FALSE(a.UpdateFromImportedCard(b, "en-US"));
  EXPECT_EQ(original_card, a);
}

TEST(CreditCardTest, IsValid) {
  CreditCard card;
  // Invalid because expired
  const base::Time now(base::Time::Now());
  base::Time::Exploded now_exploded;
  now.LocalExplode(&now_exploded);
  card.SetRawInfo(CREDIT_CARD_EXP_MONTH,
                  base::IntToString16(now_exploded.month));
  card.SetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR,
                  base::IntToString16(now_exploded.year - 1));
  card.SetRawInfo(CREDIT_CARD_NUMBER, ASCIIToUTF16("4111111111111111"));
  EXPECT_FALSE(card.IsValid());

  // Invalid because card number is not complete
  card.SetRawInfo(CREDIT_CARD_EXP_MONTH, ASCIIToUTF16("12"));
  card.SetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR, ASCIIToUTF16("2999"));
  card.SetRawInfo(CREDIT_CARD_NUMBER, ASCIIToUTF16("41111"));
  EXPECT_FALSE(card.IsValid());

  for (const char* valid_number : kValidNumbers) {
    SCOPED_TRACE(valid_number);
    card.SetRawInfo(CREDIT_CARD_NUMBER, ASCIIToUTF16(valid_number));
    EXPECT_TRUE(card.IsValid());
  }
  for (const char* invalid_number : kInvalidNumbers) {
    SCOPED_TRACE(invalid_number);
    card.SetRawInfo(CREDIT_CARD_NUMBER, ASCIIToUTF16(invalid_number));
    EXPECT_FALSE(card.IsValid());
  }
}

// Verify that we preserve exactly what the user typed for credit card numbers.
TEST(CreditCardTest, SetRawInfoCreditCardNumber) {
  CreditCard card(base::GenerateGUID(), "https://www.example.com/");

  test::SetCreditCardInfo(&card, "Bob Dylan",
                          "4321-5432-6543-xxxx", "07", "2013");
  EXPECT_EQ(ASCIIToUTF16("4321-5432-6543-xxxx"),
            card.GetRawInfo(CREDIT_CARD_NUMBER));
}

// Verify that we can handle both numeric and named months.
TEST(CreditCardTest, SetExpirationMonth) {
  CreditCard card(base::GenerateGUID(), "https://www.example.com/");

  card.SetRawInfo(CREDIT_CARD_EXP_MONTH, ASCIIToUTF16("05"));
  EXPECT_EQ(ASCIIToUTF16("05"), card.GetRawInfo(CREDIT_CARD_EXP_MONTH));
  EXPECT_EQ(5, card.expiration_month());

  card.SetRawInfo(CREDIT_CARD_EXP_MONTH, ASCIIToUTF16("7"));
  EXPECT_EQ(ASCIIToUTF16("07"), card.GetRawInfo(CREDIT_CARD_EXP_MONTH));
  EXPECT_EQ(7, card.expiration_month());

  // This should fail, and preserve the previous value.
  card.SetRawInfo(CREDIT_CARD_EXP_MONTH, ASCIIToUTF16("January"));
  EXPECT_EQ(ASCIIToUTF16("07"), card.GetRawInfo(CREDIT_CARD_EXP_MONTH));
  EXPECT_EQ(7, card.expiration_month());

  card.SetInfo(
      AutofillType(CREDIT_CARD_EXP_MONTH), ASCIIToUTF16("January"), "en-US");
  EXPECT_EQ(ASCIIToUTF16("01"), card.GetRawInfo(CREDIT_CARD_EXP_MONTH));
  EXPECT_EQ(1, card.expiration_month());

  card.SetInfo(
      AutofillType(CREDIT_CARD_EXP_MONTH), ASCIIToUTF16("Apr"), "en-US");
  EXPECT_EQ(ASCIIToUTF16("04"), card.GetRawInfo(CREDIT_CARD_EXP_MONTH));
  EXPECT_EQ(4, card.expiration_month());

  card.SetInfo(AutofillType(CREDIT_CARD_EXP_MONTH),
               UTF8ToUTF16("FÉVRIER"), "fr-FR");
  EXPECT_EQ(ASCIIToUTF16("02"), card.GetRawInfo(CREDIT_CARD_EXP_MONTH));
  EXPECT_EQ(2, card.expiration_month());
}

TEST(CreditCardTest, CreditCardType) {
  CreditCard card(base::GenerateGUID(), "https://www.example.com/");

  // The card type cannot be set directly.
  card.SetRawInfo(CREDIT_CARD_TYPE, ASCIIToUTF16("Visa"));
  EXPECT_EQ(base::string16(), card.GetRawInfo(CREDIT_CARD_TYPE));

  // Setting the number should implicitly set the type.
  card.SetRawInfo(CREDIT_CARD_NUMBER, ASCIIToUTF16("4111 1111 1111 1111"));
  EXPECT_EQ(ASCIIToUTF16("Visa"), card.GetRawInfo(CREDIT_CARD_TYPE));
}

TEST(CreditCardTest, CreditCardVerificationCode) {
  CreditCard card(base::GenerateGUID(), "https://www.example.com/");

  // The verification code cannot be set, as Chrome does not store this data.
  card.SetRawInfo(CREDIT_CARD_VERIFICATION_CODE, ASCIIToUTF16("999"));
  EXPECT_EQ(base::string16(), card.GetRawInfo(CREDIT_CARD_VERIFICATION_CODE));
}

struct GetCreditCardTypeTestCase {
  std::string card_number;
  std::string type;
  bool is_valid;
};

// We are doing batches here because INSTANTIATE_TEST_CASE_P has a
// 50 upper limit.
class GetCreditCardTypeTestBatch1
    : public testing::TestWithParam<GetCreditCardTypeTestCase> {};

TEST_P(GetCreditCardTypeTestBatch1, GetCreditCardType) {
  auto test_case = GetParam();
  base::string16 card_number = ASCIIToUTF16(test_case.card_number);
  SCOPED_TRACE(card_number);
  EXPECT_EQ(test_case.type, CreditCard::GetCreditCardType(card_number));
  EXPECT_EQ(test_case.is_valid, IsValidCreditCardNumber(card_number));
}

INSTANTIATE_TEST_CASE_P(
    CreditCardTest,
    GetCreditCardTypeTestBatch1,
    testing::Values(
        // The relevant sample numbers from
        // http://www.paypalobjects.com/en_US/vhelp/paypalmanager_help/credit_card_numbers.htm
        GetCreditCardTypeTestCase{"378282246310005", kAmericanExpressCard,
                                  true},
        GetCreditCardTypeTestCase{"371449635398431", kAmericanExpressCard,
                                  true},
        GetCreditCardTypeTestCase{"378734493671000", kAmericanExpressCard,
                                  true},
        GetCreditCardTypeTestCase{"30569309025904", kDinersCard, true},
        GetCreditCardTypeTestCase{"38520000023237", kDinersCard, true},
        GetCreditCardTypeTestCase{"6011111111111117", kDiscoverCard, true},
        GetCreditCardTypeTestCase{"6011000990139424", kDiscoverCard, true},
        GetCreditCardTypeTestCase{"3530111333300000", kJCBCard, true},
        GetCreditCardTypeTestCase{"3566002020360505", kJCBCard, true},
        GetCreditCardTypeTestCase{"5555555555554444", kMasterCard, true},
        GetCreditCardTypeTestCase{"5105105105105100", kMasterCard, true},
        GetCreditCardTypeTestCase{"4111111111111111", kVisaCard, true},
        GetCreditCardTypeTestCase{"4012888888881881", kVisaCard, true},
        GetCreditCardTypeTestCase{"4222222222222", kVisaCard, true},

        // The relevant sample numbers from
        // https://www.auricsystems.com/sample-credit-card-numbers/
        GetCreditCardTypeTestCase{"343434343434343", kAmericanExpressCard,
                                  true},
        GetCreditCardTypeTestCase{"371144371144376", kAmericanExpressCard,
                                  true},
        GetCreditCardTypeTestCase{"341134113411347", kAmericanExpressCard,
                                  true},
        GetCreditCardTypeTestCase{"36438936438936", kDinersCard, true},
        GetCreditCardTypeTestCase{"36110361103612", kDinersCard, true},
        GetCreditCardTypeTestCase{"36111111111111", kDinersCard, true},
        GetCreditCardTypeTestCase{"6011016011016011", kDiscoverCard, true},
        GetCreditCardTypeTestCase{"6011000990139424", kDiscoverCard, true},
        GetCreditCardTypeTestCase{"6011000000000004", kDiscoverCard, true},
        GetCreditCardTypeTestCase{"6011000995500000", kDiscoverCard, true},
        GetCreditCardTypeTestCase{"6500000000000002", kDiscoverCard, true},
        GetCreditCardTypeTestCase{"3566002020360505", kJCBCard, true},
        GetCreditCardTypeTestCase{"3528000000000007", kJCBCard, true},
        GetCreditCardTypeTestCase{"5500005555555559", kMasterCard, true},
        GetCreditCardTypeTestCase{"5555555555555557", kMasterCard, true},
        GetCreditCardTypeTestCase{"5454545454545454", kMasterCard, true},
        GetCreditCardTypeTestCase{"5555515555555551", kMasterCard, true},
        GetCreditCardTypeTestCase{"5405222222222226", kMasterCard, true},
        GetCreditCardTypeTestCase{"5478050000000007", kMasterCard, true},
        GetCreditCardTypeTestCase{"5111005111051128", kMasterCard, true},
        GetCreditCardTypeTestCase{"5112345112345114", kMasterCard, true},
        GetCreditCardTypeTestCase{"5115915115915118", kMasterCard, true},
        GetCreditCardTypeTestCase{"6247130048162403", kUnionPay, true},
        GetCreditCardTypeTestCase{"6247130048162403", kUnionPay, true},
        GetCreditCardTypeTestCase{"622384452162063648", kUnionPay, true},
        GetCreditCardTypeTestCase{"2204883716636153", kMirCard, true},
        GetCreditCardTypeTestCase{"2200111234567898", kMirCard, true},
        GetCreditCardTypeTestCase{"2200481349288130", kMirCard, true},

        // Empty string
        GetCreditCardTypeTestCase{std::string(), kGenericCard, false},

        // Non-numeric
        GetCreditCardTypeTestCase{"garbage", kGenericCard, false},
        GetCreditCardTypeTestCase{"4garbage", kVisaCard, false},

        // Fails Luhn check.
        GetCreditCardTypeTestCase{"4111111111111112", kVisaCard, false},
        GetCreditCardTypeTestCase{"6247130048162413", kUnionPay, false},
        GetCreditCardTypeTestCase{"2204883716636154", kMirCard, false}));

class GetCreditCardTypeTestBatch2
    : public testing::TestWithParam<GetCreditCardTypeTestCase> {};

TEST_P(GetCreditCardTypeTestBatch2, GetCreditCardType) {
  auto test_case = GetParam();
  base::string16 card_number = ASCIIToUTF16(test_case.card_number);
  SCOPED_TRACE(card_number);
  EXPECT_EQ(test_case.type, CreditCard::GetCreditCardType(card_number));
  EXPECT_EQ(test_case.is_valid, IsValidCreditCardNumber(card_number));
}

INSTANTIATE_TEST_CASE_P(
    CreditCardTest,
    GetCreditCardTypeTestBatch2,
    testing::Values(
        // Invalid length.
        GetCreditCardTypeTestCase{"3434343434343434", kAmericanExpressCard,
                                  false},
        GetCreditCardTypeTestCase{"411111111111116", kVisaCard, false},
        GetCreditCardTypeTestCase{"220011123456783", kMirCard, false},

        // Issuer Identification Numbers (IINs) that Chrome recognizes.
        GetCreditCardTypeTestCase{"4", kVisaCard, false},
        GetCreditCardTypeTestCase{"22", kMirCard, false},
        GetCreditCardTypeTestCase{"34", kAmericanExpressCard, false},
        GetCreditCardTypeTestCase{"37", kAmericanExpressCard, false},
        GetCreditCardTypeTestCase{"300", kDinersCard, false},
        GetCreditCardTypeTestCase{"301", kDinersCard, false},
        GetCreditCardTypeTestCase{"302", kDinersCard, false},
        GetCreditCardTypeTestCase{"303", kDinersCard, false},
        GetCreditCardTypeTestCase{"304", kDinersCard, false},
        GetCreditCardTypeTestCase{"305", kDinersCard, false},
        GetCreditCardTypeTestCase{"3095", kDinersCard, false},
        GetCreditCardTypeTestCase{"36", kDinersCard, false},
        GetCreditCardTypeTestCase{"38", kDinersCard, false},
        GetCreditCardTypeTestCase{"39", kDinersCard, false},
        GetCreditCardTypeTestCase{"6011", kDiscoverCard, false},
        GetCreditCardTypeTestCase{"644", kDiscoverCard, false},
        GetCreditCardTypeTestCase{"645", kDiscoverCard, false},
        GetCreditCardTypeTestCase{"646", kDiscoverCard, false},
        GetCreditCardTypeTestCase{"647", kDiscoverCard, false},
        GetCreditCardTypeTestCase{"648", kDiscoverCard, false},
        GetCreditCardTypeTestCase{"649", kDiscoverCard, false},
        GetCreditCardTypeTestCase{"65", kDiscoverCard, false},
        GetCreditCardTypeTestCase{"3528", kJCBCard, false},
        GetCreditCardTypeTestCase{"3531", kJCBCard, false},
        GetCreditCardTypeTestCase{"3589", kJCBCard, false},
        GetCreditCardTypeTestCase{"51", kMasterCard, false},
        GetCreditCardTypeTestCase{"52", kMasterCard, false},
        GetCreditCardTypeTestCase{"53", kMasterCard, false},
        GetCreditCardTypeTestCase{"54", kMasterCard, false},
        GetCreditCardTypeTestCase{"55", kMasterCard, false},
        GetCreditCardTypeTestCase{"62", kUnionPay, false},

        // Not enough data to determine an IIN uniquely.
        GetCreditCardTypeTestCase{"2", kGenericCard, false},
        GetCreditCardTypeTestCase{"3", kGenericCard, false},
        GetCreditCardTypeTestCase{"30", kGenericCard, false},
        GetCreditCardTypeTestCase{"309", kGenericCard, false},
        GetCreditCardTypeTestCase{"35", kGenericCard, false},
        GetCreditCardTypeTestCase{"5", kGenericCard, false},
        GetCreditCardTypeTestCase{"6", kGenericCard, false},
        GetCreditCardTypeTestCase{"60", kGenericCard, false},
        GetCreditCardTypeTestCase{"601", kGenericCard, false},
        GetCreditCardTypeTestCase{"64", kGenericCard, false}));

class GetCreditCardTypeTestBatch3
    : public testing::TestWithParam<GetCreditCardTypeTestCase> {};

TEST_P(GetCreditCardTypeTestBatch3, GetCreditCardType) {
  auto test_case = GetParam();
  base::string16 card_number = ASCIIToUTF16(test_case.card_number);
  SCOPED_TRACE(card_number);
  EXPECT_EQ(test_case.type, CreditCard::GetCreditCardType(card_number));
  EXPECT_EQ(test_case.is_valid, IsValidCreditCardNumber(card_number));
}

INSTANTIATE_TEST_CASE_P(
    CreditCardTest,
    GetCreditCardTypeTestBatch3,
    testing::Values(
        // Unknown IINs.
        GetCreditCardTypeTestCase{"0", kGenericCard, false},
        GetCreditCardTypeTestCase{"1", kGenericCard, false},
        GetCreditCardTypeTestCase{"306", kGenericCard, false},
        GetCreditCardTypeTestCase{"307", kGenericCard, false},
        GetCreditCardTypeTestCase{"308", kGenericCard, false},
        GetCreditCardTypeTestCase{"3091", kGenericCard, false},
        GetCreditCardTypeTestCase{"3094", kGenericCard, false},
        GetCreditCardTypeTestCase{"3096", kGenericCard, false},
        GetCreditCardTypeTestCase{"31", kGenericCard, false},
        GetCreditCardTypeTestCase{"32", kGenericCard, false},
        GetCreditCardTypeTestCase{"33", kGenericCard, false},
        GetCreditCardTypeTestCase{"351", kGenericCard, false},
        GetCreditCardTypeTestCase{"3527", kGenericCard, false},
        GetCreditCardTypeTestCase{"359", kGenericCard, false},
        GetCreditCardTypeTestCase{"50", kGenericCard, false},
        GetCreditCardTypeTestCase{"56", kGenericCard, false},
        GetCreditCardTypeTestCase{"57", kGenericCard, false},
        GetCreditCardTypeTestCase{"58", kGenericCard, false},
        GetCreditCardTypeTestCase{"59", kGenericCard, false},
        GetCreditCardTypeTestCase{"600", kGenericCard, false},
        GetCreditCardTypeTestCase{"602", kGenericCard, false},
        GetCreditCardTypeTestCase{"603", kGenericCard, false},
        GetCreditCardTypeTestCase{"604", kGenericCard, false},
        GetCreditCardTypeTestCase{"605", kGenericCard, false},
        GetCreditCardTypeTestCase{"606", kGenericCard, false},
        GetCreditCardTypeTestCase{"607", kGenericCard, false},
        GetCreditCardTypeTestCase{"608", kGenericCard, false},
        GetCreditCardTypeTestCase{"609", kGenericCard, false},
        GetCreditCardTypeTestCase{"61", kGenericCard, false},
        GetCreditCardTypeTestCase{"63", kGenericCard, false},
        GetCreditCardTypeTestCase{"640", kGenericCard, false},
        GetCreditCardTypeTestCase{"641", kGenericCard, false},
        GetCreditCardTypeTestCase{"642", kGenericCard, false},
        GetCreditCardTypeTestCase{"643", kGenericCard, false},
        GetCreditCardTypeTestCase{"66", kGenericCard, false},
        GetCreditCardTypeTestCase{"67", kGenericCard, false},
        GetCreditCardTypeTestCase{"68", kGenericCard, false},
        GetCreditCardTypeTestCase{"69", kGenericCard, false},
        GetCreditCardTypeTestCase{"7", kGenericCard, false},
        GetCreditCardTypeTestCase{"8", kGenericCard, false},
        GetCreditCardTypeTestCase{"9", kGenericCard, false},

        // Oddball case: Unknown issuer, but valid Luhn check and plausible
        // length.
        GetCreditCardTypeTestCase{"7000700070007000", kGenericCard, true}));

TEST(CreditCardTest, LastFourDigits) {
  CreditCard card(base::GenerateGUID(), "https://www.example.com/");
  ASSERT_EQ(base::string16(), card.LastFourDigits());

  test::SetCreditCardInfo(&card, "Baby Face Nelson",
                          "5212341234123489", "01", "2010");
  ASSERT_EQ(base::ASCIIToUTF16("3489"), card.LastFourDigits());

  card.SetRawInfo(CREDIT_CARD_NUMBER, ASCIIToUTF16("3489"));
  ASSERT_EQ(base::ASCIIToUTF16("3489"), card.LastFourDigits());

  card.SetRawInfo(CREDIT_CARD_NUMBER, ASCIIToUTF16("489"));
  ASSERT_EQ(base::ASCIIToUTF16("489"), card.LastFourDigits());
}

// Verifies that a credit card should be updated.
struct ShouldUpdateExpirationTestCase {
  bool should_update_expiration;
  int month;
  int year;
  CreditCard::RecordType record_type;
  CreditCard::ServerStatus server_status;
};

class ShouldUpdateExpirationTest
    : public testing::TestWithParam<ShouldUpdateExpirationTestCase> {};

class TestingTimes {
 public:
  TestingTimes() {
    now_ = base::Time::Now();
    (now_ - base::TimeDelta::FromDays(365)).LocalExplode(&last_year_);
    (now_ - base::TimeDelta::FromDays(31)).LocalExplode(&last_month_);
    now_.LocalExplode(&current_);
    (now_ + base::TimeDelta::FromDays(31)).LocalExplode(&next_month_);
    (now_ + base::TimeDelta::FromDays(365)).LocalExplode(&next_year_);
  }

  base::Time now_;
  base::Time::Exploded last_year_;
  base::Time::Exploded last_month_;
  base::Time::Exploded current_;
  base::Time::Exploded next_month_;
  base::Time::Exploded next_year_;
};

TestingTimes testingTimes;

TEST_P(ShouldUpdateExpirationTest, ShouldUpdateExpiration) {
  auto test_case = GetParam();
  CreditCard card;
  card.SetExpirationMonth(test_case.month);
  card.SetExpirationYear(test_case.year);
  card.set_record_type(test_case.record_type);
  if (card.record_type() != CreditCard::LOCAL_CARD)
    card.SetServerStatus(test_case.server_status);

  EXPECT_EQ(test_case.should_update_expiration,
            card.ShouldUpdateExpiration(testingTimes.now_));
}

INSTANTIATE_TEST_CASE_P(
    CreditCardTest,
    ShouldUpdateExpirationTest,
    testing::Values(
        // Cards that expired last year should always be updated.
        ShouldUpdateExpirationTestCase{true, testingTimes.last_year_.month,
                                       testingTimes.last_year_.year,
                                       CreditCard::LOCAL_CARD},
        ShouldUpdateExpirationTestCase{
            true, testingTimes.last_year_.month, testingTimes.last_year_.year,
            CreditCard::FULL_SERVER_CARD, CreditCard::OK},
        ShouldUpdateExpirationTestCase{
            true, testingTimes.last_year_.month, testingTimes.last_year_.year,
            CreditCard::MASKED_SERVER_CARD, CreditCard::OK},
        ShouldUpdateExpirationTestCase{
            true, testingTimes.last_year_.month, testingTimes.last_year_.year,
            CreditCard::FULL_SERVER_CARD, CreditCard::EXPIRED},
        ShouldUpdateExpirationTestCase{
            true, testingTimes.last_year_.month, testingTimes.last_year_.year,
            CreditCard::MASKED_SERVER_CARD, CreditCard::EXPIRED},

        // Cards that expired last month should always be updated.
        ShouldUpdateExpirationTestCase{true, testingTimes.last_month_.month,
                                       testingTimes.last_month_.year,
                                       CreditCard::LOCAL_CARD},
        ShouldUpdateExpirationTestCase{
            true, testingTimes.last_month_.month, testingTimes.last_month_.year,
            CreditCard::FULL_SERVER_CARD, CreditCard::OK},
        ShouldUpdateExpirationTestCase{
            true, testingTimes.last_month_.month, testingTimes.last_month_.year,
            CreditCard::MASKED_SERVER_CARD, CreditCard::OK},
        ShouldUpdateExpirationTestCase{
            true, testingTimes.last_month_.month, testingTimes.last_month_.year,
            CreditCard::FULL_SERVER_CARD, CreditCard::EXPIRED},
        ShouldUpdateExpirationTestCase{
            true, testingTimes.last_month_.month, testingTimes.last_month_.year,
            CreditCard::MASKED_SERVER_CARD, CreditCard::EXPIRED},

        // Cards that expire this month should be updated only if the server
        // status is EXPIRED.
        ShouldUpdateExpirationTestCase{false, testingTimes.current_.month,
                                       testingTimes.current_.year,
                                       CreditCard::LOCAL_CARD},
        ShouldUpdateExpirationTestCase{
            false, testingTimes.current_.month, testingTimes.current_.year,
            CreditCard::FULL_SERVER_CARD, CreditCard::OK},
        ShouldUpdateExpirationTestCase{
            false, testingTimes.current_.month, testingTimes.current_.year,
            CreditCard::MASKED_SERVER_CARD, CreditCard::OK},
        ShouldUpdateExpirationTestCase{
            true, testingTimes.current_.month, testingTimes.current_.year,
            CreditCard::FULL_SERVER_CARD, CreditCard::EXPIRED},
        ShouldUpdateExpirationTestCase{
            true, testingTimes.current_.month, testingTimes.current_.year,
            CreditCard::MASKED_SERVER_CARD, CreditCard::EXPIRED},

        // Cards that expire next month should be updated only if the server
        // status is EXPIRED.
        ShouldUpdateExpirationTestCase{false, testingTimes.next_month_.month,
                                       testingTimes.next_month_.year,
                                       CreditCard::LOCAL_CARD},
        ShouldUpdateExpirationTestCase{false, testingTimes.next_month_.month,
                                       testingTimes.next_month_.year,
                                       CreditCard::MASKED_SERVER_CARD,
                                       CreditCard::OK},
        ShouldUpdateExpirationTestCase{false, testingTimes.next_month_.month,
                                       testingTimes.next_month_.year,
                                       CreditCard::FULL_SERVER_CARD,
                                       CreditCard::OK},
        ShouldUpdateExpirationTestCase{
            true, testingTimes.next_month_.month, testingTimes.next_month_.year,
            CreditCard::MASKED_SERVER_CARD, CreditCard::EXPIRED},
        ShouldUpdateExpirationTestCase{
            true, testingTimes.next_month_.month, testingTimes.next_month_.year,
            CreditCard::FULL_SERVER_CARD, CreditCard::EXPIRED},

        // Cards that expire next year should be updated only if the server
        // status is EXPIRED.
        ShouldUpdateExpirationTestCase{false, testingTimes.next_year_.month,
                                       testingTimes.next_year_.year,
                                       CreditCard::LOCAL_CARD},
        ShouldUpdateExpirationTestCase{
            false, testingTimes.next_year_.month, testingTimes.next_year_.year,
            CreditCard::MASKED_SERVER_CARD, CreditCard::OK},
        ShouldUpdateExpirationTestCase{
            false, testingTimes.next_year_.month, testingTimes.next_year_.year,
            CreditCard::FULL_SERVER_CARD, CreditCard::OK},
        ShouldUpdateExpirationTestCase{
            true, testingTimes.next_year_.month, testingTimes.next_year_.year,
            CreditCard::MASKED_SERVER_CARD, CreditCard::EXPIRED},
        ShouldUpdateExpirationTestCase{
            true, testingTimes.next_year_.month, testingTimes.next_year_.year,
            CreditCard::FULL_SERVER_CARD, CreditCard::EXPIRED}));

// TODO(wuandy): rewriting below test with INSTANTIATE_TEST_CASE_P seems to
// trigger a complaint on windows compilers. Removing it and revert to
// original test for now.

// Test that credit card last used date suggestion can be generated correctly
// in different variations.
TEST(CreditCardTest, GetLastUsedDateForDisplay) {
  const base::Time::Exploded kTestDateTimeExploded = {
      2016, 12, 6, 10,  // Sat, Dec 10, 2016
      15,   42, 7, 0    // 15:42:07.000
  };
  base::Time kArbitraryTime;
  EXPECT_TRUE(
      base::Time::FromLocalExploded(kTestDateTimeExploded, &kArbitraryTime));

  // Test for added to chrome/chromium.
  CreditCard credit_card0(base::GenerateGUID(), "https://www.example.com");
  credit_card0.set_use_count(1);
  credit_card0.set_use_date(kArbitraryTime - base::TimeDelta::FromDays(1));
  test::SetCreditCardInfo(&credit_card0, "John Dillinger",
                          "423456789012" /* Visa */, "01", "2021");

  // Test for last used date.
  CreditCard credit_card1(base::GenerateGUID(), "https://www.example.com");
  test::SetCreditCardInfo(&credit_card1, "Clyde Barrow",
                          "347666888555" /* American Express */, "04", "2021");
  credit_card1.set_use_count(10);
  credit_card1.set_use_date(kArbitraryTime - base::TimeDelta::FromDays(10));

  // Test for last used more than one year ago.
  CreditCard credit_card2(base::GenerateGUID(), "https://www.example.com");
  credit_card2.set_use_count(5);
  credit_card2.set_use_date(kArbitraryTime - base::TimeDelta::FromDays(366));
  test::SetCreditCardInfo(&credit_card2, "Bonnie Parker",
                          "518765432109" /* Mastercard */, "12", "2021");

  static const struct {
    const char* show_expiration_date;
    const std::string& app_locale;
    base::string16 added_to_autofill_date;
    base::string16 last_used_date;
    base::string16 last_used_year_ago;
  } kTestCases[] = {
      // only show last used date.
      {"false", "en_US", ASCIIToUTF16("Added Dec 09"),
       ASCIIToUTF16("Last used Nov 30"),
       ASCIIToUTF16("Last used over a year ago")},
      // show expiration date and last used date.
      {"true", "en_US", ASCIIToUTF16("Exp: 01/21, added Dec 09"),
       ASCIIToUTF16("Exp: 04/21, last used Nov 30"),
       ASCIIToUTF16("Exp: 12/21, last used over a year ago")},
  };

  variations::testing::VariationParamsManager variation_params_;

  for (const auto& test_case : kTestCases) {
    variation_params_.SetVariationParamsWithFeatureAssociations(
        kAutofillCreditCardLastUsedDateDisplay.name,
        {{kAutofillCreditCardLastUsedDateShowExpirationDateKey,
          test_case.show_expiration_date}},
        {kAutofillCreditCardLastUsedDateDisplay.name});

    EXPECT_EQ(test_case.added_to_autofill_date,
              credit_card0.GetLastUsedDateForDisplay(test_case.app_locale));
    EXPECT_EQ(test_case.last_used_date,
              credit_card1.GetLastUsedDateForDisplay(test_case.app_locale));
    EXPECT_EQ(test_case.last_used_year_ago,
              credit_card2.GetLastUsedDateForDisplay(test_case.app_locale));
    variation_params_.ClearAllVariationParams();
  }
}

}  // namespace autofill
