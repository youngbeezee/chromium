// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/trees/debug_rect_history.h"

#include <stddef.h>

#include "base/memory/ptr_util.h"
#include "cc/base/math_util.h"
#include "cc/layers/layer_impl.h"
#include "cc/layers/layer_list_iterator.h"
#include "cc/layers/layer_utils.h"
#include "cc/layers/render_surface_impl.h"
#include "cc/trees/damage_tracker.h"
#include "cc/trees/layer_tree_host.h"
#include "cc/trees/layer_tree_host_common.h"
#include "cc/trees/layer_tree_impl.h"
#include "ui/gfx/geometry/rect_conversions.h"

namespace cc {

// static
std::unique_ptr<DebugRectHistory> DebugRectHistory::Create() {
  return base::WrapUnique(new DebugRectHistory());
}

DebugRectHistory::DebugRectHistory() {}

DebugRectHistory::~DebugRectHistory() {}

void DebugRectHistory::SaveDebugRectsForCurrentFrame(
    LayerTreeImpl* tree_impl,
    LayerImpl* hud_layer,
    const RenderSurfaceList& render_surface_list,
    const LayerTreeDebugState& debug_state) {
  // For now, clear all rects from previous frames. In the future we may want to
  // store all debug rects for a history of many frames.
  debug_rects_.clear();

  if (debug_state.show_touch_event_handler_rects)
    SaveTouchEventHandlerRects(tree_impl);

  if (debug_state.show_wheel_event_handler_rects)
    SaveWheelEventHandlerRects(tree_impl);

  if (debug_state.show_scroll_event_handler_rects)
    SaveScrollEventHandlerRects(tree_impl);

  if (debug_state.show_non_fast_scrollable_rects)
    SaveNonFastScrollableRects(tree_impl);

  if (debug_state.show_paint_rects)
    SavePaintRects(tree_impl);

  if (debug_state.show_property_changed_rects)
    SavePropertyChangedRects(tree_impl, hud_layer);

  if (debug_state.show_surface_damage_rects)
    SaveSurfaceDamageRects(render_surface_list);

  if (debug_state.show_screen_space_rects)
    SaveScreenSpaceRects(render_surface_list);

  if (debug_state.show_layer_animation_bounds_rects)
    SaveLayerAnimationBoundsRects(tree_impl);
}

void DebugRectHistory::SavePaintRects(LayerTreeImpl* tree_impl) {
  // We would like to visualize where any layer's paint rect (update rect) has
  // changed, regardless of whether this layer is skipped for actual drawing or
  // not. Therefore we traverse over all layers, not just the render surface
  // list.
  for (auto* layer : *tree_impl) {
    Region invalidation_region = layer->GetInvalidationRegionForDebugging();
    if (invalidation_region.IsEmpty() || !layer->DrawsContent())
      continue;

    for (Region::Iterator it(invalidation_region); it.has_rect(); it.next()) {
      debug_rects_.push_back(DebugRect(
          PAINT_RECT_TYPE, MathUtil::MapEnclosingClippedRect(
                               layer->ScreenSpaceTransform(), it.rect())));
    }
  }
}

void DebugRectHistory::SavePropertyChangedRects(LayerTreeImpl* tree_impl,
                                                LayerImpl* hud_layer) {
  for (LayerImpl* layer : *tree_impl) {
    if (layer == hud_layer)
      continue;

    if (!layer->LayerPropertyChanged())
      continue;

    debug_rects_.push_back(DebugRect(
        PROPERTY_CHANGED_RECT_TYPE,
        MathUtil::MapEnclosingClippedRect(layer->ScreenSpaceTransform(),
                                          gfx::Rect(layer->bounds()))));
  }
}

void DebugRectHistory::SaveSurfaceDamageRects(
    const RenderSurfaceList& render_surface_list) {
  for (size_t i = 0; i < render_surface_list.size(); ++i) {
    size_t surface_index = render_surface_list.size() - 1 - i;
    RenderSurfaceImpl* render_surface = render_surface_list[surface_index];
    DCHECK(render_surface);

    debug_rects_.push_back(DebugRect(
        SURFACE_DAMAGE_RECT_TYPE, MathUtil::MapEnclosingClippedRect(
                                      render_surface->screen_space_transform(),
                                      render_surface->GetDamageRect())));
  }
}

void DebugRectHistory::SaveScreenSpaceRects(
    const RenderSurfaceList& render_surface_list) {
  for (size_t i = 0; i < render_surface_list.size(); ++i) {
    size_t surface_index = render_surface_list.size() - 1 - i;
    RenderSurfaceImpl* render_surface = render_surface_list[surface_index];
    DCHECK(render_surface);

    debug_rects_.push_back(DebugRect(
        SCREEN_SPACE_RECT_TYPE, MathUtil::MapEnclosingClippedRect(
                                    render_surface->screen_space_transform(),
                                    render_surface->content_rect())));
  }
}

void DebugRectHistory::SaveTouchEventHandlerRects(LayerTreeImpl* tree_impl) {
  LayerTreeHostCommon::CallFunctionForEveryLayer(
      tree_impl,
      [this](LayerImpl* layer) { SaveTouchEventHandlerRectsCallback(layer); });
}

void DebugRectHistory::SaveTouchEventHandlerRectsCallback(LayerImpl* layer) {
  for (Region::Iterator iter(layer->touch_event_handler_region());
       iter.has_rect(); iter.next()) {
    debug_rects_.push_back(
        DebugRect(TOUCH_EVENT_HANDLER_RECT_TYPE,
                  MathUtil::MapEnclosingClippedRect(
                      layer->ScreenSpaceTransform(), iter.rect())));
  }
}

void DebugRectHistory::SaveWheelEventHandlerRects(LayerTreeImpl* tree_impl) {
  EventListenerProperties event_properties =
      tree_impl->event_listener_properties(EventListenerClass::kMouseWheel);
  if (event_properties == EventListenerProperties::kNone ||
      event_properties == EventListenerProperties::kPassive) {
    return;
  }

  // Since the wheel event handlers property is on the entire layer tree just
  // mark inner viewport if have listeners.
  LayerImpl* inner_viewport = tree_impl->InnerViewportScrollLayer();
  if (!inner_viewport)
    return;
  debug_rects_.push_back(DebugRect(
      WHEEL_EVENT_HANDLER_RECT_TYPE,
      MathUtil::MapEnclosingClippedRect(inner_viewport->ScreenSpaceTransform(),
                                        gfx::Rect(inner_viewport->bounds()))));
}

void DebugRectHistory::SaveScrollEventHandlerRects(LayerTreeImpl* tree_impl) {
  LayerTreeHostCommon::CallFunctionForEveryLayer(
      tree_impl,
      [this](LayerImpl* layer) { SaveScrollEventHandlerRectsCallback(layer); });
}

void DebugRectHistory::SaveScrollEventHandlerRectsCallback(LayerImpl* layer) {
  if (!layer->layer_tree_impl()->have_scroll_event_handlers())
    return;

  debug_rects_.push_back(
      DebugRect(SCROLL_EVENT_HANDLER_RECT_TYPE,
                MathUtil::MapEnclosingClippedRect(layer->ScreenSpaceTransform(),
                                                  gfx::Rect(layer->bounds()))));
}

void DebugRectHistory::SaveNonFastScrollableRects(LayerTreeImpl* tree_impl) {
  LayerTreeHostCommon::CallFunctionForEveryLayer(
      tree_impl,
      [this](LayerImpl* layer) { SaveNonFastScrollableRectsCallback(layer); });
}

void DebugRectHistory::SaveNonFastScrollableRectsCallback(LayerImpl* layer) {
  for (Region::Iterator iter(layer->non_fast_scrollable_region());
       iter.has_rect(); iter.next()) {
    debug_rects_.push_back(
        DebugRect(NON_FAST_SCROLLABLE_RECT_TYPE,
                  MathUtil::MapEnclosingClippedRect(
                      layer->ScreenSpaceTransform(), iter.rect())));
  }
}

void DebugRectHistory::SaveLayerAnimationBoundsRects(LayerTreeImpl* tree_impl) {
  for (auto it = tree_impl->rbegin(); it != tree_impl->rend(); ++it) {
    if (!(*it)->is_drawn_render_surface_layer_list_member())
      continue;

    // TODO(avallee): Figure out if we should show something for a layer who's
    // animating bounds but that we can't compute them.
    gfx::BoxF inflated_bounds;
    if (!LayerUtils::GetAnimationBounds(**it, &inflated_bounds))
      continue;

    debug_rects_.push_back(
        DebugRect(ANIMATION_BOUNDS_RECT_TYPE,
                  gfx::ToEnclosingRect(gfx::RectF(
                      inflated_bounds.x(), inflated_bounds.y(),
                      inflated_bounds.width(), inflated_bounds.height()))));
  }
}

}  // namespace cc
