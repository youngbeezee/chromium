// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.#ifndef WebViewBase_h

#ifndef WebViewBase_h
#define WebViewBase_h

#include "platform/transforms/TransformationMatrix.h"
#include "platform/wtf/RefCounted.h"
#include "public/platform/WebDisplayMode.h"
#include "public/platform/WebInputEventResult.h"
#include "public/web/WebElement.h"
#include "public/web/WebView.h"

namespace blink {

class AnimationWorkletProxyClient;
class CompositorAnimationHost;
class CompositorWorkerProxyClient;
class CompositorAnimationTimeline;
class ContextMenuProvider;
class DevToolsEmulator;
class Frame;
class GraphicsLayer;
class HitTestResult;
class Page;
class PaintLayerCompositor;
class PagePopup;
class PagePopupClient;
class PageScaleConstraintsSet;
class WebInputEvent;
class WebInputMethodControllerImpl;
class WebKeyboardEvent;
class WebLayer;
class WebLocalFrameImpl;
class WebLayerTreeView;
class WebPagePopupImpl;
class WebSettingsImpl;
struct WebRect;

// WebViewBase is a temporary class introduced to decouple the defintion of
// WebViewImpl from the concrete implementation. It was not possible to move the
// defintion of these methods to WebView as we did not want to pollute the
// public API surface area.
//
// Once WebViewImpl is moved from web to core/exported then this class should be
// removed and clients can once again depend on WebViewImpl.
class WebViewBase : public WebView, public RefCounted<WebViewBase> {
 public:
  virtual ~WebViewBase() {}

  virtual void SetBaseBackgroundColor(WebColor) = 0;
  virtual void SetBaseBackgroundColorOverride(WebColor) = 0;
  virtual void ClearBaseBackgroundColorOverride() = 0;
  virtual void SetBackgroundColorOverride(WebColor) = 0;
  virtual void ClearBackgroundColorOverride() = 0;
  virtual void SetZoomFactorOverride(float) = 0;
  virtual void SetCompositorDeviceScaleFactorOverride(float) = 0;
  virtual void SetDeviceEmulationTransform(const TransformationMatrix&) = 0;
  virtual void SetShowDebugBorders(bool) = 0;

  virtual Page* GetPage() const = 0;
  virtual Frame* FocusedCoreFrame() const = 0;
  static WebViewBase* FromPage(Page*);

  // Returns the main frame associated with this view. This may be null when
  // the page is shutting down, but will be valid at all other times.
  virtual WebLocalFrameImpl* MainFrameImpl() const = 0;

  virtual float DefaultMinimumPageScaleFactor() const = 0;
  virtual float DefaultMaximumPageScaleFactor() const = 0;
  virtual float MinimumPageScaleFactor() const = 0;
  virtual float MaximumPageScaleFactor() const = 0;
  virtual float ClampPageScaleFactorToLimits(float) const = 0;
  virtual void ResetScaleStateImmediately() = 0;

  virtual WebLayerTreeView* LayerTreeView() const = 0;
  virtual WebViewClient* Client() = 0;

  virtual void ZoomToFindInPageRect(const WebRect&) = 0;

  virtual PageScaleConstraintsSet& GetPageScaleConstraintsSet() const = 0;
  virtual Color BaseBackgroundColor() const = 0;
  virtual bool BackgroundColorOverrideEnabled() const = 0;
  virtual WebColor BackgroundColorOverride() const = 0;

  virtual void DidChangeContentsSize() = 0;
  virtual void PageScaleFactorChanged() = 0;
  virtual void MainFrameScrollOffsetChanged() = 0;
  virtual void UpdateMainFrameLayoutSize() = 0;

  virtual DevToolsEmulator* GetDevToolsEmulator() const = 0;

  // Notifies the WebView that a load has been committed. isNewNavigation
  // will be true if a new session history item should be created for that
  // load. isNavigationWithinPage will be true if the navigation does
  // not take the user away from the current page.
  virtual void DidCommitLoad(bool is_new_navigation,
                             bool is_navigation_within_page) = 0;

  virtual void ShowContextMenuAtPoint(float x,
                                      float y,
                                      ContextMenuProvider*) = 0;
  virtual void ShowContextMenuForElement(WebElement) = 0;

  virtual IntSize MainFrameSize() = 0;
  virtual bool ShouldAutoResize() const = 0;
  virtual IntSize MinAutoSize() const = 0;
  virtual IntSize MaxAutoSize() const = 0;
  virtual WebDisplayMode DisplayMode() const = 0;

  virtual void DidUpdateFullscreenSize() = 0;
  virtual void SetLastHiddenPagePopup(WebPagePopupImpl*) = 0;
  virtual WebInputEventResult SendContextMenuEvent(const WebKeyboardEvent&) = 0;

  virtual WebSpellCheckClient* SpellCheckClient() = 0;

  virtual CompositorAnimationTimeline* LinkHighlightsTimeline() const = 0;

  virtual WebSettingsImpl* SettingsImpl() = 0;

  virtual PagePopup* OpenPagePopup(PagePopupClient*) = 0;
  virtual void ClosePagePopup(PagePopup*) = 0;
  virtual void CleanupPagePopup() = 0;
  virtual LocalDOMWindow* PagePopupWindow() const = 0;

  virtual void InvalidateRect(const IntRect&) = 0;
  // Indicates two things:
  //   1) This view may have a new layout now.
  //   2) Calling updateAllLifecyclePhases() is a no-op.
  // After calling WebWidget::updateAllLifecyclePhases(), expect to get this
  // notification unless the view did not need a layout.
  virtual void LayoutUpdated(WebLocalFrameImpl*) = 0;
  virtual void ResizeAfterLayout(WebLocalFrameImpl*) = 0;

  virtual void UpdatePageDefinedViewportConstraints(
      const ViewportDescription&) = 0;

  virtual void EnterFullscreen(LocalFrame&) = 0;
  virtual void ExitFullscreen(LocalFrame&) = 0;
  virtual void FullscreenElementChanged(Element*, Element*) = 0;

  virtual bool HasOpenedPopup() const = 0;

  // Returns true if popup menus should be rendered by the browser, false if
  // they should be rendered by WebKit (which is the default).
  static bool UseExternalPopupMenus();

  virtual GraphicsLayer* RootGraphicsLayer() = 0;
  virtual void RegisterViewportLayersWithCompositor() = 0;
  virtual PaintLayerCompositor* Compositor() const = 0;

  virtual void DidUpdateBrowserControls() = 0;
  virtual FloatSize ElasticOverscroll() const = 0;
  virtual double LastFrameTimeMonotonic() const = 0;

  static const WebInputEvent* CurrentInputEvent();

  virtual void SetCompositorVisibility(bool) = 0;
  using WebWidget::SetSuppressFrameRequestsWorkaroundFor704763Only;
  using WebWidget::RecordWheelAndTouchScrollingCount;
  using WebWidget::GetCompositionCharacterBounds;
  // Returns the currently active WebInputMethodController which is the one
  // corresponding to the focused frame. It will return nullptr if there is no
  // focused frame, or if there is one but it belongs to a different local
  // root.
  virtual WebInputMethodControllerImpl* GetActiveWebInputMethodController()
      const = 0;
  virtual void ScheduleAnimationForWidget() = 0;
  virtual CompositorWorkerProxyClient* CreateCompositorWorkerProxyClient() = 0;
  virtual AnimationWorkletProxyClient* CreateAnimationWorkletProxyClient() = 0;
  virtual void SetRootGraphicsLayer(GraphicsLayer*) = 0;
  virtual void SetRootLayer(WebLayer*) = 0;
  virtual CompositorAnimationHost* AnimationHost() const = 0;
  virtual HitTestResult CoreHitTestResultAt(const WebPoint&) = 0;
};
}

#endif
