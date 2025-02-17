// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_SERVICE_MANAGER_EMBEDDER_SWITCHES_H_
#define SERVICES_SERVICE_MANAGER_EMBEDDER_SWITCHES_H_

#include "build/build_config.h"
#include "services/service_manager/embedder/service_manager_embedder_export.h"

namespace service_manager {
namespace switches {

#if defined(OS_WIN)
SERVICE_MANAGER_EMBEDDER_EXPORT
extern const char kDefaultServicePrefetchArgument[];
#endif  // defined(OS_WIN)

SERVICE_MANAGER_EMBEDDER_EXPORT extern const char kEnableLogging[];
SERVICE_MANAGER_EMBEDDER_EXPORT extern const char kProcessType[];
SERVICE_MANAGER_EMBEDDER_EXPORT extern const char kProcessTypeServiceManager[];
SERVICE_MANAGER_EMBEDDER_EXPORT extern const char kProcessTypeService[];
SERVICE_MANAGER_EMBEDDER_EXPORT extern const char kSharedFiles[];

}  // namespace switches
}  // namespace service_manager

#endif  // SERVICES_SERVICE_MANAGER_EMBEDDER_SWITCHES_H_
