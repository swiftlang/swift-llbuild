//===- unittests/Basic/ShellUtilityTest.cpp -------------------------------===//
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

#include "llbuild/Basic/ShellUtility.h"

#include "gtest/gtest.h"

using namespace llbuild;
using namespace llbuild::basic;

namespace {

TEST(UtilityTest, basic) {
  // No escapable char.
  std::string output = shellEscaped("input01");
  EXPECT_EQ(output, "input01");

  // Space.
  output = shellEscaped("input A");
  EXPECT_EQ(output, "'input A'");

  // Two spaces.
  output = shellEscaped("input A B");
  EXPECT_EQ(output, "'input A B'");

  // Double Quote.
  output = shellEscaped("input\"A");
  EXPECT_EQ(output, "'input\"A'");

  // Single Quote.
  output = shellEscaped("input'A");
  EXPECT_EQ(output, "'input'\\''A'");

  // Question Mark.
  output = shellEscaped("input?A");
  EXPECT_EQ(output, "'input?A'");

  // New line.
  output = shellEscaped("input\nA");
  EXPECT_EQ(output, "'input\nA'");

  // Multiple special chars.
  output = shellEscaped("input\nA\"B C>D*[$;()^><");
  EXPECT_EQ(output, "'input\nA\"B C>D*[$;()^><'");
}

}
