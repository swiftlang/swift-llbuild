//===- unittests/Ninja/lexer.cpp ------------------------------------------===//
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

#include "llbuild/Basic/LLVM.h"

#include "gtest/gtest.h"

using namespace llbuild;

namespace {

TEST(LexerTest, basic) {
  StringRef input = "| : || # Comment\n";
  ninja::Lexer lexer(input);

  // Check that we get the appropriate tokens.
  ninja::Token tok;
  ninja::Token& result = lexer.lex(tok);

  // Check .lex() result.
  EXPECT_EQ(&result, &tok);

  // Check first token.
  EXPECT_EQ(ninja::Token::Kind::Pipe, tok.tokenKind);
  EXPECT_EQ(&input.data()[0], tok.start);
  EXPECT_EQ(1U, tok.length);
  EXPECT_EQ(1U, tok.line);
  EXPECT_EQ(0U, tok.column);

  // Check second token.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Colon, tok.tokenKind);
  EXPECT_EQ(&input.data()[2], tok.start);
  EXPECT_EQ(1U, tok.length);
  EXPECT_EQ(1U, tok.line);
  EXPECT_EQ(2U, tok.column);

  // Check third token.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::PipePipe, tok.tokenKind);
  EXPECT_EQ(&input.data()[4], tok.start);
  EXPECT_EQ(2U, tok.length);
  EXPECT_EQ(1U, tok.line);
  EXPECT_EQ(4U, tok.column);

  // Check fourth token.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Comment, tok.tokenKind);
  EXPECT_EQ(&input.data()[7], tok.start);
  EXPECT_EQ(9U, tok.length);
  EXPECT_EQ(1U, tok.line);
  EXPECT_EQ(7U, tok.column);

  // Check fifth token.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Newline, tok.tokenKind);
  EXPECT_EQ(&input.data()[16], tok.start);
  EXPECT_EQ(1U, tok.length);
  EXPECT_EQ(1U, tok.line);
  EXPECT_EQ(16U, tok.column);

  // Check final token.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::EndOfFile, tok.tokenKind);
  EXPECT_EQ(input.end(), tok.start);
  EXPECT_EQ(0U, tok.length);
  EXPECT_EQ(2U, tok.line);
  EXPECT_EQ(0U, tok.column);

  // Check we continue to get EOF.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::EndOfFile, tok.tokenKind);
}

TEST(LexerTest, indentation) {
  StringRef input = "\
|\n\
 | |";
  ninja::Lexer lexer(input);

  // Check that we get an indentation token for whitespace, but only at the
  // start of a line.
  ninja::Token tok;
  lexer.lex(tok);

  // Check first token.
  EXPECT_EQ(ninja::Token::Kind::Pipe, tok.tokenKind);
  EXPECT_EQ(1U, tok.length);
  EXPECT_EQ(1U, tok.line);
  EXPECT_EQ(0U, tok.column);

  // Check second token (the newline).
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Newline, tok.tokenKind);
  EXPECT_EQ(1U, tok.length);
  EXPECT_EQ(1U, tok.line);
  EXPECT_EQ(1U, tok.column);

  // Check third token (our indentation token).
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Indentation, tok.tokenKind);
  EXPECT_EQ(1U, tok.length);
  EXPECT_EQ(2U, tok.line);
  EXPECT_EQ(0U, tok.column);

  // Check fourth token (the pipe following indentation).
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Pipe, tok.tokenKind);
  EXPECT_EQ(1U, tok.length);
  EXPECT_EQ(2U, tok.line);
  EXPECT_EQ(1U, tok.column);

  // Check fifth token (skipping whitespace).
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Pipe, tok.tokenKind);
  EXPECT_EQ(1U, tok.length);
  EXPECT_EQ(2U, tok.line);
  EXPECT_EQ(3U, tok.column);

  // Check final token.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::EndOfFile, tok.tokenKind);
}

TEST(LexerTest, basicIdentifierHandling) {
  StringRef input = "a b$c";
  ninja::Lexer lexer(input);
  ninja::Token tok;

  // Check first token.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Identifier, tok.tokenKind);
  EXPECT_EQ(&input.data()[0], tok.start);
  EXPECT_EQ(1U, tok.length);
  EXPECT_EQ(1U, tok.line);
  EXPECT_EQ(0U, tok.column);

  // Check second token.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Identifier, tok.tokenKind);
  EXPECT_EQ(&input.data()[2], tok.start);
  EXPECT_EQ(1U, tok.length);
  EXPECT_EQ(1U, tok.line);
  EXPECT_EQ(2U, tok.column);

  // Check third token.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Unknown, tok.tokenKind);
  EXPECT_EQ(&input.data()[3], tok.start);
  EXPECT_EQ(1U, tok.length);
  EXPECT_EQ(1U, tok.line);
  EXPECT_EQ(3U, tok.column);

  // Check fourth token.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Identifier, tok.tokenKind);
  EXPECT_EQ(&input.data()[4], tok.start);
  EXPECT_EQ(1U, tok.length);
  EXPECT_EQ(1U, tok.line);
  EXPECT_EQ(4U, tok.column);

  // Check final token.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::EndOfFile, tok.tokenKind);
}

TEST(LexerTest, identifierKeywords) {
  StringRef input = "notakeyword build default include \
pool rule subninja";
  ninja::Lexer lexer(input);
  ninja::Token tok;

  // Check first token.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Identifier, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "notakeyword", tok.length));

  // Check the various keywords.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::KWBuild, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "build", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::KWDefault, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "default", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::KWInclude, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "include", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::KWPool, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "pool", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::KWRule, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "rule", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::KWSubninja, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "subninja", tok.length));

  // Check final token.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::EndOfFile, tok.tokenKind);
}

TEST(LexerTest, pathStrings) {
  StringRef input = "this is: a| path$ str$:ing$\ncontext\n\
#hash-is-ok\n\
=equal-is-too\n";
  ninja::Lexer lexer(input);
  ninja::Token tok;

  // Put the lexer into "path" string mode.
  lexer.setMode(ninja::Lexer::LexingMode::PathString);

  // Check we still split on spaces.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::String, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "this", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::String, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "is", tok.length));

  // Check that we recognized the other tokens.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Colon, tok.tokenKind);
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::String, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "a", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Pipe, tok.tokenKind);

  // Check that we honor escape sequences.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::String, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "path$ str$:ing$\ncontext", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Newline, tok.tokenKind);

  // Check that we allow '#' and '=' in path strings.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::String, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "#hash-is-ok", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Newline, tok.tokenKind);

  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::String, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "=equal-is-too", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Newline, tok.tokenKind);

  // Check final token.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::EndOfFile, tok.tokenKind);
}

TEST(LexerTest, variableStrings) {
  StringRef input = "\
this is one string\n\
this string crosses a $\nnewline\n\
:\n\
|\n";
  ninja::Lexer lexer(input);
  ninja::Token tok;

  // Put the lexer into "variable" string mode.
  lexer.setMode(ninja::Lexer::LexingMode::VariableString);

  // Check we consume spaces in strings.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::String, tok.tokenKind);
  EXPECT_EQ(std::string("String"), tok.getKindName());
  EXPECT_EQ(0, memcmp(tok.start, "this is one string", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Newline, tok.tokenKind);

  // Check that we honor the newline escape.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::String, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "this string crosses a $\nnewline",
                      tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Newline, tok.tokenKind);

  // Check that we respect special characters at the start of the token.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::String, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, ":", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Newline, tok.tokenKind);

  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::String, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "|", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Newline, tok.tokenKind);

  // Check final token.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::EndOfFile, tok.tokenKind);
}

TEST(LexerTest, identifierSpecific) {
  StringRef input = "rule pool build default include subninja random";
  ninja::Lexer lexer(input);
  ninja::Token tok;

  // Put the lexer into "identifier specific" string mode.
  lexer.setMode(ninja::Lexer::LexingMode::IdentifierSpecific);

  // Check we recognize all of the keywords as identifiers.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Identifier, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "rule", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Identifier, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "pool", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Identifier, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "build", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Identifier, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "default", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Identifier, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "include", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Identifier, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "subninja", tok.length));
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::Identifier, tok.tokenKind);
  EXPECT_EQ(0, memcmp(tok.start, "random", tok.length));

  // Check final token.
  lexer.lex(tok);
  EXPECT_EQ(ninja::Token::Kind::EndOfFile, tok.tokenKind);
}

}
