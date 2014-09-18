//===- unittests/Ninja/Lexer.cpp ------------------------------------------===//
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

#include "llbuild/Ninja/Lexer.h"

#include "gtest/gtest.h"

using namespace llbuild;

namespace {

TEST(LexerTest, Basic) {
  char Input[] = "| : || # Comment";
  ninja::Lexer Lexer(Input, strlen(Input));

  // Check that we get the appropriate tokens.
  ninja::Token Tok;
  Lexer.lex(Tok);
  
  Tok.dump();
  EXPECT_TRUE(false);
}

}
