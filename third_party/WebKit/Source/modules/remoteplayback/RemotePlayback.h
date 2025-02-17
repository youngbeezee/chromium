// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RemotePlayback_h
#define RemotePlayback_h

#include "bindings/core/v8/ActiveScriptWrappable.h"
#include "bindings/core/v8/ScriptPromise.h"
#include "bindings/core/v8/TraceWrapperMember.h"
#include "core/dom/ExecutionContext.h"
#include "core/events/EventTarget.h"
#include "modules/ModulesExport.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/Compiler.h"
#include "platform/wtf/text/AtomicString.h"
#include "platform/wtf/text/WTFString.h"
#include "public/platform/modules/remoteplayback/WebRemotePlaybackAvailability.h"
#include "public/platform/modules/remoteplayback/WebRemotePlaybackClient.h"
#include "public/platform/modules/remoteplayback/WebRemotePlaybackState.h"

namespace blink {

class AvailabilityCallbackWrapper;
class HTMLMediaElement;
class RemotePlaybackAvailabilityCallback;
class ScriptPromiseResolver;
class ScriptState;

class MODULES_EXPORT RemotePlayback final
    : public EventTargetWithInlineData,
      public ActiveScriptWrappable<RemotePlayback>,
      NON_EXPORTED_BASE(public WebRemotePlaybackClient) {
  DEFINE_WRAPPERTYPEINFO();
  USING_GARBAGE_COLLECTED_MIXIN(RemotePlayback);

 public:
  static RemotePlayback* Create(HTMLMediaElement&);

  // Notifies this object that disableRemotePlayback attribute was set on the
  // corresponding media element.
  void RemotePlaybackDisabled();

  // EventTarget implementation.
  const WTF::AtomicString& InterfaceName() const override;
  ExecutionContext* GetExecutionContext() const override;

  // Starts notifying the page about the changes to the remote playback devices
  // availability via the provided callback. May start the monitoring of remote
  // playback devices if it isn't running yet.
  ScriptPromise watchAvailability(ScriptState*,
                                  RemotePlaybackAvailabilityCallback*);

  // Cancels updating the page via the callback specified by its id.
  ScriptPromise cancelWatchAvailability(ScriptState*, int id);

  // Cancels all the callbacks watching remote playback availability changes
  // registered with this element.
  ScriptPromise cancelWatchAvailability(ScriptState*);

  // Shows the UI allowing user to change the remote playback state of the media
  // element (by picking a remote playback device from the list, for example).
  ScriptPromise prompt(ScriptState*);

  String state() const;

  // The implementation of prompt(). Used by the native remote playback button.
  void PromptInternal();

  // The implementation of watchAvailability() and cancelWatchAvailability().
  int WatchAvailabilityInternal(AvailabilityCallbackWrapper*);
  bool CancelWatchAvailabilityInternal(int id);

  WebRemotePlaybackState GetState() const { return state_; }

  // WebRemotePlaybackClient implementation.
  void StateChanged(WebRemotePlaybackState) override;
  void AvailabilityChanged(WebRemotePlaybackAvailability) override;
  void PromptCancelled() override;
  bool RemotePlaybackAvailable() const override;

  // ScriptWrappable implementation.
  bool HasPendingActivity() const final;

  DEFINE_ATTRIBUTE_EVENT_LISTENER(connecting);
  DEFINE_ATTRIBUTE_EVENT_LISTENER(connect);
  DEFINE_ATTRIBUTE_EVENT_LISTENER(disconnect);

  DECLARE_VIRTUAL_TRACE();
  DECLARE_VIRTUAL_TRACE_WRAPPERS();

 private:
  friend class V8RemotePlayback;
  friend class RemotePlaybackTest;
  friend class MediaControlsImplTest;

  explicit RemotePlayback(HTMLMediaElement&);

  // Calls the specified availability callback with the current availability.
  // Need a void() method to post it as a task.
  void NotifyInitialAvailability(int callback_id);

  WebRemotePlaybackState state_;
  WebRemotePlaybackAvailability availability_;
  HeapHashMap<int, TraceWrapperMember<AvailabilityCallbackWrapper>>
      availability_callbacks_;
  Member<HTMLMediaElement> media_element_;
  Member<ScriptPromiseResolver> prompt_promise_resolver_;
};

}  // namespace blink

#endif  // RemotePlayback_h
