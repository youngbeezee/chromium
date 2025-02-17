// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_WEBKIT_SOURCE_PLATFORM_SCHEDULER_RENDERER_WEB_VIEW_SCHEDULER_H_
#define THIRD_PARTY_WEBKIT_SOURCE_PLATFORM_SCHEDULER_RENDERER_WEB_VIEW_SCHEDULER_H_

#include "platform/PlatformExport.h"
#include "platform/wtf/Functional.h"
#include "public/platform/BlameContext.h"

#include <memory>

namespace blink {

class WebFrameScheduler;

class PLATFORM_EXPORT WebViewScheduler {
 public:
  // Helper interface to plumb settings from WebSettings to scheduler.
  class PLATFORM_EXPORT WebViewSchedulerSettings {
   public:
    virtual ~WebViewSchedulerSettings() {}

    // Background throttling aggressiveness settings.
    virtual float ExpensiveBackgroundThrottlingCPUBudget() = 0;
    virtual float ExpensiveBackgroundThrottlingInitialBudget() = 0;
    virtual float ExpensiveBackgroundThrottlingMaxBudget() = 0;
    virtual float ExpensiveBackgroundThrottlingMaxDelay() = 0;
  };

  virtual ~WebViewScheduler() {}

  // The scheduler may throttle tasks associated with background pages.
  virtual void SetPageVisible(bool) = 0;

  // Creates a new WebFrameScheduler. The caller is responsible for deleting
  // it. All tasks executed by the frame scheduler will be attributed to
  // |BlameContext|.
  virtual std::unique_ptr<WebFrameScheduler> CreateFrameScheduler(
      BlameContext*) = 0;

  // Instructs this WebViewScheduler to use virtual time. When virtual time is
  // enabled the system doesn't actually sleep for the delays between tasks
  // before executing them. E.g: A-E are delayed tasks
  //
  // |    A   B C  D           E  (normal)
  // |-----------------------------> time
  //
  // |ABCDE                       (virtual time)
  // |-----------------------------> time
  virtual void EnableVirtualTime() = 0;

  // Disables virtual time. Note that this is only used for testing, because
  // there's no reason to do this in production.
  virtual void DisableVirtualTimeForTesting() = 0;

  // Returns true if virtual time is currently allowed to advance.
  virtual bool VirtualTimeAllowedToAdvance() const = 0;

  enum class VirtualTimePolicy {
    // In this policy virtual time is allowed to advance. If the blink scheduler
    // runs out of immediate work, the virtual timebase will be incremented so
    // that the next sceduled timer may fire.  NOTE Tasks will be run in time
    // order (as usual).
    ADVANCE,

    // In this policy virtual time is not allowed to advance. Delayed tasks
    // posted to WebTaskRunners owned by any child WebFrameSchedulers will be
    // paused, unless their scheduled run time is less than or equal to the
    // current virtual time.  Note non-delayed tasks will run as normal.
    PAUSE,

    // In this policy virtual time is allowed to advance unless there are
    // pending network fetches associated any child WebFrameScheduler, or a
    // document is being parsed on a background thread. Initially virtual time
    // is not allowed to advance until we have seen at least one load. The aim
    // being to try and make loading (more) deterministic.
    DETERMINISTIC_LOADING
  };

  // Sets the virtual time policy, which is applied imemdiatly to all child
  // WebFrameSchedulers.
  virtual void SetVirtualTimePolicy(VirtualTimePolicy) = 0;

  virtual void AudioStateChanged(bool is_audio_playing) = 0;

  virtual bool HasActiveConnectionForTest() const = 0;
};

}  // namespace blink

#endif  // THIRD_PARTY_WEBKIT_SOURCE_PLATFORM_SCHEDULER_RENDERER_WEB_VIEW_SCHEDULER_H_
