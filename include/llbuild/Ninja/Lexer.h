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
    Unknown,                  ///< <other>

    KWKindFirst = KWBuild,
    KWKindLast = KWSubninja
  };

  Kind        TokenKind;      /// The token kind.
  const char* Start;          /// The beginning of the token string.
  unsigned    Length;         /// The length of the token.
  unsigned    Line;           /// The line number of the start of this token.
  unsigned    Column;         /// The column number at the start of this token.

  /// The name of this token's kind.
  const char *getKindName() const;

  /// True if this token is a keyword.
  bool isKeyword() const {
    return TokenKind >= Kind::KWKindFirst && TokenKind <= Kind::KWKindLast;
  }

  // Dump the token to stderr.
  void dump();
};

/// Interface for lexing tokens from a Ninja build manifest.
class Lexer {
  const char* BufferStart;    ///< The buffer end position.
  const char* BufferPos;      ///< The current lexer position.
  const char* BufferEnd;      ///< The buffer end position.
  unsigned    LineNumber;     ///< The current line.
  unsigned    ColumnNumber;   ///< The current column.

  /// Eat a character or -1 from the stream.
  int getNextChar();

  /// Return the next character without consuming it from the stream. This does
  /// not perform newline canonicalization.
  int peekNextChar();

  /// Skip forward until the end of the line.
  void skipToEndOfLine();

  /// Set the token Kind and Length based on the current lexer position, and
  /// return the input.
  Token &setTokenKind(Token &Result, Token::Kind Kind) const;

  /// Set the token Kind assuming the token is an identifier or keyword, and
  /// return the input.
  Token &setIdentifierTokenKind(Token &Result) const;

  /// Lex a token, assuming the current position is the start of an identifier.
  Token &lexIdentifier(Token &Result);

public:
  explicit Lexer(const char *Data, uint64_t Length);
  ~Lexer();

  /// Return the next token from the file or EOF continually
  /// when the end of the file is reached. The input argument is
  /// used as the result, for convenience.
  Token &lex(Token &Result);

  /// Get the buffer start pointer.
  const char* getBufferStart() const { return BufferStart; }

  /// Get the buffer end pointer.
  const char* getBufferEnd() const { return BufferEnd; }
};

}
}

#endif
