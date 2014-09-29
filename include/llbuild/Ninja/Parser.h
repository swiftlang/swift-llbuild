//===- Parser.h -------------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_NINJA_PARSER_H
#define LLBUILD_NINJA_PARSER_H

#include "llbuild/Ninja/Lexer.h"

#include <string>
#include <vector>

namespace llbuild {
namespace ninja {

class Token;
class Parser;
  
/// Delegate interface for parser behavior.
class ParseActions {
public:
  typedef void* BuildResult;
  typedef void* PoolResult;
  typedef void* RuleResult;

  virtual ~ParseActions();

  virtual void error(std::string Message, const Token& At) = 0;

  /// Called at the beginning of parsing, to register the parser.
  virtual void initialize(Parser* Parser) = 0;

  /// Called at the beginning of parsing a particular manifest.
  virtual void actOnBeginManifest(std::string Name) = 0;
  /// Called at the end of parsing the current manifest.
  virtual void actOnEndManifest() = 0;

  /// Called on a variable binding declaration.
  ///
  /// \param Name The identifier token for the variable name.
  ///
  /// \param Value The identifier token for the variable value.
  virtual void actOnBindingDecl(const Token& Name, const Token& Value) = 0;

  /// Called on a default declaration.
  ///
  /// \param Names The identifier tokens for each of the names.
  virtual void actOnDefaultDecl(const std::vector<Token>& Names) = 0;

  /// Called on an include or subninja declaration.
  ///
  /// \param IsInclude Whether this is an include (as opposed to a subninja)
  /// declaration.
  ///
  /// \param Path The identifier token for the path of the file to include.
  virtual void actOnIncludeDecl(bool IsInclude, const Token& Path) = 0;

  /// @name Parameterized Decls
  /// @{

  /// Called on a "build" declaration.
  ///
  /// \param Outputs The identifier tokens for the outputs of this decl.
  ///
  /// \param Inputs The identifier tokens for all of the outputs of this decl.
  ///
  /// \param NumExplicitInputs The number of explicit inputs, listed at the
  /// beginning of the \see Inputs array.
  ///
  /// \param NumImplicitInputs The number of implicit inputs, listed immediately
  /// following the explicit inputs in the \see Inputs array. All of the
  /// remaining inputs past the implicit inputs are "order-only" inputs.
  ///
  /// \returns A result object to represent this decl, which will be passed
  /// later to \see actOnEndBuildDecl().
  virtual BuildResult actOnBeginBuildDecl(const Token& Name,
                                          const std::vector<Token> &Outputs,
                                          const std::vector<Token> &Inputs,
                                          unsigned NumExplicitInputs,
                                          unsigned NumImplicitInputs) = 0;

  /// Called on a variable binding within a "build" declaration.
  ///
  /// \param Decl The declaration the binding is present within.
  ///
  /// \param Name The identifier token for the variable name.
  ///
  /// \param Value The identifier token for the variable value.
  virtual void actOnBuildBindingDecl(BuildResult Decl, const Token& Name,
                                     const Token& Value) = 0;

  /// Called at the end of a "build" decl.
  ///
  /// \param Decl The object returned to the parser from the opening \see
  /// actOnBeginBuildDecl().
  virtual void actOnEndBuildDecl(BuildResult Decl) = 0;

  /// Called on a "pool" declaration.
  ///
  /// \param Name The name of the pool being defined.
  ///
  /// \returns A result object to represent this decl, which will be passed
  /// later to \see actOnEndPoolDecl().
  virtual PoolResult actOnBeginPoolDecl(const Token& Name) = 0;

  /// Called on a variable binding within a "pool" declaration.
  ///
  /// \param Decl The declaration the binding is present within.
  ///
  /// \param Name The identifier token for the variable name.
  ///
  /// \param Value The identifier token for the variable value.
  virtual void actOnPoolBindingDecl(PoolResult Decl, const Token& Name,
                                    const Token& Value) = 0;

  /// Called at the end of a "pool" decl.
  ///
  /// \param Decl The object returned to the parser from the opening \see
  /// actOnBeginPoolDecl().
  virtual void actOnEndPoolDecl(PoolResult Decl) = 0;

  /// Called on a "rule" declaration.
  ///
  /// \param Name The name of the rule being defined.
  ///
  /// \returns A result object to represent this decl, which will be passed
  /// later to \see actOnEndRuleDecl().
  virtual RuleResult actOnBeginRuleDecl(const Token& Name) = 0;

  /// Called on a variable binding within a "rule" declaration.
  ///
  /// \param Decl The declaration the binding is present within.
  ///
  /// \param Name The identifier token for the variable name.
  ///
  /// \param Value The identifier token for the variable value.
  virtual void actOnRuleBindingDecl(RuleResult Decl, const Token& Name,
                                    const Token& Value) = 0;

  /// Called at the end of a "rule" decl.
  ///
  /// \param Decl The object returned to the parser from the opening \see
  /// actOnBeginRuleDecl().
  virtual void actOnEndRuleDecl(RuleResult Decl) = 0;

  /// @}
};

/// Interface for parsing a Ninja build manifest.
class Parser {
  void *Impl;

public:
  Parser(const char* Data, uint64_t Length, ParseActions& Actions);
  ~Parser();

  void parse();

  /// Get the current lexer object.
  const Lexer& getLexer() const;
};

}
}

#endif
