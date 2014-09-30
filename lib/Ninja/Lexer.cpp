//===-- Lexer.cpp ---------------------------------------------------------===//
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

#include <string>
#include <iostream>
#include <iomanip>

using namespace llbuild;
using namespace llbuild::ninja;

///

const char *Token::getKindName() const {
#define CASE(name) case Kind::name: return #name
  switch (TokenKind) {
    CASE(Colon);
    CASE(Comment);
    CASE(EndOfFile);
    CASE(Equals);
    CASE(Identifier);
    CASE(Indentation);
    CASE(KWBuild);
    CASE(KWDefault);
    CASE(KWInclude);
    CASE(KWPool);
    CASE(KWRule);
    CASE(KWSubninja);
    CASE(Newline);
    CASE(Pipe);
    CASE(PipePipe);
    CASE(String);
    CASE(Unknown);
  }
#undef CASE

  return "<invalid token kind>";
}

void Token::dump() {
  std::cerr << "(Token \"" << getKindName() << "\" "
            << (const void*) Start << " " << Length << " "
            << Line << " " << Column << ")\n";
}

///

Lexer::Lexer(const char* Data, uint64_t Length)
  : BufferStart(Data), BufferPos(Data), BufferEnd(Data + Length),
    LineNumber(1), ColumnNumber(0), Mode(StringMode::None)
{
}

Lexer::~Lexer() {
}

int Lexer::peekNextChar() {
  if (BufferPos == BufferEnd)
    return -1;
  return *BufferPos;
}

int Lexer::getNextChar() {
  if (BufferPos == BufferEnd)
    return -1;

  // Handle DOS/Mac newlines here, by stripping duplicates and by returning '\n'
  // for both.
  char Result = *BufferPos++;
  if (Result == '\n' || Result == '\r') {
    if (BufferPos != BufferEnd && *BufferPos == ('\n' + '\r' - Result))
      ++BufferPos;
    Result = '\n';
  }

  if (Result == '\n') {
    ++LineNumber;
    ColumnNumber = 0;
  } else {
    ++ColumnNumber;
  }

  return Result;
}

Token &Lexer::setTokenKind(Token &Result, Token::Kind Kind) const {
  Result.TokenKind = Kind;
  Result.Length = BufferPos - Result.Start;
  return Result;
}

void Lexer::skipToEndOfLine() {
  // Skip to the end of the line, but not past the actual newline character
  // (which we want to generate a Newline token).
  for (;;) {
    int Char = peekNextChar();
    if (Char == -1 || Char == '\n')
      break;
    getNextChar();
  }
}

Token &Lexer::setIdentifierTokenKind(Token &Result) const {
  unsigned Length = BufferPos - Result.Start;
  switch (Length) {
  case 4:
    if (memcmp("rule", Result.Start, 4) == 0)
      return setTokenKind(Result, Token::Kind::KWRule);
    if (memcmp("pool", Result.Start, 4) == 0)
      return setTokenKind(Result, Token::Kind::KWPool);
    break;

  case 5:
    if (memcmp("build", Result.Start, 5) == 0)
      return setTokenKind(Result, Token::Kind::KWBuild);
    break;

  case 7:
    if (memcmp("default", Result.Start, 7) == 0)
      return setTokenKind(Result, Token::Kind::KWDefault);
    if (memcmp("include", Result.Start, 7) == 0)
      return setTokenKind(Result, Token::Kind::KWInclude);
    break;

  case 8:
    if (memcmp("subninja", Result.Start, 7) == 0)
      return setTokenKind(Result, Token::Kind::KWSubninja);
    break;
  }

  return setTokenKind(Result, Token::Kind::Identifier);
}

static bool isIdentifierChar(int Char) {
  return (Char >= 'a' && Char <= 'z') ||
    (Char >= 'A' && Char <= 'A') ||
    (Char >= '0' && Char <= '9') ||
    Char == '_' || Char == '.' || Char == '-';
}

Token &Lexer::lexIdentifier(Token &Result) {
  while (true) {
    int Char = peekNextChar();

    // If this is an escape character, skip the next character.
    if (Char == '$') {
      getNextChar(); // Consume the actual '$'.
      getNextChar(); // Consume the next character.
      continue;
    }

    // Otherwise, continue only if this is an identifier character.
    if (!isIdentifierChar(Char))
      break;

    getNextChar();
  }

  // Recognize keywords specially.
  return setIdentifierTokenKind(Result);
}

Token &Lexer::lexPathString(Token &Result) {
  // String tokens in path contexts consume until a space, ':', or '|'
  // character.
  while (true) {
    int Char = peekNextChar();

    // If this is an escape character, skip the next character.
    if (Char == '$') {
      getNextChar(); // Consume the actual '$'.
      getNextChar(); // Consume the next character.
      continue;
    }

    // Otherwise, continue only if this is not the EOL or EOF.
    if (isspace(Char) || Char == ':' || Char == '|' || Char == -1)
      break;

    getNextChar();
  }

  return setTokenKind(Result, Token::Kind::String);
}

Token &Lexer::lexVariableString(Token &Result) {
  // String tokens in variable assignments consume until the end of the line.
  while (true) {
    int Char = peekNextChar();

    // If this is an escape character, skip the next character.
    if (Char == '$') {
      getNextChar(); // Consume the actual '$'.
      getNextChar(); // Consume the next character.
      continue;
    }

    // Otherwise, continue only if this is not the EOL or EOF.
    if (Char == '\n' || Char == -1)
      break;

    getNextChar();
  }

  return setTokenKind(Result, Token::Kind::String);
}

static bool isNonNewlineSpace(int Char) {
  return isspace(Char) && Char != '\n';
}

Token &Lexer::lex(Token &Result) {
  // Check if we are positioned at whitespace.
  if (isNonNewlineSpace(peekNextChar())) {
    // If we are at the start of a line, then any leading whitespace should be
    // parsed as an indentation token.
    if (ColumnNumber == 0) {
      Result.Start = BufferPos;
      Result.Line = LineNumber;
      Result.Column = ColumnNumber;

      do {
        getNextChar();
      } while (isNonNewlineSpace(peekNextChar()));

      return setTokenKind(Result, Token::Kind::Indentation);
    }

    // Otherwise, skip whitespace.
    do {
      getNextChar();
    } while (isNonNewlineSpace(peekNextChar()));
  }

  // Initialize the token position.
  Result.Start = BufferPos;
  Result.Line = LineNumber;
  Result.Column = ColumnNumber;

  // Check if we are at a string mode independent token.
  int Char = getNextChar();
  if (Char == '\n')
    return setTokenKind(Result, Token::Kind::Newline);
  if (Char == -1)
    return setTokenKind(Result, Token::Kind::EndOfFile);

  // If we are in string lexing mode, delegate immediately if appropriate.
  if (Mode == StringMode::Variable)
    return lexVariableString(Result);
  if (Mode == StringMode::Path) {
    // Only delegate for characters not special to path lexing.
    if (Char != ':' && Char != '|')
      return lexPathString(Result);
  }

  // Otherwise, lex from the regular token set.
  switch (Char) {
  case ':': return setTokenKind(Result, Token::Kind::Colon);
  case '=': return setTokenKind(Result, Token::Kind::Equals);

  case '#': {
    skipToEndOfLine();
    return setTokenKind(Result, Token::Kind::Comment);
  }

  case '|': {
    if (peekNextChar() == '|')
      return getNextChar(), setTokenKind(Result, Token::Kind::PipePipe);
    return setTokenKind(Result, Token::Kind::Pipe);
  }

  default:
    if (isIdentifierChar(Char))
      return lexIdentifier(Result);

    return setTokenKind(Result, Token::Kind::Unknown);
  }
}
