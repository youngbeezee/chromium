// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/launcher/chrome_launcher_controller.h"

#include "ash/multi_profile_uma.h"
#include "ash/public/cpp/shelf_item.h"
#include "ash/public/interfaces/constants.mojom.h"
#include "ash/resources/grit/ash_resources.h"
#include "ash/shelf/shelf_model.h"
#include "ash/shelf/wm_shelf.h"
#include "ash/shell.h"
#include "ash/shell_port.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/system/tray/system_tray_delegate.h"
#include "ash/wm_window.h"
#include "base/memory/ptr_util.h"
#include "base/strings/pattern.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/chromeos/arc/arc_util.h"
#include "chrome/browser/extensions/extension_app_icon_loader.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/prefs/pref_service_syncable_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/app_list/app_list_syncable_service_factory.h"
#include "chrome/browser/ui/app_list/arc/arc_app_icon_loader.h"
#include "chrome/browser/ui/app_list/arc/arc_app_utils.h"
#include "chrome/browser/ui/ash/app_sync_ui_state.h"
#include "chrome/browser/ui/ash/ash_util.h"
#include "chrome/browser/ui/ash/chrome_launcher_prefs.h"
#include "chrome/browser/ui/ash/launcher/app_shortcut_launcher_item_controller.h"
#include "chrome/browser/ui/ash/launcher/app_window_launcher_controller.h"
#include "chrome/browser/ui/ash/launcher/app_window_launcher_item_controller.h"
#include "chrome/browser/ui/ash/launcher/arc_app_deferred_launcher_controller.h"
#include "chrome/browser/ui/ash/launcher/arc_app_window_launcher_controller.h"
#include "chrome/browser/ui/ash/launcher/browser_shortcut_launcher_item_controller.h"
#include "chrome/browser/ui/ash/launcher/browser_status_monitor.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_controller_util.h"
#include "chrome/browser/ui/ash/launcher/launcher_arc_app_updater.h"
#include "chrome/browser/ui/ash/launcher/launcher_controller_helper.h"
#include "chrome/browser/ui/ash/launcher/launcher_extension_app_updater.h"
#include "chrome/browser/ui/ash/launcher/multi_profile_app_window_launcher_controller.h"
#include "chrome/browser/ui/ash/launcher/multi_profile_browser_status_monitor.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_util.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_window_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/theme_resources.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/signin/core/account_id/account_id.h"
#include "components/strings/grit/components_strings.h"
#include "components/sync_preferences/pref_service_syncable.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/service_manager_connection.h"
#include "extensions/common/extension.h"
#include "services/service_manager/public/cpp/connector.h"
#include "ui/aura/window.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/display/types/display_constants.h"
#include "ui/keyboard/keyboard_util.h"
#include "ui/resources/grit/ui_resources.h"

using extension_misc::kChromeAppId;
using extension_misc::kGmailAppId;

namespace {

int64_t GetDisplayIDForShelf(ash::WmShelf* shelf) {
  display::Display display =
      shelf->GetWindow()->GetRootWindow()->GetDisplayNearestWindow();
  DCHECK(display.is_valid());
  return display.id();
}

// A callback that does nothing after shelf item selection handling.
void NoopCallback(ash::ShelfAction, base::Optional<ash::MenuItemList>) {}

// Calls ItemSelected with |source|, default arguments, and no callback.
void SelectItemWithSource(ash::ShelfItemDelegate* delegate,
                          ash::ShelfLaunchSource source) {
  delegate->ItemSelected(nullptr, display::kInvalidDisplayId, source,
                         base::Bind(&NoopCallback));
}

// Returns true if the given |item| has a pinned shelf item type.
bool ItemTypeIsPinned(const ash::ShelfItem& item) {
  return item.type == ash::TYPE_PINNED_APP ||
         item.type == ash::TYPE_BROWSER_SHORTCUT;
}

}  // namespace

// A class to get events from ChromeOS when a user gets changed or added.
class ChromeLauncherControllerUserSwitchObserver
    : public user_manager::UserManager::UserSessionStateObserver {
 public:
  ChromeLauncherControllerUserSwitchObserver(
      ChromeLauncherController* controller)
      : controller_(controller) {
    DCHECK(user_manager::UserManager::IsInitialized());
    user_manager::UserManager::Get()->AddSessionStateObserver(this);
  }
  ~ChromeLauncherControllerUserSwitchObserver() override {
    user_manager::UserManager::Get()->RemoveSessionStateObserver(this);
  }

  // user_manager::UserManager::UserSessionStateObserver overrides:
  void UserAddedToSession(const user_manager::User* added_user) override;

  // ChromeLauncherControllerUserSwitchObserver:
  void OnUserProfileReadyToSwitch(Profile* profile);

 private:
  // Add a user to the session.
  void AddUser(Profile* profile);

  // The owning ChromeLauncherController.
  ChromeLauncherController* controller_;

  // Users which were just added to the system, but which profiles were not yet
  // (fully) loaded.
  std::set<std::string> added_user_ids_waiting_for_profiles_;

  DISALLOW_COPY_AND_ASSIGN(ChromeLauncherControllerUserSwitchObserver);
};

void ChromeLauncherControllerUserSwitchObserver::UserAddedToSession(
    const user_manager::User* active_user) {
  Profile* profile =
      multi_user_util::GetProfileFromAccountId(active_user->GetAccountId());
  // If we do not have a profile yet, we postpone forwarding the notification
  // until it is loaded.
  if (!profile) {
    added_user_ids_waiting_for_profiles_.insert(
        active_user->GetAccountId().GetUserEmail());
  } else {
    AddUser(profile);
  }
}

void ChromeLauncherControllerUserSwitchObserver::OnUserProfileReadyToSwitch(
    Profile* profile) {
  if (!added_user_ids_waiting_for_profiles_.empty()) {
    // Check if the profile is from a user which was on the waiting list.
    // TODO(alemate): added_user_ids_waiting_for_profiles_ should be
    // a set<AccountId>
    std::string user_id =
        multi_user_util::GetAccountIdFromProfile(profile).GetUserEmail();
    std::set<std::string>::iterator it =
        std::find(added_user_ids_waiting_for_profiles_.begin(),
                  added_user_ids_waiting_for_profiles_.end(), user_id);
    if (it != added_user_ids_waiting_for_profiles_.end()) {
      added_user_ids_waiting_for_profiles_.erase(it);
      AddUser(profile->GetOriginalProfile());
    }
  }
}

void ChromeLauncherControllerUserSwitchObserver::AddUser(Profile* profile) {
  if (chrome::MultiUserWindowManager::GetMultiProfileMode() ==
      chrome::MultiUserWindowManager::MULTI_PROFILE_MODE_SEPARATED)
    chrome::MultiUserWindowManager::GetInstance()->AddUser(profile);
  controller_->AdditionalUserAddedToSession(profile->GetOriginalProfile());
}

// static
ChromeLauncherController* ChromeLauncherController::instance_ = nullptr;

ChromeLauncherController::ChromeLauncherController(Profile* profile,
                                                   ash::ShelfModel* model)
    : model_(model), observer_binding_(this), weak_ptr_factory_(this) {
  DCHECK(!instance_);
  instance_ = this;

  DCHECK(model_);

  if (!profile) {
    // If no profile was passed, we take the currently active profile and use it
    // as the owner of the current desktop.
    // Use the original profile as on chromeos we may get a temporary off the
    // record profile, unless in guest session (where off the record profile is
    // the right one).
    profile = ProfileManager::GetActiveUserProfile();
    if (!profile->IsGuestSession() && !profile->IsSystemProfile())
      profile = profile->GetOriginalProfile();

    app_sync_ui_state_ = AppSyncUIState::Get(profile);
    if (app_sync_ui_state_)
      app_sync_ui_state_->AddObserver(this);
  }

  // All profile relevant settings get bound to the current profile.
  AttachProfile(profile);
  DCHECK_EQ(profile, profile_);
  model_->AddObserver(this);

  if (arc::IsArcAllowedForProfile(profile))
    arc_deferred_launcher_.reset(new ArcAppDeferredLauncherController(this));

  // In multi profile mode we might have a window manager. We try to create it
  // here. If the instantiation fails, the manager is not needed.
  chrome::MultiUserWindowManager::CreateInstance();

  // On Chrome OS using multi profile we want to switch the content of the shelf
  // with a user change. Note that for unit tests the instance can be NULL.
  if (chrome::MultiUserWindowManager::GetMultiProfileMode() !=
      chrome::MultiUserWindowManager::MULTI_PROFILE_MODE_OFF) {
    user_switch_observer_.reset(
        new ChromeLauncherControllerUserSwitchObserver(this));
  }

  std::unique_ptr<AppWindowLauncherController> extension_app_window_controller;
  // Create our v1/v2 application / browser monitors which will inform the
  // launcher of status changes.
  if (chrome::MultiUserWindowManager::GetMultiProfileMode() ==
      chrome::MultiUserWindowManager::MULTI_PROFILE_MODE_SEPARATED) {
    // If running in separated destkop mode, we create the multi profile version
    // of status monitor.
    browser_status_monitor_.reset(new MultiProfileBrowserStatusMonitor(this));
    browser_status_monitor_->Initialize();
    extension_app_window_controller.reset(
        new MultiProfileAppWindowLauncherController(this));
  } else {
    // Create our v1/v2 application / browser monitors which will inform the
    // launcher of status changes.
    browser_status_monitor_.reset(new BrowserStatusMonitor(this));
    browser_status_monitor_->Initialize();
    extension_app_window_controller.reset(
        new ExtensionAppWindowLauncherController(this));
  }
  app_window_controllers_.push_back(std::move(extension_app_window_controller));
  app_window_controllers_.push_back(
      base::MakeUnique<ArcAppWindowLauncherController>(this));

  // Right now ash::Shell isn't created for tests.
  // TODO(mukai): Allows it to observe display change and write tests.
  if (ash::Shell::HasInstance())
    ash::Shell::Get()->window_tree_host_manager()->AddObserver(this);
}

ChromeLauncherController::~ChromeLauncherController() {
  // Reset the BrowserStatusMonitor as it has a weak pointer to this.
  browser_status_monitor_.reset();

  // Reset the app window controllers here since it has a weak pointer to this.
  app_window_controllers_.clear();

  model_->RemoveObserver(this);
  if (ash::Shell::HasInstance())
    ash::Shell::Get()->window_tree_host_manager()->RemoveObserver(this);

  // Release all profile dependent resources.
  ReleaseProfile();

  // Get rid of the multi user window manager instance.
  chrome::MultiUserWindowManager::DeleteInstance();

  if (instance_ == this)
    instance_ = nullptr;
}

void ChromeLauncherController::Init() {
  // Start observing the shelf controller.
  if (ConnectToShelfController()) {
    ash::mojom::ShelfObserverAssociatedPtrInfo ptr_info;
    observer_binding_.Bind(&ptr_info);
    shelf_controller_->AddObserver(std::move(ptr_info));
  }

  CreateBrowserShortcutLauncherItem();
  UpdateAppLaunchersFromPref();

  // TODO(sky): update unit test so that this test isn't necessary.
  if (ash::Shell::HasInstance())
    SetVirtualKeyboardBehaviorFromPrefs();

  prefs_observer_ =
      ash::launcher::ChromeLauncherPrefsObserver::CreateIfNecessary(profile());
}

ash::ShelfID ChromeLauncherController::CreateAppLauncherItem(
    std::unique_ptr<ash::ShelfItemDelegate> item_delegate,
    ash::ShelfItemStatus status) {
  return InsertAppLauncherItem(std::move(item_delegate), status,
                               model_->item_count(), ash::TYPE_APP);
}

const ash::ShelfItem* ChromeLauncherController::GetItem(ash::ShelfID id) const {
  const int index = model_->ItemIndexByID(id);
  if (index >= 0 && index < model_->item_count())
    return &model_->items()[index];
  return nullptr;
}

void ChromeLauncherController::SetItemType(ash::ShelfID id,
                                           ash::ShelfItemType type) {
  const ash::ShelfItem* item = GetItem(id);
  if (item && item->type != type) {
    ash::ShelfItem new_item = *item;
    new_item.type = type;
    model_->Set(model_->ItemIndexByID(id), new_item);
  }
}

void ChromeLauncherController::SetItemStatus(ash::ShelfID id,
                                             ash::ShelfItemStatus status) {
  const ash::ShelfItem* item = GetItem(id);
  if (item && item->status != status) {
    ash::ShelfItem new_item = *item;
    new_item.status = status;
    model_->Set(model_->ItemIndexByID(id), new_item);
  }
}

void ChromeLauncherController::CloseLauncherItem(ash::ShelfID id) {
  CHECK(id);
  if (IsPinned(id)) {
    // Create a new shortcut delegate.
    SetItemStatus(id, ash::STATUS_CLOSED);
    model_->SetShelfItemDelegate(id, AppShortcutLauncherItemController::Create(
                                         GetItem(id)->app_launch_id));
  } else {
    RemoveShelfItem(id);
  }
}

void ChromeLauncherController::UnpinShelfItemInternal(ash::ShelfID id) {
  const ash::ShelfItem* item = GetItem(id);
  if (item && item->status != ash::STATUS_CLOSED)
    UnpinRunningAppInternal(model_->ItemIndexByID(id));
  else
    RemoveShelfItem(id);
}

bool ChromeLauncherController::IsPinned(ash::ShelfID id) {
  const ash::ShelfItem* item = GetItem(id);
  return item && ItemTypeIsPinned(*item);
}

void ChromeLauncherController::SetV1AppStatus(const std::string& app_id,
                                              ash::ShelfItemStatus status) {
  ash::ShelfID id = GetShelfIDForAppID(app_id);
  const ash::ShelfItem* item = GetItem(id);
  if (item) {
    if (!IsPinned(id) && status == ash::STATUS_CLOSED)
      RemoveShelfItem(id);
    else
      SetItemStatus(id, status);
  } else if (status != ash::STATUS_CLOSED && !app_id.empty()) {
    InsertAppLauncherItem(
        AppShortcutLauncherItemController::Create(ash::AppLaunchId(app_id)),
        status, model_->item_count(), ash::TYPE_APP);
  }
}

void ChromeLauncherController::Launch(ash::ShelfID id, int event_flags) {
  ash::ShelfItemDelegate* delegate = model_->GetShelfItemDelegate(id);
  if (!delegate)
    return;  // In case invoked from menu and item closed while menu up.

  // Launching some items replaces the associated item delegate instance,
  // which destroys the app and launch id strings; making copies avoid crashes.
  LaunchApp(ash::AppLaunchId(delegate->app_id(), delegate->launch_id()),
            ash::LAUNCH_FROM_UNKNOWN, event_flags);
}

void ChromeLauncherController::Close(ash::ShelfID id) {
  ash::ShelfItemDelegate* delegate = model_->GetShelfItemDelegate(id);
  if (!delegate)
    return;  // May happen if menu closed.
  delegate->Close();
}

bool ChromeLauncherController::IsOpen(ash::ShelfID id) {
  const ash::ShelfItem* item = GetItem(id);
  return item && item->status != ash::STATUS_CLOSED;
}

bool ChromeLauncherController::IsPlatformApp(ash::ShelfID id) {
  std::string app_id = GetAppIDForShelfID(id);
  const extensions::Extension* extension =
      GetExtensionForAppID(app_id, profile());
  // An extension can be synced / updated at any time and therefore not be
  // available.
  return extension ? extension->is_platform_app() : false;
}

void ChromeLauncherController::LaunchApp(ash::AppLaunchId id,
                                         ash::ShelfLaunchSource source,
                                         int event_flags) {
  launcher_controller_helper_->LaunchApp(id, source, event_flags);
}

void ChromeLauncherController::ActivateApp(const std::string& app_id,
                                           ash::ShelfLaunchSource source,
                                           int event_flags) {
  // If there is an existing non-shortcut delegate for this app, open it.
  ash::ShelfID id = GetShelfIDForAppID(app_id);
  if (id) {
    SelectItemWithSource(model_->GetShelfItemDelegate(id), source);
    return;
  }

  // Create a temporary application launcher item and use it to see if there are
  // running instances.
  ash::AppLaunchId app_launch_id(app_id);
  std::unique_ptr<AppShortcutLauncherItemController> item_delegate =
      AppShortcutLauncherItemController::Create(app_launch_id);
  if (!item_delegate->GetRunningApplications().empty())
    SelectItemWithSource(item_delegate.get(), source);
  else
    LaunchApp(app_launch_id, source, event_flags);
}

void ChromeLauncherController::SetLauncherItemImage(
    ash::ShelfID shelf_id,
    const gfx::ImageSkia& image) {
  const ash::ShelfItem* item = GetItem(shelf_id);
  if (item) {
    ash::ShelfItem new_item = *item;
    new_item.image = image;
    model_->Set(model_->ItemIndexByID(shelf_id), new_item);
  }
}

void ChromeLauncherController::UpdateAppState(content::WebContents* contents,
                                              AppState app_state) {
  std::string app_id = launcher_controller_helper_->GetAppID(contents);

  // Check if the gMail app is loaded and it matches the given content.
  // This special treatment is needed to address crbug.com/234268.
  if (app_id.empty() && ContentCanBeHandledByGmailApp(contents))
    app_id = kGmailAppId;

  // Check the old |app_id| for a tab. If the contents has changed we need to
  // remove it from the previous app.
  if (web_contents_to_app_id_.find(contents) != web_contents_to_app_id_.end()) {
    std::string last_app_id = web_contents_to_app_id_[contents];
    if (last_app_id != app_id) {
      ash::ShelfID id = GetShelfIDForAppID(last_app_id);
      if (id) {
        // Since GetAppState() will use |web_contents_to_app_id_| we remove
        // the connection before calling it.
        web_contents_to_app_id_.erase(contents);
        SetItemStatus(id, GetAppState(last_app_id));
      }
    }
  }

  if (app_state == APP_STATE_REMOVED)
    web_contents_to_app_id_.erase(contents);
  else
    web_contents_to_app_id_[contents] = app_id;

  ash::ShelfID id = GetShelfIDForAppID(app_id);
  if (id) {
    SetItemStatus(id, (app_state == APP_STATE_WINDOW_ACTIVE ||
                       app_state == APP_STATE_ACTIVE)
                          ? ash::STATUS_ACTIVE
                          : GetAppState(app_id));
  }
}

ash::ShelfID ChromeLauncherController::GetShelfIDForWebContents(
    content::WebContents* contents) {
  std::string app_id = launcher_controller_helper_->GetAppID(contents);
  if (app_id.empty() && ContentCanBeHandledByGmailApp(contents))
    app_id = kGmailAppId;

  ash::ShelfID id = GetShelfIDForAppID(app_id);
  // If there is no dedicated app item, use the browser shortcut item.
  return id == ash::kInvalidShelfID ? GetShelfIDForAppID(kChromeAppId) : id;
}

void ChromeLauncherController::SetRefocusURLPatternForTest(ash::ShelfID id,
                                                           const GURL& url) {
  const ash::ShelfItem* item = GetItem(id);
  if (item && !IsPlatformApp(id) &&
      (item->type == ash::TYPE_PINNED_APP || item->type == ash::TYPE_APP)) {
    ash::ShelfItemDelegate* delegate = model_->GetShelfItemDelegate(id);
    AppShortcutLauncherItemController* item_controller =
        static_cast<AppShortcutLauncherItemController*>(delegate);
    item_controller->set_refocus_url(url);
  } else {
    NOTREACHED() << "Invalid launcher item or type";
  }
}

ash::ShelfAction ChromeLauncherController::ActivateWindowOrMinimizeIfActive(
    ui::BaseWindow* window,
    bool allow_minimize) {
  // In separated desktop mode we might have to teleport a window back to the
  // current user.
  if (chrome::MultiUserWindowManager::GetMultiProfileMode() ==
      chrome::MultiUserWindowManager::MULTI_PROFILE_MODE_SEPARATED) {
    aura::Window* native_window = window->GetNativeWindow();
    const AccountId& current_account_id =
        multi_user_util::GetAccountIdFromProfile(profile());
    chrome::MultiUserWindowManager* manager =
        chrome::MultiUserWindowManager::GetInstance();
    if (!manager->IsWindowOnDesktopOfUser(native_window, current_account_id)) {
      ash::MultiProfileUMA::RecordTeleportAction(
          ash::MultiProfileUMA::TELEPORT_WINDOW_RETURN_BY_LAUNCHER);
      manager->ShowWindowForUser(native_window, current_account_id);
      window->Activate();
      return ash::SHELF_ACTION_WINDOW_ACTIVATED;
    }
  }

  if (window->IsActive() && allow_minimize) {
    window->Minimize();
    return ash::SHELF_ACTION_WINDOW_MINIMIZED;
  }

  window->Show();
  window->Activate();
  return ash::SHELF_ACTION_WINDOW_ACTIVATED;
}

void ChromeLauncherController::ActiveUserChanged(
    const std::string& user_email) {
  // Store the order of running applications for the user which gets inactive.
  RememberUnpinnedRunningApplicationOrder();
  // Coming here the default profile is already switched. All profile specific
  // resources get released and the new profile gets attached instead.
  ReleaseProfile();
  // When coming here, the active user has already be changed so that we can
  // set it as active.
  AttachProfile(ProfileManager::GetActiveUserProfile());
  // Update the V1 applications.
  browser_status_monitor_->ActiveUserChanged(user_email);
  // Switch the running applications to the new user.
  for (auto& controller : app_window_controllers_)
    controller->ActiveUserChanged(user_email);
  // Update the user specific shell properties from the new user profile.
  // Shelf preferences are loaded in ChromeLauncherController::AttachProfile.
  UpdateAppLaunchersFromPref();
  SetVirtualKeyboardBehaviorFromPrefs();

  // Restore the order of running, but unpinned applications for the activated
  // user.
  RestoreUnpinnedRunningApplicationOrder(user_email);
  // TODO(crbug.com/557406): Fix this interaction pattern in Mash.
  if (!ash_util::IsRunningInMash()) {
    // Inform the system tray of the change.
    ash::Shell::Get()->system_tray_delegate()->ActiveUserWasChanged();
    // Force on-screen keyboard to reset.
    if (keyboard::IsKeyboardEnabled())
      ash::Shell::Get()->CreateKeyboard();
  }
}

void ChromeLauncherController::AdditionalUserAddedToSession(Profile* profile) {
  // Switch the running applications to the new user.
  for (auto& controller : app_window_controllers_)
    controller->AdditionalUserAddedToSession(profile);
}

ash::MenuItemList ChromeLauncherController::GetAppMenuItemsForTesting(
    const ash::ShelfItem& item) {
  ash::ShelfItemDelegate* delegate = model_->GetShelfItemDelegate(item.id);
  return delegate ? delegate->GetAppMenuItems(ui::EF_NONE)
                  : ash::MenuItemList();
}

std::vector<content::WebContents*>
ChromeLauncherController::GetV1ApplicationsFromAppId(
    const std::string& app_id) {
  const ash::ShelfItem* item = GetItem(GetShelfIDForAppID(app_id));
  // If there is no such item pinned to the launcher, no menu gets created.
  if (!item || item->type != ash::TYPE_PINNED_APP)
    return std::vector<content::WebContents*>();
  ash::ShelfItemDelegate* delegate = model_->GetShelfItemDelegate(item->id);
  AppShortcutLauncherItemController* item_controller =
      static_cast<AppShortcutLauncherItemController*>(delegate);
  return item_controller->GetRunningApplications();
}

void ChromeLauncherController::ActivateShellApp(const std::string& app_id,
                                                int window_index) {
  const ash::ShelfItem* item = GetItem(GetShelfIDForAppID(app_id));
  if (item &&
      (item->type == ash::TYPE_APP || item->type == ash::TYPE_PINNED_APP)) {
    ash::ShelfItemDelegate* delegate = model_->GetShelfItemDelegate(item->id);
    AppWindowLauncherItemController* item_controller =
        delegate->AsAppWindowLauncherItemController();
    item_controller->ActivateIndexedApp(window_index);
  }
}

bool ChromeLauncherController::IsWebContentHandledByApplication(
    content::WebContents* web_contents,
    const std::string& app_id) {
  if ((web_contents_to_app_id_.find(web_contents) !=
       web_contents_to_app_id_.end()) &&
      (web_contents_to_app_id_[web_contents] == app_id))
    return true;
  return (app_id == kGmailAppId && ContentCanBeHandledByGmailApp(web_contents));
}

bool ChromeLauncherController::ContentCanBeHandledByGmailApp(
    content::WebContents* web_contents) {
  ash::ShelfID id = GetShelfIDForAppID(kGmailAppId);
  if (id) {
    const GURL url = web_contents->GetURL();
    // We need to extend the application matching for the gMail app beyond the
    // manifest file's specification. This is required because of the namespace
    // overlap with the offline app ("/mail/mu/").
    if (!base::MatchPattern(url.path(), "/mail/mu/*") &&
        base::MatchPattern(url.path(), "/mail/*") &&
        GetExtensionForAppID(kGmailAppId, profile()) &&
        GetExtensionForAppID(kGmailAppId, profile())->OverlapsWithOrigin(url))
      return true;
  }
  return false;
}

gfx::Image ChromeLauncherController::GetAppListIcon(
    content::WebContents* web_contents) const {
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  if (IsIncognito(web_contents))
    return rb.GetImageNamed(IDR_ASH_SHELF_LIST_INCOGNITO_BROWSER);
  favicon::FaviconDriver* favicon_driver =
      favicon::ContentFaviconDriver::FromWebContents(web_contents);
  gfx::Image result = favicon_driver->GetFavicon();
  if (result.IsEmpty())
    return rb.GetImageNamed(IDR_DEFAULT_FAVICON);
  return result;
}

base::string16 ChromeLauncherController::GetAppListTitle(
    content::WebContents* web_contents) const {
  base::string16 title = web_contents->GetTitle();
  if (!title.empty())
    return title;
  WebContentsToAppIDMap::const_iterator iter =
      web_contents_to_app_id_.find(web_contents);
  if (iter != web_contents_to_app_id_.end()) {
    std::string app_id = iter->second;
    const extensions::Extension* extension =
        GetExtensionForAppID(app_id, profile());
    if (extension)
      return base::UTF8ToUTF16(extension->name());
  }
  return l10n_util::GetStringUTF16(IDS_NEW_TAB_TITLE);
}

BrowserShortcutLauncherItemController*
ChromeLauncherController::GetBrowserShortcutLauncherItemController() {
  ash::ShelfID id = GetShelfIDForAppID(kChromeAppId);
  ash::mojom::ShelfItemDelegate* delegate = model_->GetShelfItemDelegate(id);
  DCHECK(delegate) << "There should be always be a browser shortcut item.";
  return static_cast<BrowserShortcutLauncherItemController*>(delegate);
}

bool ChromeLauncherController::ShelfBoundsChangesProbablyWithUser(
    ash::WmShelf* shelf,
    const AccountId& account_id) const {
  Profile* other_profile = multi_user_util::GetProfileFromAccountId(account_id);
  if (!other_profile || other_profile == profile())
    return false;

  // Note: The Auto hide state from preferences is not the same as the actual
  // visibility of the shelf. Depending on all the various states (full screen,
  // no window on desktop, multi user, ..) the shelf could be shown - or not.
  PrefService* prefs = profile()->GetPrefs();
  PrefService* other_prefs = other_profile->GetPrefs();
  const int64_t display = GetDisplayIDForShelf(shelf);
  const bool currently_shown =
      ash::SHELF_AUTO_HIDE_BEHAVIOR_NEVER ==
      ash::launcher::GetShelfAutoHideBehaviorPref(prefs, display);
  const bool other_shown =
      ash::SHELF_AUTO_HIDE_BEHAVIOR_NEVER ==
      ash::launcher::GetShelfAutoHideBehaviorPref(other_prefs, display);

  return currently_shown != other_shown ||
         ash::launcher::GetShelfAlignmentPref(prefs, display) !=
             ash::launcher::GetShelfAlignmentPref(other_prefs, display);
}

void ChromeLauncherController::OnUserProfileReadyToSwitch(Profile* profile) {
  if (user_switch_observer_.get())
    user_switch_observer_->OnUserProfileReadyToSwitch(profile);
}

ArcAppDeferredLauncherController*
ChromeLauncherController::GetArcDeferredLauncher() {
  return arc_deferred_launcher_.get();
}

const std::string& ChromeLauncherController::GetLaunchIDForShelfID(
    ash::ShelfID id) {
  ash::ShelfItemDelegate* delegate = model_->GetShelfItemDelegate(id);
  return delegate ? delegate->launch_id() : base::EmptyString();
}

AppIconLoader* ChromeLauncherController::GetAppIconLoaderForApp(
    const std::string& app_id) {
  for (const auto& app_icon_loader : app_icon_loaders_) {
    if (app_icon_loader->CanLoadImageForApp(app_id))
      return app_icon_loader.get();
  }

  return nullptr;
}

void ChromeLauncherController::SetShelfAutoHideBehaviorFromPrefs() {
  if (!ConnectToShelfController() || updating_shelf_pref_from_observer_)
    return;

  // The pref helper functions return default values for invalid display ids.
  PrefService* prefs = profile_->GetPrefs();
  for (const auto& display : display::Screen::GetScreen()->GetAllDisplays()) {
    shelf_controller_->SetAutoHideBehavior(
        ash::launcher::GetShelfAutoHideBehaviorPref(prefs, display.id()),
        display.id());
  }
}

void ChromeLauncherController::SetShelfAlignmentFromPrefs() {
  if (!ConnectToShelfController() || updating_shelf_pref_from_observer_)
    return;

  // The pref helper functions return default values for invalid display ids.
  PrefService* prefs = profile_->GetPrefs();
  for (const auto& display : display::Screen::GetScreen()->GetAllDisplays()) {
    shelf_controller_->SetAlignment(
        ash::launcher::GetShelfAlignmentPref(prefs, display.id()),
        display.id());
  }
}

void ChromeLauncherController::SetShelfBehaviorsFromPrefs() {
  SetShelfAutoHideBehaviorFromPrefs();
  SetShelfAlignmentFromPrefs();
}

ChromeLauncherController::ScopedPinSyncDisabler
ChromeLauncherController::GetScopedPinSyncDisabler() {
  // Only one temporary disabler should not exist at a time.
  DCHECK(should_sync_pin_changes_);
  return base::MakeUnique<base::AutoReset<bool>>(&should_sync_pin_changes_,
                                                 false);
}

void ChromeLauncherController::SetLauncherControllerHelperForTest(
    std::unique_ptr<LauncherControllerHelper> helper) {
  launcher_controller_helper_ = std::move(helper);
}

void ChromeLauncherController::SetAppIconLoadersForTest(
    std::vector<std::unique_ptr<AppIconLoader>>& loaders) {
  app_icon_loaders_.clear();
  for (auto& loader : loaders)
    app_icon_loaders_.push_back(std::move(loader));
}

void ChromeLauncherController::SetProfileForTest(Profile* profile) {
  profile_ = profile;
}

ash::ShelfID ChromeLauncherController::GetShelfIDForAppID(
    const std::string& app_id) {
  return model_->GetShelfIDForAppID(app_id);
}

ash::ShelfID ChromeLauncherController::GetShelfIDForAppIDAndLaunchID(
    const std::string& app_id,
    const std::string& launch_id) {
  return model_->GetShelfIDForAppIDAndLaunchID(app_id, launch_id);
}

const std::string& ChromeLauncherController::GetAppIDForShelfID(
    ash::ShelfID id) {
  return model_->GetAppIDForShelfID(id);
}

void ChromeLauncherController::PinAppWithID(const std::string& app_id) {
  model_->PinAppWithID(app_id);
}

bool ChromeLauncherController::IsAppPinned(const std::string& app_id) {
  return model_->IsAppPinned(app_id);
}

void ChromeLauncherController::UnpinAppWithID(const std::string& app_id) {
  model_->UnpinAppWithID(app_id);
}

///////////////////////////////////////////////////////////////////////////////
// LauncherAppUpdater::Delegate:

void ChromeLauncherController::OnAppInstalled(
    content::BrowserContext* browser_context,
    const std::string& app_id) {
  if (IsAppPinned(app_id)) {
    // Clear and re-fetch to ensure icon is up-to-date.
    AppIconLoader* app_icon_loader = GetAppIconLoaderForApp(app_id);
    if (app_icon_loader) {
      app_icon_loader->ClearImage(app_id);
      app_icon_loader->FetchImage(app_id);
    }
  }

  UpdateAppLaunchersFromPref();
}

void ChromeLauncherController::OnAppUpdated(
    content::BrowserContext* browser_context,
    const std::string& app_id) {
  AppIconLoader* app_icon_loader = GetAppIconLoaderForApp(app_id);
  if (app_icon_loader)
    app_icon_loader->UpdateImage(app_id);
}

void ChromeLauncherController::OnAppUninstalledPrepared(
    content::BrowserContext* browser_context,
    const std::string& app_id) {
  // Since we might have windowed apps of this type which might have
  // outstanding locks which needs to be removed.
  const Profile* profile = Profile::FromBrowserContext(browser_context);
  ash::ShelfID shelf_id = GetShelfIDForAppID(app_id);
  if (shelf_id != ash::kInvalidShelfID)
    CloseWindowedAppsFromRemovedExtension(app_id, profile);

  if (IsAppPinned(app_id)) {
    if (profile == this->profile() && shelf_id != ash::kInvalidShelfID) {
      // Some apps may be removed locally. Unpin the item without removing the
      // pin position from profile preferences. When needed, it is automatically
      // deleted on app list model update.
      UnpinShelfItemInternal(shelf_id);
    }
    AppIconLoader* app_icon_loader = GetAppIconLoaderForApp(app_id);
    if (app_icon_loader)
      app_icon_loader->ClearImage(app_id);
  }
}

///////////////////////////////////////////////////////////////////////////////
// AppIconLoaderDelegate:

void ChromeLauncherController::OnAppImageUpdated(const std::string& app_id,
                                                 const gfx::ImageSkia& image) {
  // TODO: need to get this working for shortcuts.
  for (int index = 0; index < model_->item_count(); ++index) {
    ash::ShelfItem item = model_->items()[index];
    ash::ShelfItemDelegate* delegate = model_->GetShelfItemDelegate(item.id);
    if (item.type == ash::TYPE_APP_PANEL || !delegate ||
        delegate->image_set_by_controller() ||
        item.app_launch_id.app_id() != app_id) {
      continue;
    }
    item.image = image;
    if (arc_deferred_launcher_)
      arc_deferred_launcher_->MaybeApplySpinningEffect(app_id, &item.image);
    model_->Set(index, item);
    // It's possible we're waiting on more than one item, so don't break.
  }
}

///////////////////////////////////////////////////////////////////////////////
// ChromeLauncherController protected:

bool ChromeLauncherController::ConnectToShelfController() {
  if (shelf_controller_.is_bound())
    return true;

  auto* connection = content::ServiceManagerConnection::GetForProcess();
  auto* connector = connection ? connection->GetConnector() : nullptr;
  // Unit tests may not have a connector.
  if (!connector)
    return false;

  connector->BindInterface(ash::mojom::kServiceName, &shelf_controller_);
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// ChromeLauncherController private:

ash::ShelfID ChromeLauncherController::CreateAppShortcutLauncherItem(
    const ash::AppLaunchId& app_launch_id,
    int index) {
  return InsertAppLauncherItem(
      AppShortcutLauncherItemController::Create(app_launch_id),
      ash::STATUS_CLOSED, index, ash::TYPE_PINNED_APP);
}

void ChromeLauncherController::RememberUnpinnedRunningApplicationOrder() {
  RunningAppListIds list;
  for (int i = 0; i < model_->item_count(); i++) {
    if (model_->items()[i].type == ash::TYPE_APP)
      list.push_back(GetAppIDForShelfID(model_->items()[i].id));
  }
  const std::string user_email =
      multi_user_util::GetAccountIdFromProfile(profile()).GetUserEmail();
  last_used_running_application_order_[user_email] = list;
}

void ChromeLauncherController::RestoreUnpinnedRunningApplicationOrder(
    const std::string& user_id) {
  const RunningAppListIdMap::iterator app_id_list =
      last_used_running_application_order_.find(user_id);
  if (app_id_list == last_used_running_application_order_.end())
    return;

  // Find the first insertion point for running applications.
  int running_index = model_->FirstRunningAppIndex();
  for (const std::string& app_id : app_id_list->second) {
    const ash::ShelfItem* item = GetItem(GetShelfIDForAppID(app_id));
    if (item && item->type == ash::TYPE_APP) {
      int app_index = model_->ItemIndexByID(item->id);
      DCHECK_GE(app_index, 0);
      if (running_index != app_index)
        model_->Move(running_index, app_index);
      running_index++;
    }
  }
}

void ChromeLauncherController::RemoveShelfItem(ash::ShelfID id) {
  const int index = model_->ItemIndexByID(id);
  if (index >= 0 && index < model_->item_count())
    model_->RemoveItemAt(index);
}

void ChromeLauncherController::PinRunningAppInternal(int index,
                                                     ash::ShelfID shelf_id) {
  DCHECK_EQ(GetItem(shelf_id)->type, ash::TYPE_APP);
  SetItemType(shelf_id, ash::TYPE_PINNED_APP);
  int running_index = model_->ItemIndexByID(shelf_id);
  if (running_index < index)
    --index;
  if (running_index != index)
    model_->Move(running_index, index);
}

void ChromeLauncherController::UnpinRunningAppInternal(int index) {
  DCHECK(index >= 0 && index < model_->item_count());
  ash::ShelfItem item = model_->items()[index];
  DCHECK_EQ(item.type, ash::TYPE_PINNED_APP);
  SetItemType(item.id, ash::TYPE_APP);
}

void ChromeLauncherController::SyncPinPosition(ash::ShelfID shelf_id) {
  DCHECK(should_sync_pin_changes_);
  DCHECK(shelf_id);

  const int max_index = model_->item_count();
  const int index = model_->ItemIndexByID(shelf_id);
  DCHECK_GT(index, 0);

  const std::string& app_id = GetAppIDForShelfID(shelf_id);
  DCHECK(!app_id.empty());
  const std::string& launch_id = GetLaunchIDForShelfID(shelf_id);

  std::string app_id_before;
  std::string launch_id_before;
  std::vector<ash::AppLaunchId> app_launch_ids_after;

  for (int i = index - 1; i > 0; --i) {
    const ash::ShelfID shelf_id_before = model_->items()[i].id;
    if (IsPinned(shelf_id_before)) {
      app_id_before = GetAppIDForShelfID(shelf_id_before);
      DCHECK(!app_id_before.empty());
      launch_id_before = GetLaunchIDForShelfID(shelf_id_before);
      break;
    }
  }

  for (int i = index + 1; i < max_index; ++i) {
    const ash::ShelfID shelf_id_after = model_->items()[i].id;
    if (IsPinned(shelf_id_after)) {
      const std::string app_id_after = GetAppIDForShelfID(shelf_id_after);
      DCHECK(!app_id_after.empty());
      const std::string launch_id_after = GetLaunchIDForShelfID(shelf_id_after);
      app_launch_ids_after.push_back(
          ash::AppLaunchId(app_id_after, launch_id_after));
    }
  }

  ash::AppLaunchId app_launch_id_before =
      app_id_before.empty() ? ash::AppLaunchId()
                            : ash::AppLaunchId(app_id_before, launch_id_before);

  ash::launcher::SetPinPosition(profile(), ash::AppLaunchId(app_id, launch_id),
                                app_launch_id_before, app_launch_ids_after);
}

void ChromeLauncherController::OnSyncModelUpdated() {
  UpdateAppLaunchersFromPref();
}

void ChromeLauncherController::OnIsSyncingChanged() {
  UpdateAppLaunchersFromPref();
}

void ChromeLauncherController::ScheduleUpdateAppLaunchersFromPref() {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&ChromeLauncherController::UpdateAppLaunchersFromPref,
                 weak_ptr_factory_.GetWeakPtr()));
}

void ChromeLauncherController::UpdateAppLaunchersFromPref() {
  // Do not sync pin changes during this function to avoid cyclical updates.
  // This function makes the shelf model reflect synced prefs, and should not
  // cyclically trigger sync changes (eg. ShelfItemAdded calls SyncPinPosition).
  ScopedPinSyncDisabler scoped_pin_sync_disabler = GetScopedPinSyncDisabler();

  const std::vector<ash::AppLaunchId> pinned_apps =
      ash::launcher::GetPinnedAppsFromPrefs(profile()->GetPrefs(),
                                            launcher_controller_helper_.get());

  int index = 0;
  // Skip app list items if it exists.
  if (model_->items()[0].type == ash::TYPE_APP_LIST)
    ++index;

  // Apply pins in two steps. At the first step, go through the list of apps to
  // pin, move existing pin to current position specified by |index| or create
  // the new pin at that position.
  for (const auto& pref_app_launch_id : pinned_apps) {
    // Filter out apps that may be mapped wrongly.
    // TODO(khmel):  b/31703859 is to refactore shelf mapping.
    const std::string app_id = pref_app_launch_id.app_id();
    const std::string shelf_app_id =
        ArcAppWindowLauncherController::GetShelfAppIdFromArcAppId(app_id);
    if (shelf_app_id != app_id)
      continue;

    // Update apps icon if applicable.
    OnAppUpdated(profile(), app_id);

    // Find existing pin or app from the right of current |index|.
    int app_index = index;
    for (; app_index < model_->item_count(); ++app_index) {
      const ash::ShelfItem& item = model_->items()[app_index];
      if (item.app_launch_id.app_id() == app_id &&
          item.app_launch_id.launch_id() == pref_app_launch_id.launch_id()) {
        break;
      }
    }
    if (app_index < model_->item_count()) {
      // Found existing pin or running app.
      const ash::ShelfItem item = model_->items()[app_index];
      if (ItemTypeIsPinned(item)) {
        // Just move to required position or keep it inplace.
        model_->Move(app_index, index);
      } else {
        PinRunningAppInternal(index, item.id);
      }
      DCHECK_EQ(model_->ItemIndexByID(item.id), index);
    } else {
      // This is fresh pin. Create new one.
      DCHECK_NE(app_id, kChromeAppId);
      CreateAppShortcutLauncherItem(pref_app_launch_id, index);
    }
    ++index;
  }

  // At second step remove any pin to the right from the current index.
  while (index < model_->item_count()) {
    const ash::ShelfItem item = model_->items()[index];
    if (item.type == ash::TYPE_PINNED_APP)
      UnpinShelfItemInternal(item.id);
    else
      ++index;
  }

  UpdatePolicyPinnedAppsFromPrefs();
}

void ChromeLauncherController::UpdatePolicyPinnedAppsFromPrefs() {
  for (int index = 0; index < model_->item_count(); index++) {
    ash::ShelfItem item = model_->items()[index];
    const bool pinned_by_policy =
        GetPinnableForAppID(item.app_launch_id.app_id(), profile()) ==
        AppListControllerDelegate::PIN_FIXED;
    if (item.pinned_by_policy != pinned_by_policy) {
      item.pinned_by_policy = pinned_by_policy;
      model_->Set(index, item);
    }
  }
}

void ChromeLauncherController::SetVirtualKeyboardBehaviorFromPrefs() {
  const PrefService* service = profile()->GetPrefs();
  const bool was_enabled = keyboard::IsKeyboardEnabled();
  if (!service->HasPrefPath(prefs::kTouchVirtualKeyboardEnabled)) {
    keyboard::SetKeyboardShowOverride(keyboard::KEYBOARD_SHOW_OVERRIDE_NONE);
  } else {
    const bool enable =
        service->GetBoolean(prefs::kTouchVirtualKeyboardEnabled);
    keyboard::SetKeyboardShowOverride(
        enable ? keyboard::KEYBOARD_SHOW_OVERRIDE_ENABLED
               : keyboard::KEYBOARD_SHOW_OVERRIDE_DISABLED);
  }
  // TODO(crbug.com/557406): Fix this interaction pattern in Mash.
  if (!ash_util::IsRunningInMash()) {
    const bool is_enabled = keyboard::IsKeyboardEnabled();
    if (was_enabled && !is_enabled)
      ash::Shell::Get()->DeactivateKeyboard();
    else if (is_enabled && !was_enabled)
      ash::Shell::Get()->CreateKeyboard();
  }
}

ash::ShelfItemStatus ChromeLauncherController::GetAppState(
    const std::string& app_id) {
  ash::ShelfItemStatus status = ash::STATUS_CLOSED;
  for (WebContentsToAppIDMap::iterator it = web_contents_to_app_id_.begin();
       it != web_contents_to_app_id_.end(); ++it) {
    if (it->second == app_id) {
      Browser* browser = chrome::FindBrowserWithWebContents(it->first);
      // Usually there should never be an item in our |web_contents_to_app_id_|
      // list which got deleted already. However - in some situations e.g.
      // Browser::SwapTabContent there is temporarily no associated browser.
      if (!browser)
        continue;
      if (browser->window()->IsActive()) {
        return browser->tab_strip_model()->GetActiveWebContents() == it->first
                   ? ash::STATUS_ACTIVE
                   : ash::STATUS_RUNNING;
      } else {
        status = ash::STATUS_RUNNING;
      }
    }
  }
  return status;
}

ash::ShelfID ChromeLauncherController::InsertAppLauncherItem(
    std::unique_ptr<ash::ShelfItemDelegate> item_delegate,
    ash::ShelfItemStatus status,
    int index,
    ash::ShelfItemType shelf_item_type) {
  ash::ShelfID id = model_->next_id();
  CHECK(!GetItem(id));
  CHECK(item_delegate);
  // Ash's ShelfWindowWatcher handles app panel windows separately.
  DCHECK_NE(ash::TYPE_APP_PANEL, shelf_item_type);
  ash::ShelfItem item;
  item.status = status;
  item.type = shelf_item_type;
  item.app_launch_id = item_delegate->app_launch_id();
  // Set the delegate first to avoid constructing one in ShelfItemAdded.
  model_->SetShelfItemDelegate(id, std::move(item_delegate));
  model_->AddAt(index, item);
  return id;
}

void ChromeLauncherController::CreateBrowserShortcutLauncherItem() {
  // Do not sync the pin position of the browser shortcut item when it is added;
  // its initial position before prefs have loaded is unimportant and the sync
  // service may not yet be initialized.
  ScopedPinSyncDisabler scoped_pin_sync_disabler = GetScopedPinSyncDisabler();

  ash::ShelfItem browser_shortcut;
  browser_shortcut.type = ash::TYPE_BROWSER_SHORTCUT;
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  browser_shortcut.image = *rb.GetImageSkiaNamed(IDR_PRODUCT_LOGO_32);
  browser_shortcut.title = l10n_util::GetStringUTF16(IDS_PRODUCT_NAME);
  browser_shortcut.app_launch_id = ash::AppLaunchId(kChromeAppId);
  ash::ShelfID id = model_->next_id();
  std::unique_ptr<BrowserShortcutLauncherItemController> item_delegate =
      base::MakeUnique<BrowserShortcutLauncherItemController>(model_);
  BrowserShortcutLauncherItemController* item_controller = item_delegate.get();
  // Set the delegate first to avoid constructing another one in ShelfItemAdded.
  model_->SetShelfItemDelegate(id, std::move(item_delegate));
  model_->AddAt(0, browser_shortcut);
  item_controller->UpdateBrowserItemState();
}

bool ChromeLauncherController::IsIncognito(
    const content::WebContents* web_contents) const {
  const Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  return profile->IsOffTheRecord() && !profile->IsGuestSession() &&
         !profile->IsSystemProfile();
}

int ChromeLauncherController::FindInsertionPoint() {
  for (int i = model_->item_count() - 1; i > 0; --i) {
    if (ItemTypeIsPinned(model_->items()[i]))
      return i;
  }
  return 0;
}

void ChromeLauncherController::CloseWindowedAppsFromRemovedExtension(
    const std::string& app_id,
    const Profile* profile) {
  // This function cannot rely on the controller's enumeration functionality
  // since the extension has already been unloaded.
  const BrowserList* browser_list = BrowserList::GetInstance();
  std::vector<Browser*> browser_to_close;
  for (BrowserList::const_reverse_iterator it =
           browser_list->begin_last_active();
       it != browser_list->end_last_active(); ++it) {
    Browser* browser = *it;
    if (!browser->is_type_tabbed() && browser->is_type_popup() &&
        browser->is_app() &&
        app_id ==
            web_app::GetExtensionIdFromApplicationName(browser->app_name()) &&
        profile == browser->profile()) {
      browser_to_close.push_back(browser);
    }
  }
  while (!browser_to_close.empty()) {
    TabStripModel* tab_strip = browser_to_close.back()->tab_strip_model();
    if (!tab_strip->empty())
      tab_strip->CloseWebContentsAt(0, TabStripModel::CLOSE_NONE);
    browser_to_close.pop_back();
  }
}

void ChromeLauncherController::AttachProfile(Profile* profile_to_attach) {
  profile_ = profile_to_attach;
  // Either add the profile to the list of known profiles and make it the active
  // one for some functions of LauncherControllerHelper or create a new one.
  if (!launcher_controller_helper_.get()) {
    launcher_controller_helper_ =
        base::MakeUnique<LauncherControllerHelper>(profile_);
  } else {
    launcher_controller_helper_->set_profile(profile_);
  }

  // TODO(skuhne): The AppIconLoaderImpl has the same problem. Each loaded
  // image is associated with a profile (its loader requires the profile).
  // Since icon size changes are possible, the icon could be requested to be
  // reloaded. However - having it not multi profile aware would cause problems
  // if the icon cache gets deleted upon user switch.
  std::unique_ptr<AppIconLoader> extension_app_icon_loader =
      base::MakeUnique<extensions::ExtensionAppIconLoader>(
          profile_, extension_misc::EXTENSION_ICON_SMALL, this);
  app_icon_loaders_.push_back(std::move(extension_app_icon_loader));

  if (arc::IsArcAllowedForProfile(profile_)) {
    std::unique_ptr<AppIconLoader> arc_app_icon_loader =
        base::MakeUnique<ArcAppIconLoader>(
            profile_, extension_misc::EXTENSION_ICON_SMALL, this);
    app_icon_loaders_.push_back(std::move(arc_app_icon_loader));
  }

  SetShelfBehaviorsFromPrefs();

  pref_change_registrar_.Init(profile()->GetPrefs());
  pref_change_registrar_.Add(
      prefs::kPolicyPinnedLauncherApps,
      base::Bind(&ChromeLauncherController::UpdateAppLaunchersFromPref,
                 base::Unretained(this)));
  // Handling of prefs::kArcEnabled change should be called deferred to avoid
  // race condition when OnAppUninstalledPrepared for ARC apps is called after
  // UpdateAppLaunchersFromPref.
  pref_change_registrar_.Add(
      prefs::kArcEnabled,
      base::Bind(&ChromeLauncherController::ScheduleUpdateAppLaunchersFromPref,
                 base::Unretained(this)));
  pref_change_registrar_.Add(
      prefs::kShelfAlignmentLocal,
      base::Bind(&ChromeLauncherController::SetShelfAlignmentFromPrefs,
                 base::Unretained(this)));
  pref_change_registrar_.Add(
      prefs::kShelfAutoHideBehaviorLocal,
      base::Bind(&ChromeLauncherController::SetShelfAutoHideBehaviorFromPrefs,
                 base::Unretained(this)));
  pref_change_registrar_.Add(
      prefs::kShelfPreferences,
      base::Bind(&ChromeLauncherController::SetShelfBehaviorsFromPrefs,
                 base::Unretained(this)));
  pref_change_registrar_.Add(
      prefs::kTouchVirtualKeyboardEnabled,
      base::Bind(&ChromeLauncherController::SetVirtualKeyboardBehaviorFromPrefs,
                 base::Unretained(this)));

  std::unique_ptr<LauncherAppUpdater> extension_app_updater(
      new LauncherExtensionAppUpdater(this, profile()));
  app_updaters_.push_back(std::move(extension_app_updater));

  if (arc::IsArcAllowedForProfile(profile())) {
    std::unique_ptr<LauncherAppUpdater> arc_app_updater(
        new LauncherArcAppUpdater(this, profile()));
    app_updaters_.push_back(std::move(arc_app_updater));
  }

  app_list::AppListSyncableService* app_service =
      app_list::AppListSyncableServiceFactory::GetForProfile(profile());
  if (app_service)
    app_service->AddObserverAndStart(this);

  PrefServiceSyncableFromProfile(profile())->AddObserver(this);
}

void ChromeLauncherController::ReleaseProfile() {
  if (app_sync_ui_state_)
    app_sync_ui_state_->RemoveObserver(this);

  app_updaters_.clear();

  prefs_observer_.reset();

  pref_change_registrar_.RemoveAll();

  app_list::AppListSyncableService* app_service =
      app_list::AppListSyncableServiceFactory::GetForProfile(profile());
  if (app_service)
    app_service->RemoveObserver(this);

  PrefServiceSyncableFromProfile(profile())->RemoveObserver(this);
}

///////////////////////////////////////////////////////////////////////////////
// ash::mojom::ShelfObserver:

void ChromeLauncherController::OnShelfCreated(int64_t display_id) {
  if (!ConnectToShelfController())
    return;

  // The pref helper functions return default values for invalid display ids.
  PrefService* prefs = profile_->GetPrefs();
  shelf_controller_->SetAlignment(
      ash::launcher::GetShelfAlignmentPref(prefs, display_id), display_id);
  shelf_controller_->SetAutoHideBehavior(
      ash::launcher::GetShelfAutoHideBehaviorPref(prefs, display_id),
      display_id);
}

void ChromeLauncherController::OnAlignmentChanged(ash::ShelfAlignment alignment,
                                                  int64_t display_id) {
  // The locked alignment is set temporarily and not saved to preferences.
  if (alignment == ash::SHELF_ALIGNMENT_BOTTOM_LOCKED)
    return;
  DCHECK(!updating_shelf_pref_from_observer_);
  base::AutoReset<bool> updating(&updating_shelf_pref_from_observer_, true);
  // This will uselessly store a preference value for invalid display ids.
  ash::launcher::SetShelfAlignmentPref(profile_->GetPrefs(), display_id,
                                       alignment);
}

void ChromeLauncherController::OnAutoHideBehaviorChanged(
    ash::ShelfAutoHideBehavior auto_hide,
    int64_t display_id) {
  DCHECK(!updating_shelf_pref_from_observer_);
  base::AutoReset<bool> updating(&updating_shelf_pref_from_observer_, true);
  // This will uselessly store a preference value for invalid display ids.
  ash::launcher::SetShelfAutoHideBehaviorPref(profile_->GetPrefs(), display_id,
                                              auto_hide);
}

///////////////////////////////////////////////////////////////////////////////
// ash::ShelfModelObserver:

void ChromeLauncherController::ShelfItemAdded(int index) {
  // Update the pin position preference as needed.
  ash::ShelfItem item = model_->items()[index];
  if (ItemTypeIsPinned(item) && should_sync_pin_changes_)
    SyncPinPosition(item.id);

  // TODO(khmel): Fix this Arc application id mapping. See http://b/31703859
  const std::string shelf_app_id =
      ArcAppWindowLauncherController::GetShelfAppIdFromArcAppId(
          item.app_launch_id.app_id());

  // Fetch and update the icon for the app's item.
  AppIconLoader* app_icon_loader = GetAppIconLoaderForApp(shelf_app_id);
  if (app_icon_loader) {
    app_icon_loader->FetchImage(shelf_app_id);
    app_icon_loader->UpdateImage(shelf_app_id);
  }

  // Update the item with any missing Chrome-specific info.
  if (item.type == ash::TYPE_APP || item.type == ash::TYPE_PINNED_APP) {
    bool needs_update = false;
    if (item.image.isNull()) {
      needs_update = true;
      item.image = extensions::util::GetDefaultAppIcon();
    }
    if (item.title.empty()) {
      needs_update = true;
      item.title =
          LauncherControllerHelper::GetAppTitle(profile(), shelf_app_id);
    }
    ash::ShelfItemStatus status = GetAppState(shelf_app_id);
    if (status != item.status && status != ash::STATUS_CLOSED) {
      needs_update = true;
      item.status = status;
    }
    if (needs_update)
      model_->Set(index, item);
  }

  // Construct a ShelfItemDelegate for the item if one does not yet exist.
  if (!model_->GetShelfItemDelegate(item.id)) {
    model_->SetShelfItemDelegate(
        item.id, AppShortcutLauncherItemController::Create(ash::AppLaunchId(
                     shelf_app_id, item.app_launch_id.launch_id())));
  }
}

void ChromeLauncherController::ShelfItemRemoved(
    int index,
    const ash::ShelfItem& old_item) {
  // TODO(khmel): Fix this Arc application id mapping. See http://b/31703859
  const std::string shelf_app_id =
      ArcAppWindowLauncherController::GetShelfAppIdFromArcAppId(
          old_item.app_launch_id.app_id());

  // Remove the pin position from preferences as needed.
  if (ItemTypeIsPinned(old_item) && should_sync_pin_changes_) {
    ash::AppLaunchId app_launch_id(shelf_app_id,
                                   old_item.app_launch_id.launch_id());
    ash::launcher::RemovePinPosition(profile(), app_launch_id);
  }

  AppIconLoader* app_icon_loader = GetAppIconLoaderForApp(shelf_app_id);
  if (app_icon_loader)
    app_icon_loader->ClearImage(shelf_app_id);
}

void ChromeLauncherController::ShelfItemMoved(int start_index,
                                              int target_index) {
  // Update the pin position preference as needed.
  const ash::ShelfItem& item = model_->items()[target_index];
  DCHECK_NE(ash::TYPE_APP_LIST, item.type);
  if (ItemTypeIsPinned(item) && should_sync_pin_changes_)
    SyncPinPosition(item.id);
}

void ChromeLauncherController::ShelfItemChanged(
    int index,
    const ash::ShelfItem& old_item) {
  if (!should_sync_pin_changes_)
    return;

  const ash::ShelfItem& item = model_->items()[index];
  // Add or remove the pin position from preferences as needed.
  if (!ItemTypeIsPinned(old_item) && ItemTypeIsPinned(item)) {
    SyncPinPosition(item.id);
  } else if (ItemTypeIsPinned(old_item) && !ItemTypeIsPinned(item)) {
    // TODO(khmel): Fix this Arc application id mapping. See http://b/31703859
    const std::string shelf_app_id =
        ArcAppWindowLauncherController::GetShelfAppIdFromArcAppId(
            old_item.app_launch_id.app_id());

    ash::AppLaunchId app_launch_id(shelf_app_id,
                                   old_item.app_launch_id.launch_id());
    ash::launcher::RemovePinPosition(profile(), app_launch_id);
  }
}

///////////////////////////////////////////////////////////////////////////////
// ash::WindowTreeHostManager::Observer:

void ChromeLauncherController::OnDisplayConfigurationChanged() {
  // In BOTTOM_LOCKED state, ignore the call of SetShelfBehaviorsFromPrefs.
  // Because it might be called by some operations, like crbug.com/627040
  // rotating screen.
  ash::WmShelf* shelf =
      ash::WmShelf::ForWindow(ash::ShellPort::Get()->GetPrimaryRootWindow());
  if (shelf->alignment() != ash::SHELF_ALIGNMENT_BOTTOM_LOCKED)
    SetShelfBehaviorsFromPrefs();
}

///////////////////////////////////////////////////////////////////////////////
// AppSyncUIStateObserver:

void ChromeLauncherController::OnAppSyncUIStatusChanged() {
  // Update the app list button title to reflect the syncing status.
  base::string16 title = l10n_util::GetStringUTF16(
      app_sync_ui_state_->status() == AppSyncUIState::STATUS_SYNCING
          ? IDS_ASH_SHELF_APP_LIST_LAUNCHER_SYNCING_TITLE
          : IDS_ASH_SHELF_APP_LIST_LAUNCHER_TITLE);

  const int app_list_index = model_->GetItemIndexForType(ash::TYPE_APP_LIST);
  DCHECK_GE(app_list_index, 0);
  ash::ShelfItem item = model_->items()[app_list_index];
  if (item.title != title) {
    item.title = title;
    model_->Set(app_list_index, item);
  }
}
