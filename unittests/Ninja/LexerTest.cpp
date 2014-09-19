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
  char Input[] = "| : || # Comment\n";
  size_t InputSize = strlen(Input);
  ninja::Lexer Lexer(Input, InputSize);

  // Check that we get the appropriate tokens.
  ninja::Token Tok;
  ninja::Token &Result = Lexer.lex(Tok);

  // Check .lex() result.
  EXPECT_EQ(&Result, &Tok);

  // Check first token.
  EXPECT_EQ(ninja::Token::Kind::Pipe, Tok.TokenKind);
  EXPECT_EQ(&Input[0], Tok.Start);
  EXPECT_EQ(1U, Tok.Length);
  EXPECT_EQ(1U, Tok.Line);
  EXPECT_EQ(0U, Tok.Column);

  // Check second token.
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::Colon, Tok.TokenKind);
  EXPECT_EQ(&Input[2], Tok.Start);
  EXPECT_EQ(1U, Tok.Length);
  EXPECT_EQ(1U, Tok.Line);
  EXPECT_EQ(2U, Tok.Column);

  // Check third token.
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::PipePipe, Tok.TokenKind);
  EXPECT_EQ(&Input[4], Tok.Start);
  EXPECT_EQ(2U, Tok.Length);
  EXPECT_EQ(1U, Tok.Line);
  EXPECT_EQ(4U, Tok.Column);

  // Check fourth token.
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::Comment, Tok.TokenKind);
  EXPECT_EQ(&Input[7], Tok.Start);
  EXPECT_EQ(9U, Tok.Length);
  EXPECT_EQ(1U, Tok.Line);
  EXPECT_EQ(7U, Tok.Column);

  // Check fifth token.
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::Newline, Tok.TokenKind);
  EXPECT_EQ(&Input[16], Tok.Start);
  EXPECT_EQ(1U, Tok.Length);
  EXPECT_EQ(1U, Tok.Line);
  EXPECT_EQ(16U, Tok.Column);

  // Check final token.
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::EndOfFile, Tok.TokenKind);
  EXPECT_EQ(&Input[strlen(Input)], Tok.Start);
  EXPECT_EQ(0U, Tok.Length);
  EXPECT_EQ(2U, Tok.Line);
  EXPECT_EQ(0, Tok.Column);

  // Check we continue to get EOF.
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::EndOfFile, Tok.TokenKind);
}

}
