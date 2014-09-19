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
  default:
    CASE(Unknown);
    CASE(Colon);
    CASE(Comment);
    CASE(EndOfFile);
    CASE(Equals);
    CASE(Indention);
    CASE(Identifier);
    CASE(KWBuild);
    CASE(KWDefault);
    CASE(KWPool);
    CASE(KWRule);
    CASE(Newline);
    CASE(Pipe);
    CASE(PipePipe);
  }
#undef CASE
}

void Token::dump() {
  std::cerr << "(Token \"" << getKindName() << "\" "
            << (const void*) Start << " " << Length << " "
            << Line << " " << Column << ")\n";
}

///

Lexer::Lexer(const char* Data, uint64_t Length)
  : BufferPos(Data), BufferEnd(Data + Length), LineNumber(1), ColumnNumber(0)
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

Token &Lexer::setTokenKind(Token &Result, Token::Kind Kind) {
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

static bool isNonNewlineSpace(int Char) {
  return isspace(Char) && Char != '\n';
}

Token &Lexer::lex(Token &Result) {
  Result.TokenKind = Token::Kind::Unknown;
  Result.Length = 0;
  Result.Start = BufferPos;
  
  // Skip whitespace.
  while (isNonNewlineSpace(peekNextChar()))
    getNextChar();

  Result.Start = BufferPos;
  Result.Line = LineNumber;
  Result.Column = ColumnNumber;
  int Char = getNextChar();
  switch (Char) {
  case -1:  return setTokenKind(Result, Token::Kind::EndOfFile);
    
  case ':': return setTokenKind(Result, Token::Kind::Colon);
  case '=': return setTokenKind(Result, Token::Kind::Equals);
  case '\n': return setTokenKind(Result, Token::Kind::Newline);

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
    return setTokenKind(Result, Token::Kind::Unknown);
  }
}
