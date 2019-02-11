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

std::string quoted(std::string s) {
#if defined(_WIN32)
  std::string quote = "\"";
#else
  std::string quote = "'";
#endif
  return quote + s + quote;
}

TEST(UtilityTest, basic) {
  // No escapable char.
  std::string output = shellEscaped("input01");
  EXPECT_EQ(output, "input01");

  // Space.
  output = shellEscaped("input A");
#if defined(_WIN32)
  std::string quote = "\"";
#else
  std::string quote = "'";
#endif

  EXPECT_EQ(output, quoted("input A"));

  // Two spaces.
  output = shellEscaped("input A B");
  EXPECT_EQ(output, quoted("input A B"));

  // Double Quote.
  output = shellEscaped("input\"A");
#if defined(_WIN32)
  EXPECT_EQ(output, quoted("input") + "\\" + quote + quoted("A"));
#else
  EXPECT_EQ(output, quoted("input\"A"));
#endif

  // Single Quote.
  output = shellEscaped("input'A");
#if defined(_WIN32)
  EXPECT_EQ(output, quoted("input'A"));
#else
  EXPECT_EQ(output, "'input'\\''A'");
#endif

  // Question Mark.
  output = shellEscaped("input?A");
  EXPECT_EQ(output, quoted("input?A"));

  // New line.
  output = shellEscaped("input\nA");
  EXPECT_EQ(output, quoted("input\nA"));

  // Multiple special chars.
#if defined(_WIN32)
  output = shellEscaped("input\nA'B C>D*[$;()^><");
  EXPECT_EQ(output, quoted("input\nA'B C>D*[$;()^><"));
#else
  output = shellEscaped("input\nA\"B C>D*[$;()^><");
  EXPECT_EQ(output, quoted("input\nA\"B C>D*[$;()^><"));
#endif
}

}
