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

namespace {

class ParserImpl {
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

  /// Check that the current token is of the expected kind and consume it,
  /// returning the token.
  Token consumeExpectedToken(Token::Kind Kind) {
    assert(Tok.TokenKind == Kind && "Unexpected token!");
    Token Result = Tok;
    getNextNonCommentToken();
    return Result;
  }

  /// Consume the current token if it matches the given kind, returning whether
  /// or not it was consumed.
  bool consumeIfToken(Token::Kind Kind) {
    if (Tok.TokenKind == Kind) {
      getNextNonCommentToken();
      return true;
    } else {
      return false;
    }
  }

  /// Consume tokens until past the next newline (or end of file).
  void skipPastEOL() {
    while (Tok.TokenKind != Token::Kind::Newline &&
           Tok.TokenKind != Token::Kind::EndOfFile)
      Lexer.lex(Tok);

    // Always consume at least one token.
    consumeToken();
  }

  /// Parse a top-level declaration.
  void parseDecl();

  void parseBindingDecl();
  void parseDefaultDecl();
  void parseIncludeDecl();
  void parseParameterizedDecl();

  bool parseBuildSpecifier(void **Decl_Out);
  bool parseRuleSpecifier(void **Decl_Out);
  bool parsePoolSpecifier(void **Decl_Out);

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
    parseDecl();
  }
  Actions.actOnEndManifest();
}

/// Parse a declaration.
///
/// decl ::= default-decl | include-decl | binding-decl | parameterized-decl
void ParserImpl::parseDecl() {
  switch (Tok.TokenKind) {
  case Token::Kind::Newline:
    consumeToken();
    break;

  case Token::Kind::KWBuild:
  case Token::Kind::KWRule:
  case Token::Kind::KWPool:
    parseParameterizedDecl();
    break;

  case Token::Kind::KWDefault:
    parseDefaultDecl();
    break;

  case Token::Kind::KWInclude:
  case Token::Kind::KWSubninja:
    parseIncludeDecl();
    break;

  case Token::Kind::Identifier:
    parseBindingDecl();
    break;

  default:
    error("unexpected token");
    skipPastEOL();
  }
}

/// binding-decl ::= identifier '=' var-expr-list '\n'
void ParserImpl::parseBindingDecl() {
  Token Name = consumeExpectedToken(Token::Kind::Identifier);

  // Expect a binding to be followed by '='.
  if (!consumeIfToken(Token::Kind::Equals)) {
    error("expected '=' token");
    return skipPastEOL();
  }

  // Consume the RHS.
  //
  // FIXME: We need to put the lexer in a different mode, where it accepts
  // everything until the end of a line as an expression string. Also, empty
  // bindings are allowed.
  if (Tok.TokenKind != Token::Kind::Identifier) {
    error("expected variable value");
    return skipPastEOL();
  }

  Token Value = consumeExpectedToken(Token::Kind::Identifier);

  // The binding should be terminated by a newline.
  if (!consumeIfToken(Token::Kind::Newline)) {
    error("expected newline token");
    return skipPastEOL();
  }
  
  Actions.actOnBindingDecl(Name, Value);
}

/// default-decl ::= "default" identifier-list '\n'
void ParserImpl::parseDefaultDecl() {
  consumeExpectedToken(Token::Kind::KWDefault);

  // Check we have at least one identifier.
  if (Tok.TokenKind != Token::Kind::Identifier) {
    error("expected identifier token");
    return skipPastEOL();
  }

  // Consume all the identifiers.
  std::vector<Token> Names;
  do {
    Names.push_back(consumeExpectedToken(Token::Kind::Identifier));
  } while (Tok.TokenKind == Token::Kind::Identifier);

  // The list should be terminated by a newline.
  if (!consumeIfToken(Token::Kind::Newline)) {
    error("expected newline token");
    return skipPastEOL();
  }

  Actions.actOnDefaultDecl(Names);
}

/// include-decl ::= ( "include" | "subninja" ) identifier '\n'
void ParserImpl::parseIncludeDecl() {
  bool IsInclude = Tok.TokenKind == Token::Kind::KWInclude;
  consumeExpectedToken(IsInclude ? Token::Kind::KWInclude :
                       Token::Kind::KWSubninja);

  if (Tok.TokenKind != Token::Kind::Identifier) {
    error("expected identifier token");
    return skipPastEOL();
  }

  Token Path = consumeExpectedToken(Token::Kind::Identifier);

  if (!consumeIfToken(Token::Kind::Newline)) {
    error("expected newline token");
    return skipPastEOL();
  }

  Actions.actOnIncludeDecl(IsInclude, Path);
}

/// Parse a parameterized declaration (one followed by variable bindings).
///
/// parameterized-decl ::= parameterized-specifier indented-binding*
/// parameterized-specifier ::= build-spec | pool-spec | rule-spec
void ParserImpl::parseParameterizedDecl() {
  // Begin by parsing the specifier.
  void *Decl;
  bool Success;
  Token::Kind Kind = Tok.TokenKind;
  switch (Kind) {
  case Token::Kind::KWBuild:
    Success = parseBuildSpecifier(&Decl);
    break;
  case Token::Kind::KWPool:
    Success = parsePoolSpecifier(&Decl);
    break;
  default:
    assert(Kind == Token::Kind::KWRule);
    Success = parseRuleSpecifier(&Decl);
    break;
  }

  // If parsing the specifier failed, skip forward until we reach a non-indented
  // line.
  if (!Success) {
    while (Tok.TokenKind == Token::Kind::Indentation)
      skipPastEOL();
    return;
  }

  // Otherwise, parse the set of indented bindings.

  // FIXME: Implement.
  error("FIXME: implement parameterized decl");
  skipPastEOL();

  switch (Kind) {
  case Token::Kind::KWBuild:
    Actions.actOnEndBuildDecl(static_cast<ParseActions::BuildResult>(Decl));
    break;
  case Token::Kind::KWPool:
    assert(Kind == Token::Kind::KWPool);
    Actions.actOnEndPoolDecl(static_cast<ParseActions::PoolResult>(Decl));
    break;
  default:
    assert(Kind == Token::Kind::KWRule);
    Actions.actOnEndRuleDecl(static_cast<ParseActions::RuleResult>(Decl));
    break;
  }
}

/// build-spec ::= "build" identifier-list ":" identifier identifier-list
///                [ "|" identifier-list ] [ "||" identifier-list" ] '\n'
bool ParserImpl::parseBuildSpecifier(void **Decl_Out) {
  // FIXME: Implement.
  error("FIXME: implement build specifier decl");
  skipPastEOL();
  return false;
}

/// pool-spec ::= "pool" identifier '\n'
bool ParserImpl::parsePoolSpecifier(void **Decl_Out) {
  consumeExpectedToken(Token::Kind::KWPool);

  if (Tok.TokenKind != Token::Kind::Identifier) {
    error("expected identifier token");
    return false;
  }

  Token Name = consumeExpectedToken(Token::Kind::Identifier);

  if (!consumeIfToken(Token::Kind::Newline)) {
    error("expected newline token");
    return false;
  }

  ParseActions::PoolResult Result = Actions.actOnBeginPoolDecl(Name);
  *Decl_Out = static_cast<void*>(Result);

  return true;
}

/// rule-spec ::= "rule" identifier '\n'
bool ParserImpl::parseRuleSpecifier(void **Decl_Out) {
  consumeExpectedToken(Token::Kind::KWRule);

  if (Tok.TokenKind != Token::Kind::Identifier) {
    error("expected identifier token");
    return false;
  }

  Token Name = consumeExpectedToken(Token::Kind::Identifier);

  if (!consumeIfToken(Token::Kind::Newline)) {
    error("expected newline token");
    return false;
  }

  ParseActions::RuleResult Result = Actions.actOnBeginRuleDecl(Name);
  *Decl_Out = static_cast<void*>(Result);

  return true;
}

}

#pragma mark - Parser

Parser::Parser(const char* Data, uint64_t Length,
               ParseActions &Actions)
  : Impl(static_cast<void*>(new ParserImpl(Data, Length, Actions)))
{
}

Parser::~Parser() {
  delete static_cast<ParserImpl*>(Impl);
}

void Parser::parse() {
  // Initialize the actions.
  static_cast<ParserImpl*>(Impl)->getActions().initialize(this);

  static_cast<ParserImpl*>(Impl)->parse();
}

const Lexer& Parser::getLexer() const {
  return static_cast<ParserImpl*>(Impl)->getLexer();
}
