// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DOMArrayBufferView_h
#define DOMArrayBufferView_h

#include "bindings/core/v8/ScriptWrappable.h"
#include "core/CoreExport.h"
#include "core/dom/DOMArrayBuffer.h"
#include "core/dom/DOMSharedArrayBuffer.h"
#include "platform/wtf/typed_arrays/ArrayBufferView.h"

namespace blink {

class CORE_EXPORT DOMArrayBufferView
    : public GarbageCollectedFinalized<DOMArrayBufferView>,
      public ScriptWrappable {
  DEFINE_WRAPPERTYPEINFO();

 public:
  typedef WTF::ArrayBufferView::ViewType ViewType;
  static const ViewType kTypeInt8 = WTF::ArrayBufferView::kTypeInt8;
  static const ViewType kTypeUint8 = WTF::ArrayBufferView::kTypeUint8;
  static const ViewType kTypeUint8Clamped =
      WTF::ArrayBufferView::kTypeUint8Clamped;
  static const ViewType kTypeInt16 = WTF::ArrayBufferView::kTypeInt16;
  static const ViewType kTypeUint16 = WTF::ArrayBufferView::kTypeUint16;
  static const ViewType kTypeInt32 = WTF::ArrayBufferView::kTypeInt32;
  static const ViewType kTypeUint32 = WTF::ArrayBufferView::kTypeUint32;
  static const ViewType kTypeFloat32 = WTF::ArrayBufferView::kTypeFloat32;
  static const ViewType kTypeFloat64 = WTF::ArrayBufferView::kTypeFloat64;
  static const ViewType kTypeDataView = WTF::ArrayBufferView::kTypeDataView;

  virtual ~DOMArrayBufferView() {}

  DOMArrayBuffer* buffer() const {
    DCHECK(!IsShared());
    if (!dom_array_buffer_)
      dom_array_buffer_ = DOMArrayBuffer::Create(View()->Buffer());

    return static_cast<DOMArrayBuffer*>(dom_array_buffer_.Get());
  }

  DOMSharedArrayBuffer* BufferShared() const {
    DCHECK(IsShared());
    if (!dom_array_buffer_)
      dom_array_buffer_ = DOMSharedArrayBuffer::Create(View()->Buffer());

    return static_cast<DOMSharedArrayBuffer*>(dom_array_buffer_.Get());
  }

  DOMArrayBufferBase* BufferBase() const {
    if (IsShared())
      return BufferShared();

    return buffer();
  }

  const WTF::ArrayBufferView* View() const { return buffer_view_.Get(); }
  WTF::ArrayBufferView* View() { return buffer_view_.Get(); }

  ViewType GetType() const { return View()->GetType(); }
  const char* TypeName() { return View()->TypeName(); }
  void* BaseAddress() const { return View()->BaseAddress(); }
  unsigned byteOffset() const { return View()->ByteOffset(); }
  unsigned byteLength() const { return View()->ByteLength(); }
  unsigned TypeSize() const { return View()->TypeSize(); }
  void SetNeuterable(bool flag) { return View()->SetNeuterable(flag); }
  bool IsShared() const { return View()->IsShared(); }

  void* BaseAddressMaybeShared() const {
    return View()->BaseAddressMaybeShared();
  }

  v8::Local<v8::Object> Wrap(v8::Isolate*,
                             v8::Local<v8::Object> creation_context) override {
    NOTREACHED();
    return v8::Local<v8::Object>();
  }

  DEFINE_INLINE_VIRTUAL_TRACE() { visitor->Trace(dom_array_buffer_); }

 protected:
  explicit DOMArrayBufferView(PassRefPtr<WTF::ArrayBufferView> buffer_view)
      : buffer_view_(std::move(buffer_view)) {
    DCHECK(buffer_view_);
  }
  DOMArrayBufferView(PassRefPtr<WTF::ArrayBufferView> buffer_view,
                     DOMArrayBufferBase* dom_array_buffer)
      : buffer_view_(std::move(buffer_view)),
        dom_array_buffer_(dom_array_buffer) {
    DCHECK(buffer_view_);
    DCHECK(dom_array_buffer_);
    DCHECK_EQ(dom_array_buffer_->Buffer(), buffer_view_->Buffer());
  }

 private:
  RefPtr<WTF::ArrayBufferView> buffer_view_;
  mutable Member<DOMArrayBufferBase> dom_array_buffer_;
};

}  // namespace blink

#endif  // DOMArrayBufferView_h
