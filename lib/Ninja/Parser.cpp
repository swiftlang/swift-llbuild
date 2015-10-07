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

#include "llbuild/Basic/LLVM.h"
#include "llbuild/Ninja/Lexer.h"

using namespace llbuild;
using namespace llbuild::ninja;

#pragma mark - ParseActions

ParseActions::~ParseActions() {
}

#pragma mark - Parser Implementation

namespace {

class ParserImpl {
  Lexer lexer;
  ParseActions &actions;

  /// The currently lexed token.
  Token tok;

  /// @name Diagnostics Support
  /// @{

  void error(std::string message, const Token &at) {
    actions.error(message, at);
  }

  void error(std::string message) {
    error(message, tok);
  }

  /// @}

  void getNextNonCommentToken() {
    do {
      lexer.lex(tok);
    } while (tok.tokenKind == Token::Kind::Comment);
  }

  /// Consume the current 'peek token' and lex the next one.
  void consumeToken() {
    getNextNonCommentToken();
  }

  /// Check that the current token is of the expected kind and consume it,
  /// returning the token.
  Token consumeExpectedToken(Token::Kind kind) {
    assert(tok.tokenKind == kind && "Unexpected token!");
    Token result = tok;
    getNextNonCommentToken();
    return result;
  }

  /// Consume the current token if it matches the given kind, returning whether
  /// or not it was consumed.
  bool consumeIfToken(Token::Kind kind) {
    if (tok.tokenKind == kind) {
      getNextNonCommentToken();
      return true;
    } else {
      return false;
    }
  }

  /// Consume tokens until past the next newline (or end of file).
  void skipPastEOL() {
    while (tok.tokenKind != Token::Kind::Newline &&
           tok.tokenKind != Token::Kind::EndOfFile)
      lexer.lex(tok);

    // Always consume at least one token.
    consumeToken();
  }

  /// Parse a top-level declaration.
  void parseDecl();

  /// Parse a variable binding.
  ///
  /// \param name_out [out] On success, the identifier token for the binding
  /// name.
  ///
  /// \param value_out [out] On success, the string token for the binding
  /// value.
  ///
  /// \returns True on success. On failure, the lexer will be advanced past the
  /// next newline (see \see skipPastEOL()).
  bool parseBindingInternal(Token* name_out, Token* value_out);

  void parseBindingDecl();
  void parseDefaultDecl();
  void parseIncludeDecl();
  void parseParameterizedDecl();

  bool parseBuildSpecifier(ParseActions::BuildResult *decl_out);
  bool parsePoolSpecifier(ParseActions::PoolResult *decl_out);
  bool parseRuleSpecifier(ParseActions::RuleResult *decl_out);

public:
  ParserImpl(const char* data, uint64_t length,
             ParseActions& actions);

  void parse();

  ParseActions& getActions() { return actions; }
  const class Lexer& getLexer() const { return lexer; }
};

ParserImpl::ParserImpl(const char* data, uint64_t length, ParseActions& actions)
    : lexer(StringRef(data, length)), actions(actions)
{
}

/// Parse the file.
void ParserImpl::parse() {
  // Initialize the Lexer.
  getNextNonCommentToken();

  actions.actOnBeginManifest("<main>");
  while (tok.tokenKind != Token::Kind::EndOfFile) {
    // Check that no one accidentally left the lexer in the wrong mode.
    assert(lexer.getMode() == Lexer::LexingMode::None);

    parseDecl();
  }
  actions.actOnEndManifest();
}

/// Parse a declaration.
///
/// decl ::= default-decl | include-decl | binding-decl | parameterized-decl
void ParserImpl::parseDecl() {
  switch (tok.tokenKind) {
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

/// binding-decl ::= identifier '=' var-string '\n'
bool ParserImpl::parseBindingInternal(Token* name_out, Token* value_out) {
  // The leading token should be an identifier.
  if (tok.tokenKind != Token::Kind::Identifier) {
    error("expected variable name");
    skipPastEOL();
    return false;
  }

  *name_out = consumeExpectedToken(Token::Kind::Identifier);

  // Expect the variable name to be followed by '='.
  if (tok.tokenKind != Token::Kind::Equals) {
    error("expected '=' token");
    skipPastEOL();
    return false;
  }

  // Consume the '=' and the RHS.
  //
  // We must consume the RHS in variable lexing mode, so given our lookahead
  // design we switch modes, consume the equals (which will advance the lexer),
  // and then immediately switch back.
  lexer.setMode(Lexer::LexingMode::VariableString);
  consumeExpectedToken(Token::Kind::Equals);
  lexer.setMode(Lexer::LexingMode::None);

  // If the RHS is a newline, we have an empty binding which we handle as a
  // special case.
  if (tok.tokenKind == Token::Kind::Newline) {
    // Derive an empty string token from the newline.
    *value_out = consumeExpectedToken(Token::Kind::Newline);
    value_out->tokenKind = Token::Kind::String;
    value_out->length = 0;
    return true;
  }

  // Otherwise, check that the RHS is a string.
  if (tok.tokenKind != Token::Kind::String) {
    error("expected variable value");
    skipPastEOL();
    return false;
  }

  *value_out = consumeExpectedToken(Token::Kind::String);

  // The binding should be terminated by a newline.
  if (!consumeIfToken(Token::Kind::Newline)) {
    error("expected newline token");
    skipPastEOL();
    return false;
  }

  return true;
}

/// binding-decl ::= identifier '=' var-string '\n'
void ParserImpl::parseBindingDecl() {
  Token name, value;
  if (parseBindingInternal(&name, &value))
    actions.actOnBindingDecl(name, value);
}

/// default-decl ::= "default" path-string-list '\n'
void ParserImpl::parseDefaultDecl() {
  // Put the lexer in path string mode.
  lexer.setMode(Lexer::LexingMode::PathString);
  consumeExpectedToken(Token::Kind::KWDefault);

  // Consume all the strings.
  SmallVector<Token, 8> names;
  while (tok.tokenKind == Token::Kind::String) {
    names.push_back(consumeExpectedToken(Token::Kind::String));
  }

  // Reset the string mode.
  lexer.setMode(Lexer::LexingMode::None);

  // Verify we have at least one name.
  if (names.empty()) {
    error("expected target path string");
    return skipPastEOL();
  }

  // The list should be terminated by a newline.
  if (!consumeIfToken(Token::Kind::Newline)) {
    error("expected newline token");
    return skipPastEOL();
  }

  actions.actOnDefaultDecl(names);
}

/// include-decl ::= ( "include" | "subninja" ) path-string '\n'
void ParserImpl::parseIncludeDecl() {
  // Put the lexer in path string mode for one token.
  lexer.setMode(Lexer::LexingMode::PathString);
  bool isInclude = tok.tokenKind == Token::Kind::KWInclude;
  consumeExpectedToken(isInclude ? Token::Kind::KWInclude :
                       Token::Kind::KWSubninja);
  lexer.setMode(Lexer::LexingMode::None);

  if (tok.tokenKind != Token::Kind::String) {
    error("expected path string");
    return skipPastEOL();
  }

  Token path = consumeExpectedToken(Token::Kind::String);

  if (!consumeIfToken(Token::Kind::Newline)) {
    error("expected newline token");
    return skipPastEOL();
  }

  actions.actOnIncludeDecl(isInclude, path);
}

/// Parse a parameterized declaration (one followed by variable bindings).
///
/// parameterized-decl ::= parameterized-specifier indented-binding*
/// parameterized-specifier ::= build-spec | pool-spec | rule-spec
/// indented-binding := indention binding-decl
void ParserImpl::parseParameterizedDecl() {
  // Begin by parsing the specifier.
  union {
    ParseActions::BuildResult asBuild;
    ParseActions::PoolResult asPool;
    ParseActions::RuleResult asRule;
  } decl;
  bool success;
  Token startTok = tok;
  Token::Kind kind = tok.tokenKind;
  switch (kind) {
  case Token::Kind::KWBuild:
    success = parseBuildSpecifier(&decl.asBuild);
    break;
  case Token::Kind::KWPool:
    success = parsePoolSpecifier(&decl.asPool);
    break;
  default:
    assert(kind == Token::Kind::KWRule);
    success = parseRuleSpecifier(&decl.asRule);
    break;
  }

  // If parsing the specifier failed, skip forward until we reach a non-indented
  // line.
  if (!success) {
    do {
      skipPastEOL();
    } while (tok.tokenKind == Token::Kind::Indentation);

    return;
  }

  // Otherwise, parse the set of indented bindings.
  while (tok.tokenKind == Token::Kind::Indentation) {
    // Put the lexer in identifier specific mode for the binding. It will
    // automatically be taken out by parseBindingInternal switching for the
    // value.
    lexer.setMode(Lexer::LexingMode::IdentifierSpecific);
    consumeExpectedToken(Token::Kind::Indentation);

    // Allow blank lines in parameterized decls.
    if (tok.tokenKind == Token::Kind::Newline) {
      // Ensure we switch out of identifier specific mode.
      lexer.setMode(Lexer::LexingMode::None);
      consumeExpectedToken(Token::Kind::Newline);
      continue;
    }

    Token name, value;
    if (parseBindingInternal(&name, &value)) {
      // Dispatch to the appropriate parser action.
      switch (kind) {
      case Token::Kind::KWBuild:
        actions.actOnBuildBindingDecl(decl.asBuild, name, value);
        break;
      case Token::Kind::KWPool:
        actions.actOnPoolBindingDecl(decl.asPool, name, value);
        break;
      default:
        assert(kind == Token::Kind::KWRule);
        actions.actOnRuleBindingDecl(decl.asRule, name, value);
        break;
      }
    }
  }

  switch (kind) {
  case Token::Kind::KWBuild:
    actions.actOnEndBuildDecl(decl.asBuild, startTok);
    break;
  case Token::Kind::KWPool:
    actions.actOnEndPoolDecl(decl.asPool, startTok);
    break;
  default:
    assert(kind == Token::Kind::KWRule);
    actions.actOnEndRuleDecl(decl.asRule, startTok);
    break;
  }
}

/// build-spec ::= "build" path-string-list ":" path-string path-string-list
///                [ "|" path-string-list ] [ "||" path-string-list" ] '\n'
bool ParserImpl::parseBuildSpecifier(ParseActions::BuildResult *decl_out) {
  // Put the lexer in path string mode for the entire specifier parsing.
  lexer.setMode(Lexer::LexingMode::PathString);
  consumeExpectedToken(Token::Kind::KWBuild);

  // Parse the output list.
  if (tok.tokenKind != Token::Kind::String) {
    error("expected output path string");
    lexer.setMode(Lexer::LexingMode::None);
    return false;
  }
  SmallVector<Token, 8> outputs;
  do {
    outputs.push_back(consumeExpectedToken(Token::Kind::String));
  } while (tok.tokenKind == Token::Kind::String);

  // Expect the string list to be terminated by a colon.
  if (tok.tokenKind != Token::Kind::Colon) {
    error("expected ':' token");
    lexer.setMode(Lexer::LexingMode::None);
    return false;
  }

  // Parse the rule name identifier, switching out of path string mode for this
  // one lex.
  lexer.setMode(Lexer::LexingMode::IdentifierSpecific);
  consumeExpectedToken(Token::Kind::Colon);
  lexer.setMode(Lexer::LexingMode::PathString);

  if (tok.tokenKind != Token::Kind::Identifier) {
    error("expected rule name identifier");
    lexer.setMode(Lexer::LexingMode::None);
    return false;
  }
  Token name = consumeExpectedToken(Token::Kind::Identifier);

  // Parse the explicit inputs.
  SmallVector<Token, 8> inputs;
  while (tok.tokenKind == Token::Kind::String) {
    inputs.push_back(consumeExpectedToken(Token::Kind::String));
  }
  unsigned numExplicitInputs = inputs.size();

  // Parse the implicit inputs, if present.
  if (consumeIfToken(Token::Kind::Pipe)) {
    while (tok.tokenKind == Token::Kind::String) {
      inputs.push_back(consumeExpectedToken(Token::Kind::String));
    }
  }
  unsigned numImplicitInputs = inputs.size() - numExplicitInputs;

  // Parse the order-only inputs, if present.
  if (consumeIfToken(Token::Kind::PipePipe)) {
    while (tok.tokenKind == Token::Kind::String) {
      inputs.push_back(consumeExpectedToken(Token::Kind::String));
    }
  }

  // Reset the lexer mode.
  lexer.setMode(Lexer::LexingMode::None);

  if (!consumeIfToken(Token::Kind::Newline)) {
    error("expected newline token");
    return false;
  }

  *decl_out = actions.actOnBeginBuildDecl(name, outputs, inputs,
                                          numExplicitInputs, numImplicitInputs);

  return true;
}

/// pool-spec ::= "pool" identifier '\n'
bool ParserImpl::parsePoolSpecifier(ParseActions::PoolResult *decl_out) {
  // Put the lexer in identifier specific mode for the name.
  lexer.setMode(Lexer::LexingMode::IdentifierSpecific);
  consumeExpectedToken(Token::Kind::KWPool);
  lexer.setMode(Lexer::LexingMode::None);

  if (tok.tokenKind != Token::Kind::Identifier) {
    error("expected pool name identifier");
    return false;
  }

  Token name = consumeExpectedToken(Token::Kind::Identifier);

  if (!consumeIfToken(Token::Kind::Newline)) {
    error("expected newline token");
    return false;
  }

  *decl_out = actions.actOnBeginPoolDecl(name);

  return true;
}

/// rule-spec ::= "rule" identifier '\n'
bool ParserImpl::parseRuleSpecifier(ParseActions::RuleResult *decl_out) {
  // Put the lexer in identifier specific mode for the name.
  lexer.setMode(Lexer::LexingMode::IdentifierSpecific);
  consumeExpectedToken(Token::Kind::KWRule);
  lexer.setMode(Lexer::LexingMode::None);

  if (tok.tokenKind != Token::Kind::Identifier) {
    error("expected rule name identifier");
    return false;
  }

  Token name = consumeExpectedToken(Token::Kind::Identifier);

  if (!consumeIfToken(Token::Kind::Newline)) {
    error("expected newline token");
    return false;
  }

  *decl_out = actions.actOnBeginRuleDecl(name);

  return true;
}

}

#pragma mark - Parser

Parser::Parser(const char* data, uint64_t length,
               ParseActions& actions)
  : impl(static_cast<void*>(new ParserImpl(data, length, actions)))
{
}

Parser::~Parser() {
  delete static_cast<ParserImpl*>(impl);
}

void Parser::parse() {
  // Initialize the actions.
  static_cast<ParserImpl*>(impl)->getActions().initialize(this);

  static_cast<ParserImpl*>(impl)->parse();
}

const Lexer& Parser::getLexer() const {
  return static_cast<ParserImpl*>(impl)->getLexer();
}
