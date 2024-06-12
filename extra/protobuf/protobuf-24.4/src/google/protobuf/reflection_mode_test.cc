// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google Inc.  All rights reserved.
// https://developers.google.com/protocol-buffers/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#include "google/protobuf/reflection_mode.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace google {
namespace protobuf {
namespace internal {

#ifndef PROTOBUF_NO_THREADLOCAL

TEST(ReflectionModeTest, SimpleScopedReflection) {
  ASSERT_EQ(ScopedReflectionMode::current_reflection_mode(),
            ReflectionMode::kDefault);
  ScopedReflectionMode scope(ReflectionMode::kDiagnostics);
  EXPECT_EQ(ScopedReflectionMode::current_reflection_mode(),
            ReflectionMode::kDiagnostics);
}

TEST(ReflectionModeTest, CleanNestedScopedReflection) {
  ASSERT_EQ(ScopedReflectionMode::current_reflection_mode(),
            ReflectionMode::kDefault);
  {
    ScopedReflectionMode scope1(ReflectionMode::kDebugString);
    EXPECT_EQ(ScopedReflectionMode::current_reflection_mode(),
              ReflectionMode::kDebugString);
    {
      ScopedReflectionMode scope2(ReflectionMode::kDiagnostics);
      EXPECT_EQ(ScopedReflectionMode::current_reflection_mode(),
                ReflectionMode::kDiagnostics);
    }
    EXPECT_EQ(ScopedReflectionMode::current_reflection_mode(),
              ReflectionMode::kDebugString);
  }
  EXPECT_EQ(ScopedReflectionMode::current_reflection_mode(),
            ReflectionMode::kDefault);
}

TEST(ReflectionModeTest, UglyNestedScopedReflection) {
  ASSERT_EQ(ScopedReflectionMode::current_reflection_mode(),
            ReflectionMode::kDefault);
  ScopedReflectionMode scope1(ReflectionMode::kDebugString);
  EXPECT_EQ(ScopedReflectionMode::current_reflection_mode(),
            ReflectionMode::kDebugString);
  ScopedReflectionMode scope2(ReflectionMode::kDiagnostics);
  EXPECT_EQ(ScopedReflectionMode::current_reflection_mode(),
            ReflectionMode::kDiagnostics);
}

TEST(ReflectionModeTest, DebugStringModeDoesNotReplaceDiagnosticsMode) {
  ASSERT_EQ(ScopedReflectionMode::current_reflection_mode(),
            ReflectionMode::kDefault);
  ScopedReflectionMode scope1(ReflectionMode::kDiagnostics);
  {
    ScopedReflectionMode scope2(ReflectionMode::kDebugString);
    EXPECT_EQ(ScopedReflectionMode::current_reflection_mode(),
              ReflectionMode::kDiagnostics);
  }
}

#else

TEST(ReflectionModeTest, AlwaysReturnDefaultWhenNoThreadLocal) {
  ASSERT_EQ(ScopedReflectionMode::current_reflection_mode(),
            ReflectionMode::kDefault);
  {
    ScopedReflectionMode scope1(ReflectionMode::kDebugString);
    EXPECT_EQ(ScopedReflectionMode::current_reflection_mode(),
              ReflectionMode::kDefault);
    {
      ScopedReflectionMode scope2(ReflectionMode::kDiagnostics);
      EXPECT_EQ(ScopedReflectionMode::current_reflection_mode(),
                ReflectionMode::kDefault);
    }
    EXPECT_EQ(ScopedReflectionMode::current_reflection_mode(),
              ReflectionMode::kDefault);
  }
  EXPECT_EQ(ScopedReflectionMode::current_reflection_mode(),
            ReflectionMode::kDefault);
}

#endif

}  // namespace internal
}  // namespace protobuf
}  // namespace google
