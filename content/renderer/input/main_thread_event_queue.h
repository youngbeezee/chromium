// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_INPUT_MAIN_THREAD_EVENT_QUEUE_H_
#define CONTENT_RENDERER_INPUT_MAIN_THREAD_EVENT_QUEUE_H_

#include <deque>
#include "base/feature_list.h"
#include "base/memory/weak_ptr.h"
#include "content/common/content_export.h"
#include "content/common/input/input_event_ack_state.h"
#include "content/common/input/input_event_dispatch_type.h"
#include "content/public/common/content_features.h"
#include "content/renderer/input/main_thread_event_queue_task_list.h"
#include "content/renderer/input/scoped_web_input_event_with_latency_info.h"
#include "third_party/WebKit/public/platform/WebInputEvent.h"
#include "third_party/WebKit/public/platform/scheduler/renderer/renderer_scheduler.h"
#include "ui/events/blink/web_input_event_traits.h"
#include "ui/latency/latency_info.h"

namespace content {

// All interaction with the MainThreadEventQueueClient will occur
// on the main thread.
class CONTENT_EXPORT MainThreadEventQueueClient {
 public:
  // Handle an |event| that was previously queued (possibly
  // coalesced with another event). Implementors must implement
  // this callback.
  virtual InputEventAckState HandleInputEvent(
      const blink::WebCoalescedInputEvent& event,
      const ui::LatencyInfo& latency_info,
      InputEventDispatchType dispatch_type) = 0;

  virtual void SendInputEventAck(blink::WebInputEvent::Type type,
                                 InputEventAckState ack_result,
                                 uint32_t touch_event_id) = 0;
  virtual void SetNeedsMainFrame() = 0;
};

// MainThreadEventQueue implements a queue for events that need to be
// queued between the compositor and main threads. This queue is managed
// by a lock where events are enqueued by the compositor thread
// and dequeued by the main thread.
//
// Below some example flows are how the code behaves.
// Legend: B=Browser, C=Compositor, M=Main Thread, NB=Non-blocking
//         BL=Blocking, PT=Post Task, ACK=Acknowledgement
//
// Normal blocking event sent to main thread.
//   B        C        M
//   ---(BL)-->
//         (queue)
//            ---(PT)-->
//                  (deque)
//   <-------(ACK)------
//
// Non-blocking event sent to main thread.
//   B        C        M
//   ---(NB)-->
//         (queue)
//            ---(PT)-->
//                  (deque)
//
// Non-blocking followed by blocking event sent to main thread.
//   B        C        M
//   ---(NB)-->
//         (queue)
//            ---(PT)-->
//   ---(BL)-->
//         (queue)
//            ---(PT)-->
//                  (deque)
//                  (deque)
//   <-------(ACK)------
//
class CONTENT_EXPORT MainThreadEventQueue
    : public base::RefCountedThreadSafe<MainThreadEventQueue> {
 public:
  MainThreadEventQueue(
      MainThreadEventQueueClient* client,
      const scoped_refptr<base::SingleThreadTaskRunner>& main_task_runner,
      blink::scheduler::RendererScheduler* renderer_scheduler,
      bool allow_raf_aligned_input);

  // Called once the compositor has handled |event| and indicated that it is
  // a non-blocking event to be queued to the main thread.
  bool HandleEvent(ui::WebScopedInputEvent event,
                   const ui::LatencyInfo& latency,
                   InputEventDispatchType dispatch_type,
                   InputEventAckState ack_result);
  void DispatchRafAlignedInput(base::TimeTicks frame_time);
  void QueueClosure(const base::Closure& closure);

  void ClearClient();

 protected:
  friend class base::RefCountedThreadSafe<MainThreadEventQueue>;
  virtual ~MainThreadEventQueue();
  void QueueEvent(std::unique_ptr<MainThreadEventQueueTask> event);
  void PostTaskToMainThread();
  void DispatchEvents();
  void PossiblyScheduleMainFrame();
  void SetNeedsMainFrame();
  InputEventAckState HandleEventOnMainThread(
      const blink::WebCoalescedInputEvent& event,
      const ui::LatencyInfo& latency,
      InputEventDispatchType dispatch_type);
  void SendInputEventAck(const blink::WebInputEvent& event,
                         InputEventAckState ack_result,
                         uint32_t touch_event_id);

  void SendEventToMainThread(const blink::WebInputEvent* event,
                             const ui::LatencyInfo& latency,
                             InputEventDispatchType original_dispatch_type);

  bool IsRafAlignedInputDisabled() const;
  bool IsRafAlignedEvent(
      const std::unique_ptr<MainThreadEventQueueTask>& item) const;

  friend class QueuedWebInputEvent;
  friend class MainThreadEventQueueTest;
  friend class MainThreadEventQueueInitializationTest;
  MainThreadEventQueueClient* client_;
  bool last_touch_start_forced_nonblocking_due_to_fling_;
  bool enable_fling_passive_listener_flag_;
  bool enable_non_blocking_due_to_main_thread_responsiveness_flag_;
  base::TimeDelta main_thread_responsiveness_threshold_;
  bool handle_raf_aligned_touch_input_;
  bool handle_raf_aligned_mouse_input_;

  // Contains data to be shared between main thread and compositor thread.
  struct SharedState {
    SharedState();
    ~SharedState();

    MainThreadEventQueueTaskList events_;
    // A BeginMainFrame has been requested but not received yet.
    bool sent_main_frame_request_;
    // A PostTask to the main thread has been sent but not executed yet.
    bool sent_post_task_;
    base::TimeTicks last_async_touch_move_timestamp_;
  };

  // Lock used to serialize |shared_state_|.
  base::Lock shared_state_lock_;
  SharedState shared_state_;

  scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;
  blink::scheduler::RendererScheduler* renderer_scheduler_;

  DISALLOW_COPY_AND_ASSIGN(MainThreadEventQueue);
};

}  // namespace content

#endif  // CONTENT_RENDERER_INPUT_MAIN_THREAD_EVENT_QUEUE_H_
