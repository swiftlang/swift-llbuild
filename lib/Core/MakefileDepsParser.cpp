//===-- MakefileDepsParser.cpp --------------------------------------------===//
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

#include "llbuild/Core/MakefileDepsParser.h"

using namespace llbuild;
using namespace llbuild::core;

MakefileDepsParser::ParseActions::~ParseActions() {}

#pragma mark - MakefileDepsParser Implementation

static bool IsWordChar(int Char) {
  switch (Char) {
  case '\0':
  case '\t':
  case '\n':
  case ' ':
  case '$':
  case ':':
  case ';':
  case '=':
  case '|':
  case '%':
    return false;
  default:
    return true;
  }
}

static void SkipWhitespaceAndComments(const char*& Cur, const char* End) {
  for (; Cur != End; ++Cur) {
    int Char = *Cur;

    // Skip comments.
    if (Char == '#') {
      // Skip to the next newline.
      while (Cur + 1 != End && Cur[1] == '\n')
        ++Cur;
      continue;
    }

    if (Char == ' ' || Char == '\t' || Char == '\n')
      continue;

    break;
  }
}

static void SkipNonNewlineWhitespace(const char*& Cur, const char* End) {
  for (; Cur != End; ++Cur) {
    int Char = *Cur;

    // Skip regular whitespace.
    if (Char == ' ' || Char == '\t')
      continue;

    // If this is an escaped newline, also skip it.
    if (Char == '\\' && Cur + 1 != End && Cur[1] == '\n') {
      ++Cur;
      continue;
    }

    // Otherwise, stop scanning.
    break;
  }
}

static void SkipToEndOfLine(const char*& Cur, const char* End) {
  for (; Cur != End; ++Cur) {
    int Char = *Cur;

    if (Char == '\n') {
      ++Cur;
      break;
    }
  }
}

static void LexWord(const char*& Cur, const char* End) {
  for (; Cur != End; ++Cur) {
    int Char = *Cur;

    // Check if this is an escape sequence.
    if (Char == '\\') {
      // If this is a line continuation, it ends the word.
      if (Cur + 1 != End && Cur[1] == '\n')
        break;

      // Otherwise, skip the escaped character.
      ++Cur;
      continue;
    }

    // Otherwise, if this is not a valid word character then skip it.
    if (!IsWordChar(*Cur))
      break;
  }
}

void MakefileDepsParser::parse() {
  const char* Cur = Data;
  const char* End = Data + Length;

  // While we have input data...
  while (Cur != End) {
    // Skip leading whitespace and comments.
    SkipWhitespaceAndComments(Cur, End);

    // If we have reached the end of the input, we are done.
    if (Cur == End)
      break;
    
    // The next token should be a word.
    const char* WordStart = Cur;
    LexWord(Cur, End);
    if (Cur == WordStart) {
      Actions.error("unexpected character in file", Cur - Data);
      SkipToEndOfLine(Cur, End);
      continue;
    }
    Actions.actOnRuleStart(WordStart, Cur - WordStart);

    // The next token should be a colon.
    SkipNonNewlineWhitespace(Cur, End);
    if (Cur == End || *Cur != ':') {
      Actions.error("missing ':' following rule", Cur - Data);
      Actions.actOnRuleEnd();
      SkipToEndOfLine(Cur, End);
      continue;
    }

    // Skip the colon.
    ++Cur;

    // Consume dependency words until we reach the end of a line.
    while (Cur != End) {
      // Skip forward and check for EOL.
      SkipNonNewlineWhitespace(Cur, End);
      if (Cur == End || *Cur == '\n')
        break;

      // Otherwise, we should have a word.
      const char* WordStart = Cur;
      LexWord(Cur, End);
      if (Cur == WordStart) {
        Actions.error("unexpected character in prerequisites", Cur - Data);
        SkipToEndOfLine(Cur, End);
        continue;
      }
      Actions.actOnRuleDependency(WordStart, Cur - WordStart);
    }
    Actions.actOnRuleEnd();
  }
}
