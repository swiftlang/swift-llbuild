//===- unittests/Basic/Defer.cpp ------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/Basic/Defer.h"

#include "gtest/gtest.h"

using namespace llbuild;
using namespace llbuild::basic;

namespace {

TEST(DeferTest, basic) {
  int defer_count = 0;

  // Test basic macro use
  {
    llbuild_defer { defer_count++; };
    EXPECT_EQ(0, defer_count);
  }
  EXPECT_EQ(1, defer_count);

  // Test multiple defers
  defer_count = 0;
  {
    llbuild_defer { defer_count += 1; };
    EXPECT_EQ(0, defer_count);
    llbuild_defer { defer_count += 2; };
    EXPECT_EQ(0, defer_count);
  }
  EXPECT_EQ(3, defer_count);

  // Test/show direct use of RAII builder
  defer_count = 0;
  {
    auto deferred = makeScopeDefer([&](){ defer_count += 1; });
    EXPECT_EQ(0, defer_count);
  }
  EXPECT_EQ(1, defer_count);

  // Test/show direct use of ScopeDefer template
  defer_count = 0;
  {
    auto deferred = ScopeDefer<std::function<void()>>([&](){ defer_count += 1; });
    EXPECT_EQ(0, defer_count);
  }
  EXPECT_EQ(1, defer_count);
}

}
