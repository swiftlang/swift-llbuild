//===- unittests/Basic/SerialQueueTest.cpp --------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/Basic/SerialQueue.h"

#include "gtest/gtest.h"

using namespace llbuild;
using namespace llbuild::basic;

namespace {

TEST(SerialQueueTest, basic) {
  // Check that we can execute async and sync ops.

  int a = 0;
  int b = 0;
  int c = 0;
  {
    SerialQueue q;
    q.async([&]() {
        printf("a = 1\n");
        a = 1;
      });
    q.async([&]() {
        printf("b = 1\n");
        b = 1;
      });
    q.sync([&]() {
        printf("c = 1\n");
        c = 1;
      });
  }
  EXPECT_EQ(a, 1);
  EXPECT_EQ(b, 1);
  EXPECT_EQ(c, 1);
}

}
