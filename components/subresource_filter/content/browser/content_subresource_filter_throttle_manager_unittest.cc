// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/subresource_filter/content/browser/content_subresource_filter_throttle_manager.h"

#include <memory>
#include <utility>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/test/test_simple_task_runner.h"
#include "components/subresource_filter/content/browser/async_document_subresource_filter.h"
#include "components/subresource_filter/content/common/subresource_filter_messages.h"
#include "components/subresource_filter/core/common/activation_level.h"
#include "components/subresource_filter/core/common/activation_state.h"
#include "components/subresource_filter/core/common/proto/rules.pb.h"
#include "components/subresource_filter/core/common/test_ruleset_creator.h"
#include "components/subresource_filter/core/common/test_ruleset_utils.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/mock_render_process_host.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_renderer_host.h"
#include "net/base/net_errors.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/url_constants.h"

namespace subresource_filter {

const char kTestURLWithActivation[] = "https://www.page-with-activation.com/";
const char kTestURLWithActivation2[] =
    "https://www.page-with-activation-2.com/";
const char kTestURLWithDryRun[] = "https://www.page-with-dryrun.com/";

// Enum determining when the mock page state throttle notifies the throttle
// manager of page level activation state.
enum PageActivationNotificationTiming {
  WILL_START_REQUEST,
  WILL_PROCESS_RESPONSE,
};

// Simple throttle that sends page-level activation to the manager for a
// specific set of URLs.
class MockPageStateActivationThrottle : public content::NavigationThrottle {
 public:
  MockPageStateActivationThrottle(
      content::NavigationHandle* navigation_handle,
      PageActivationNotificationTiming activation_throttle_state,
      ContentSubresourceFilterThrottleManager* throttle_manager)
      : content::NavigationThrottle(navigation_handle),
        activation_throttle_state_(activation_throttle_state),
        throttle_manager_(throttle_manager) {
    // Add some default activations.
    mock_page_activations_[GURL(kTestURLWithActivation)] =
        ActivationState(ActivationLevel::ENABLED);
    mock_page_activations_[GURL(kTestURLWithActivation2)] =
        ActivationState(ActivationLevel::ENABLED);
    mock_page_activations_[GURL(kTestURLWithDryRun)] =
        ActivationState(ActivationLevel::DRYRUN);
  }
  ~MockPageStateActivationThrottle() override {}

  // content::NavigationThrottle:
  content::NavigationThrottle::ThrottleCheckResult WillStartRequest() override {
    return MaybeNotifyActivation(WILL_START_REQUEST);
  }

  content::NavigationThrottle::ThrottleCheckResult WillProcessResponse()
      override {
    return MaybeNotifyActivation(WILL_PROCESS_RESPONSE);
  }
  const char* GetNameForLogging() override {
    return "MockPageStateActivationThrottle";
  }

 private:
  content::NavigationThrottle::ThrottleCheckResult MaybeNotifyActivation(
      PageActivationNotificationTiming throttle_state) {
    if (throttle_state == activation_throttle_state_) {
      auto it = mock_page_activations_.find(navigation_handle()->GetURL());
      if (it != mock_page_activations_.end()) {
        throttle_manager_->NotifyPageActivationComputed(navigation_handle(),
                                                        it->second);
      }
    }
    return content::NavigationThrottle::PROCEED;
  }

  std::map<GURL, ActivationState> mock_page_activations_;
  PageActivationNotificationTiming activation_throttle_state_;
  ContentSubresourceFilterThrottleManager* throttle_manager_;

  DISALLOW_COPY_AND_ASSIGN(MockPageStateActivationThrottle);
};

class ContentSubresourceFilterThrottleManagerTest
    : public content::RenderViewHostTestHarness,
      public content::WebContentsObserver,
      public ContentSubresourceFilterThrottleManager::Delegate,
      public ::testing::WithParamInterface<PageActivationNotificationTiming> {
 public:
  ContentSubresourceFilterThrottleManagerTest()
      : ContentSubresourceFilterThrottleManager::Delegate() {}
  ~ContentSubresourceFilterThrottleManagerTest() override {}

  // content::RenderViewHostTestHarness:
  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();

    NavigateAndCommit(GURL("https://example.first"));

    Observe(RenderViewHostTestHarness::web_contents());

    // Initialize the ruleset dealer.
    std::vector<proto::UrlRule> rules;
    rules.push_back(testing::CreateWhitelistRuleForDocument(
        "whitelist.com", proto::ACTIVATION_TYPE_DOCUMENT,
        {"page-with-activation.com"}));
    rules.push_back(testing::CreateSuffixRule("disallowed.html"));
    ASSERT_NO_FATAL_FAILURE(test_ruleset_creator_.CreateRulesetWithRules(
        rules, &test_ruleset_pair_));

    // Make the blocking task runner run on the current task runner for the
    // tests, to ensure that the NavigationSimulator properly runs all necessary
    // tasks while waiting for throttle checks to finish.
    dealer_handle_ = base::MakeUnique<VerifiedRulesetDealer::Handle>(
        base::MessageLoop::current()->task_runner());
    dealer_handle_->SetRulesetFile(
        testing::TestRuleset::Open(test_ruleset_pair_.indexed));

    throttle_manager_ =
        base::MakeUnique<ContentSubresourceFilterThrottleManager>(
            this, dealer_handle_.get(),
            RenderViewHostTestHarness::web_contents());
  }

  void TearDown() override {
    throttle_manager_.reset();
    dealer_handle_.reset();
    base::RunLoop().RunUntilIdle();
    content::RenderViewHostTestHarness::TearDown();
  }

  void ExpectActivationSignalForFrame(content::RenderFrameHost* rfh,
                                      bool expect_activation) {
    content::MockRenderProcessHost* render_process_host =
        static_cast<content::MockRenderProcessHost*>(rfh->GetProcess());
    const IPC::Message* message =
        render_process_host->sink().GetFirstMessageMatching(
            SubresourceFilterMsg_ActivateForNextCommittedLoad::ID);
    ASSERT_EQ(expect_activation, !!message);
    if (expect_activation) {
      std::tuple<ActivationState> args;
      SubresourceFilterMsg_ActivateForNextCommittedLoad::Read(message, &args);
      ActivationLevel level = std::get<0>(args).activation_level;
      EXPECT_NE(ActivationLevel::DISABLED, level);
    }
    render_process_host->sink().ClearMessages();
  }

  // Helper methods:

  void CreateTestNavigation(const GURL& url,
                            content::RenderFrameHost* render_frame_host) {
    DCHECK(!navigation_simulator_);
    DCHECK(render_frame_host);
    navigation_simulator_ =
        content::NavigationSimulator::CreateRendererInitiated(
            url, render_frame_host);
  }

  void CreateSubframeWithTestNavigation(const GURL& url,
                                        content::RenderFrameHost* parent) {
    content::RenderFrameHost* subframe =
        content::RenderFrameHostTester::For(parent)->AppendChild(
            base::StringPrintf("subframe-%s", url.spec().c_str()));
    CreateTestNavigation(url, subframe);
  }

  void SimulateStartAndExpectResult(
      content::NavigationThrottle::ThrottleCheckResult expect_result) {
    navigation_simulator_->Start();
    content::NavigationThrottle::ThrottleCheckResult result =
        navigation_simulator_->GetLastThrottleCheckResult();
    EXPECT_EQ(expect_result, result);
    if (result != content::NavigationThrottle::PROCEED)
      navigation_simulator_.reset();
  }

  void SimulateRedirectAndExpectResult(
      const GURL& new_url,
      content::NavigationThrottle::ThrottleCheckResult expect_result) {
    navigation_simulator_->Redirect(new_url);
    content::NavigationThrottle::ThrottleCheckResult result =
        navigation_simulator_->GetLastThrottleCheckResult();
    EXPECT_EQ(expect_result, result);
    if (result != content::NavigationThrottle::PROCEED)
      navigation_simulator_.reset();
  }

  // Returns the RenderFrameHost that the navigation commit in.
  content::RenderFrameHost* SimulateCommitAndExpectResult(
      content::NavigationThrottle::ThrottleCheckResult expect_result) {
    navigation_simulator_->Commit();
    content::NavigationThrottle::ThrottleCheckResult result =
        navigation_simulator_->GetLastThrottleCheckResult();
    EXPECT_EQ(expect_result, result);

    auto scoped_simulator = std::move(navigation_simulator_);
    if (result == content::NavigationThrottle::PROCEED)
      return scoped_simulator->GetFinalRenderFrameHost();
    return nullptr;
  }

  void SimulateSameDocumentCommit() {
    navigation_simulator_->CommitSameDocument();
    navigation_simulator_.reset();
  }

  void SimulateFailedNavigation(net::Error error) {
    navigation_simulator_->Fail(error);
    if (error != net::ERR_ABORTED) {
      navigation_simulator_->CommitErrorPage();
    }
    navigation_simulator_.reset();
  }

  void NavigateAndCommitMainFrame(const GURL& url) {
    CreateTestNavigation(url, main_rfh());
    SimulateStartAndExpectResult(content::NavigationThrottle::PROCEED);
    SimulateCommitAndExpectResult(content::NavigationThrottle::PROCEED);
  }

  void SuppressActivationForUrl(const GURL& url) {
    urls_to_suppress_activation_.insert(url);
  }

  bool ManagerHasRulesetHandle() {
    return throttle_manager_->ruleset_handle_for_testing();
  }

  int disallowed_notification_count() { return disallowed_notification_count_; }

  int attempted_frame_activations() { return attempted_frame_activations_; }

 protected:
  // content::WebContentsObserver
  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override {
    if (navigation_handle->IsSameDocument())
      return;

    // Inject the proper throttles at this time.
    std::vector<std::unique_ptr<content::NavigationThrottle>> throttles;
    PageActivationNotificationTiming state =
        ::testing::UnitTest::GetInstance()->current_test_info()->value_param()
            ? GetParam()
            : WILL_PROCESS_RESPONSE;
    throttles.push_back(base::MakeUnique<MockPageStateActivationThrottle>(
        navigation_handle, state, throttle_manager_.get()));
    throttle_manager_->MaybeAppendNavigationThrottles(navigation_handle,
                                                      &throttles);
    for (auto& it : throttles) {
      navigation_handle->RegisterThrottleForTesting(std::move(it));
    }
  }

  // ContentSubresourceFilterThrottleManager::Delegate:
  void OnFirstSubresourceLoadDisallowed() override {
    ++disallowed_notification_count_;
  }

  bool ShouldSuppressActivation(
      content::NavigationHandle* navigation_handle) override {
    ++attempted_frame_activations_;
    return urls_to_suppress_activation_.find(navigation_handle->GetURL()) !=
           urls_to_suppress_activation_.end();
  }

 private:
  testing::TestRulesetCreator test_ruleset_creator_;
  testing::TestRulesetPair test_ruleset_pair_;

  std::set<GURL> urls_to_suppress_activation_;

  std::unique_ptr<VerifiedRulesetDealer::Handle> dealer_handle_;

  std::unique_ptr<ContentSubresourceFilterThrottleManager> throttle_manager_;

  std::unique_ptr<content::NavigationSimulator> navigation_simulator_;

  // Incremented on every OnFirstSubresourceLoadDisallowed call.
  int disallowed_notification_count_ = 0;

  // Incremented  every time the manager queries the harness for activation
  // suppression.
  int attempted_frame_activations_ = 0;

  DISALLOW_COPY_AND_ASSIGN(ContentSubresourceFilterThrottleManagerTest);
};

INSTANTIATE_TEST_CASE_P(PageActivationNotificationTiming,
                        ContentSubresourceFilterThrottleManagerTest,
                        ::testing::Values(WILL_START_REQUEST,
                                          WILL_PROCESS_RESPONSE));

TEST_P(ContentSubresourceFilterThrottleManagerTest,
       ActivateMainFrameAndFilterSubframeNavigation) {
  // Commit a navigation that triggers page level activation.
  NavigateAndCommitMainFrame(GURL(kTestURLWithActivation));
  ExpectActivationSignalForFrame(main_rfh(), true /* expect_activation */);

  // A disallowed subframe navigation should be successfully filtered.
  CreateSubframeWithTestNavigation(
      GURL("https://www.example.com/disallowed.html"), main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::CANCEL);

  EXPECT_EQ(1, disallowed_notification_count());
  EXPECT_EQ(1, attempted_frame_activations());
}

TEST_P(ContentSubresourceFilterThrottleManagerTest,
       ActivateMainFrameAndDoNotFilterDryRun) {
  // Commit a navigation that triggers page level activation.
  NavigateAndCommitMainFrame(GURL(kTestURLWithDryRun));
  ExpectActivationSignalForFrame(main_rfh(), true /* expect_activation */);

  // A disallowed subframe navigation should not be filtered in dry-run mode.
  CreateSubframeWithTestNavigation(
      GURL("https://www.example.com/disallowed.html"), main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::PROCEED);
  content::RenderFrameHost* child =
      SimulateCommitAndExpectResult(content::NavigationThrottle::PROCEED);
  // But it should still be activated.
  ExpectActivationSignalForFrame(child, true /* expect_activation */);

  EXPECT_EQ(0, disallowed_notification_count());
  EXPECT_EQ(2, attempted_frame_activations());
}

TEST_P(ContentSubresourceFilterThrottleManagerTest,
       ActivateMainFrameAndFilterSubframeNavigationOnRedirect) {
  // Commit a navigation that triggers page level activation.
  NavigateAndCommitMainFrame(GURL(kTestURLWithActivation));
  ExpectActivationSignalForFrame(main_rfh(), true /* expect_activation */);

  // A disallowed subframe navigation via redirect should be successfully
  // filtered.
  CreateSubframeWithTestNavigation(
      GURL("https://www.example.com/before-redirect.html"), main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::PROCEED);
  SimulateRedirectAndExpectResult(
      GURL("https://www.example.com/disallowed.html"),
      content::NavigationThrottle::CANCEL);

  EXPECT_EQ(1, disallowed_notification_count());
  EXPECT_EQ(1, attempted_frame_activations());
}

TEST_P(ContentSubresourceFilterThrottleManagerTest,
       ActivateMainFrameAndDoNotFilterSubframeNavigation) {
  // Commit a navigation that triggers page level activation.
  NavigateAndCommitMainFrame(GURL(kTestURLWithActivation));
  ExpectActivationSignalForFrame(main_rfh(), true /* expect_activation */);

  // An allowed subframe navigation should complete successfully.
  CreateSubframeWithTestNavigation(
      GURL("https://www.example.com/allowed1.html"), main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::PROCEED);
  SimulateRedirectAndExpectResult(GURL("https://www.example.com/allowed2.html"),
                                  content::NavigationThrottle::PROCEED);
  content::RenderFrameHost* child =
      SimulateCommitAndExpectResult(content::NavigationThrottle::PROCEED);
  ExpectActivationSignalForFrame(child, true /* expect_activation */);

  EXPECT_EQ(0, disallowed_notification_count());
  EXPECT_EQ(2, attempted_frame_activations());
}

// This should fail if the throttle manager notifies the delegate twice of a
// disallowed load for the same page load.
TEST_P(ContentSubresourceFilterThrottleManagerTest,
       ActivateMainFrameAndFilterTwoSubframeNavigations) {
  // Commit a navigation that triggers page level activation.
  NavigateAndCommitMainFrame(GURL(kTestURLWithActivation));
  ExpectActivationSignalForFrame(main_rfh(), true /* expect_activation */);

  // A disallowed subframe navigation should be successfully filtered.
  CreateSubframeWithTestNavigation(
      GURL("https://www.example.com/1/disallowed.html"), main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::CANCEL);

  EXPECT_EQ(1, disallowed_notification_count());

  CreateSubframeWithTestNavigation(
      GURL("https://www.example.com/2/disallowed.html"), main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::CANCEL);

  EXPECT_EQ(1, disallowed_notification_count());
  EXPECT_EQ(1, attempted_frame_activations());
}

TEST_P(ContentSubresourceFilterThrottleManagerTest,
       ActivateTwoMainFramesAndFilterTwoSubframeNavigations) {
  // Commit a navigation that triggers page level activation.
  NavigateAndCommitMainFrame(GURL(kTestURLWithActivation));
  ExpectActivationSignalForFrame(main_rfh(), true /* expect_activation */);

  // A disallowed subframe navigation should be successfully filtered.
  CreateSubframeWithTestNavigation(
      GURL("https://www.example.com/1/disallowed.html"), main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::CANCEL);

  EXPECT_EQ(1, disallowed_notification_count());

  // Commit another navigation that triggers page level activation.
  NavigateAndCommitMainFrame(GURL(kTestURLWithActivation2));
  ExpectActivationSignalForFrame(main_rfh(), true /* expect_activation */);

  CreateSubframeWithTestNavigation(
      GURL("https://www.example.com/2/disallowed.html"), main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::CANCEL);

  EXPECT_EQ(2, disallowed_notification_count());
  EXPECT_EQ(2, attempted_frame_activations());
}

// Test that the disallow load notification will not be repeated for the first
// disallowed load that follows a same-document navigation.
TEST_P(ContentSubresourceFilterThrottleManagerTest,
       ActivateMainFrameDoNotNotifyAfterSameDocumentNav) {
  // Commit a navigation that triggers page level activation.
  NavigateAndCommitMainFrame(GURL(kTestURLWithActivation));
  ExpectActivationSignalForFrame(main_rfh(), true /* expect_activation */);

  // A disallowed subframe navigation should be successfully filtered.
  CreateSubframeWithTestNavigation(
      GURL("https://www.example.com/1/disallowed.html"), main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::CANCEL);

  EXPECT_EQ(1, disallowed_notification_count());

  // Commit another navigation that triggers page level activation.
  GURL url2 = GURL(base::StringPrintf("%s#ref", kTestURLWithActivation));
  CreateTestNavigation(url2, main_rfh());
  SimulateSameDocumentCommit();
  ExpectActivationSignalForFrame(main_rfh(), false /* expect_activation */);

  CreateSubframeWithTestNavigation(
      GURL("https://www.example.com/2/disallowed.html"), main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::CANCEL);

  EXPECT_EQ(1, disallowed_notification_count());
  EXPECT_EQ(1, attempted_frame_activations());
}

TEST_P(ContentSubresourceFilterThrottleManagerTest,
       DoNotFilterForInactiveFrame) {
  NavigateAndCommitMainFrame(GURL("https://do-not-activate.html"));
  ExpectActivationSignalForFrame(main_rfh(), false /* expect_activation */);

  // A subframe navigation should complete successfully.
  CreateSubframeWithTestNavigation(GURL("https://www.example.com/allowed.html"),
                                   main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::PROCEED);
  content::RenderFrameHost* child =
      SimulateCommitAndExpectResult(content::NavigationThrottle::PROCEED);
  ExpectActivationSignalForFrame(child, false /* expect_activation */);

  EXPECT_EQ(0, disallowed_notification_count());
  EXPECT_EQ(0, attempted_frame_activations());
}

TEST_P(ContentSubresourceFilterThrottleManagerTest, SuppressActivation) {
  SuppressActivationForUrl(GURL(kTestURLWithActivation));
  NavigateAndCommitMainFrame(GURL(kTestURLWithActivation));
  ExpectActivationSignalForFrame(main_rfh(), false /* expect_activation */);

  // A subframe navigation should complete successfully.
  CreateSubframeWithTestNavigation(GURL("https://www.example.com/allowed.html"),
                                   main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::PROCEED);
  content::RenderFrameHost* child =
      SimulateCommitAndExpectResult(content::NavigationThrottle::PROCEED);
  ExpectActivationSignalForFrame(child, false /* expect_activation */);

  EXPECT_EQ(0, disallowed_notification_count());
  EXPECT_EQ(1, attempted_frame_activations());
}

// Once there are no activated frames, the manager drops its ruleset handle. If
// another frame is activated, make sure the handle is regenerated.
TEST_P(ContentSubresourceFilterThrottleManagerTest, RulesetHandleRegeneration) {
  NavigateAndCommitMainFrame(GURL(kTestURLWithActivation));
  ExpectActivationSignalForFrame(main_rfh(), true /* expect_activation */);

  CreateSubframeWithTestNavigation(
      GURL("https://www.example.com/disallowed.html"), main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::CANCEL);

  EXPECT_EQ(1, disallowed_notification_count());

  // Simulate a renderer crash which should delete the frame.
  EXPECT_TRUE(ManagerHasRulesetHandle());
  process()->SimulateCrash();
  EXPECT_FALSE(ManagerHasRulesetHandle());

  NavigateAndCommit(GURL("https://example.reset"));
  NavigateAndCommitMainFrame(GURL(kTestURLWithActivation));
  ExpectActivationSignalForFrame(main_rfh(), true /* expect_activation */);

  CreateSubframeWithTestNavigation(
      GURL("https://www.example.com/disallowed.html"), main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::CANCEL);

  EXPECT_EQ(2, disallowed_notification_count());
  EXPECT_EQ(2, attempted_frame_activations());
}

TEST_P(ContentSubresourceFilterThrottleManagerTest,
       SameSiteNavigation_RulesetGoesAway) {
  GURL same_site_inactive_url =
      GURL(base::StringPrintf("%ssuppressed.html", kTestURLWithActivation));
  SuppressActivationForUrl(same_site_inactive_url);

  NavigateAndCommitMainFrame(GURL(kTestURLWithActivation));
  ExpectActivationSignalForFrame(main_rfh(), true /* expect_activation */);
  EXPECT_TRUE(ManagerHasRulesetHandle());

  NavigateAndCommitMainFrame(same_site_inactive_url);
  ExpectActivationSignalForFrame(main_rfh(), false /* expect_activation */);
  EXPECT_FALSE(ManagerHasRulesetHandle());

  // A subframe navigation should complete successfully.
  CreateSubframeWithTestNavigation(
      GURL("https://www.example.com/disallowed.html"), main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::PROCEED);
  content::RenderFrameHost* child =
      SimulateCommitAndExpectResult(content::NavigationThrottle::PROCEED);
  ExpectActivationSignalForFrame(child, false /* expect_activation */);

  EXPECT_EQ(0, disallowed_notification_count());
  EXPECT_EQ(1, attempted_frame_activations());
}

TEST_P(ContentSubresourceFilterThrottleManagerTest,
       SameSiteFailedNavigation_MaintainActivation) {
  NavigateAndCommitMainFrame(GURL(kTestURLWithActivation));
  ExpectActivationSignalForFrame(main_rfh(), true /* expect_activation */);
  EXPECT_TRUE(ManagerHasRulesetHandle());

  GURL same_site_inactive_url =
      GURL(base::StringPrintf("%ssuppressed.html", kTestURLWithActivation));
  SuppressActivationForUrl(same_site_inactive_url);

  CreateTestNavigation(same_site_inactive_url, main_rfh());
  SimulateFailedNavigation(net::ERR_ABORTED);
  EXPECT_TRUE(ManagerHasRulesetHandle());
  ExpectActivationSignalForFrame(main_rfh(), false /* expect_activation */);

  // A subframe navigation fail.
  CreateSubframeWithTestNavigation(
      GURL("https://www.example.com/disallowed.html"), main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::CANCEL);

  EXPECT_EQ(1, disallowed_notification_count());
  EXPECT_EQ(1, attempted_frame_activations());
}

TEST_P(ContentSubresourceFilterThrottleManagerTest,
       FailedNavigationToErrorPage_NoActivation) {
  NavigateAndCommitMainFrame(GURL(kTestURLWithActivation));
  ExpectActivationSignalForFrame(main_rfh(), true /* expect_activation */);
  EXPECT_TRUE(ManagerHasRulesetHandle());

  GURL same_site_inactive_url =
      GURL(base::StringPrintf("%ssuppressed.html", kTestURLWithActivation));
  SuppressActivationForUrl(same_site_inactive_url);

  CreateTestNavigation(same_site_inactive_url, main_rfh());
  SimulateFailedNavigation(net::ERR_FAILED);
  EXPECT_FALSE(ManagerHasRulesetHandle());
  ExpectActivationSignalForFrame(main_rfh(), false /* expect_activation */);

  CreateSubframeWithTestNavigation(
      GURL("https://www.example.com/disallowed.html"), main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::PROCEED);
  content::RenderFrameHost* child =
      SimulateCommitAndExpectResult(content::NavigationThrottle::PROCEED);
  ExpectActivationSignalForFrame(child, false /* expect_activation */);

  EXPECT_EQ(0, disallowed_notification_count());
  EXPECT_EQ(1, attempted_frame_activations());
}

// Ensure activation propagates into great-grandchild frames, including cross
// process ones.
TEST_P(ContentSubresourceFilterThrottleManagerTest, ActivationPropagation) {
  NavigateAndCommitMainFrame(GURL(kTestURLWithActivation));
  ExpectActivationSignalForFrame(main_rfh(), true /* expect_activation */);

  // Navigate a subframe to a URL that is not itself disallowed. Subresource
  // filtering for this subframe document should still be activated.
  CreateSubframeWithTestNavigation(GURL("https://www.a.com/allowed.html"),
                                   main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::PROCEED);
  content::RenderFrameHost* subframe1 =
      SimulateCommitAndExpectResult(content::NavigationThrottle::PROCEED);
  ExpectActivationSignalForFrame(subframe1, true /* expect_activation */);

  // Navigate a sub-subframe to a URL that is not itself disallowed. Subresource
  // filtering for this subframe document should still be activated.
  CreateSubframeWithTestNavigation(GURL("https://www.b.com/allowed.html"),
                                   subframe1);
  SimulateStartAndExpectResult(content::NavigationThrottle::PROCEED);
  content::RenderFrameHost* subframe2 =
      SimulateCommitAndExpectResult(content::NavigationThrottle::PROCEED);
  ExpectActivationSignalForFrame(subframe2, true /* expect_activation */);

  // A final, nested subframe navigation is filtered.
  CreateSubframeWithTestNavigation(GURL("https://www.c.com/disallowed.html"),
                                   subframe2);
  SimulateStartAndExpectResult(content::NavigationThrottle::CANCEL);

  EXPECT_EQ(1, disallowed_notification_count());
  EXPECT_EQ(3, attempted_frame_activations());
}

// Ensure activation propagates through whitelisted documents.
TEST_P(ContentSubresourceFilterThrottleManagerTest, ActivationPropagation2) {
  NavigateAndCommitMainFrame(GURL(kTestURLWithActivation));
  ExpectActivationSignalForFrame(main_rfh(), true /* expect_activation */);

  // Navigate a subframe that is not filtered, but should still activate.
  CreateSubframeWithTestNavigation(GURL("https://whitelist.com"), main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::PROCEED);
  content::RenderFrameHost* subframe1 =
      SimulateCommitAndExpectResult(content::NavigationThrottle::PROCEED);
  ExpectActivationSignalForFrame(subframe1, true /* expect_activation */);

  // Navigate a sub-subframe that is not filtered due to the whitelist.
  CreateSubframeWithTestNavigation(
      GURL("https://www.example.com/disallowed.html"), subframe1);
  SimulateStartAndExpectResult(content::NavigationThrottle::PROCEED);
  content::RenderFrameHost* subframe2 =
      SimulateCommitAndExpectResult(content::NavigationThrottle::PROCEED);
  ExpectActivationSignalForFrame(subframe2, true /* expect_activation */);

  EXPECT_EQ(3, attempted_frame_activations());
  EXPECT_EQ(0, disallowed_notification_count());

  // An identical series of events that don't match whitelist rules cause
  // filtering.
  CreateSubframeWithTestNavigation(GURL("https://average-joe.com"), main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::PROCEED);
  content::RenderFrameHost* subframe3 =
      SimulateCommitAndExpectResult(content::NavigationThrottle::PROCEED);
  ExpectActivationSignalForFrame(subframe3, true /* expect_activation */);

  // Navigate a sub-subframe that is not filtered due to the whitelist.
  CreateSubframeWithTestNavigation(
      GURL("https://www.example.com/disallowed.html"), subframe3);
  SimulateStartAndExpectResult(content::NavigationThrottle::CANCEL);

  EXPECT_EQ(4, attempted_frame_activations());
  EXPECT_EQ(1, disallowed_notification_count());
}

// Same-site navigations within a single RFH do not persist activation.
TEST_P(ContentSubresourceFilterThrottleManagerTest,
       SameSiteNavigationStopsActivation) {
  NavigateAndCommitMainFrame(GURL(kTestURLWithActivation));
  ExpectActivationSignalForFrame(main_rfh(), true /* expect_activation */);
  EXPECT_EQ(1, attempted_frame_activations());

  // Mock a same-site navigation, in the same RFH, this URL does not trigger
  // page level activation.
  NavigateAndCommitMainFrame(
      GURL(base::StringPrintf("%s/some_path/", kTestURLWithActivation)));
  EXPECT_EQ(1, attempted_frame_activations());
  ExpectActivationSignalForFrame(main_rfh(), false /* expect_activation */);

  CreateSubframeWithTestNavigation(
      GURL("https://www.example.com/disallowed.html"), main_rfh());
  SimulateStartAndExpectResult(content::NavigationThrottle::PROCEED);
  content::RenderFrameHost* child =
      SimulateCommitAndExpectResult(content::NavigationThrottle::PROCEED);
  ExpectActivationSignalForFrame(child, false /* expect_activation */);

  EXPECT_EQ(0, disallowed_notification_count());
  EXPECT_EQ(1, attempted_frame_activations());
}

// TODO(csharrison): Make sure the following conditions are exercised in tests:
//
// - Synchronous navigations to about:blank. These hit issues with the
//   NavigationSimulator currently.

}  // namespace subresource_filter
