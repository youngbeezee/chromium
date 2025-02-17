// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CLIENT_IOS_APP_HOST_VIEW_CONTROLLER_H_
#define REMOTING_CLIENT_IOS_APP_HOST_VIEW_CONTROLLER_H_

#import <GLKit/GLKit.h>

@class RemotingClient;

@interface HostViewController : GLKViewController

- (id)initWithClient:(RemotingClient*)client;

@end

#endif  // REMOTING_CLIENT_IOS_APP_REMOTING_HOST_VIEW_CONTROLLER_H_
