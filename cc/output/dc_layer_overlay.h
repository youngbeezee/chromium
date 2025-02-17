// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_OUTPUT_DC_LAYER_OVERLAY_H_
#define CC_OUTPUT_DC_LAYER_OVERLAY_H_

#include "base/memory/ref_counted.h"
#include "cc/quads/render_pass.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkMatrix44.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gl/dc_renderer_layer_params.h"

namespace cc {
class DrawQuad;
class ResourceProvider;

class CC_EXPORT DCLayerOverlaySharedState
    : public base::RefCounted<DCLayerOverlaySharedState> {
 public:
  DCLayerOverlaySharedState() {}
  int z_order = 0;
  // If |is_clipped| is true, then clip to |clip_rect| in the target space.
  bool is_clipped = false;
  gfx::RectF clip_rect;
  // The opacity property for the CAayer.
  float opacity = 1;
  // The transform to apply to the DCLayer.
  SkMatrix44 transform = SkMatrix44(SkMatrix44::kIdentity_Constructor);

 private:
  friend class base::RefCounted<DCLayerOverlaySharedState>;
  ~DCLayerOverlaySharedState() {}
};

// Holds all information necessary to construct a DCLayer from a DrawQuad.
class CC_EXPORT DCLayerOverlay {
 public:
  DCLayerOverlay();
  DCLayerOverlay(const DCLayerOverlay& other);
  ~DCLayerOverlay();

  // State that is frequently shared between consecutive DCLayerOverlays.
  scoped_refptr<DCLayerOverlaySharedState> shared_state;

  // Resource ids that correspond to the DXGI textures to set as the contents
  // of the DCLayer.
  DrawQuad::Resources resources;
  // The contents rect property for the DCLayer.
  gfx::RectF contents_rect;
  // The bounds for the DCLayer in pixels.
  gfx::RectF bounds_rect;
  // The background color property for the DCLayer.
  SkColor background_color = SK_ColorTRANSPARENT;
  // The edge anti-aliasing mask property for the DCLayer.
  unsigned edge_aa_mask = 0;
  // The minification and magnification filters for the DCLayer.
  unsigned filter;
  // If |rpdq| is present, then the renderer must draw the filter effects and
  // copy the result into an IOSurface.
  const RenderPassDrawQuad* rpdq = nullptr;
};

typedef std::vector<DCLayerOverlay> DCLayerOverlayList;

class DCLayerOverlayProcessor {
 public:
  enum DCLayerResult {
    DC_LAYER_SUCCESS,
    DC_LAYER_FAILED_QUAD_BLEND_MODE,
    DC_LAYER_FAILED_TEXTURE_NOT_CANDIDATE,
    DC_LAYER_FAILED_OCCLUDED,
    DC_LAYER_FAILED_UNKNOWN
  };

  void Process(ResourceProvider* resource_provider,
               const gfx::RectF& display_rect,
               QuadList* quad_list,
               gfx::Rect* overlay_damage_rect,
               gfx::Rect* damage_rect,
               DCLayerOverlayList* ca_layer_overlays);
  void ClearOverlayState() {
    previous_frame_underlay_rect_ = gfx::Rect();
    previous_occlusion_bounding_box_ = gfx::Rect();
  }

 private:
  DCLayerResult FromDrawQuad(ResourceProvider* resource_provider,
                             const gfx::RectF& display_rect,
                             QuadList::ConstIterator quad_list_begin,
                             QuadList::ConstIterator quad,
                             DCLayerOverlay* ca_layer_overlay);

  gfx::Rect previous_frame_underlay_rect_;
  gfx::Rect previous_occlusion_bounding_box_;
  gfx::RectF previous_display_rect_;
};

}  // namespace cc

#endif  // CC_OUTPUT_DC_LAYER_OVERLAY_H_
