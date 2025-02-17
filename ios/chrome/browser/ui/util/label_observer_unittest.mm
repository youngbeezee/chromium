// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/ui/util/label_observer.h"

#import "base/mac/scoped_nsobject.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/gtest_mac.h"
#include "testing/platform_test.h"
#include "url/gurl.h"

namespace {

class LabelObserverTest : public PlatformTest {
 protected:
  void SetUp() override {
    label_.reset([[UILabel alloc] initWithFrame:CGRectZero]);
    observer_.reset([[LabelObserver observerForLabel:label_.get()] retain]);
    [observer_ startObserving];
  }

  ~LabelObserverTest() override { [observer_ stopObserving]; }

  UILabel* label() { return label_.get(); }
  LabelObserver* observer() { return observer_.get(); }

  base::scoped_nsobject<UILabel> label_;
  base::scoped_nsobject<LabelObserver> observer_;
};

// Tests that all types of LabelObserverActions are successfully called.
TEST_F(LabelObserverTest, SimpleTest) {
  __block BOOL text_action_called = NO;
  [observer() addTextChangedAction:^(UILabel* label) {
    text_action_called = YES;
  }];
  label().text = @"text";
  EXPECT_TRUE(text_action_called);
  __block BOOL layout_action_called = NO;
  [observer() addLayoutChangedAction:^(UILabel* label) {
    layout_action_called = YES;
  }];
  [label() sizeToFit];
  EXPECT_TRUE(layout_action_called);
  __block BOOL style_action_called = NO;
  [observer() addStyleChangedAction:^(UILabel* label) {
    style_action_called = YES;
  }];
  label().textColor = [UIColor blueColor];
  EXPECT_TRUE(style_action_called);
}

// Tests that a LabelObserverAction that causes another KVO callback does not
// get called twice
TEST_F(LabelObserverTest, CallCountTest) {
  __block NSUInteger text_action_call_count = 0;
  [observer() addTextChangedAction:^(UILabel* label) {
    ++text_action_call_count;
    label.text = [label.text stringByAppendingString:@"x"];
  }];
  label().text = @"x";
  EXPECT_EQ(1U, text_action_call_count);
  EXPECT_NSEQ(label().text, @"xx");
}

// Tests that LabelObserverActions are called in the same order in which they
// are added.
TEST_F(LabelObserverTest, ObserverActionOrderTest) {
  __block BOOL first_action_called = NO;
  __block BOOL second_action_called = NO;
  [observer() addTextChangedAction:^(UILabel* label) {
    EXPECT_FALSE(first_action_called);
    EXPECT_FALSE(second_action_called);
    first_action_called = YES;
  }];
  [observer() addTextChangedAction:^(UILabel* label) {
    EXPECT_TRUE(first_action_called);
    EXPECT_FALSE(second_action_called);
    second_action_called = YES;
  }];
  label().text = @"test";
  EXPECT_TRUE(first_action_called);
  EXPECT_TRUE(second_action_called);
}

}  // namespace
