//===- DataIDTests.cpp ----------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/CAS/DataID.h"

#include "gtest/gtest.h"

using namespace llbuild;
using namespace llbuild::CAS;

TEST(DataIDTests, basic) {
  auto empty = DataID::empty();
  auto empty2 = DataID::empty();
  auto invalid = DataID::invalid();
  auto invalid2 = DataID::invalid();

  EXPECT_EQ(empty, empty2);
  EXPECT_EQ(invalid, invalid2);
  EXPECT_NE(empty, invalid);
}
