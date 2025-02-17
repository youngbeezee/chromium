// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_UI_WEBUI_MOJO_WEB_UI_IOS_CONTROLLER_H_
#define IOS_CHROME_BROWSER_UI_WEBUI_MOJO_WEB_UI_IOS_CONTROLLER_H_

#import "ios/web/public/web_state/web_state.h"
#include "ios/web/public/webui/web_ui_ios.h"
#include "ios/web/public/webui/web_ui_ios_controller.h"
#include "services/service_manager/public/cpp/interface_factory.h"

// This class is intended for web ui pages that use mojo. It is expected that
// subclasses will do two things:
// . In the constructor invoke AddMojoResourcePath() to register the bindings
//   files, eg:
//     AddMojoResourcePath("chrome/browser/ui/webui/version.mojom",
//                         IDR_IOS_VERSION_MOJO_JS);
// . Override BindUIHandler() to create and bind the implementation of the
//   bindings.
template <typename Interface>
class MojoWebUIIOSController
    : public web::WebUIIOSController,
      public service_manager::InterfaceFactory<Interface> {
 public:
  explicit MojoWebUIIOSController(web::WebUIIOS* web_ui)
      : web::WebUIIOSController(web_ui) {
    web_ui->GetWebState()->GetMojoInterfaceRegistry()->AddInterface(this);
  }

 protected:
  // Invoked to create the specific bindings implementation.
  virtual void BindUIHandler(mojo::InterfaceRequest<Interface> request) = 0;

 private:
  // service_manager::InterfaceFactory overrides:
  void Create(const service_manager::Identity& remote_identity,
              mojo::InterfaceRequest<Interface> request) override {
    BindUIHandler(std::move(request));
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MojoWebUIIOSController);
};

#endif  // IOS_CHROME_BROWSER_UI_WEBUI_MOJO_WEB_UI_IOS_CONTROLLER_H_
