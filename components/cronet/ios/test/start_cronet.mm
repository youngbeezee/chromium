// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cronet/Cronet.h>
#import <Foundation/Foundation.h>

#include "components/cronet/ios/test/start_cronet.h"

#include "base/strings/stringprintf.h"
#include "base/strings/sys_string_conversions.h"
#include "components/grpc_support/test/quic_test_server.h"

namespace cronet {

void StartCronetIfNecessary(int port) {
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    [Cronet setUserAgent:@"CronetTest/1.0.0.0" partial:NO];
    [Cronet setHttp2Enabled:true];
    [Cronet setQuicEnabled:true];
    [Cronet setAcceptLanguages:@"en-US,en"];
    [Cronet
        setExperimentalOptions:
            [NSString
                stringWithFormat:@"{\"ssl_key_log_file\":\"%@\"}",
                                 [Cronet
                                     getNetLogPathForFile:@"SSLKEYLOGFILE"]]];
    [Cronet addQuicHint:@"test.example.com" port:443 altPort:443];
    [Cronet enableTestCertVerifierForTesting];
    [Cronet setHttpCacheType:CRNHttpCacheTypeDisabled];
    [Cronet start];
  }
  NSString* rules = base::SysUTF8ToNSString(
      base::StringPrintf("MAP test.example.com 127.0.0.1:%d,"
                         "MAP notfound.example.com ~NOTFOUND",
                         port));
  [Cronet setHostResolverRulesForTesting:rules];
}

}  // namespace cronet
