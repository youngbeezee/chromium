// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bindings/core/v8/SerializedScriptValue.h"

#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/SerializedScriptValueFactory.h"
#include "bindings/core/v8/V8BindingForCore.h"
#include "bindings/core/v8/V8BindingForTesting.h"
#include "bindings/core/v8/V8File.h"
#include "bindings/core/v8/V8ImageData.h"
#include "core/fileapi/File.h"
#include "platform/testing/UnitTestHelpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

TEST(SerializedScriptValueTest, WireFormatRoundTrip) {
  V8TestingScope scope;

  v8::Local<v8::Value> v8OriginalTrue = v8::True(scope.GetIsolate());
  RefPtr<SerializedScriptValue> sourceSerializedScriptValue =
      SerializedScriptValue::Serialize(
          scope.GetIsolate(), v8OriginalTrue,
          SerializedScriptValue::SerializeOptions(), ASSERT_NO_EXCEPTION);

  Vector<char> wireData;
  sourceSerializedScriptValue->ToWireBytes(wireData);

  RefPtr<SerializedScriptValue> serializedScriptValue =
      SerializedScriptValue::Create(wireData.data(), wireData.size());
  v8::Local<v8::Value> deserialized =
      serializedScriptValue->Deserialize(scope.GetIsolate());
  EXPECT_TRUE(deserialized->IsTrue());
}

TEST(SerializedScriptValueTest, WireFormatVersion17NoByteSwapping) {
  V8TestingScope scope;

  const uint8_t data[] = {0xFF, 0x11, 0xFF, 0x0D, 0x54, 0x00};
  RefPtr<SerializedScriptValue> serializedScriptValue =
      SerializedScriptValue::Create(reinterpret_cast<const char*>(data),
                                    sizeof(data));
  v8::Local<v8::Value> deserialized =
      serializedScriptValue->Deserialize(scope.GetIsolate());
  EXPECT_TRUE(deserialized->IsTrue());
}

TEST(SerializedScriptValueTest, WireFormatVersion16ByteSwapping) {
  V8TestingScope scope;

  // Using UChar instead of uint8_t to get ntohs() byte swapping.
  const UChar data[] = {0xFF10, 0xFF0D, 0x5400};
  RefPtr<SerializedScriptValue> serializedScriptValue =
      SerializedScriptValue::Create(reinterpret_cast<const char*>(data),
                                    sizeof(data));
  v8::Local<v8::Value> deserialized =
      serializedScriptValue->Deserialize(scope.GetIsolate());
  EXPECT_TRUE(deserialized->IsTrue());
}

TEST(SerializedScriptValueTest, WireFormatVersion13ByteSwapping) {
  V8TestingScope scope;

  // Using UChar instead of uint8_t to get ntohs() byte swapping.
  const UChar data[] = {0xFF0D, 0x5400};
  RefPtr<SerializedScriptValue> serializedScriptValue =
      SerializedScriptValue::Create(reinterpret_cast<const char*>(data),
                                    sizeof(data));
  v8::Local<v8::Value> deserialized =
      serializedScriptValue->Deserialize(scope.GetIsolate());
  EXPECT_TRUE(deserialized->IsTrue());
}

TEST(SerializedScriptValueTest, WireFormatVersion0ByteSwapping) {
  V8TestingScope scope;

  // Using UChar instead of uint8_t to get ntohs() byte swapping.
  const UChar data[] = {0x5400};
  RefPtr<SerializedScriptValue> serializedScriptValue =
      SerializedScriptValue::Create(reinterpret_cast<const char*>(data),
                                    sizeof(data));
  v8::Local<v8::Value> deserialized =
      serializedScriptValue->Deserialize(scope.GetIsolate());
  EXPECT_TRUE(deserialized->IsTrue());
}

TEST(SerializedScriptValueTest, WireFormatVersion0ImageData) {
  V8TestingScope scope;
  v8::Isolate* isolate = scope.GetIsolate();

  // Using UChar instead of uint8_t to get ntohs() byte swapping.
  //
  // This builds the smallest possible ImageData whose first data byte is 0xFF,
  // as follows.
  //
  // width = 127, encoded as 0xFF 0x00 (degenerate varint)
  // height = 1, encoded as 0x01 (varint)
  // pixelLength = 508 (127 * 1 * 4), encoded as 0xFC 0x03 (varint)
  // pixel data = 508 bytes, all zero
  Vector<UChar> data;
  data.push_back(0x23FF);
  data.push_back(0x001);
  data.push_back(0xFC03);
  data.resize(257);  // (508 pixel data + 6 header bytes) / 2

  RefPtr<SerializedScriptValue> serializedScriptValue =
      SerializedScriptValue::Create(reinterpret_cast<const char*>(data.data()),
                                    data.size() * sizeof(UChar));
  v8::Local<v8::Value> deserialized =
      serializedScriptValue->Deserialize(isolate);
  ASSERT_TRUE(deserialized->IsObject());
  v8::Local<v8::Object> deserializedObject = deserialized.As<v8::Object>();
  ASSERT_TRUE(V8ImageData::hasInstance(deserializedObject, isolate));
  ImageData* imageData = V8ImageData::toImpl(deserializedObject);
  EXPECT_EQ(imageData->width(), 127);
  EXPECT_EQ(imageData->height(), 1);
}

TEST(SerializedScriptValueTest, UserSelectedFile) {
  V8TestingScope scope;
  String file_path = testing::BlinkRootDir();
  file_path.append("/Source/bindings/core/v8/SerializedScriptValueTest.cpp");
  File* original_file = File::Create(file_path);
  ASSERT_TRUE(original_file->HasBackingFile());
  ASSERT_EQ(File::kIsUserVisible, original_file->GetUserVisibility());
  ASSERT_EQ(file_path, original_file->GetPath());

  v8::Local<v8::Value> v8_original_file =
      ToV8(original_file, scope.GetContext()->Global(), scope.GetIsolate());
  RefPtr<SerializedScriptValue> serialized_script_value =
      SerializedScriptValue::Serialize(
          scope.GetIsolate(), v8_original_file,
          SerializedScriptValue::SerializeOptions(), ASSERT_NO_EXCEPTION);
  v8::Local<v8::Value> v8_file =
      serialized_script_value->Deserialize(scope.GetIsolate());

  ASSERT_TRUE(V8File::hasInstance(v8_file, scope.GetIsolate()));
  File* file = V8File::toImpl(v8::Local<v8::Object>::Cast(v8_file));
  EXPECT_TRUE(file->HasBackingFile());
  EXPECT_EQ(File::kIsUserVisible, file->GetUserVisibility());
  EXPECT_EQ(file_path, file->GetPath());
}

TEST(SerializedScriptValueTest, FileConstructorFile) {
  V8TestingScope scope;
  RefPtr<BlobDataHandle> blob_data_handle = BlobDataHandle::Create();
  File* original_file = File::Create("hello.txt", 12345678.0, blob_data_handle);
  ASSERT_FALSE(original_file->HasBackingFile());
  ASSERT_EQ(File::kIsNotUserVisible, original_file->GetUserVisibility());
  ASSERT_EQ("hello.txt", original_file->name());

  v8::Local<v8::Value> v8_original_file =
      ToV8(original_file, scope.GetContext()->Global(), scope.GetIsolate());
  RefPtr<SerializedScriptValue> serialized_script_value =
      SerializedScriptValue::Serialize(
          scope.GetIsolate(), v8_original_file,
          SerializedScriptValue::SerializeOptions(), ASSERT_NO_EXCEPTION);
  v8::Local<v8::Value> v8_file =
      serialized_script_value->Deserialize(scope.GetIsolate());

  ASSERT_TRUE(V8File::hasInstance(v8_file, scope.GetIsolate()));
  File* file = V8File::toImpl(v8::Local<v8::Object>::Cast(v8_file));
  EXPECT_FALSE(file->HasBackingFile());
  EXPECT_EQ(File::kIsNotUserVisible, file->GetUserVisibility());
  EXPECT_EQ("hello.txt", file->name());
}

}  // namespace blink
