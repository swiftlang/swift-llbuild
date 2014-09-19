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

TEST(LexerTest, Indentation) {
  char Input[] = "\
|\n\
 | |";
  size_t InputSize = strlen(Input);
  ninja::Lexer Lexer(Input, InputSize);

  // Check that we get an indentation token for whitespace, but only at the
  // start of a line.
  ninja::Token Tok;
  Lexer.lex(Tok);

  // Check first token.
  EXPECT_EQ(ninja::Token::Kind::Pipe, Tok.TokenKind);
  EXPECT_EQ(1U, Tok.Length);
  EXPECT_EQ(1U, Tok.Line);
  EXPECT_EQ(0U, Tok.Column);

  // Check second token (the newline).
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::Newline, Tok.TokenKind);
  EXPECT_EQ(1U, Tok.Length);
  EXPECT_EQ(1U, Tok.Line);
  EXPECT_EQ(1U, Tok.Column);

  // Check third token (our indentation token).
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::Indentation, Tok.TokenKind);
  EXPECT_EQ(1U, Tok.Length);
  EXPECT_EQ(2U, Tok.Line);
  EXPECT_EQ(0U, Tok.Column);

  // Check fourth token (the pipe following indentation).
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::Pipe, Tok.TokenKind);
  EXPECT_EQ(1U, Tok.Length);
  EXPECT_EQ(2U, Tok.Line);
  EXPECT_EQ(1U, Tok.Column);

  // Check fifth token (skipping whitespace).
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::Pipe, Tok.TokenKind);
  EXPECT_EQ(1U, Tok.Length);
  EXPECT_EQ(2U, Tok.Line);
  EXPECT_EQ(3U, Tok.Column);

  // Check final token.
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::EndOfFile, Tok.TokenKind);
}

TEST(LexerTest, BasicIdentifierHandling) {
  char Input[] = "a b$ c";
  size_t InputSize = strlen(Input);
  ninja::Lexer Lexer(Input, InputSize);
  ninja::Token Tok;

  // Check first token.
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::Identifier, Tok.TokenKind);
  EXPECT_EQ(&Input[0], Tok.Start);
  EXPECT_EQ(1U, Tok.Length);
  EXPECT_EQ(1U, Tok.Line);
  EXPECT_EQ(0U, Tok.Column);

  // Check second token.
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::Identifier, Tok.TokenKind);
  EXPECT_EQ(&Input[2], Tok.Start);
  EXPECT_EQ(4U, Tok.Length);
  EXPECT_EQ(1U, Tok.Line);
  EXPECT_EQ(2U, Tok.Column);

  // Check final token.
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::EndOfFile, Tok.TokenKind);
}

TEST(LexerTest, IdentifierKeywords) {
  char Input[] = "notakeyword build default include \
pool rule subninja";
  size_t InputSize = strlen(Input);
  ninja::Lexer Lexer(Input, InputSize);
  ninja::Token Tok;

  // Check first token.
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::Identifier, Tok.TokenKind);
  EXPECT_EQ(0, memcmp(Tok.Start, "notakeyword", Tok.Length));

  // Check the various keywords.
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::KWBuild, Tok.TokenKind);
  EXPECT_EQ(0, memcmp(Tok.Start, "build", Tok.Length));
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::KWDefault, Tok.TokenKind);
  EXPECT_EQ(0, memcmp(Tok.Start, "default", Tok.Length));
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::KWInclude, Tok.TokenKind);
  EXPECT_EQ(0, memcmp(Tok.Start, "include", Tok.Length));
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::KWPool, Tok.TokenKind);
  EXPECT_EQ(0, memcmp(Tok.Start, "pool", Tok.Length));
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::KWRule, Tok.TokenKind);
  EXPECT_EQ(0, memcmp(Tok.Start, "rule", Tok.Length));
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::KWSubninja, Tok.TokenKind);
  EXPECT_EQ(0, memcmp(Tok.Start, "subninja", Tok.Length));

  // Check final token.
  Lexer.lex(Tok);
  EXPECT_EQ(ninja::Token::Kind::EndOfFile, Tok.TokenKind);
}

}
