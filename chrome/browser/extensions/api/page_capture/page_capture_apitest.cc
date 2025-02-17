// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/extensions/api/page_capture/page_capture_api.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/common/chrome_switches.h"
#include "chromeos/login/login_state.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/test_utils.h"
#include "extensions/browser/extension_dialog_auto_confirm.h"
#include "net/dns/mock_host_resolver.h"

using extensions::PageCaptureSaveAsMHTMLFunction;
using extensions::ScopedTestDialogAutoConfirm;

class ExtensionPageCaptureApiTest : public ExtensionApiTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    ExtensionApiTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(switches::kJavaScriptFlags, "--expose-gc");
  }
};

class PageCaptureSaveAsMHTMLDelegate
    : public PageCaptureSaveAsMHTMLFunction::TestDelegate {
 public:
  PageCaptureSaveAsMHTMLDelegate() {
    PageCaptureSaveAsMHTMLFunction::SetTestDelegate(this);
  }

  virtual ~PageCaptureSaveAsMHTMLDelegate() {
    PageCaptureSaveAsMHTMLFunction::SetTestDelegate(NULL);
  }

  void OnTemporaryFileCreated(const base::FilePath& temp_file) override {
    temp_file_ = temp_file;
  }

  base::FilePath temp_file_;
};

IN_PROC_BROWSER_TEST_F(ExtensionPageCaptureApiTest, SaveAsMHTML) {
  host_resolver()->AddRule("www.a.com", "127.0.0.1");
  ASSERT_TRUE(StartEmbeddedTestServer());
  PageCaptureSaveAsMHTMLDelegate delegate;
  ASSERT_TRUE(RunExtensionTest("page_capture")) << message_;
  // Make sure the MHTML data gets written to the temporary file.
  ASSERT_FALSE(delegate.temp_file_.empty());
  // Flush the message loops to make sure the delete happens.
  content::RunAllPendingInMessageLoop(content::BrowserThread::FILE);
  content::RunAllPendingInMessageLoop(content::BrowserThread::IO);
  // Make sure the temporary file is destroyed once the javascript side reads
  // the contents.
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  ASSERT_FALSE(base::PathExists(delegate.temp_file_));
}

#if defined(OS_CHROMEOS)
IN_PROC_BROWSER_TEST_F(ExtensionPageCaptureApiTest,
                       PublicSessionRequestAllowed) {
  host_resolver()->AddRule("www.a.com", "127.0.0.1");
  ASSERT_TRUE(StartEmbeddedTestServer());
  PageCaptureSaveAsMHTMLDelegate delegate;
  // Set Public Session state.
  chromeos::LoginState::Get()->SetLoggedInState(
      chromeos::LoginState::LOGGED_IN_ACTIVE,
      chromeos::LoginState::LOGGED_IN_USER_PUBLIC_ACCOUNT);
  // Resolve Permission dialog with Allow.
  ScopedTestDialogAutoConfirm auto_confirm(ScopedTestDialogAutoConfirm::ACCEPT);
  ASSERT_TRUE(RunExtensionTest("page_capture")) << message_;
  ASSERT_FALSE(delegate.temp_file_.empty());
  content::RunAllPendingInMessageLoop(content::BrowserThread::FILE);
  content::RunAllPendingInMessageLoop(content::BrowserThread::IO);
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  ASSERT_FALSE(base::PathExists(delegate.temp_file_));
}

IN_PROC_BROWSER_TEST_F(ExtensionPageCaptureApiTest,
                       PublicSessionRequestDenied) {
  host_resolver()->AddRule("www.a.com", "127.0.0.1");
  ASSERT_TRUE(StartEmbeddedTestServer());
  // Set Public Session state.
  chromeos::LoginState::Get()->SetLoggedInState(
      chromeos::LoginState::LOGGED_IN_ACTIVE,
      chromeos::LoginState::LOGGED_IN_USER_PUBLIC_ACCOUNT);
  // Resolve Permission dialog with Deny.
  ScopedTestDialogAutoConfirm auto_confirm(ScopedTestDialogAutoConfirm::CANCEL);
  ASSERT_TRUE(RunExtensionTestWithArg("page_capture", "REQUEST_DENIED"))
      << message_;
}
#endif  // defined(OS_CHROMEOS)
