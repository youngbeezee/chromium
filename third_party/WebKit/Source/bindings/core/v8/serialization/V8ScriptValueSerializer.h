// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8ScriptValueSerializer_h
#define V8ScriptValueSerializer_h

#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/ScriptState.h"
#include "bindings/core/v8/SerializationTag.h"
#include "bindings/core/v8/SerializedScriptValue.h"
#include "core/CoreExport.h"
#include "platform/wtf/Allocator.h"
#include "platform/wtf/Noncopyable.h"
#include "platform/wtf/RefPtr.h"
#include "v8/include/v8.h"

namespace blink {

class File;
class Transferables;

// Serializes V8 values according to the HTML structured clone algorithm:
// https://html.spec.whatwg.org/multipage/infrastructure.html#structured-clone
//
// Supports only basic JavaScript objects and core DOM types. Support for
// modules types is implemented in a subclass.
//
// A serializer cannot be used multiple times; it is expected that its serialize
// method will be invoked exactly once.
class GC_PLUGIN_IGNORE("https://crbug.com/644725")
    CORE_EXPORT V8ScriptValueSerializer : public v8::ValueSerializer::Delegate {
  STACK_ALLOCATED();
  WTF_MAKE_NONCOPYABLE(V8ScriptValueSerializer);

 public:
  using Options = SerializedScriptValue::SerializeOptions;
  explicit V8ScriptValueSerializer(RefPtr<ScriptState>,
                                   const Options& = Options());

  void SetInlineWasm(bool inline_wasm) { inline_wasm_ = inline_wasm; }

  RefPtr<SerializedScriptValue> Serialize(v8::Local<v8::Value>,
                                          ExceptionState&);

  static const uint32_t kLatestVersion;

 protected:
  // Returns true if the DOM object was successfully written.
  // If false is returned and no more specific exception is thrown, a generic
  // DataCloneError message will be used.
  virtual bool WriteDOMObject(ScriptWrappable*, ExceptionState&);

  void WriteTag(SerializationTag tag) {
    uint8_t tag_byte = tag;
    serializer_.WriteRawBytes(&tag_byte, 1);
  }
  void WriteUint32(uint32_t value) { serializer_.WriteUint32(value); }
  void WriteUint64(uint64_t value) { serializer_.WriteUint64(value); }
  void WriteDouble(double value) { serializer_.WriteDouble(value); }
  void WriteRawBytes(const void* data, size_t size) {
    serializer_.WriteRawBytes(data, size);
  }
  void WriteUTF8String(const String&);

 private:
  // Transfer is split into two phases: scanning the transferables so that we
  // don't have to serialize the data (just an index), and finalizing (to
  // neuter objects in the source context).
  // This separation is required by the spec (it prevents neutering from
  // happening if there's a failure earlier in serialization).
  void PrepareTransfer(ExceptionState&);
  void FinalizeTransfer(ExceptionState&);

  // Shared between File and FileList logic; does not write a leading tag.
  bool WriteFile(File*, ExceptionState&);

  // v8::ValueSerializer::Delegate
  void ThrowDataCloneError(v8::Local<v8::String> message) override;
  v8::Maybe<bool> WriteHostObject(v8::Isolate*,
                                  v8::Local<v8::Object> message) override;
  v8::Maybe<uint32_t> GetSharedArrayBufferId(
      v8::Isolate*,
      v8::Local<v8::SharedArrayBuffer>) override;

  v8::Maybe<uint32_t> GetWasmModuleTransferId(
      v8::Isolate*,
      v8::Local<v8::WasmCompiledModule>) override;
  void* ReallocateBufferMemory(void* old_buffer,
                               size_t,
                               size_t* actual_size) override;
  void FreeBufferMemory(void* buffer) override;

  RefPtr<ScriptState> script_state_;
  RefPtr<SerializedScriptValue> serialized_script_value_;
  v8::ValueSerializer serializer_;
  const Transferables* transferables_ = nullptr;
  const ExceptionState* exception_state_ = nullptr;
  WebBlobInfoArray* blob_info_array_ = nullptr;
  ArrayBufferArray shared_array_buffers_;
  bool inline_wasm_ = false;
  bool for_storage_ = false;
#if DCHECK_IS_ON()
  bool serialize_invoked_ = false;
#endif
};

}  // namespace blink

#endif  // V8ScriptValueSerializer_h
