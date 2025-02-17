// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file has been auto-generated by code_generator_v8.py.
// DO NOT MODIFY!

// This file has been generated from the Jinja2 template in
// third_party/WebKit/Source/bindings/templates/callback_function.cpp.tmpl

// clang-format off

#include "VoidCallbackFunctionTypedef.h"

#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/IDLTypes.h"
#include "bindings/core/v8/NativeValueTraitsImpl.h"
#include "bindings/core/v8/ScriptState.h"
#include "bindings/core/v8/ToV8ForCore.h"
#include "bindings/core/v8/V8BindingForCore.h"
#include "core/dom/ExecutionContext.h"
#include "platform/wtf/Assertions.h"

namespace blink {

// static
VoidCallbackFunctionTypedef* VoidCallbackFunctionTypedef::Create(ScriptState* scriptState, v8::Local<v8::Value> callback) {
  if (IsUndefinedOrNull(callback))
    return nullptr;
  return new VoidCallbackFunctionTypedef(scriptState, v8::Local<v8::Function>::Cast(callback));
}

VoidCallbackFunctionTypedef::VoidCallbackFunctionTypedef(ScriptState* scriptState, v8::Local<v8::Function> callback)
    : m_scriptState(scriptState),
    m_callback(scriptState->GetIsolate(), this, callback) {
  DCHECK(!m_callback.IsEmpty());
}

DEFINE_TRACE_WRAPPERS(VoidCallbackFunctionTypedef) {
  visitor->TraceWrappers(m_callback.Cast<v8::Value>());
}

bool VoidCallbackFunctionTypedef::call(ScriptWrappable* scriptWrappable, const String& arg) {
  if (m_callback.IsEmpty())
    return false;

  if (!m_scriptState->ContextIsValid())
    return false;

  ExecutionContext* context = ExecutionContext::From(m_scriptState.Get());
  DCHECK(context);
  if (context->IsContextSuspended() || context->IsContextDestroyed())
    return false;

  // TODO(bashi): Make sure that using DummyExceptionStateForTesting is OK.
  // crbug.com/653769
  DummyExceptionStateForTesting exceptionState;
  ScriptState::Scope scope(m_scriptState.Get());
  v8::Isolate* isolate = m_scriptState->GetIsolate();

  v8::Local<v8::Value> thisValue = ToV8(
      scriptWrappable,
      m_scriptState->GetContext()->Global(),
      isolate);

  v8::Local<v8::Value> argArgument = V8String(m_scriptState->GetIsolate(), arg);
  v8::Local<v8::Value> argv[] = { argArgument };
  v8::TryCatch exceptionCatcher(isolate);
  exceptionCatcher.SetVerbose(true);

  v8::Local<v8::Value> v8ReturnValue;
  if (!V8ScriptRunner::CallFunction(m_callback.NewLocal(isolate),
                                    context,
                                    thisValue,
                                    1,
                                    argv,
                                    isolate).ToLocal(&v8ReturnValue)) {
    return false;
  }

  return true;
}

VoidCallbackFunctionTypedef* NativeValueTraits<VoidCallbackFunctionTypedef>::NativeValue(v8::Isolate* isolate, v8::Local<v8::Value> value, ExceptionState& exceptionState) {
  VoidCallbackFunctionTypedef* nativeValue = VoidCallbackFunctionTypedef::Create(ScriptState::Current(isolate), value);
  if (!nativeValue) {
    exceptionState.ThrowTypeError(ExceptionMessages::FailedToConvertJSValue(
        "VoidCallbackFunctionTypedef"));
  }
  return nativeValue;
}

}  // namespace blink
