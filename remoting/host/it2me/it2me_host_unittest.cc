// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/it2me/it2me_host.h"

#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/policy/core/common/fake_async_policy_loader.h"
#include "components/policy/policy_constants.h"
#include "remoting/base/auto_thread_task_runner.h"
#include "remoting/host/chromoting_host_context.h"
#include "remoting/host/it2me/it2me_confirmation_dialog.h"
#include "remoting/host/policy_watcher.h"
#include "remoting/signaling/fake_signal_strategy.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace remoting {

namespace {

// Shortening some type names for readability.
typedef protocol::ValidatingAuthenticator::Result ValidationResult;
typedef It2MeConfirmationDialog::Result DialogResult;

const char kTestUserName[] = "ficticious_user@gmail.com";
const char kTestClientJid[] = "ficticious_user@gmail.com/jid_resource";
const char kTestClientJid2[] = "ficticious_user_2@gmail.com/jid_resource";
const char kTestClientUsernameNoJid[] = "completely_ficticious_user@gmail.com";
const char kTestClientJidWithSlash[] = "fake/user@gmail.com/jid_resource";
const char kResourceOnly[] = "/jid_resource";
const char kMatchingDomain[] = "gmail.com";
const char kMismatchedDomain1[] = "similar_to_gmail.com";
const char kMismatchedDomain2[] = "gmail_at_the_beginning.com";
const char kMismatchedDomain3[] = "not_even_close.com";

}  // namespace

class FakeIt2MeConfirmationDialog : public It2MeConfirmationDialog {
 public:
  FakeIt2MeConfirmationDialog(const std::string& remote_user_email,
                              DialogResult dialog_result);
  ~FakeIt2MeConfirmationDialog() override;

  // It2MeConfirmationDialog implementation.
  void Show(const std::string& remote_user_email,
            const ResultCallback& callback) override;

 private:
  FakeIt2MeConfirmationDialog();

  std::string remote_user_email_;
  DialogResult dialog_result_ = DialogResult::OK;

  DISALLOW_COPY_AND_ASSIGN(FakeIt2MeConfirmationDialog);
};

FakeIt2MeConfirmationDialog::FakeIt2MeConfirmationDialog() {}

FakeIt2MeConfirmationDialog::FakeIt2MeConfirmationDialog(
    const std::string& remote_user_email,
    DialogResult dialog_result)
    : remote_user_email_(remote_user_email), dialog_result_(dialog_result) {}

FakeIt2MeConfirmationDialog::~FakeIt2MeConfirmationDialog() {}

void FakeIt2MeConfirmationDialog::Show(const std::string& remote_user_email,
                                       const ResultCallback& callback) {
  EXPECT_STREQ(remote_user_email_.c_str(), remote_user_email.c_str());

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(callback, dialog_result_));
}

class FakeIt2MeDialogFactory : public It2MeConfirmationDialogFactory {
 public:
  FakeIt2MeDialogFactory();
  ~FakeIt2MeDialogFactory() override;

  std::unique_ptr<It2MeConfirmationDialog> Create() override;

  void set_dialog_result(DialogResult dialog_result) {
    dialog_result_ = dialog_result;
  }

  void set_remote_user_email(const std::string& remote_user_email) {
    remote_user_email_ = remote_user_email;
  }

 private:
  std::string remote_user_email_;
  DialogResult dialog_result_ = DialogResult::OK;

  DISALLOW_COPY_AND_ASSIGN(FakeIt2MeDialogFactory);
};

FakeIt2MeDialogFactory::FakeIt2MeDialogFactory()
    : remote_user_email_(kTestUserName) {}

FakeIt2MeDialogFactory::~FakeIt2MeDialogFactory() {}

std::unique_ptr<It2MeConfirmationDialog> FakeIt2MeDialogFactory::Create() {
  EXPECT_FALSE(remote_user_email_.empty());
  return base::MakeUnique<FakeIt2MeConfirmationDialog>(remote_user_email_,
                                                       dialog_result_);
}

class It2MeHostTest : public testing::Test, public It2MeHost::Observer {
 public:
  It2MeHostTest();
  ~It2MeHostTest() override;

  // testing::Test interface.
  void SetUp() override;
  void TearDown() override;

  void OnValidationComplete(const base::Closure& resume_callback,
                            ValidationResult validation_result);

 protected:
  // It2MeHost::Observer interface.
  void OnClientAuthenticated(const std::string& client_username) override;
  void OnStoreAccessCode(const std::string& access_code,
                         base::TimeDelta access_code_lifetime) override;
  void OnNatPolicyChanged(bool nat_traversal_enabled) override;
  void OnStateChanged(It2MeHostState state,
                      const std::string& error_message) override;

  void SetPolicies(
      std::initializer_list<std::pair<base::StringPiece, const base::Value&>>
          policies);

  void RunUntilStateChanged(It2MeHostState expected_state);

  void RunValidationCallback(const std::string& remote_jid);

  void StartHost();
  void ShutdownHost();

  ValidationResult validation_result_ = ValidationResult::SUCCESS;

  base::Closure state_change_callback_;

  It2MeHostState last_host_state_ = It2MeHostState::kDisconnected;

  // Used to set ConfirmationDialog behavior.
  FakeIt2MeDialogFactory* dialog_factory_ = nullptr;

 private:
  void StartupHostStateHelper(const base::Closure& quit_closure);

  std::unique_ptr<base::MessageLoop> message_loop_;
  std::unique_ptr<base::RunLoop> run_loop_;

  scoped_refptr<AutoThreadTaskRunner> network_task_runner_;
  scoped_refptr<AutoThreadTaskRunner> ui_task_runner_;

  scoped_refptr<It2MeHost> it2me_host_;
  // The FakeAsyncPolicyLoader is owned by it2me_host_, but we retain a raw
  // pointer so we can control the policy contents.
  policy::FakeAsyncPolicyLoader* policy_loader_ = nullptr;

  base::WeakPtrFactory<It2MeHostTest> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(It2MeHostTest);
};

It2MeHostTest::It2MeHostTest() : weak_factory_(this) {}

It2MeHostTest::~It2MeHostTest() {}

void It2MeHostTest::SetUp() {
  message_loop_.reset(new base::MessageLoop());
  run_loop_.reset(new base::RunLoop());

  std::unique_ptr<ChromotingHostContext> host_context(
      ChromotingHostContext::Create(new AutoThreadTaskRunner(
          base::ThreadTaskRunnerHandle::Get(), run_loop_->QuitClosure())));
  network_task_runner_ = host_context->network_task_runner();
  ui_task_runner_ = host_context->ui_task_runner();

  std::unique_ptr<FakeIt2MeDialogFactory> dialog_factory(
      new FakeIt2MeDialogFactory());
  dialog_factory_ = dialog_factory.get();
  policy_loader_ =
      new policy::FakeAsyncPolicyLoader(base::ThreadTaskRunnerHandle::Get());
  it2me_host_ = new It2MeHost(
      std::move(host_context),
      PolicyWatcher::CreateFromPolicyLoaderForTesting(
          base::WrapUnique(policy_loader_)),
      std::move(dialog_factory), weak_factory_.GetWeakPtr(),
      base::WrapUnique(
          new FakeSignalStrategy(SignalingAddress("fake_local_jid"))),
      kTestUserName, "fake_bot_jid");
}

void It2MeHostTest::TearDown() {
  // Shutdown the host if it hasn't been already. Without this, the call to
  // run_loop_->Run() may never return.
  it2me_host_->Disconnect();
  network_task_runner_ = nullptr;
  ui_task_runner_ = nullptr;
  it2me_host_ = nullptr;
  run_loop_->Run();
}

void It2MeHostTest::OnValidationComplete(const base::Closure& resume_callback,
                                         ValidationResult validation_result) {
  validation_result_ = validation_result;

  ui_task_runner_->PostTask(FROM_HERE, resume_callback);
}

void It2MeHostTest::SetPolicies(
    std::initializer_list<std::pair<base::StringPiece, const base::Value&>>
        policies) {
  policy::PolicyBundle bundle;
  policy::PolicyMap& map = bundle.Get(
      policy::PolicyNamespace(policy::POLICY_DOMAIN_CHROME, std::string()));
  for (const auto& policy : policies) {
    map.Set(policy.first.as_string(), policy::POLICY_LEVEL_MANDATORY,
            policy::POLICY_SCOPE_MACHINE, policy::POLICY_SOURCE_PLATFORM,
            policy.second.CreateDeepCopy(), nullptr);
  }
  policy_loader_->SetPolicies(bundle);
  policy_loader_->PostReloadOnBackgroundThread(true);
  base::RunLoop().RunUntilIdle();
}

void It2MeHostTest::StartupHostStateHelper(const base::Closure& quit_closure) {
  if (last_host_state_ == It2MeHostState::kRequestedAccessCode) {
    network_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&It2MeHost::SetStateForTesting, it2me_host_.get(),
                   It2MeHostState::kReceivedAccessCode, std::string()));
  } else if (last_host_state_ != It2MeHostState::kStarting) {
    quit_closure.Run();
    return;
  }
  state_change_callback_ = base::Bind(&It2MeHostTest::StartupHostStateHelper,
                                      base::Unretained(this), quit_closure);
}

void It2MeHostTest::StartHost() {
  it2me_host_->Connect();

  base::RunLoop run_loop;
  state_change_callback_ =
      base::Bind(&It2MeHostTest::StartupHostStateHelper, base::Unretained(this),
                 run_loop.QuitClosure());
  run_loop.Run();
}

void It2MeHostTest::RunUntilStateChanged(It2MeHostState expected_state) {
  if (last_host_state_ == expected_state) {
    // Bail out early if the state is already correct.
    return;
  }

  base::RunLoop run_loop;
  state_change_callback_ = run_loop.QuitClosure();
  run_loop.Run();
}

void It2MeHostTest::RunValidationCallback(const std::string& remote_jid) {
  base::RunLoop run_loop;

  network_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(it2me_host_->GetValidationCallbackForTesting(), remote_jid,
                 base::Bind(&It2MeHostTest::OnValidationComplete,
                            base::Unretained(this), run_loop.QuitClosure())));

  run_loop.Run();
}

void It2MeHostTest::OnClientAuthenticated(const std::string& client_username) {}

void It2MeHostTest::OnStoreAccessCode(const std::string& access_code,
                                      base::TimeDelta access_code_lifetime) {}

void It2MeHostTest::OnNatPolicyChanged(bool nat_traversal_enabled) {}

void It2MeHostTest::OnStateChanged(It2MeHostState state,
                                   const std::string& error_message) {
  last_host_state_ = state;

  if (state_change_callback_) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::ResetAndReturn(&state_change_callback_));
  }
}

void It2MeHostTest::ShutdownHost() {
  if (it2me_host_) {
    it2me_host_->Disconnect();
    RunUntilStateChanged(It2MeHostState::kDisconnected);
  }
}

TEST_F(It2MeHostTest, HostValidation_NoHostDomainPolicy) {
  StartHost();
  ASSERT_EQ(It2MeHostState::kReceivedAccessCode, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, HostValidation_HostDomainPolicy_MatchingDomain) {
  SetPolicies(
      {{policy::key::kRemoteAccessHostDomain, base::Value(kMatchingDomain)}});
  StartHost();
  ASSERT_EQ(It2MeHostState::kReceivedAccessCode, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, HostValidation_HostDomainPolicy_NoMatch) {
  SetPolicies({{policy::key::kRemoteAccessHostDomain,
                base::Value(kMismatchedDomain3)}});
  StartHost();
  ASSERT_EQ(It2MeHostState::kInvalidDomainError, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, HostValidation_HostDomainPolicy_MatchStart) {
  SetPolicies({{policy::key::kRemoteAccessHostDomain,
                base::Value(kMismatchedDomain2)}});
  StartHost();
  ASSERT_EQ(It2MeHostState::kInvalidDomainError, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, HostValidation_HostDomainPolicy_MatchEnd) {
  SetPolicies({{policy::key::kRemoteAccessHostDomain,
                base::Value(kMismatchedDomain1)}});
  StartHost();
  ASSERT_EQ(It2MeHostState::kInvalidDomainError, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, HostValidation_HostDomainListPolicy_MatchFirst) {
  base::ListValue domains;
  domains.AppendString(kMatchingDomain);
  domains.AppendString(kMismatchedDomain1);
  SetPolicies({{policy::key::kRemoteAccessHostDomainList, domains}});
  StartHost();
  ASSERT_EQ(It2MeHostState::kReceivedAccessCode, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, HostValidation_HostDomainListPolicy_MatchSecond) {
  base::ListValue domains;
  domains.AppendString(kMismatchedDomain1);
  domains.AppendString(kMatchingDomain);
  SetPolicies({{policy::key::kRemoteAccessHostDomainList, domains}});
  StartHost();
  ASSERT_EQ(It2MeHostState::kReceivedAccessCode, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, HostValidation_HostDomainListPolicy_NoMatch) {
  base::ListValue domains;
  domains.AppendString(kMismatchedDomain1);
  domains.AppendString(kMismatchedDomain2);
  SetPolicies({{policy::key::kRemoteAccessHostDomainList, domains}});
  StartHost();
  ASSERT_EQ(It2MeHostState::kInvalidDomainError, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, HostValidation_HostDomainBothPolicies_BothMatch) {
  base::ListValue domains;
  domains.AppendString(kMatchingDomain);
  domains.AppendString(kMismatchedDomain1);
  SetPolicies(
      {{policy::key::kRemoteAccessHostDomain, base::Value(kMatchingDomain)},
       {policy::key::kRemoteAccessHostDomainList, domains}});
  StartHost();
  ASSERT_EQ(It2MeHostState::kReceivedAccessCode, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, HostValidation_HostDomainBothPolicies_ListMatch) {
  base::ListValue domains;
  domains.AppendString(kMismatchedDomain1);
  domains.AppendString(kMatchingDomain);
  SetPolicies(
      {{policy::key::kRemoteAccessHostDomain, base::Value(kMismatchedDomain1)},
       {policy::key::kRemoteAccessHostDomainList, domains}});
  // Should succeed even though the legacy policy would deny.
  StartHost();
  ASSERT_EQ(It2MeHostState::kReceivedAccessCode, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, HostValidation_HostDomainBothPolicies_LegacyMatch) {
  base::ListValue domains;
  domains.AppendString(kMismatchedDomain1);
  domains.AppendString(kMismatchedDomain2);
  SetPolicies(
      {{policy::key::kRemoteAccessHostDomain, base::Value(kMatchingDomain)},
       {policy::key::kRemoteAccessHostDomainList, domains}});
  // Should fail even though the legacy policy would allow.
  StartHost();
  ASSERT_EQ(It2MeHostState::kInvalidDomainError, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, ConnectionValidation_NoClientDomainPolicy_ValidJid) {
  StartHost();
  RunValidationCallback(kTestClientJid);
  ASSERT_EQ(ValidationResult::SUCCESS, validation_result_);
  ASSERT_EQ(It2MeHostState::kConnecting, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, ConnectionValidation_NoClientDomainPolicy_InvalidJid) {
  StartHost();
  RunValidationCallback(kTestClientUsernameNoJid);
  ASSERT_EQ(ValidationResult::ERROR_INVALID_ACCOUNT, validation_result_);
  RunUntilStateChanged(It2MeHostState::kDisconnected);
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest,
       ConnectionValidation_NoClientDomainPolicy_InvalidUsername) {
  StartHost();
  dialog_factory_->set_remote_user_email("fake");
  RunValidationCallback(kTestClientJidWithSlash);
  ASSERT_EQ(ValidationResult::SUCCESS, validation_result_);
  ASSERT_EQ(It2MeHostState::kConnecting, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, ConnectionValidation_NoClientDomainPolicy_ResourceOnly) {
  StartHost();
  RunValidationCallback(kResourceOnly);
  ASSERT_EQ(ValidationResult::ERROR_INVALID_ACCOUNT, validation_result_);
  RunUntilStateChanged(It2MeHostState::kDisconnected);
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, ConnectionValidation_ClientDomainPolicy_MatchingDomain) {
  SetPolicies({{policy::key::kRemoteAccessHostClientDomain,
                base::Value(kMatchingDomain)}});
  StartHost();
  RunValidationCallback(kTestClientJid);
  ASSERT_EQ(ValidationResult::SUCCESS, validation_result_);
  ASSERT_EQ(It2MeHostState::kConnecting, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, ConnectionValidation_ClientDomainPolicy_InvalidUserName) {
  SetPolicies({{policy::key::kRemoteAccessHostClientDomain,
                base::Value(kMatchingDomain)}});
  StartHost();
  RunValidationCallback(kTestClientJidWithSlash);
  ASSERT_EQ(ValidationResult::ERROR_INVALID_ACCOUNT, validation_result_);
  RunUntilStateChanged(It2MeHostState::kDisconnected);
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, ConnectionValidation_ClientDomainPolicy_NoJid) {
  SetPolicies({{policy::key::kRemoteAccessHostClientDomain,
                base::Value(kMatchingDomain)}});
  StartHost();
  RunValidationCallback(kTestClientUsernameNoJid);
  RunUntilStateChanged(It2MeHostState::kDisconnected);
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
  ASSERT_EQ(ValidationResult::ERROR_INVALID_ACCOUNT, validation_result_);
}

TEST_F(It2MeHostTest, ConnectionValidation_WrongClientDomain_NoMatch) {
  SetPolicies({{policy::key::kRemoteAccessHostClientDomain,
                base::Value(kMismatchedDomain3)}});
  StartHost();
  RunValidationCallback(kTestClientJid);
  ASSERT_EQ(ValidationResult::ERROR_INVALID_ACCOUNT, validation_result_);
  RunUntilStateChanged(It2MeHostState::kDisconnected);
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, ConnectionValidation_WrongClientDomain_MatchStart) {
  SetPolicies({{policy::key::kRemoteAccessHostClientDomain,
                base::Value(kMismatchedDomain2)}});
  StartHost();
  RunValidationCallback(kTestClientJid);
  ASSERT_EQ(ValidationResult::ERROR_INVALID_ACCOUNT, validation_result_);
  RunUntilStateChanged(It2MeHostState::kDisconnected);
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, ConnectionValidation_WrongClientDomain_MatchEnd) {
  SetPolicies({{policy::key::kRemoteAccessHostClientDomain,
                base::Value(kMismatchedDomain1)}});
  StartHost();
  RunValidationCallback(kTestClientJid);
  ASSERT_EQ(ValidationResult::ERROR_INVALID_ACCOUNT, validation_result_);
  RunUntilStateChanged(It2MeHostState::kDisconnected);
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, ConnectionValidation_ClientDomainListPolicy_MatchFirst) {
  base::ListValue domains;
  domains.AppendString(kMatchingDomain);
  domains.AppendString(kMismatchedDomain1);
  SetPolicies({{policy::key::kRemoteAccessHostClientDomainList, domains}});
  StartHost();
  RunValidationCallback(kTestClientJid);
  ASSERT_EQ(ValidationResult::SUCCESS, validation_result_);
  ASSERT_EQ(It2MeHostState::kConnecting, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, ConnectionValidation_ClientDomainListPolicy_MatchSecond) {
  base::ListValue domains;
  domains.AppendString(kMismatchedDomain1);
  domains.AppendString(kMatchingDomain);
  SetPolicies({{policy::key::kRemoteAccessHostClientDomainList, domains}});
  StartHost();
  RunValidationCallback(kTestClientJid);
  ASSERT_EQ(ValidationResult::SUCCESS, validation_result_);
  ASSERT_EQ(It2MeHostState::kConnecting, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, ConnectionValidation_ClientDomainListPolicy_NoMatch) {
  base::ListValue domains;
  domains.AppendString(kMismatchedDomain1);
  domains.AppendString(kMismatchedDomain2);
  SetPolicies({{policy::key::kRemoteAccessHostClientDomainList, domains}});
  StartHost();
  RunValidationCallback(kTestClientJid);
  ASSERT_EQ(ValidationResult::ERROR_INVALID_ACCOUNT, validation_result_);
  RunUntilStateChanged(It2MeHostState::kDisconnected);
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, ConnectionValidation_ClientDomainBothPolicies_BothMatch) {
  base::ListValue domains;
  domains.AppendString(kMatchingDomain);
  domains.AppendString(kMismatchedDomain1);
  SetPolicies({{policy::key::kRemoteAccessHostClientDomain,
                base::Value(kMatchingDomain)},
               {policy::key::kRemoteAccessHostClientDomainList, domains}});
  StartHost();
  RunValidationCallback(kTestClientJid);
  ASSERT_EQ(ValidationResult::SUCCESS, validation_result_);
  ASSERT_EQ(It2MeHostState::kConnecting, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, ConnectionValidation_ClientDomainBothPolicies_ListMatch) {
  base::ListValue domains;
  domains.AppendString(kMismatchedDomain1);
  domains.AppendString(kMatchingDomain);
  SetPolicies({{policy::key::kRemoteAccessHostClientDomain,
                base::Value(kMismatchedDomain1)},
               {policy::key::kRemoteAccessHostClientDomainList, domains}});
  // Should succeed even though the legacy policy would deny.
  StartHost();
  RunValidationCallback(kTestClientJid);
  ASSERT_EQ(ValidationResult::SUCCESS, validation_result_);
  ASSERT_EQ(It2MeHostState::kConnecting, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest,
       ConnectionValidation_ClientDomainBothPolicies_LegacyMatch) {
  base::ListValue domains;
  domains.AppendString(kMismatchedDomain1);
  domains.AppendString(kMismatchedDomain2);
  SetPolicies({{policy::key::kRemoteAccessHostClientDomain,
                base::Value(kMatchingDomain)},
               {policy::key::kRemoteAccessHostClientDomainList, domains}});
  // Should fail even though the legacy policy would allow.
  StartHost();
  RunValidationCallback(kTestClientJid);
  ASSERT_EQ(ValidationResult::ERROR_INVALID_ACCOUNT, validation_result_);
  RunUntilStateChanged(It2MeHostState::kDisconnected);
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, ConnectionValidation_ConfirmationDialog_Accept) {
  StartHost();
  RunValidationCallback(kTestClientJid);
  ASSERT_EQ(ValidationResult::SUCCESS, validation_result_);
  ASSERT_EQ(It2MeHostState::kConnecting, last_host_state_);
  ShutdownHost();
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, ConnectionValidation_ConfirmationDialog_Reject) {
  dialog_factory_->set_dialog_result(DialogResult::CANCEL);
  StartHost();
  RunValidationCallback(kTestClientJid);
  ASSERT_EQ(ValidationResult::ERROR_REJECTED_BY_USER, validation_result_);
  RunUntilStateChanged(It2MeHostState::kDisconnected);
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

TEST_F(It2MeHostTest, MultipleConnectionsTriggerDisconnect) {
  StartHost();
  RunValidationCallback(kTestClientJid);
  ASSERT_EQ(ValidationResult::SUCCESS, validation_result_);
  ASSERT_EQ(It2MeHostState::kConnecting, last_host_state_);

  RunValidationCallback(kTestClientJid2);
  ASSERT_EQ(ValidationResult::ERROR_TOO_MANY_CONNECTIONS, validation_result_);
  RunUntilStateChanged(It2MeHostState::kDisconnected);
  ASSERT_EQ(It2MeHostState::kDisconnected, last_host_state_);
}

}  // namespace remoting
