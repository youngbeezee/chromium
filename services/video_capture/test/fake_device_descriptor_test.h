// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_VIDEO_CAPTURE_TEST_FAKE_DEVICE_DESCRIPTOR_TEST_H_
#define SERVICES_VIDEO_CAPTURE_TEST_FAKE_DEVICE_DESCRIPTOR_TEST_H_

#include "services/video_capture/test/service_test.h"

namespace video_capture {

// Test fixture that obtains the descriptor of the fake device by enumerating
// the devices of the fake device factory.
class FakeDeviceDescriptorTest : public ServiceTest {
 public:
  FakeDeviceDescriptorTest();
  ~FakeDeviceDescriptorTest() override;

  void SetUp() override;

 protected:
  media::VideoCaptureDeviceInfo fake_device_info_;
};

}  // namespace video_capture

#endif  // SERVICES_VIDEO_CAPTURE_TEST_FAKE_DEVICE_DESCRIPTOR_TEST_H_
