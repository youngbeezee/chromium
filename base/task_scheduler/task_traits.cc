// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_scheduler/task_traits.h"

#include <stddef.h>

#include <ostream>

#include "base/logging.h"

namespace base {

TaskTraits::TaskTraits() = default;
TaskTraits::~TaskTraits() = default;

TaskTraits& TaskTraits::MayBlock() {
  may_block_ = true;
  return *this;
}

TaskTraits& TaskTraits::WithBaseSyncPrimitives() {
  with_base_sync_primitives_ = true;
  return *this;
}

TaskTraits& TaskTraits::WithPriority(TaskPriority priority) {
  priority_set_explicitly_ = true;
  priority_ = priority;
  return *this;
}

TaskTraits& TaskTraits::WithShutdownBehavior(
    TaskShutdownBehavior shutdown_behavior) {
  shutdown_behavior_ = shutdown_behavior;
  return *this;
}

const char* TaskPriorityToString(TaskPriority task_priority) {
  switch (task_priority) {
    case TaskPriority::BACKGROUND:
      return "BACKGROUND";
    case TaskPriority::USER_VISIBLE:
      return "USER_VISIBLE";
    case TaskPriority::USER_BLOCKING:
      return "USER_BLOCKING";
  }
  NOTREACHED();
  return "";
}

const char* TaskShutdownBehaviorToString(
    TaskShutdownBehavior shutdown_behavior) {
  switch (shutdown_behavior) {
    case TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN:
      return "CONTINUE_ON_SHUTDOWN";
    case TaskShutdownBehavior::SKIP_ON_SHUTDOWN:
      return "SKIP_ON_SHUTDOWN";
    case TaskShutdownBehavior::BLOCK_SHUTDOWN:
      return "BLOCK_SHUTDOWN";
  }
  NOTREACHED();
  return "";
}

std::ostream& operator<<(std::ostream& os, const TaskPriority& task_priority) {
  os << TaskPriorityToString(task_priority);
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const TaskShutdownBehavior& shutdown_behavior) {
  os << TaskShutdownBehaviorToString(shutdown_behavior);
  return os;
}

}  // namespace base
