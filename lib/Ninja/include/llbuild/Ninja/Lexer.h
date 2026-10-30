//===- Lexer.h --------------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_NINJA_LEXER_H
#define LLBUILD_NINJA_LEXER_H

#include <cstdint>

#include "llvm/ADT/StringRef.h"

#include "llbuild/Basic/LLVM.h"

namespace llbuild {
namespace ninja {

struct Token {
  enum class Kind {
    Colon,                    ///< ':'
    Comment,                  ///< '# ...'
    EndOfFile,                ///< <end of file>
    Equals,                   ///< '='
    Indentation,              ///< Leading whitespace
    Identifier,               ///< "Identifiers", really everything else
    KWBuild,                  ///< 'build' keyword
    KWDefault,                ///< 'default' keyword
    KWInclude,                ///< 'include' keyword
    KWPool,                   ///< 'pool' keyword
    KWRule,                   ///< 'rule' keyword
    KWSubninja,               ///< 'subninja' keyword
    Newline,                  ///< The end of a line.
    Pipe,                     ///< '|'
    PipePipe,                 ///< '||'
    String,                   ///< Strings, only lexed in string mode.
    Unknown,                  ///< <other>

    KWKindFirst = KWBuild,
    KWKindLast = KWSubninja
  };

  const char* start;          /// The beginning of the token string.
  Kind        tokenKind;      /// The token kind.
  unsigned    length;         /// The length of the token.
  unsigned    line;           /// The line number of the start of this token.
  unsigned    column;         /// The column number at the start of this token.

  /// The name of this token's kind.
  const char *getKindName() const;

  /// True if this token is a keyword.
  bool isKeyword() const {
    return tokenKind >= Kind::KWKindFirst && tokenKind <= Kind::KWKindLast;
  }

  // Dump the token to stderr.
  void dump();
};

/// Interface for lexing tokens from a Ninja build manifest.
///
/// The Ninja manifest language unfortunately has no real string token, rather,
/// the lexing is done in a context sensitive fashion and string tokens are only
/// recognized when the lexer is in a specific mode. Identifier tokens also
/// behave slightly different when in lexed in a context where only an
/// identifier is expected. See \see Lexer::LexingMode and \see
/// Lexer::setMode().
class Lexer {
public:
  enum class LexingMode {
    /// No string tokens will be recognized, identifier tokens will follow usual
    /// rules.
    None,

    /// No string tokens will be recognized, identifier tokens will be lexed
    /// follow specific rules.
    IdentifierSpecific,

    /// Strings will be lexed as expected for path references.
    PathString,

    /// Strings will be lexed as expected for variable assignents.
    VariableString,
  };

private:
  /// The buffer contents.
  StringRef buffer;

  const char* bufferPos;      ///< The current lexer position.
  unsigned    lineNumber;     ///< The current line.
  unsigned    columnNumber;   ///< The current column.
  LexingMode  mode;           ///< The current lexing mode.

  /// Eat a character or -1 from the stream.
  int getNextChar();

  /// Return the next character without consuming it from the stream. This does
  /// not perform newline canonicalization.
  int peekNextChar();

  /// Skip forward until the end of the line.
  void skipToEndOfLine();

  /// Set the token Kind and Length based on the current lexer position, and
  /// return the input.
  Token& setTokenKind(Token& result, Token::Kind kind) const;

  /// Set the token Kind assuming the token is an identifier or keyword, and
  /// return the input.
  Token& setIdentifierTokenKind(Token& result) const;

  /// Lex a token, assuming the current position is the start of an identifier.
  Token& lexIdentifier(Token& result);

  /// Lex a token, assuming the current position is the start of a string and
  /// the lexer is in the "path" string mode.
  Token& lexPathString(Token& result);

  /// Lex a token, assuming the current position is the start of a string and
  /// the lexer is in the "variable" string mode.
  Token& lexVariableString(Token& result);

public:
  explicit Lexer(StringRef contents);
  ~Lexer();

  /// Return the next token from the file or EOF continually
  /// when the end of the file is reached. The input argument is
  /// used as the result, for convenience.
  Token& lex(Token& result);

  /// Get the buffer contents.
  StringRef getBuffer() const { return buffer; }

  /// Get the current lexing mode.
  LexingMode getMode() const { return mode; }

  /// Set the current lexing mode.
  void setMode(LexingMode value) { mode = value; }

  /// @name Utility Methods
  /// @{

  /// Check whether the given \arg Char is valid in an identifier.
  static bool isIdentifierChar(char c) {
    return (c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') ||
      c == '_' || c == '.' || c == '-';
  }

  /// Check whether the given \arg Char is valid in a simple identifier (one
  /// which can appear in the middle of an expression string outside braces).
  static bool isSimpleIdentifierChar(char c) {
    return (c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') ||
      c == '_' || c == '-';
  }

  /// @}
};

}
}

#endif
