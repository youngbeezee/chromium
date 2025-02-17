// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/toolbar/app_menu_button.h"

#include "base/command_line.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_otr_state.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/app_menu_model.h"
#include "chrome/browser/ui/views/extensions/browser_action_drag_data.h"
#include "chrome/browser/ui/views/toolbar/app_menu.h"
#include "chrome/browser/ui/views/toolbar/app_menu_animation.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/grit/theme_resources.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/theme_provider.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/keyboard/keyboard_controller.h"
#include "ui/views/controls/button/label_button_border.h"
#include "ui/views/controls/menu/menu_listener.h"
#include "ui/views/metrics.h"

namespace {
const float kIconSize = 16;
}  // namespace

// static
bool AppMenuButton::g_open_app_immediately_for_testing = false;

AppMenuButton::AppMenuButton(ToolbarView* toolbar_view)
    : views::MenuButton(base::string16(), toolbar_view, false),
      severity_(AppMenuIconController::Severity::NONE),
      type_(AppMenuIconController::IconType::NONE),
      toolbar_view_(toolbar_view),
      should_use_new_icon_(false),
      margin_trailing_(0),
      weak_factory_(this) {
  SetInkDropMode(InkDropMode::ON);
  SetFocusPainter(nullptr);

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kEnableNewAppMenuIcon)) {
    toolbar_view_->browser()->tab_strip_model()->AddObserver(this);
    should_use_new_icon_ = true;
  }
}

AppMenuButton::~AppMenuButton() {}

void AppMenuButton::SetSeverity(AppMenuIconController::IconType type,
                                AppMenuIconController::Severity severity,
                                bool animate) {
  type_ = type;
  severity_ = severity;
  UpdateIcon(animate);
}

void AppMenuButton::ShowMenu(bool for_drop) {
  if (menu_ && menu_->IsShowing())
    return;

#if defined(USE_AURA)
  keyboard::KeyboardController* keyboard_controller =
      keyboard::KeyboardController::GetInstance();
  if (keyboard_controller && keyboard_controller->keyboard_visible()) {
    keyboard_controller->HideKeyboard(
        keyboard::KeyboardController::HIDE_REASON_AUTOMATIC);
  }
#endif

  Browser* browser = toolbar_view_->browser();

  menu_.reset(new AppMenu(browser, for_drop ? AppMenu::FOR_DROP : 0));
  menu_model_.reset(new AppMenuModel(toolbar_view_, browser));
  menu_->Init(menu_model_.get());

  for (views::MenuListener& observer : menu_listeners_)
    observer.OnMenuOpened();

  base::TimeTicks menu_open_time = base::TimeTicks::Now();
  menu_->RunMenu(this);

  if (!for_drop) {
    // Record the time-to-action for the menu. We don't record in the case of a
    // drag-and-drop command because menus opened for drag-and-drop don't block
    // the message loop.
    UMA_HISTOGRAM_TIMES("Toolbar.AppMenuTimeToAction",
                        base::TimeTicks::Now() - menu_open_time);
  }

  AnimateIconIfPossible();
}

void AppMenuButton::CloseMenu() {
  if (menu_)
    menu_->CloseMenu();
  menu_.reset();
}

bool AppMenuButton::IsMenuShowing() const {
  return menu_ && menu_->IsShowing();
}

void AppMenuButton::AddMenuListener(views::MenuListener* listener) {
  menu_listeners_.AddObserver(listener);
}

void AppMenuButton::RemoveMenuListener(views::MenuListener* listener) {
  menu_listeners_.RemoveObserver(listener);
}

gfx::Size AppMenuButton::GetPreferredSize() const {
  gfx::Rect rect(gfx::Size(kIconSize, kIconSize));
  rect.Inset(gfx::Insets(-ToolbarButton::kInteriorPadding));
  return rect.size();
}

void AppMenuButton::Layout() {
  if (animation_) {
    ink_drop_container()->SetBoundsRect(GetLocalBounds());
    image()->SetBoundsRect(GetLocalBounds());
    return;
  }

  views::MenuButton::Layout();
}

void AppMenuButton::OnPaint(gfx::Canvas* canvas) {
  if (!animation_) {
    views::MenuButton::OnPaint(canvas);
    return;
  }

  gfx::Rect bounds = GetLocalBounds();
  bounds.Inset(gfx::Insets(ToolbarButton::kInteriorPadding));
  animation_->PaintAppMenu(canvas, bounds);
}

void AppMenuButton::TabInsertedAt(TabStripModel* tab_strip_model,
                                  content::WebContents* contents,
                                  int index,
                                  bool foreground) {
  AnimateIconIfPossible();
}

void AppMenuButton::UpdateIcon(bool should_animate) {
  SkColor severity_color = gfx::kPlaceholderColor;
  SkColor toolbar_icon_color =
      GetThemeProvider()->GetColor(ThemeProperties::COLOR_TOOLBAR_BUTTON_ICON);
  const ui::NativeTheme* native_theme = GetNativeTheme();
  switch (severity_) {
    case AppMenuIconController::Severity::NONE:
      severity_color = toolbar_icon_color;
      break;
    case AppMenuIconController::Severity::LOW:
      severity_color = native_theme->GetSystemColor(
          ui::NativeTheme::kColorId_AlertSeverityLow);
      break;
    case AppMenuIconController::Severity::MEDIUM:
      severity_color = native_theme->GetSystemColor(
          ui::NativeTheme::kColorId_AlertSeverityMedium);
      break;
    case AppMenuIconController::Severity::HIGH:
      severity_color = native_theme->GetSystemColor(
          ui::NativeTheme::kColorId_AlertSeverityHigh);
      break;
  }

  if (should_use_new_icon_) {
    if (!animation_)
      animation_ = base::MakeUnique<AppMenuAnimation>(this, toolbar_icon_color);

    animation_->set_target_color(severity_color);
    if (should_animate)
      AnimateIconIfPossible();

    return;
  }

  const gfx::VectorIcon* icon_id = nullptr;
  switch (type_) {
    case AppMenuIconController::IconType::NONE:
      icon_id = &kBrowserToolsIcon;
      DCHECK_EQ(AppMenuIconController::Severity::NONE, severity_);
      break;
    case AppMenuIconController::IconType::UPGRADE_NOTIFICATION:
      icon_id = &kBrowserToolsUpdateIcon;
      break;
    case AppMenuIconController::IconType::GLOBAL_ERROR:
    case AppMenuIconController::IconType::INCOMPATIBILITY_WARNING:
      icon_id = &kBrowserToolsErrorIcon;
      break;
  }

  SetImage(views::Button::STATE_NORMAL,
           gfx::CreateVectorIcon(*icon_id, severity_color));
}

void AppMenuButton::SetTrailingMargin(int margin) {
  margin_trailing_ = margin;
  UpdateThemedBorder();
  InvalidateLayout();
}

void AppMenuButton::AnimateIconIfPossible() {
  if (!animation_ || !should_use_new_icon_ ||
      severity_ == AppMenuIconController::Severity::NONE) {
    return;
  }

  animation_->StartAnimation();
}

void AppMenuButton::AppMenuAnimationStarted() {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
}

void AppMenuButton::AppMenuAnimationEnded() {
  DestroyLayer();
}

const char* AppMenuButton::GetClassName() const {
  return "AppMenuButton";
}

std::unique_ptr<views::LabelButtonBorder> AppMenuButton::CreateDefaultBorder()
    const {
  std::unique_ptr<views::LabelButtonBorder> border =
      MenuButton::CreateDefaultBorder();

  // Adjust border insets to follow the margin change,
  // which will be reflected in where the border is painted
  // through GetThemePaintRect().
  gfx::Insets insets(border->GetInsets());
  insets += gfx::Insets(0, 0, 0, margin_trailing_);
  border->set_insets(insets);

  return border;
}

gfx::Rect AppMenuButton::GetThemePaintRect() const {
  gfx::Rect rect(MenuButton::GetThemePaintRect());
  rect.Inset(0, 0, margin_trailing_, 0);
  return rect;
}

bool AppMenuButton::GetDropFormats(
    int* formats,
    std::set<ui::Clipboard::FormatType>* format_types) {
  return BrowserActionDragData::GetDropFormats(format_types);
}

bool AppMenuButton::AreDropTypesRequired() {
  return BrowserActionDragData::AreDropTypesRequired();
}

bool AppMenuButton::CanDrop(const ui::OSExchangeData& data) {
  return BrowserActionDragData::CanDrop(data,
                                        toolbar_view_->browser()->profile());
}

void AppMenuButton::OnDragEntered(const ui::DropTargetEvent& event) {
  DCHECK(!weak_factory_.HasWeakPtrs());
  if (!g_open_app_immediately_for_testing) {
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&AppMenuButton::ShowMenu, weak_factory_.GetWeakPtr(),
                       true),
        base::TimeDelta::FromMilliseconds(views::GetMenuShowDelay()));
  } else {
    ShowMenu(true);
  }
}

int AppMenuButton::OnDragUpdated(const ui::DropTargetEvent& event) {
  return ui::DragDropTypes::DRAG_MOVE;
}

void AppMenuButton::OnDragExited() {
  weak_factory_.InvalidateWeakPtrs();
}

int AppMenuButton::OnPerformDrop(const ui::DropTargetEvent& event) {
  return ui::DragDropTypes::DRAG_MOVE;
}
