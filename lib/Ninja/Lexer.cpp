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

#include "llbuild/Basic/LLVM.h"

#include <cstring>
#include <string>
#include <iostream>
#include <iomanip>

using namespace llbuild;
using namespace llbuild::ninja;

///

const char* Token::getKindName() const {
#define CASE(name) case Kind::name: return #name
  switch (tokenKind) {
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

#ifndef NDEBUG
void Token::dump() {
  std::cerr << "(Token \"" << getKindName() << "\" "
            << (const void*) start << " " << length << " "
            << line << " " << column << ")\n";
}
#endif

///

Lexer::Lexer(StringRef buffer)
  : buffer(buffer), bufferPos(buffer.data()), lineNumber(1), columnNumber(0),
    mode(LexingMode::None)
{
}

Lexer::~Lexer() {
}

int Lexer::peekNextChar() {
  if (bufferPos == buffer.end())
    return -1;
  return *bufferPos;
}

int Lexer::getNextChar() {
  if (bufferPos == buffer.end())
    return -1;

  // Handle DOS/Mac newlines here, by stripping duplicates and by returning '\n'
  // for both.
  char result = *bufferPos++;
  if (result == '\n' || result == '\r') {
    if (bufferPos != buffer.end() && *bufferPos == ('\n' + '\r' - result))
      ++bufferPos;
    result = '\n';
  }

  if (result == '\n') {
    ++lineNumber;
    columnNumber = 0;
  } else {
    ++columnNumber;
  }

  return result;
}

Token& Lexer::setTokenKind(Token& result, Token::Kind kind) const {
  result.tokenKind = kind;
  result.length = bufferPos - result.start;
  return result;
}

void Lexer::skipToEndOfLine() {
  // Skip to the end of the line, but not past the actual newline character
  // (which we want to generate a Newline token).
  for (;;) {
    int c = peekNextChar();
    if (c == -1 || c == '\n')
      break;
    getNextChar();
  }
}

Token& Lexer::setIdentifierTokenKind(Token& result) const {
  unsigned length = bufferPos - result.start;
  switch (length) {
  case 4:
    if (memcmp("rule", result.start, 4) == 0)
      return setTokenKind(result, Token::Kind::KWRule);
    if (memcmp("pool", result.start, 4) == 0)
      return setTokenKind(result, Token::Kind::KWPool);
    break;

  case 5:
    if (memcmp("build", result.start, 5) == 0)
      return setTokenKind(result, Token::Kind::KWBuild);
    break;

  case 7:
    if (memcmp("default", result.start, 7) == 0)
      return setTokenKind(result, Token::Kind::KWDefault);
    if (memcmp("include", result.start, 7) == 0)
      return setTokenKind(result, Token::Kind::KWInclude);
    break;

  case 8:
    if (memcmp("subninja", result.start, 7) == 0)
      return setTokenKind(result, Token::Kind::KWSubninja);
    break;
  }

  return setTokenKind(result, Token::Kind::Identifier);
}

Token& Lexer::lexIdentifier(Token& result) {
  // Consume characters as long as we are in an identifier.
  while (Lexer::isIdentifierChar(peekNextChar())) {
    getNextChar();
  }

  // If we are in identifier specific mode, ignore keywords.
  if (mode == Lexer::LexingMode::IdentifierSpecific)
    return setTokenKind(result, Token::Kind::Identifier);

  // Recognize keywords specially.
  return setIdentifierTokenKind(result);
}

static bool isNonNewlineSpace(int c) {
  return isspace(c) && c != '\n';
}

Token &Lexer::lexPathString(Token &result) {
  // String tokens in path contexts consume until a space, ':', or '|'
  // character.
  while (true) {
    int c = peekNextChar();

    // If this is an escape character, skip the next character.
    if (c == '$') {
      getNextChar(); // Consume the actual '$'.

      // Consume the next character.
      c = getNextChar();

      // If the character was a newline, consume any leading spaces.
      if (c == '\n') {
        while (isNonNewlineSpace(peekNextChar()))
          getNextChar();
      }

      continue;
    }

    // Otherwise, continue only if this is not the EOL or EOF.
    if (isspace(c) || c == ':' || c == '|' || c == -1)
      break;

    getNextChar();
  }

  return setTokenKind(result, Token::Kind::String);
}

Token& Lexer::lexVariableString(Token& result) {
  // String tokens in variable assignments consume until the end of the line.
  while (true) {
    int c = peekNextChar();

    // If this is an escape character, skip the next character.
    if (c == '$') {
      getNextChar(); // Consume the actual '$'.
      getNextChar(); // Consume the next character.
      continue;
    }

    // Otherwise, continue only if this is not the EOL or EOF.
    if (c == '\n' || c == -1)
      break;

    getNextChar();
  }

  return setTokenKind(result, Token::Kind::String);
}

Token& Lexer::lex(Token& result) {
  // Check if we need to emit an indentation token.
  int c = peekNextChar();
  if (isNonNewlineSpace(c) && columnNumber == 0) {
    // If we are at the start of a line, then any leading whitespace should be
    // parsed as an indentation token.
    //
    // We do not need to handle "$\n" sequences here because they will be
    // consumed next, and the exact length of the indentation token is never
    // used.
    if (columnNumber == 0) {
      result.start = bufferPos;
      result.line = lineNumber;
      result.column = columnNumber;

      do {
        getNextChar();
      } while (isNonNewlineSpace(peekNextChar()));

      return setTokenKind(result, Token::Kind::Indentation);
    }
  }

  // Otherwise, consume any leading whitespace or "$\n" escape sequences (except
  // at the start of lines, which Ninja does not recognize).
  while (true) {
    // Check for escape sequences.
    if (c == '$' && columnNumber != 0) {
      // If this is a newline escape, consume it.
      if (bufferPos + 1 != buffer.end() && bufferPos[1] == '\n') {
        getNextChar();
        getNextChar();
      } else {
        // Otherwise, break out and lex normally.
        break;
      }
    } else if (isNonNewlineSpace(c)) {
      getNextChar();
    } else {
      break;
    }
    
    c = peekNextChar();
  }

  // Initialize the token position.
  result.start = bufferPos;
  result.line = lineNumber;
  result.column = columnNumber;

  // Check if we are at a string mode independent token.
  if (c == '\n') {
    getNextChar();
    return setTokenKind(result, Token::Kind::Newline);
  }
  if (c == -1)
    return setTokenKind(result, Token::Kind::EndOfFile);

  // If we are in string lexing mode, delegate immediately if appropriate.
  if (mode == LexingMode::VariableString)
    return lexVariableString(result);
  if (mode == LexingMode::PathString) {
    // Only delegate for characters not special to path lexing.
    if (c != ':' && c != '|')
      return lexPathString(result);
  }

  // Otherwise, consume the character and lex from the regular token set.
  getNextChar();
  switch (c) {
  case ':': return setTokenKind(result, Token::Kind::Colon);
  case '=': return setTokenKind(result, Token::Kind::Equals);

  case '#': {
    skipToEndOfLine();
    return setTokenKind(result, Token::Kind::Comment);
  }

  case '|': {
    if (peekNextChar() == '|') {
      (void) getNextChar();
      return setTokenKind(result, Token::Kind::PipePipe);
    }
    return setTokenKind(result, Token::Kind::Pipe);
  }

  default:
    if (Lexer::isIdentifierChar(c))
      return lexIdentifier(result);

    return setTokenKind(result, Token::Kind::Unknown);
  }
}
