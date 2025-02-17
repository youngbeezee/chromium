// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ImageCapture_h
#define ImageCapture_h

#include <memory>
#include "bindings/core/v8/ActiveScriptWrappable.h"
#include "bindings/core/v8/ScriptPromise.h"
#include "core/dom/ContextLifecycleObserver.h"
#include "core/events/EventTarget.h"
#include "media/capture/mojo/image_capture.mojom-blink.h"
#include "modules/EventTargetModules.h"
#include "modules/ModulesExport.h"
#include "modules/mediastream/MediaTrackCapabilities.h"
#include "modules/mediastream/MediaTrackConstraintSet.h"
#include "modules/mediastream/MediaTrackSettings.h"
#include "platform/AsyncMethodRunner.h"

namespace blink {

class ExceptionState;
class MediaStreamTrack;
class MediaTrackConstraints;
class PhotoSettings;
class ScriptPromiseResolver;
class WebImageCaptureFrameGrabber;

// TODO(mcasas): Consideradding a LayoutTest checking that this class is not
// garbage collected while it has event listeners.
class MODULES_EXPORT ImageCapture final
    : public EventTargetWithInlineData,
      public ActiveScriptWrappable<ImageCapture>,
      public ContextLifecycleObserver {
  USING_GARBAGE_COLLECTED_MIXIN(ImageCapture);
  DEFINE_WRAPPERTYPEINFO();

 public:
  static ImageCapture* Create(ExecutionContext*,
                              MediaStreamTrack*,
                              ExceptionState&);
  ~ImageCapture() override;

  // EventTarget implementation.
  const AtomicString& InterfaceName() const override;
  ExecutionContext* GetExecutionContext() const override;

  // ScriptWrappable implementation.
  bool HasPendingActivity() const final;

  // ContextLifecycleObserver
  void ContextDestroyed(ExecutionContext*) override;

  MediaStreamTrack* videoStreamTrack() const { return stream_track_.Get(); }

  ScriptPromise getPhotoCapabilities(ScriptState*);

  ScriptPromise setOptions(ScriptState*, const PhotoSettings&);

  ScriptPromise takePhoto(ScriptState*);

  ScriptPromise grabFrame(ScriptState*);

  MediaTrackCapabilities& GetMediaTrackCapabilities();
  void SetMediaTrackConstraints(ScriptPromiseResolver*,
                                const HeapVector<MediaTrackConstraintSet>&);
  const MediaTrackConstraintSet& GetMediaTrackConstraints() const;
  void ClearMediaTrackConstraints(ScriptPromiseResolver*);
  void GetMediaTrackSettings(MediaTrackSettings&) const;

  // TODO(mcasas): Remove this service method, https://crbug.com/338503.
  bool HasNonImageCaptureConstraints(const MediaTrackConstraints&) const;

  DECLARE_VIRTUAL_TRACE();

 private:
  ImageCapture(ExecutionContext*, MediaStreamTrack*);

  void OnPhotoCapabilities(ScriptPromiseResolver*,
                           media::mojom::blink::PhotoCapabilitiesPtr);
  void OnSetOptions(ScriptPromiseResolver*, bool);
  void OnTakePhoto(ScriptPromiseResolver*, media::mojom::blink::BlobPtr);
  void OnCapabilitiesUpdate(media::mojom::blink::PhotoCapabilitiesPtr);

  void OnCapabilitiesUpdateInternal(
      const media::mojom::blink::PhotoCapabilities&);
  void OnServiceConnectionError();

  Member<MediaStreamTrack> stream_track_;
  std::unique_ptr<WebImageCaptureFrameGrabber> frame_grabber_;
  media::mojom::blink::ImageCapturePtr service_;

  MediaTrackCapabilities capabilities_;
  MediaTrackSettings settings_;
  MediaTrackConstraintSet current_constraints_;

  HeapHashSet<Member<ScriptPromiseResolver>> service_requests_;
};

}  // namespace blink

#endif  // ImageCapture_h
