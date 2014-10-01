//===- Manifest.h -----------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_NINJA_MANIFEST_H
#define LLBUILD_NINJA_MANIFEST_H

#include <string>
#include <unordered_map>

namespace llbuild {
namespace ninja {

/// This class represents a set of name to value variable bindings.
class BindingSet {
  /// The parent binding scope, if any.
  const BindingSet *ParentScope = 0;

  /// The actual bindings, mapping from Name to Value.
  std::unordered_map<std::string, std::string> Entries;

public:
  BindingSet(const BindingSet* ParentScope = 0) : ParentScope(ParentScope) {}

  /// Get the parent scope.
  const BindingSet* getParentScope() const {
    return ParentScope;
  }

  /// Get the map of bindings.
  const std::unordered_map<std::string, std::string>& getEntries() const {
    return Entries;
  }

  /// Insert a binding into the set.
  void insert(const std::string& Name, const std::string& Value) {
    Entries[Name] = Value;
  }

  /// Look up the given variable name in the binding set, returning its value or
  /// the empty string if not found.
  std::string lookup(const std::string& Name) const {
    auto it = Entries.find(Name);
    if (it != Entries.end())
      return it->second;

    if (ParentScope)
      return ParentScope->lookup(Name);

    return "";
  }
};

/// A rule represents a template which can be expanded to produce a particular
/// command.
class Rule {
  /// The name of the rule.
  std::string Name;

  /// The rule parameters, which are all unexpanded string exprs.
  //
  // FIXME: It would be nice to optimize this more, and the common case is that
  // we have a fixed set of values which are never dynamically expanded for most
  // parameters *other* than the command.
  std::unordered_map<std::string, std::string> Parameters;

public:
  explicit Rule(const std::string& Name) : Name(Name) {}

  const std::string& getName() const { return Name; }

  std::unordered_map<std::string, std::string>& getParameters() {
    return Parameters;
  }
  const std::unordered_map<std::string, std::string>& getParameters() const {
    return Parameters;
  }

  /// Check whether the given string is a valid rule parameter.
  static bool isValidParameterName(const std::string& Name);
};


/// A manifest represents the complete set of rules and commands used to perform
/// a build.
class Manifest {
  /// The top level variable bindings.
  BindingSet Bindings;

  /// The rules in the manifest, stored as a map on the rule name.
  //
  // FIXME: This is an inefficent map, given that the string is contained
  // inside the rule.
  typedef std::unordered_map<std::string, std::unique_ptr<Rule>> rule_set;
  rule_set Rules;

public:
  /// Get the final set of top level variable bindings.
  BindingSet& getBindings() { return Bindings; }
  /// Get the final set of top level variable bindings.
  const BindingSet& getBindings() const { return Bindings; }

  rule_set& getRules() {
    return Rules;
  }
  const rule_set& getRules() const {
    return Rules;
  }
};

}
}

#endif
