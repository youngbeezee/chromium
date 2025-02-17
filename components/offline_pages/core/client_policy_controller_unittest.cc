// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/offline_pages/core/client_policy_controller.h"

#include <algorithm>

#include "base/bind.h"
#include "base/time/time.h"
#include "components/offline_pages/core/client_namespace_constants.h"
#include "testing/gtest/include/gtest/gtest.h"

using LifetimeType = offline_pages::LifetimePolicy::LifetimeType;

namespace offline_pages {

namespace {
const char kUndefinedNamespace[] = "undefined";

bool isTemporary(const OfflinePageClientPolicy& policy) {
  return policy.lifetime_policy.lifetime_type == LifetimeType::TEMPORARY;
}
}  // namespace

class ClientPolicyControllerTest : public testing::Test {
 public:
  ClientPolicyController* controller() { return controller_.get(); }

  // testing::Test
  void SetUp() override;
  void TearDown() override;

 protected:
  void ExpectDownloadSupport(std::string name_space, bool expectation);
  void ExpectRecentTab(std::string name_space, bool expectation);
  void ExpectOnlyOriginalTab(std::string name_space, bool expectation);
  void ExpectDisabledWhenPrefetchDisabled(std::string name_space,
                                          bool expectation);

 private:
  std::unique_ptr<ClientPolicyController> controller_;
};

void ClientPolicyControllerTest::SetUp() {
  controller_.reset(new ClientPolicyController());
}

void ClientPolicyControllerTest::TearDown() {
  controller_.reset();
}

void ClientPolicyControllerTest::ExpectDownloadSupport(std::string name_space,
                                                       bool expectation) {
  std::vector<std::string> cache =
      controller()->GetNamespacesSupportedByDownload();
  auto result = std::find(cache.begin(), cache.end(), name_space);
  EXPECT_EQ(expectation, result != cache.end())
      << "Namespace " << name_space
      << " had incorrect download support when getting namespaces supported by "
         "download.";
  EXPECT_EQ(expectation, controller()->IsSupportedByDownload(name_space))
      << "Namespace " << name_space
      << " had incorrect download support when directly checking if supported "
         "by download.";
}

void ClientPolicyControllerTest::ExpectRecentTab(std::string name_space,
                                                 bool expectation) {
  std::vector<std::string> cache =
      controller()->GetNamespacesShownAsRecentlyVisitedSite();
  auto result = std::find(cache.begin(), cache.end(), name_space);
  EXPECT_EQ(expectation, result != cache.end())
      << "Namespace " << name_space
      << " had incorrect recent tab support when getting namespaces shown as a"
         " recently visited site.";
  EXPECT_EQ(expectation, controller()->IsShownAsRecentlyVisitedSite(name_space))
      << "Namespace " << name_space
      << " had incorrect recent tab support when directly checking if shown as"
         " a recently visited site.";
}

void ClientPolicyControllerTest::ExpectOnlyOriginalTab(std::string name_space,
                                                       bool expectation) {
  std::vector<std::string> cache =
      controller()->GetNamespacesRestrictedToOriginalTab();
  auto result = std::find(cache.begin(), cache.end(), name_space);
  EXPECT_EQ(expectation, result != cache.end())
      << "Namespace " << name_space
      << " had incorrect restriction when getting namespaces restricted to"
         " the original tab";
  EXPECT_EQ(expectation, controller()->IsRestrictedToOriginalTab(name_space))
      << "Namespace " << name_space
      << " had incorrect restriction when directly checking if the namespace"
         " is restricted to the original tab";
}

void ClientPolicyControllerTest::ExpectDisabledWhenPrefetchDisabled(
    std::string name_space,
    bool expectation) {
  std::vector<std::string> cache =
      controller()->GetNamespacesDisabledWhenPrefetchDisabled();
  auto result = std::find(cache.begin(), cache.end(), name_space);
  EXPECT_EQ(expectation, result != cache.end())
      << "Namespace " << name_space
      << " had incorrect prefetch pref support when getting namespaces"
         " disabled when prefetch settings are disabled.";
  EXPECT_EQ(expectation,
            controller()->IsDisabledWhenPrefetchDisabled(name_space))
      << "Namespace " << name_space
      << " had incorrect download support when directly checking if disabled"
         " when prefetch settings are disabled.";
}

TEST_F(ClientPolicyControllerTest, FallbackTest) {
  OfflinePageClientPolicy policy = controller()->GetPolicy(kUndefinedNamespace);
  EXPECT_EQ(policy.name_space, kDefaultNamespace);
  EXPECT_TRUE(isTemporary(policy));
  EXPECT_TRUE(controller()->IsRemovedOnCacheReset(kUndefinedNamespace));
  ExpectDownloadSupport(kUndefinedNamespace, false);
  ExpectRecentTab(kUndefinedNamespace, false);
  ExpectOnlyOriginalTab(kUndefinedNamespace, false);
  ExpectDisabledWhenPrefetchDisabled(kUndefinedNamespace, false);
}

TEST_F(ClientPolicyControllerTest, CheckBookmarkDefined) {
  OfflinePageClientPolicy policy = controller()->GetPolicy(kBookmarkNamespace);
  EXPECT_EQ(policy.name_space, kBookmarkNamespace);
  EXPECT_TRUE(isTemporary(policy));
  EXPECT_TRUE(controller()->IsRemovedOnCacheReset(kBookmarkNamespace));
  ExpectDownloadSupport(kBookmarkNamespace, false);
  ExpectRecentTab(kBookmarkNamespace, false);
  ExpectOnlyOriginalTab(kBookmarkNamespace, false);
  ExpectDisabledWhenPrefetchDisabled(kBookmarkNamespace, false);
}

TEST_F(ClientPolicyControllerTest, CheckLastNDefined) {
  OfflinePageClientPolicy policy = controller()->GetPolicy(kLastNNamespace);
  EXPECT_EQ(policy.name_space, kLastNNamespace);
  EXPECT_TRUE(isTemporary(policy));
  EXPECT_TRUE(controller()->IsRemovedOnCacheReset(kLastNNamespace));
  ExpectDownloadSupport(kLastNNamespace, false);
  ExpectRecentTab(kLastNNamespace, true);
  ExpectOnlyOriginalTab(kLastNNamespace, true);
  ExpectDisabledWhenPrefetchDisabled(kLastNNamespace, false);
}

TEST_F(ClientPolicyControllerTest, CheckAsyncDefined) {
  OfflinePageClientPolicy policy = controller()->GetPolicy(kAsyncNamespace);
  EXPECT_EQ(policy.name_space, kAsyncNamespace);
  EXPECT_FALSE(isTemporary(policy));
  EXPECT_FALSE(controller()->IsRemovedOnCacheReset(kAsyncNamespace));
  ExpectDownloadSupport(kAsyncNamespace, true);
  ExpectRecentTab(kAsyncNamespace, false);
  ExpectOnlyOriginalTab(kAsyncNamespace, false);
  ExpectDisabledWhenPrefetchDisabled(kAsyncNamespace, false);
}

TEST_F(ClientPolicyControllerTest, CheckCCTDefined) {
  OfflinePageClientPolicy policy = controller()->GetPolicy(kCCTNamespace);
  EXPECT_EQ(policy.name_space, kCCTNamespace);
  EXPECT_TRUE(isTemporary(policy));
  EXPECT_TRUE(controller()->IsRemovedOnCacheReset(kCCTNamespace));
  ExpectDownloadSupport(kCCTNamespace, false);
  ExpectRecentTab(kCCTNamespace, false);
  ExpectOnlyOriginalTab(kCCTNamespace, false);
  ExpectDisabledWhenPrefetchDisabled(kCCTNamespace, true);
}

TEST_F(ClientPolicyControllerTest, CheckDownloadDefined) {
  OfflinePageClientPolicy policy = controller()->GetPolicy(kDownloadNamespace);
  EXPECT_EQ(policy.name_space, kDownloadNamespace);
  EXPECT_FALSE(isTemporary(policy));
  EXPECT_FALSE(controller()->IsRemovedOnCacheReset(kDownloadNamespace));
  ExpectDownloadSupport(kDownloadNamespace, true);
  ExpectRecentTab(kDownloadNamespace, false);
  ExpectOnlyOriginalTab(kDownloadNamespace, false);
  ExpectDisabledWhenPrefetchDisabled(kDownloadNamespace, false);
}

TEST_F(ClientPolicyControllerTest, CheckNTPSuggestionsDefined) {
  OfflinePageClientPolicy policy =
      controller()->GetPolicy(kNTPSuggestionsNamespace);
  EXPECT_EQ(policy.name_space, kNTPSuggestionsNamespace);
  EXPECT_FALSE(isTemporary(policy));
  EXPECT_FALSE(controller()->IsRemovedOnCacheReset(kNTPSuggestionsNamespace));
  ExpectDownloadSupport(kNTPSuggestionsNamespace, true);
  ExpectRecentTab(kNTPSuggestionsNamespace, false);
  ExpectOnlyOriginalTab(kNTPSuggestionsNamespace, false);
  ExpectDisabledWhenPrefetchDisabled(kNTPSuggestionsNamespace, false);
}

TEST_F(ClientPolicyControllerTest, CheckSuggestedArticlesDefined) {
  OfflinePageClientPolicy policy =
      controller()->GetPolicy(kSuggestedArticlesNamespace);
  EXPECT_EQ(policy.name_space, kSuggestedArticlesNamespace);
  EXPECT_TRUE(isTemporary(policy));
  EXPECT_TRUE(controller()->IsRemovedOnCacheReset(kSuggestedArticlesNamespace));
  ExpectDownloadSupport(kSuggestedArticlesNamespace, false);
  ExpectRecentTab(kSuggestedArticlesNamespace, false);
  ExpectOnlyOriginalTab(kSuggestedArticlesNamespace, false);
  ExpectDisabledWhenPrefetchDisabled(kSuggestedArticlesNamespace, true);
}

}  // namespace offline_pages
