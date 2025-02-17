// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/serviceworkers/ServiceWorkerWindowClientCallback.h"

#include "bindings/core/v8/ScriptPromiseResolver.h"
#include "bindings/core/v8/V8ThrowException.h"
#include "core/dom/DOMException.h"
#include "modules/serviceworkers/ServiceWorkerError.h"
#include "modules/serviceworkers/ServiceWorkerWindowClient.h"
#include "platform/wtf/PtrUtil.h"

namespace blink {

void NavigateClientCallback::OnSuccess(
    std::unique_ptr<WebServiceWorkerClientInfo> client_info) {
  if (!resolver_->GetExecutionContext() ||
      resolver_->GetExecutionContext()->IsContextDestroyed())
    return;
  resolver_->Resolve(ServiceWorkerWindowClient::Take(
      resolver_.Get(), WTF::WrapUnique(client_info.release())));
}

void NavigateClientCallback::OnError(const WebServiceWorkerError& error) {
  if (!resolver_->GetExecutionContext() ||
      resolver_->GetExecutionContext()->IsContextDestroyed())
    return;

  if (error.error_type == WebServiceWorkerError::kErrorTypeNavigation) {
    ScriptState::Scope scope(resolver_->GetScriptState());
    resolver_->Reject(V8ThrowException::CreateTypeError(
        resolver_->GetScriptState()->GetIsolate(), error.message));
    return;
  }

  resolver_->Reject(ServiceWorkerError::Take(resolver_.Get(), error));
}

}  // namespace blink
