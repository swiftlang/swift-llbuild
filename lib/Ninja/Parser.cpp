//===-- Parser.cpp --------------------------------------------------------===//
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

#include "llbuild/Ninja/Parser.h"

#include "llbuild/Ninja/Lexer.h"

using namespace llbuild;
using namespace llbuild::ninja;

#pragma mark - ParseActions

ParseActions::~ParseActions() {
}

#pragma mark - Parser Implementation

class ninja::ParserImpl {
  Lexer Lexer;
  ParseActions &Actions;

  /// The currently lexed token.
  Token Tok;

  /// @name Diagnostics Support
  /// @{

  void error(std::string Message, const Token &At) {
    Actions.error(Message, At);
  }

  void error(std::string Message) {
    error(Message, Tok);
  }

  /// @}

  void getNextNonCommentToken() {
    do {
      Lexer.lex(Tok);
    } while (Tok.TokenKind == Token::Kind::Comment);
  }

  /// Consume the current 'peek token' and lex the next one.
  void consumeToken() {
    getNextNonCommentToken();
  }

  /// Consume tokens until past the next newline (or end of file).
  void skipPastEOL() {
    while (Tok.TokenKind != Token::Kind::Newline &&
           Tok.TokenKind != Token::Kind::EndOfFile)
      Lexer.lex(Tok);

    // Always consume at least one token.
    Lexer.lex(Tok);
  }

  /// Parse a top-level declaration.
  void ParseDecl();

public:
  ParserImpl(const char* Data, uint64_t Length,
             ParseActions &Actions);

  void parse();

  ParseActions& getActions() { return Actions; }
  const class Lexer& getLexer() const { return Lexer; }
};

ParserImpl::ParserImpl(const char* Data, uint64_t Length, ParseActions &Actions)
  : Lexer(Data, Length), Actions(Actions)
{
}

/// Parse the file.
void ParserImpl::parse() {
  // Initialize the Lexer.
  getNextNonCommentToken();

  Actions.actOnBeginManifest("<main>");
  while (Tok.TokenKind != Token::Kind::EndOfFile) {
    ParseDecl();
  }
  Actions.actOnEndManifest();
}

/// Parse a declaration.
void ParserImpl::ParseDecl() {
  switch (Tok.TokenKind) {
  default:
    error("unexpected token");
    skipPastEOL();
  }
}

#pragma mark - Parser

Parser::Parser(const char* Data, uint64_t Length,
               ParseActions &Actions)
  : Impl(new ParserImpl(Data, Length, Actions))
{
}

Parser::~Parser() {
}

void Parser::parse() {
  // Initialize the actions.
  Impl->getActions().initialize(this);

  Impl->parse();
}

const Lexer& Parser::getLexer() const {
  return Impl->getLexer();
}

