//===- MakefileDepsParser.h -------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_CORE_MAKEFILEDEPSPARSER_H
#define LLBUILD_CORE_MAKEFILEDEPSPARSER_H

#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/StringRef.h"

#include <cstdint>

namespace llbuild {
namespace core {

/// Interface for parsing compiler-style dependencies output, which is a
/// restricted subset of the Makefile syntax.
class MakefileDepsParser {
public:
  /// Delegate interface for parser behavior.
  struct ParseActions {
    virtual ~ParseActions();

    /// Called if an error is encountered in parsing the input.
    ///
    /// \param message A C-string text message including information on the
    /// error.
    ///
    /// \param position The approximate position of the error in the input
    /// buffer.
    virtual void error(const char* message, uint64_t position) = 0;

    /// Called when a new rule is encountered.
    ///
    /// \param name - A pointer to the rule name string start.
    ///
    /// \param length - The raw length of the rule name string, including escape
    /// sequences.
    ///
    /// \param unescapedWord - An unescaped version of the name.
    virtual void actOnRuleStart(const char* name, uint64_t length,
                                const StringRef unescapedWord) = 0;

    /// Called when a new dependency is found for the current rule.
    ///
    /// This is only called between paired calls to \see actOnRuleStart() and
    /// \see actOnRuleEnd().
    ///
    /// \param dependency - A pointer to the dependency string start.
    ///
    /// \param length - The raw length of the dependency string, including
    /// escape sequences.
    ///
    /// \param unescapedWord - An unescaped version of the dependency.
    virtual void actOnRuleDependency(const char* dependency, uint64_t length,
                                     const StringRef unescapedWord) = 0;

    /// Called when a rule is complete.
    virtual void actOnRuleEnd() = 0;
  };

  const char* data;
  uint64_t length;
  ParseActions& actions;
  
public:
  MakefileDepsParser(const char* data, uint64_t length, ParseActions& actions)
    : data(data), length(length), actions(actions) {}

  void parse();
};

}
}

#endif
