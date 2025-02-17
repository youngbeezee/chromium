// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/input/scrollbar_animation_controller.h"

#include <algorithm>

#include "base/time/time.h"
#include "cc/trees/layer_tree_impl.h"

namespace cc {

std::unique_ptr<ScrollbarAnimationController>
ScrollbarAnimationController::CreateScrollbarAnimationControllerAndroid(
    ElementId scroll_element_id,
    ScrollbarAnimationControllerClient* client,
    base::TimeDelta fade_delay,
    base::TimeDelta fade_duration) {
  return base::WrapUnique(new ScrollbarAnimationController(
      scroll_element_id, client, fade_delay, fade_duration));
}

std::unique_ptr<ScrollbarAnimationController>
ScrollbarAnimationController::CreateScrollbarAnimationControllerAuraOverlay(
    ElementId scroll_element_id,
    ScrollbarAnimationControllerClient* client,
    base::TimeDelta fade_delay,
    base::TimeDelta fade_duration,
    base::TimeDelta thinning_duration) {
  return base::WrapUnique(new ScrollbarAnimationController(
      scroll_element_id, client, fade_delay, fade_duration, thinning_duration));
}

ScrollbarAnimationController::ScrollbarAnimationController(
    ElementId scroll_element_id,
    ScrollbarAnimationControllerClient* client,
    base::TimeDelta fade_delay,
    base::TimeDelta fade_duration)
    : client_(client),
      fade_delay_(fade_delay),
      fade_duration_(fade_duration),
      need_trigger_scrollbar_show_(false),
      is_animating_(false),
      animation_change_(NONE),
      scroll_element_id_(scroll_element_id),
      currently_scrolling_(false),
      show_in_fast_scroll_(false),
      opacity_(0.0f),
      show_scrollbars_on_scroll_gesture_(false),
      need_thinning_animation_(false),
      weak_factory_(this) {
  ApplyOpacityToScrollbars(0.0f);
}

ScrollbarAnimationController::ScrollbarAnimationController(
    ElementId scroll_element_id,
    ScrollbarAnimationControllerClient* client,
    base::TimeDelta fade_delay,
    base::TimeDelta fade_duration,
    base::TimeDelta thinning_duration)
    : client_(client),
      fade_delay_(fade_delay),
      fade_duration_(fade_duration),
      need_trigger_scrollbar_show_(false),
      is_animating_(false),
      animation_change_(NONE),
      scroll_element_id_(scroll_element_id),
      currently_scrolling_(false),
      show_in_fast_scroll_(false),
      opacity_(0.0f),
      show_scrollbars_on_scroll_gesture_(true),
      need_thinning_animation_(true),
      weak_factory_(this) {
  vertical_controller_ = SingleScrollbarAnimationControllerThinning::Create(
      scroll_element_id, ScrollbarOrientation::VERTICAL, client,
      thinning_duration);
  horizontal_controller_ = SingleScrollbarAnimationControllerThinning::Create(
      scroll_element_id, ScrollbarOrientation::HORIZONTAL, client,
      thinning_duration);
  ApplyOpacityToScrollbars(0.0f);
}

ScrollbarAnimationController::~ScrollbarAnimationController() {}

ScrollbarSet ScrollbarAnimationController::Scrollbars() const {
  return client_->ScrollbarsFor(scroll_element_id_);
}

SingleScrollbarAnimationControllerThinning&
ScrollbarAnimationController::GetScrollbarAnimationController(
    ScrollbarOrientation orientation) const {
  DCHECK(need_thinning_animation_);
  if (orientation == ScrollbarOrientation::VERTICAL)
    return *(vertical_controller_.get());
  else
    return *(horizontal_controller_.get());
}

void ScrollbarAnimationController::StartAnimation() {
  DCHECK(animation_change_ != NONE);
  delayed_scrollbar_animation_.Cancel();
  is_animating_ = true;
  last_awaken_time_ = base::TimeTicks();
  client_->SetNeedsAnimateForScrollbarAnimation();
}

void ScrollbarAnimationController::StopAnimation() {
  delayed_scrollbar_animation_.Cancel();
  is_animating_ = false;
  animation_change_ = NONE;
}

void ScrollbarAnimationController::PostDelayedAnimation(
    AnimationChange animation_change) {
  animation_change_ = animation_change;
  delayed_scrollbar_animation_.Cancel();
  delayed_scrollbar_animation_.Reset(
      base::Bind(&ScrollbarAnimationController::StartAnimation,
                 weak_factory_.GetWeakPtr()));
  client_->PostDelayedScrollbarAnimationTask(
      delayed_scrollbar_animation_.callback(), fade_delay_);
}

bool ScrollbarAnimationController::Animate(base::TimeTicks now) {
  bool animated = false;

  if (is_animating_) {
    DCHECK(animation_change_ != NONE);
    if (last_awaken_time_.is_null())
      last_awaken_time_ = now;

    float progress = AnimationProgressAtTime(now);
    RunAnimationFrame(progress);

    if (is_animating_)
      client_->SetNeedsAnimateForScrollbarAnimation();
    animated = true;
  }

  if (need_thinning_animation_) {
    animated |= vertical_controller_->Animate(now);
    animated |= horizontal_controller_->Animate(now);
  }

  return animated;
}

float ScrollbarAnimationController::AnimationProgressAtTime(
    base::TimeTicks now) {
  base::TimeDelta delta = now - last_awaken_time_;
  float progress = delta.InSecondsF() / fade_duration_.InSecondsF();
  return std::max(std::min(progress, 1.f), 0.f);
}

void ScrollbarAnimationController::RunAnimationFrame(float progress) {
  float opacity;

  DCHECK(animation_change_ != NONE);
  if (animation_change_ == FADE_IN) {
    opacity = std::max(progress, opacity_);
  } else {
    opacity = std::min(1.f - progress, opacity_);
  }

  ApplyOpacityToScrollbars(opacity);
  if (progress == 1.f)
    StopAnimation();
}

void ScrollbarAnimationController::DidScrollBegin() {
  currently_scrolling_ = true;
}

void ScrollbarAnimationController::DidScrollEnd() {
  bool has_scrolled = show_in_fast_scroll_;
  show_in_fast_scroll_ = false;

  currently_scrolling_ = false;

  // We don't fade out scrollbar if they need thinning animation and mouse is
  // near.
  if (need_thinning_animation_ && MouseIsNearAnyScrollbar())
    return;

  if (has_scrolled)
    PostDelayedAnimation(FADE_OUT);
}

void ScrollbarAnimationController::DidScrollUpdate() {
  if (need_thinning_animation_ && Captured())
    return;

  StopAnimation();

  Show();

  // As an optimization, we avoid spamming fade delay tasks during active fast
  // scrolls.  But if we're not within one, we need to post every scroll update.
  if (!currently_scrolling_) {
    // We don't fade out scrollbar if they need thinning animation and mouse is
    // near.
    if (!need_thinning_animation_ || !MouseIsNearAnyScrollbar())
      PostDelayedAnimation(FADE_OUT);
  } else {
    show_in_fast_scroll_ = true;
  }

  if (need_thinning_animation_) {
    vertical_controller_->UpdateThumbThicknessScale();
    horizontal_controller_->UpdateThumbThicknessScale();
  }
}

void ScrollbarAnimationController::WillUpdateScroll() {
  if (show_scrollbars_on_scroll_gesture_)
    DidScrollUpdate();
}

void ScrollbarAnimationController::DidRequestShowFromMainThread() {
  DidScrollUpdate();
}

void ScrollbarAnimationController::DidMouseDown() {
  if (!need_thinning_animation_ || ScrollbarsHidden())
    return;

  vertical_controller_->DidMouseDown();
  horizontal_controller_->DidMouseDown();
}

void ScrollbarAnimationController::DidMouseUp() {
  if (!need_thinning_animation_ || !Captured())
    return;

  vertical_controller_->DidMouseUp();
  horizontal_controller_->DidMouseUp();

  if (!MouseIsNearAnyScrollbar())
    PostDelayedAnimation(FADE_OUT);
}

void ScrollbarAnimationController::DidMouseLeave() {
  if (!need_thinning_animation_)
    return;

  vertical_controller_->DidMouseLeave();
  horizontal_controller_->DidMouseLeave();

  delayed_scrollbar_animation_.Cancel();
  need_trigger_scrollbar_show_ = false;

  if (ScrollbarsHidden() || Captured())
    return;

  PostDelayedAnimation(FADE_OUT);
}

void ScrollbarAnimationController::DidMouseMoveNear(
    ScrollbarOrientation orientation,
    float distance) {
  if (!need_thinning_animation_)
    return;

  bool need_trigger_scrollbar_show_before = need_trigger_scrollbar_show_;

  GetScrollbarAnimationController(orientation).DidMouseMoveNear(distance);

  need_trigger_scrollbar_show_ =
      CalcNeedTriggerScrollbarShow(orientation, distance);

  if (Captured())
    return;

  if (ScrollbarsHidden()) {
    if (need_trigger_scrollbar_show_before != need_trigger_scrollbar_show_) {
      if (need_trigger_scrollbar_show_) {
        PostDelayedAnimation(FADE_IN);
      } else {
        delayed_scrollbar_animation_.Cancel();
      }
    }
  } else {
    if (MouseIsNearAnyScrollbar()) {
      Show();
      StopAnimation();
    } else if (!is_animating_) {
      PostDelayedAnimation(FADE_OUT);
    }
  }
}

bool ScrollbarAnimationController::CalcNeedTriggerScrollbarShow(
    ScrollbarOrientation orientation,
    float distance) const {
  DCHECK(need_thinning_animation_);

  if (vertical_controller_->mouse_is_over_scrollbar() ||
      horizontal_controller_->mouse_is_over_scrollbar())
    return true;

  for (ScrollbarLayerImplBase* scrollbar : Scrollbars()) {
    if (scrollbar->orientation() != orientation)
      continue;

    if (distance < kMouseMoveDistanceToTriggerFadeIn)
      return true;
  }

  return false;
}

bool ScrollbarAnimationController::MouseIsOverScrollbar(
    ScrollbarOrientation orientation) const {
  DCHECK(need_thinning_animation_);
  return GetScrollbarAnimationController(orientation).mouse_is_over_scrollbar();
}

bool ScrollbarAnimationController::MouseIsNearScrollbar(
    ScrollbarOrientation orientation) const {
  DCHECK(need_thinning_animation_);
  return GetScrollbarAnimationController(orientation).mouse_is_near_scrollbar();
}

bool ScrollbarAnimationController::MouseIsNearAnyScrollbar() const {
  DCHECK(need_thinning_animation_);
  return vertical_controller_->mouse_is_near_scrollbar() ||
         horizontal_controller_->mouse_is_near_scrollbar();
}

bool ScrollbarAnimationController::ScrollbarsHidden() const {
  return opacity_ == 0.0f;
}

bool ScrollbarAnimationController::Captured() const {
  DCHECK(need_thinning_animation_);
  return vertical_controller_->captured() || horizontal_controller_->captured();
}

void ScrollbarAnimationController::Show() {
  delayed_scrollbar_animation_.Cancel();
  ApplyOpacityToScrollbars(1.0f);
}

void ScrollbarAnimationController::ApplyOpacityToScrollbars(float opacity) {
  for (ScrollbarLayerImplBase* scrollbar : Scrollbars()) {
    if (!scrollbar->is_overlay_scrollbar())
      continue;
    float effective_opacity = scrollbar->CanScrollOrientation() ? opacity : 0;
    scrollbar->SetOverlayScrollbarLayerOpacityAnimated(effective_opacity);
  }

  bool previouslyVisible = opacity_ > 0.0f;
  bool currentlyVisible = opacity > 0.0f;

  if (opacity_ != opacity)
    client_->SetNeedsRedrawForScrollbarAnimation();

  opacity_ = opacity;

  if (previouslyVisible != currentlyVisible)
    client_->DidChangeScrollbarVisibility();
}

}  // namespace cc
