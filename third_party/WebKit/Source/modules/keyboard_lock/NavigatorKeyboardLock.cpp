// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/keyboard_lock/NavigatorKeyboardLock.h"

#include "bindings/core/v8/ScriptPromise.h"
#include "bindings/core/v8/ScriptPromiseResolver.h"
#include "bindings/core/v8/V8Binding.h"
#include "core/frame/LocalFrame.h"
#include "platform/heap/Persistent.h"
#include "platform/wtf/Assertions.h"
#include "platform/wtf/Functional.h"
#include "public/platform/InterfaceProvider.h"

namespace blink {

NavigatorKeyboardLock::NavigatorKeyboardLock(Navigator& navigator)
    : Supplement<Navigator>(navigator) {}

NavigatorKeyboardLock& NavigatorKeyboardLock::From(Navigator& navigator) {
  NavigatorKeyboardLock* supplement = static_cast<NavigatorKeyboardLock*>(
      Supplement<Navigator>::From(navigator, SupplementName()));
  if (!supplement) {
    supplement = new NavigatorKeyboardLock(navigator);
    ProvideTo(navigator, SupplementName(), supplement);
  }
  return *supplement;
}

// static
ScriptPromise NavigatorKeyboardLock::requestKeyLock(
    ScriptState* state,
    Navigator& navigator,
    const Vector<String>& keycodes) {
  return NavigatorKeyboardLock::From(navigator).requestKeyLock(state, keycodes);
}

ScriptPromise NavigatorKeyboardLock::requestKeyLock(
    ScriptState* state,
    const Vector<String>& keycodes) {
  DCHECK(state);
  if (request_keylock_resolver_) {
    // TODO(zijiehe): Reject with a DOMException once it has been defined in the
    // spec. See https://github.com/w3c/keyboard-lock/issues/18.
    return ScriptPromise::Reject(
        state, V8String(state->GetIsolate(),
                        "Last requestKeyLock() has not finished yet."));
  }

  if (!EnsureServiceConnected()) {
      return ScriptPromise::Reject(
          state, V8String(state->GetIsolate(), "Current frame is detached."));
  }

  request_keylock_resolver_ = ScriptPromiseResolver::Create(state);
  service_->RequestKeyLock(
      keycodes,
      ConvertToBaseCallback(WTF::Bind(
          &NavigatorKeyboardLock::LockRequestFinished, WrapPersistent(this))));
  return request_keylock_resolver_->Promise();
}

void NavigatorKeyboardLock::cancelKeyLock() {
  if (!EnsureServiceConnected()) {
    // Current frame is detached.
    return;
  }

  service_->CancelKeyLock();
}

// static
void NavigatorKeyboardLock::cancelKeyLock(Navigator& navigator) {
  NavigatorKeyboardLock::From(navigator).cancelKeyLock();
}

bool NavigatorKeyboardLock::EnsureServiceConnected() {
  if (!service_) {
    LocalFrame* frame = GetSupplementable()->GetFrame();
    if (!frame) {
      return false;
    }
    frame->GetInterfaceProvider()->GetInterface(mojo::MakeRequest(&service_));
  }

  DCHECK(service_);
  return true;
}

void NavigatorKeyboardLock::LockRequestFinished(bool allowed,
                                                const String& reason) {
  DCHECK(request_keylock_resolver_);
  // TODO(zijiehe): Reject with a DOMException once it has been defined in the
  // spec.
  if (allowed)
    request_keylock_resolver_->Resolve();
  else
    request_keylock_resolver_->Reject(reason);
  request_keylock_resolver_ = nullptr;
}

// static
const char* NavigatorKeyboardLock::SupplementName() {
  return "NavigatorKeyboardLock";
}

DEFINE_TRACE(NavigatorKeyboardLock) {
  visitor->Trace(request_keylock_resolver_);
  Supplement<Navigator>::Trace(visitor);
}

}  // namespace blink
