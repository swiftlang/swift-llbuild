//===--- utils/unittest/UnitTestMain/TestMain.cpp -------------------------===//
//
// Copyright (c) 2014 Apple Inc. All rights reserved.
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
