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

#include "llvm/ADT/ArrayRef.h"

#include "llbuild/Basic/LLVM.h"
#include "llbuild/Ninja/Lexer.h"

#include <string>

namespace llbuild {
namespace ninja {

class Parser;
struct Token;
  
/// Delegate interface for parser behavior.
class ParseActions {
public:
  typedef void* BuildResult;
  typedef void* PoolResult;
  typedef void* RuleResult;

  virtual ~ParseActions();

  virtual void error(std::string message, const Token& at) = 0;

  /// Called at the beginning of parsing, to register the parser.
  virtual void initialize(Parser* parser) = 0;

  /// Called at the beginning of parsing a particular manifest.
  virtual void actOnBeginManifest(std::string name) = 0;
  /// Called at the end of parsing the current manifest.
  virtual void actOnEndManifest() = 0;

  /// Called on a variable binding declaration.
  ///
  /// \param name The identifier token for the variable name.
  ///
  /// \param value The identifier token for the variable value.
  virtual void actOnBindingDecl(const Token& name, const Token& value) = 0;

  /// Called on a default declaration.
  ///
  /// \param names The identifier tokens for each of the names.
  virtual void actOnDefaultDecl(ArrayRef<Token> names) = 0;

  /// Called on an include or subninja declaration.
  ///
  /// \param isInclude Whether this is an include (as opposed to a subninja)
  /// declaration.
  ///
  /// \param path The identifier token for the path of the file to include.
  virtual void actOnIncludeDecl(bool isInclude, const Token& path) = 0;

  /// @name Parameterized Decls
  /// @{

  /// Called on a "build" declaration.
  ///
  /// \param outputs The identifier tokens for the outputs of this decl.
  ///
  /// \param inputs The identifier tokens for all of the outputs of this decl.
  ///
  /// \param numExplicitInputs The number of explicit inputs, listed at the
  /// beginning of the \see Inputs array.
  ///
  /// \param numImplicitInputs The number of implicit inputs, listed immediately
  /// following the explicit inputs in the \see Inputs array. All of the
  /// remaining inputs past the implicit inputs are "order-only" inputs.
  ///
  /// \returns A result object to represent this decl, which will be passed
  /// later to \see actOnEndBuildDecl().
  virtual BuildResult actOnBeginBuildDecl(const Token& name,
                                          ArrayRef<Token> outputs,
                                          ArrayRef<Token> inputs,
                                          unsigned numExplicitInputs,
                                          unsigned numImplicitInputs) = 0;

  /// Called on a variable binding within a "build" declaration.
  ///
  /// \param decl The declaration the binding is present within.
  ///
  /// \param name The identifier token for the variable name.
  ///
  /// \param value The identifier token for the variable value.
  virtual void actOnBuildBindingDecl(BuildResult decl, const Token& name,
                                     const Token& value) = 0;

  /// Called at the end of a "build" decl.
  ///
  /// \param decl The object returned to the parser from the opening \see
  /// actOnBeginBuildDecl().
  ///
  /// \param start The token from the decl start, for use in diagnostics.
  virtual void actOnEndBuildDecl(BuildResult decl, const Token& start) = 0;

  /// Called on a "pool" declaration.
  ///
  /// \param name The name of the pool being defined.
  ///
  /// \returns A result object to represent this decl, which will be passed
  /// later to \see actOnEndPoolDecl().
  virtual PoolResult actOnBeginPoolDecl(const Token& name) = 0;

  /// Called on a variable binding within a "pool" declaration.
  ///
  /// \param decl The declaration the binding is present within.
  ///
  /// \param name The identifier token for the variable name.
  ///
  /// \param value The identifier token for the variable value.
  virtual void actOnPoolBindingDecl(PoolResult decl, const Token& name,
                                    const Token& value) = 0;

  /// Called at the end of a "pool" decl.
  ///
  /// \param decl The object returned to the parser from the opening \see
  /// actOnBeginPoolDecl().
  ///
  /// \param start The token from the decl start, for use in diagnostics.
  virtual void actOnEndPoolDecl(PoolResult decl, const Token& start) = 0;

  /// Called on a "rule" declaration.
  ///
  /// \param name The name of the rule being defined.
  ///
  /// \returns A result object to represent this decl, which will be passed
  /// later to \see actOnEndRuleDecl().
  virtual RuleResult actOnBeginRuleDecl(const Token& name) = 0;

  /// Called on a variable binding within a "rule" declaration.
  ///
  /// \param decl The declaration the binding is present within.
  ///
  /// \param name The identifier token for the variable name.
  ///
  /// \param value The identifier token for the variable value.
  virtual void actOnRuleBindingDecl(RuleResult decl, const Token& name,
                                    const Token& value) = 0;

  /// Called at the end of a "rule" decl.
  ///
  /// \param decl The object returned to the parser from the opening \see
  /// actOnBeginRuleDecl().
  ///
  /// \param start The token from the decl start, for use in diagnostics.
  virtual void actOnEndRuleDecl(RuleResult decl, const Token& start) = 0;

  /// @}
};

/// Interface for parsing a Ninja build manifest.
class Parser {
  void *impl;

public:
  Parser(const char* data, uint64_t length, ParseActions& actions);
  ~Parser();

  void parse();

  /// Get the current lexer object.
  const Lexer& getLexer() const;
};

}
}

#endif
