// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/update/tray_update.h"

#include "ash/public/interfaces/update.mojom.h"
#include "ash/shell.h"
#include "ash/system/tray/system_tray.h"
#include "ash/system/tray/system_tray_controller.h"
#include "ash/test/ash_test.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/events/event.h"
#include "ui/views/controls/label.h"

namespace ash {

using TrayUpdateTest = AshTest;

// Tests that the update icon becomes visible when an update becomes
// available.
TEST_F(TrayUpdateTest, VisibilityAfterUpdate) {
  SystemTray* tray = GetPrimarySystemTray();
  TrayUpdate* tray_update = tray->tray_update();

  // The system starts with no update pending, so the icon isn't visible.
  EXPECT_FALSE(tray_update->tray_view()->visible());

  // Simulate an update.
  Shell::Get()->system_tray_controller()->ShowUpdateIcon(
      mojom::UpdateSeverity::LOW, false, mojom::UpdateType::SYSTEM);

  // Tray item is now visible.
  EXPECT_TRUE(tray_update->tray_view()->visible());

  tray->ShowDefaultView(BUBBLE_CREATE_NEW);
  base::string16 label = tray_update->GetLabelForTesting()->text();
  EXPECT_EQ("Restart to update", base::UTF16ToUTF8(label));
}

TEST_F(TrayUpdateTest, VisibilityAfterFlashUpdate) {
  SystemTray* tray = GetPrimarySystemTray();
  TrayUpdate* tray_update = tray->tray_update();

  // Simulate an update.
  Shell::Get()->system_tray_controller()->ShowUpdateIcon(
      mojom::UpdateSeverity::LOW, false, mojom::UpdateType::FLASH);

  // Tray item is now visible.
  EXPECT_TRUE(tray_update->tray_view()->visible());

  tray->ShowDefaultView(BUBBLE_CREATE_NEW);
  base::string16 label = tray_update->GetLabelForTesting()->text();
  EXPECT_EQ("Restart to update Adobe Flash Player", base::UTF16ToUTF8(label));
}

}  // namespace ash
