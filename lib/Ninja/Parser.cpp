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

  /// Parse a variable binding.
  ///
  /// \param Name_Out [out] On success, the identifier token for the binding
  /// name.
  ///
  /// \param Value_Out [out] On success, the string token for the binding
  /// value.
  ///
  /// \returns True on success. On failure, the lexer will be advanced past the
  /// next newline (see \see skipPastEOL()).
  bool parseBindingInternal(Token* Name_Out, Token* Value_Out);

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
    // Check that no one accidentally left the lexer in the wrong mode.
    assert(Lexer.getStringMode() == Lexer::StringMode::None);

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

/// binding-decl ::= identifier '=' var-string '\n'
bool ParserImpl::parseBindingInternal(Token* Name_Out, Token* Value_Out) {
  // The leading token should be an identifier.
  if (Tok.TokenKind != Token::Kind::Identifier) {
    error("expected variable name");
    skipPastEOL();
    return false;
  }

  *Name_Out = consumeExpectedToken(Token::Kind::Identifier);

  // Expect the variable name to be followed by '='.
  if (Tok.TokenKind != Token::Kind::Equals) {
    error("expected '=' token");
    skipPastEOL();
    return false;
  }

  // Consume the '=' and the RHS.
  //
  // We must consume the RHS in variable lexing mode, so given our lookahead
  // design we switch modes, consume the equals (which will advance the lexer),
  // and then immediately switch back.
  Lexer.setStringMode(Lexer::StringMode::Variable);
  consumeExpectedToken(Token::Kind::Equals);
  Lexer.setStringMode(Lexer::StringMode::None);

  // If the RHS is a newline, we have an empty binding which we handle as a
  // special case.
  if (Tok.TokenKind == Token::Kind::Newline) {
    // Derive an empty string token from the newline.
    *Value_Out = consumeExpectedToken(Token::Kind::Newline);
    Value_Out->TokenKind = Token::Kind::String;
    Value_Out->Length = 0;
    return true;
  }

  // Otherwise, check that the RHS is a string.
  if (Tok.TokenKind != Token::Kind::String) {
    error("expected variable value");
    skipPastEOL();
    return false;
  }

  *Value_Out = consumeExpectedToken(Token::Kind::String);

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
  Token Name, Value;
  if (parseBindingInternal(&Name, &Value))
    Actions.actOnBindingDecl(Name, Value);
}

/// default-decl ::= "default" path-string-list '\n'
void ParserImpl::parseDefaultDecl() {
  // Put the lexer in path string mode.
  Lexer.setStringMode(Lexer::StringMode::Path);
  consumeExpectedToken(Token::Kind::KWDefault);

  // Consume all the strings.
  std::vector<Token> Names;
  while (Tok.TokenKind == Token::Kind::String) {
    Names.push_back(consumeExpectedToken(Token::Kind::String));
  }

  // Reset the string mode.
  Lexer.setStringMode(Lexer::StringMode::None);

  // Verify we have at least one name.
  if (Names.empty()) {
    error("expected target path string");
    return skipPastEOL();
  }

  // The list should be terminated by a newline.
  if (!consumeIfToken(Token::Kind::Newline)) {
    error("expected newline token");
    return skipPastEOL();
  }

  Actions.actOnDefaultDecl(Names);
}

/// include-decl ::= ( "include" | "subninja" ) path-string '\n'
void ParserImpl::parseIncludeDecl() {
  // Put the lexer in path string mode for one token.
  Lexer.setStringMode(Lexer::StringMode::Path);
  bool IsInclude = Tok.TokenKind == Token::Kind::KWInclude;
  consumeExpectedToken(IsInclude ? Token::Kind::KWInclude :
                       Token::Kind::KWSubninja);
  Lexer.setStringMode(Lexer::StringMode::None);

  if (Tok.TokenKind != Token::Kind::String) {
    error("expected path string");
    return skipPastEOL();
  }

  Token Path = consumeExpectedToken(Token::Kind::String);

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
/// indented-binding := indention binding-decl
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
  while (consumeIfToken(Token::Kind::Indentation)) {
    Token Name, Value;
    if (parseBindingInternal(&Name, &Value)) {
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

/// build-spec ::= "build" path-string-list ":" path-string path-string-list
///                [ "|" path-string-list ] [ "||" path-string-list" ] '\n'
bool ParserImpl::parseBuildSpecifier(ParseActions::BuildResult *Decl_Out) {
  // Put the lexer in path string mode for the entire specifier parsing.
  Lexer.setStringMode(Lexer::StringMode::Path);
  consumeExpectedToken(Token::Kind::KWBuild);

  // Parse the output list.
  if (Tok.TokenKind != Token::Kind::String) {
    error("expected output path string");
    Lexer.setStringMode(Lexer::StringMode::None);
    return false;
  }
  std::vector<Token> Outputs;
  do {
    Outputs.push_back(consumeExpectedToken(Token::Kind::String));
  } while (Tok.TokenKind == Token::Kind::String);

  // Expect the string list to be terminated by a colon.
  if (!consumeIfToken(Token::Kind::Colon)) {
    error("expected ':' token");
    Lexer.setStringMode(Lexer::StringMode::None);
    return false;
  }

  // Parse the rule name.
  if (Tok.TokenKind != Token::Kind::String) {
    error("expected rule name string");
    Lexer.setStringMode(Lexer::StringMode::None);
    return false;
  }
  Token Name = consumeExpectedToken(Token::Kind::String);

  // Parse the explicit inputs.
  std::vector<Token> Inputs;
  while (Tok.TokenKind == Token::Kind::String) {
    Inputs.push_back(consumeExpectedToken(Token::Kind::String));
  }
  unsigned NumExplicitInputs = Inputs.size();

  // Parse the implicit inputs, if present.
  if (consumeIfToken(Token::Kind::Pipe)) {
    while (Tok.TokenKind == Token::Kind::String) {
      Inputs.push_back(consumeExpectedToken(Token::Kind::String));
    }
  }
  unsigned NumImplicitInputs = Inputs.size() - NumExplicitInputs;

  // Parse the order-only inputs, if present.
  if (consumeIfToken(Token::Kind::PipePipe)) {
    while (Tok.TokenKind == Token::Kind::String) {
      Inputs.push_back(consumeExpectedToken(Token::Kind::String));
    }
  }

  // Reset the lexer mode.
  Lexer.setStringMode(Lexer::StringMode::None);

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
    error("expected pool name identifier");
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
    error("expected rule name identifier");
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
