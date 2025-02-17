// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_SERVICE_H_
#define SERVICES_UI_SERVICE_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "components/discardable_memory/public/interfaces/discardable_shared_memory_manager.mojom.h"
#include "services/service_manager/public/cpp/binder_registry.h"
#include "services/service_manager/public/cpp/interface_factory.h"
#include "services/service_manager/public/cpp/service.h"
#include "services/service_manager/public/cpp/service_runner.h"
#include "services/tracing/public/cpp/provider.h"
#include "services/ui/ime/ime_registrar_impl.h"
#include "services/ui/ime/ime_server_impl.h"
#include "services/ui/input_devices/input_device_server.h"
#include "services/ui/public/interfaces/accessibility_manager.mojom.h"
#include "services/ui/public/interfaces/clipboard.mojom.h"
#include "services/ui/public/interfaces/display_manager.mojom.h"
#include "services/ui/public/interfaces/gpu.mojom.h"
#include "services/ui/public/interfaces/ime/ime.mojom.h"
#include "services/ui/public/interfaces/user_access_manager.mojom.h"
#include "services/ui/public/interfaces/user_activity_monitor.mojom.h"
#include "services/ui/public/interfaces/window_manager_window_tree_factory.mojom.h"
#include "services/ui/public/interfaces/window_server_test.mojom.h"
#include "services/ui/public/interfaces/window_tree.mojom.h"
#include "services/ui/public/interfaces/window_tree_host.mojom.h"
#include "services/ui/ws/user_id.h"
#include "services/ui/ws/window_server_delegate.h"

#if defined(USE_OZONE)
#include "ui/ozone/public/client_native_pixmap_factory_ozone.h"
#endif

namespace discardable_memory {
class DiscardableSharedMemoryManager;
}

namespace display {
class ScreenManager;
}

namespace service_manager {
class Connector;
}

namespace ui {

class PlatformEventSource;

namespace ws {
class WindowServer;
}

class Service
    : public service_manager::Service,
      public ws::WindowServerDelegate,
      public service_manager::InterfaceFactory<mojom::AccessibilityManager>,
      public service_manager::InterfaceFactory<mojom::Clipboard>,
      public service_manager::InterfaceFactory<mojom::DisplayManager>,
      public service_manager::InterfaceFactory<mojom::Gpu>,
      public service_manager::InterfaceFactory<mojom::IMERegistrar>,
      public service_manager::InterfaceFactory<mojom::IMEServer>,
      public service_manager::InterfaceFactory<mojom::UserAccessManager>,
      public service_manager::InterfaceFactory<mojom::UserActivityMonitor>,
      public service_manager::InterfaceFactory<
          mojom::WindowManagerWindowTreeFactory>,
      public service_manager::InterfaceFactory<mojom::WindowTreeFactory>,
      public service_manager::InterfaceFactory<mojom::WindowTreeHostFactory>,
      public service_manager::InterfaceFactory<
          discardable_memory::mojom::DiscardableSharedMemoryManager>,
      public service_manager::InterfaceFactory<mojom::WindowServerTest> {
 public:
  Service();
  ~Service() override;

 private:
  // How the ScreenManager is configured.
  enum ScreenManagerConfig {
    // Initial state.
    UNKNOWN,

    // ScreenManager runs locally.
    INTERNAL,

    // Used when the window manager supplies a value of false for
    // |automatically_create_display_roots|. In this config the ScreenManager
    // is configured to forward calls.
    FORWARDING,
  };

  // Holds InterfaceRequests received before the first WindowTreeHost Display
  // has been established.
  struct PendingRequest;
  struct UserState;

  using UserIdToUserState = std::map<ws::UserId, std::unique_ptr<UserState>>;

  void InitializeResources(service_manager::Connector* connector);

  // Returns the user specific state for the user id of |remote_identity|.
  // Service owns the return value.
  // TODO(sky): if we allow removal of user ids then we need to close anything
  // associated with the user (all incoming pipes...) on removal.
  UserState* GetUserState(const service_manager::Identity& remote_identity);

  void AddUserIfNecessary(const service_manager::Identity& remote_identity);

  // service_manager::Service:
  void OnStart() override;
  void OnBindInterface(const service_manager::ServiceInfo& source_info,
                       const std::string& interface_name,
                       mojo::ScopedMessagePipeHandle interface_pipe) override;

  // WindowServerDelegate:
  void StartDisplayInit() override;
  void OnFirstDisplayReady() override;
  void OnNoMoreDisplays() override;
  bool IsTestConfig() const override;
  void OnWillCreateTreeForWindowManager(
      bool automatically_create_display_roots) override;

  // service_manager::InterfaceFactory<mojom::AccessibilityManager>
  // implementation.
  void Create(const service_manager::Identity& remote_identity,
              mojom::AccessibilityManagerRequest request) override;

  // service_manager::InterfaceFactory<mojom::Clipboard> implementation.
  void Create(const service_manager::Identity& remote_identity,
              mojom::ClipboardRequest request) override;

  // service_manager::InterfaceFactory<mojom::DisplayManager> implementation.
  void Create(const service_manager::Identity& remote_identity,
              mojom::DisplayManagerRequest request) override;

  // service_manager::InterfaceFactory<mojom::Gpu> implementation.
  void Create(const service_manager::Identity& remote_identity,
              mojom::GpuRequest request) override;

  // service_manager::InterfaceFactory<mojom::IMERegistrar> implementation.
  void Create(const service_manager::Identity& remote_identity,
              mojom::IMERegistrarRequest request) override;

  // service_manager::InterfaceFactory<mojom::IMEServer> implementation.
  void Create(const service_manager::Identity& remote_identity,
              mojom::IMEServerRequest request) override;

  // service_manager::InterfaceFactory<mojom::UserAccessManager> implementation.
  void Create(const service_manager::Identity& remote_identity,
              mojom::UserAccessManagerRequest request) override;

  // service_manager::InterfaceFactory<mojom::UserActivityMonitor>
  // implementation.
  void Create(const service_manager::Identity& remote_identity,
              mojom::UserActivityMonitorRequest request) override;

  // service_manager::InterfaceFactory<mojom::WindowManagerWindowTreeFactory>
  // implementation.
  void Create(const service_manager::Identity& remote_identity,
              mojom::WindowManagerWindowTreeFactoryRequest request) override;

  // service_manager::InterfaceFactory<mojom::WindowTreeFactory>:
  void Create(const service_manager::Identity& remote_identity,
              mojom::WindowTreeFactoryRequest request) override;

  // service_manager::InterfaceFactory<mojom::WindowTreeHostFactory>:
  void Create(const service_manager::Identity& remote_identity,
              mojom::WindowTreeHostFactoryRequest request) override;

  // service_manager::InterfaceFactory<
  //    discardable_memory::mojom::DiscardableSharedMemoryManager>:
  void Create(const service_manager::Identity& remote_identity,
              discardable_memory::mojom::DiscardableSharedMemoryManagerRequest
                  request) override;

  // service_manager::InterfaceFactory<mojom::WindowServerTest> implementation.
  void Create(const service_manager::Identity& remote_identity,
              mojom::WindowServerTestRequest request) override;

  std::unique_ptr<ws::WindowServer> window_server_;
  std::unique_ptr<ui::PlatformEventSource> event_source_;
  tracing::Provider tracing_;
  using PendingRequests = std::vector<std::unique_ptr<PendingRequest>>;
  PendingRequests pending_requests_;

  UserIdToUserState user_id_to_user_state_;

  // Provides input-device information via Mojo IPC. Registers Mojo interfaces
  // and must outlive |registry_|.
  InputDeviceServer input_device_server_;

  bool test_config_;
#if defined(USE_OZONE)
  std::unique_ptr<gfx::ClientNativePixmapFactory> client_native_pixmap_factory_;
#endif

  // Manages display hardware and handles display management. May register Mojo
  // interfaces and must outlive |registry_|.
  std::unique_ptr<display::ScreenManager> screen_manager_;

  IMERegistrarImpl ime_registrar_;
  IMEServerImpl ime_server_;

  std::unique_ptr<discardable_memory::DiscardableSharedMemoryManager>
      discardable_shared_memory_manager_;

  service_manager::BinderRegistry registry_;

  // Set to true in StartDisplayInit().
  bool is_gpu_ready_ = false;
  ScreenManagerConfig screen_manager_config_ = ScreenManagerConfig::UNKNOWN;

  DISALLOW_COPY_AND_ASSIGN(Service);
};

}  // namespace ui

#endif  // SERVICES_UI_SERVICE_H_
