// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/renderer/api_invocation_errors.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace extensions {
namespace api_errors {

// Tests chaining errors for more complicated errors. More of a set of example
// strings than a test of the logic itself (which is pretty simple).
TEST(APIInvocationErrors, ChainedErrors) {
  EXPECT_EQ("Error at index 0: Invalid type: expected string, found integer.",
            IndexError(0, InvalidType(kTypeString, kTypeInteger)));
  EXPECT_EQ(
      "Error at property 'foo': Invalid type: expected string, found integer.",
      PropertyError("foo", InvalidType(kTypeString, kTypeInteger)));
  EXPECT_EQ(
      "Error at property 'foo': Error at index 1: "
      "Invalid type: expected string, found integer.",
      PropertyError("foo",
                    IndexError(1, InvalidType(kTypeString, kTypeInteger))));
}

}  // namespace api_errors
}  // namespace extensions
