// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/app/content_service_manager_main_delegate.h"

#include "base/command_line.h"
#include "content/public/app/content_main_delegate.h"
#include "content/public/app/content_main_runner.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/service_names.mojom.h"

namespace content {

ContentServiceManagerMainDelegate::ContentServiceManagerMainDelegate(
    const ContentMainParams& params)
    : content_main_params_(params),
      content_main_runner_(ContentMainRunner::Create()) {}

ContentServiceManagerMainDelegate::~ContentServiceManagerMainDelegate() =
    default;

int ContentServiceManagerMainDelegate::Initialize(
    const InitializeParams& params) {
#if defined(OS_ANDROID)
  // May be called twice on Android due to the way browser startup requests are
  // dispatched by the system.
  if (initialized_)
    return -1;
#endif

#if defined(OS_MACOSX)
  content_main_params_.autorelease_pool = params.autorelease_pool;
#endif

  return content_main_runner_->Initialize(content_main_params_);
}

int ContentServiceManagerMainDelegate::RunEmbedderProcess() {
  return content_main_runner_->Run();
}

void ContentServiceManagerMainDelegate::ShutDownEmbedderProcess() {
#if !defined(OS_ANDROID)
  content_main_runner_->Shutdown();
#endif
}

service_manager::ProcessType
ContentServiceManagerMainDelegate::OverrideProcessType() {
  return content_main_params_.delegate->OverrideProcessType();
}

std::unique_ptr<base::Value>
ContentServiceManagerMainDelegate::CreateServiceCatalog() {
  return content_main_params_.delegate->CreateServiceCatalog();
}

bool ContentServiceManagerMainDelegate::ShouldLaunchAsServiceProcess(
    const service_manager::Identity& identity) {
  return identity.name() != mojom::kPackagedServicesServiceName;
}

void ContentServiceManagerMainDelegate::AdjustServiceProcessCommandLine(
    const service_manager::Identity& identity,
    base::CommandLine* command_line) {
  if (identity.name() == mojom::kPackagedServicesServiceName) {
    // When launching the browser process, ensure that we don't inherit any
    // process type flag. When content embeds Service Manager, a process with no
    // type is launched as a browser process.
    base::CommandLine::SwitchMap switches = command_line->GetSwitches();
    switches.erase(switches::kProcessType);
    *command_line = base::CommandLine(command_line->GetProgram());
    for (const auto& sw : switches)
      command_line->AppendSwitchNative(sw.first, sw.second);
  }

  content_main_params_.delegate->AdjustServiceProcessCommandLine(identity,
                                                                 command_line);
}

bool ContentServiceManagerMainDelegate::
    ShouldTerminateServiceManagerOnInstanceQuit(
        const service_manager::Identity& identity,
        int* exit_code) {
  return content_main_params_.delegate
      ->ShouldTerminateServiceManagerOnInstanceQuit(identity, exit_code);
}

void ContentServiceManagerMainDelegate::OnServiceManagerInitialized(
    const base::Closure& quit_closure,
    service_manager::BackgroundServiceManager* service_manager) {
  return content_main_params_.delegate->OnServiceManagerInitialized(
      quit_closure, service_manager);
}

std::unique_ptr<service_manager::Service>
ContentServiceManagerMainDelegate::CreateEmbeddedService(
    const std::string& service_name) {
  // TODO

  return content_main_params_.delegate->CreateEmbeddedService(service_name);
}

}  // namespace content
