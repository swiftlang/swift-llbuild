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

  bool parseBuildSpecifier(ParseActions::BuildResult *Decl_Out);
  bool parsePoolSpecifier(ParseActions::PoolResult *Decl_Out);
  bool parseRuleSpecifier(ParseActions::RuleResult *Decl_Out);

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
  union {
    ParseActions::BuildResult AsBuild;
    ParseActions::PoolResult AsPool;
    ParseActions::RuleResult AsRule;
  } Decl;
  bool Success;
  Token::Kind Kind = Tok.TokenKind;
  switch (Kind) {
  case Token::Kind::KWBuild:
    Success = parseBuildSpecifier(&Decl.AsBuild);
    break;
  case Token::Kind::KWPool:
    Success = parsePoolSpecifier(&Decl.AsPool);
    break;
  default:
    assert(Kind == Token::Kind::KWRule);
    Success = parseRuleSpecifier(&Decl.AsRule);
    break;
  }

  // If parsing the specifier failed, skip forward until we reach a non-indented
  // line.
  if (!Success) {
    do {
      skipPastEOL();
    } while (Tok.TokenKind == Token::Kind::Indentation);

    return;
  }

  // Otherwise, parse the set of indented bindings.
  //
  // NOTE: This is similar to parseBindingDecl(), and should be kept in sync.
  while (consumeIfToken(Token::Kind::Indentation)) {
    // The leading token should be an identifier.
    if (Tok.TokenKind != Token::Kind::Identifier) {
      error("expected identifier token");
      skipPastEOL();
      continue;
    }

    Token Name = consumeExpectedToken(Token::Kind::Identifier);

    // Expect a binding to be followed by '='.
    if (!consumeIfToken(Token::Kind::Equals)) {
      error("expected '=' token");
      skipPastEOL();
      continue;
    }

    // Consume the RHS.
    //
    // FIXME: We need to put the lexer in a different mode, where it accepts
    // everything until the end of a line as an expression string. Also, empty
    // bindings are allowed.
    if (Tok.TokenKind != Token::Kind::Identifier) {
      error("expected variable value");
      skipPastEOL();
      continue;
    }

    Token Value = consumeExpectedToken(Token::Kind::Identifier);

    // The binding should be terminated by a newline.
    if (!consumeIfToken(Token::Kind::Newline)) {
      error("expected newline token");
      skipPastEOL();
      continue;
    }

    // Dispatch to the appropriate parser action.
    switch (Kind) {
    case Token::Kind::KWBuild:
      Actions.actOnBuildBindingDecl(Decl.AsBuild, Name, Value);
      break;
    case Token::Kind::KWPool:
      Actions.actOnPoolBindingDecl(Decl.AsPool, Name, Value);
      break;
    default:
      assert(Kind == Token::Kind::KWRule);
      Actions.actOnRuleBindingDecl(Decl.AsRule, Name, Value);
      break;
    }
  }

  switch (Kind) {
  case Token::Kind::KWBuild:
    Actions.actOnEndBuildDecl(Decl.AsBuild);
    break;
  case Token::Kind::KWPool:
    Actions.actOnEndPoolDecl(Decl.AsPool);
    break;
  default:
    assert(Kind == Token::Kind::KWRule);
    Actions.actOnEndRuleDecl(Decl.AsRule);
    break;
  }
}

/// build-spec ::= "build" identifier-list ":" identifier identifier-list
///                [ "|" identifier-list ] [ "||" identifier-list" ] '\n'
bool ParserImpl::parseBuildSpecifier(ParseActions::BuildResult *Decl_Out) {
  consumeExpectedToken(Token::Kind::KWBuild);

  // Parse the output list.
  //
  // FIXME: This also needs to put the lexer into a special mode, '=' characters
  // are allowed as identifiers here.
  if (Tok.TokenKind != Token::Kind::Identifier) {
    error("expected identifier token");
    return false;
  }
  std::vector<Token> Outputs;
  do {
    Outputs.push_back(consumeExpectedToken(Token::Kind::Identifier));
  } while (Tok.TokenKind == Token::Kind::Identifier);

  // Expect the identifier list to be terminated by a colon.
  if (!consumeIfToken(Token::Kind::Colon)) {
    error("expected ':' token");
    return false;
  }

  // Parse the rule name.
  if (Tok.TokenKind != Token::Kind::Identifier) {
    error("expected identifier token");
    return false;
  }
  Token Name = consumeExpectedToken(Token::Kind::Identifier);

  // Parse the explicit inputs.
  std::vector<Token> Inputs;
  while (Tok.TokenKind == Token::Kind::Identifier) {
    Inputs.push_back(consumeExpectedToken(Token::Kind::Identifier));
  }
  unsigned NumExplicitInputs = Inputs.size();

  // Parse the implicit inputs, if present.
  if (consumeIfToken(Token::Kind::Pipe)) {
    while (Tok.TokenKind == Token::Kind::Identifier) {
      Inputs.push_back(consumeExpectedToken(Token::Kind::Identifier));
    }
  }
  unsigned NumImplicitInputs = Inputs.size() - NumExplicitInputs;

  // Parse the order-only inputs, if present.
  if (consumeIfToken(Token::Kind::PipePipe)) {
    while (Tok.TokenKind == Token::Kind::Identifier) {
      Inputs.push_back(consumeExpectedToken(Token::Kind::Identifier));
    }
  }

  if (!consumeIfToken(Token::Kind::Newline)) {
    error("expected newline token");
    return false;
  }

  *Decl_Out = Actions.actOnBeginBuildDecl(Name, Outputs, Inputs,
                                          NumExplicitInputs, NumImplicitInputs);

  return true;
}

/// pool-spec ::= "pool" identifier '\n'
bool ParserImpl::parsePoolSpecifier(ParseActions::PoolResult *Decl_Out) {
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

  *Decl_Out = Actions.actOnBeginPoolDecl(Name);

  return true;
}

/// rule-spec ::= "rule" identifier '\n'
bool ParserImpl::parseRuleSpecifier(ParseActions::RuleResult *Decl_Out) {
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

  *Decl_Out = Actions.actOnBeginRuleDecl(Name);

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
