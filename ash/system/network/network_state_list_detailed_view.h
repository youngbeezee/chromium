// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_NETWORK_NETWORK_STATE_LIST_DETAILED_VIEW_H_
#define ASH_SYSTEM_NETWORK_NETWORK_STATE_LIST_DETAILED_VIEW_H_

#include <memory>
#include <string>

#include "ash/login_status.h"
#include "ash/system/tray/tray_details_view.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "ui/gfx/image/image.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/custom_button.h"

namespace ash {
class NetworkListViewBase;
}

namespace views {
class BubbleDialogDelegateView;
}

namespace ash {
class SystemTrayItem;

namespace tray {

class NetworkStateListDetailedView
    : public TrayDetailsView,
      public base::SupportsWeakPtr<NetworkStateListDetailedView> {
 public:
  enum ListType { LIST_TYPE_NETWORK, LIST_TYPE_VPN };

  NetworkStateListDetailedView(SystemTrayItem* owner,
                               ListType list_type,
                               LoginStatus login);
  ~NetworkStateListDetailedView() override;

  void Init();

  // Called when the contents of the network list have changed or when any
  // Manager properties (e.g. technology state) have changed.
  void Update();

  // Called when the user clicks on an entry representing a network in the list.
  void OnNetworkEntryClicked(views::View* sender);

  void RelayoutScrollList();

 private:
  class InfoBubble;

  // TrayDetailsView:
  void HandleViewClicked(views::View* view) override;
  void HandleButtonPressed(views::Button* sender,
                           const ui::Event& event) override;
  void CreateExtraTitleRowButtons() override;

  // Launches the WebUI settings in a browser and closes the system menu.
  void ShowSettings();

  // Update UI components.
  void UpdateNetworkList();
  void UpdateHeaderButtons();

  // Create and manage the network info bubble.
  void ToggleInfoBubble();
  bool ResetInfoBubble();
  void OnInfoBubbleDestroyed();
  views::View* CreateNetworkInfoView();

  // Periodically request a network scan.
  void CallRequestScan();

  // Type of list (all networks or vpn)
  ListType list_type_;

  // Track login state.
  LoginStatus login_;

  views::CustomButton* info_button_;
  views::CustomButton* settings_button_;
  views::CustomButton* proxy_settings_button_;

  // A small bubble for displaying network info.
  views::BubbleDialogDelegateView* info_bubble_;

  std::unique_ptr<NetworkListViewBase> network_list_view_;

  DISALLOW_COPY_AND_ASSIGN(NetworkStateListDetailedView);
};

}  // namespace tray
}  // namespace ash

#endif  // ASH_SYSTEM_NETWORK_NETWORK_STATE_LIST_DETAILED_VIEW_H_
