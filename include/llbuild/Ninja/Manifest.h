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

#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"

#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace llbuild {
namespace ninja {

class Pool;
class Rule;

/// This class represents a set of name to value variable bindings.
class BindingSet {
  /// The parent binding scope, if any.
  const BindingSet *parentScope = 0;

  /// The actual bindings, mapping from Name to Value.
  llvm::StringMap<std::string> entries;

public:
  BindingSet(const BindingSet* parentScope = 0) : parentScope(parentScope) {}

  /// Get the parent scope.
  const BindingSet* getParentScope() const {
    return parentScope;
  }

  /// Get the map of bindings.
  const llvm::StringMap<std::string>& getEntries() const {
    return entries;
  }

  /// Insert a binding into the set.
  void insert(StringRef name, StringRef value) {
    entries[name] = value;
  }

  /// Look up the given variable name in the binding set, returning its value or
  /// the empty string if not found.
  StringRef lookup(StringRef name) const {
    auto it = entries.find(name);
    if (it != entries.end())
      return it->second;

    if (parentScope)
      return parentScope->lookup(name);

    return "";
  }
};

/// A node represents a unique path as present in the manifest.
//
// FIXME: Figure out what the deal is with normalization.
class Node {
  std::string path;

public:
  explicit Node(StringRef path) : path(path) {}

  const std::string& getPath() const { return path; }
};

/// A pool represents a generic bucket for organizing commands.
class Pool {
  /// The name of the pool.
  std::string name;

  /// The pool depth, or 0 if unspecified.
  uint32_t depth = 0;

public:
  explicit Pool(StringRef name) : name(name) {}

  const std::string& getName() const { return name; }

  uint32_t getDepth() const {
    return depth;
  }
  void setDepth(uint32_t Value) {
    depth = Value;
  }
};

/// A command represents something which can be executed to produce certain
/// outputs from certain inputs.
///
class Command {
public:
  enum class DepsStyleKind {
    /// The command doesn't use implicit dependencies.
    None = 0,

    /// The command should use GCC style implicit dependencies, where the
    /// DepsFile attribute specifies the name of a file in which the tool will
    /// write out implicit dependencies in the Makefile format.
    GCC = 1,

    /// The command should use MSVC style implicit dependencies, in which they
    /// are parsed as part of the output of the compiler.
    MSVC = 2
  };
  
private:
  /// The rule used to derive the command properties.
  Rule* rule;

  /// The outputs of the command.
  std::vector<Node*> outputs;

  /// The list of all inputs to the command, include explicit as well as
  /// implicit and order-only inputs (which are determined by their position in
  /// the array and the \see numExplicitInputs and \see numImplicitInputs
  /// variables).
  std::vector<Node*> inputs;

  /// The number of explicit inputs, at the start of the \see inputs array.
  unsigned numExplicitInputs;

  /// The number of implicit inputs, immediately following the explicit inputs
  /// in the \see inputs array. The remaining inputs are the order-only ones.
  unsigned numImplicitInputs;

  /// The command parameters, which are used to evaluate the rule template.
  //
  // FIXME: It might be substantially better to evaluate all of these in the
  // context of the rule up-front (during loading).
  llvm::StringMap<std::string> parameters;

  Pool* executionPool;

  std::string commandString;
  std::string description;
  std::string depsFile;

  unsigned depsStyle: 2;
  unsigned isGenerator: 1;
  unsigned shouldRestat: 1;

public:
  // FIXME: Use an rvalue reference for the outputs and inputs here to avoid
  // copying, but requires SmallVectorImpl to take a move constructor.
  explicit Command(class Rule* rule,
                   ArrayRef<Node*> outputs,
                   ArrayRef<Node*> inputs,
                   unsigned numExplicitInputs,
                   unsigned numImplicitInputs)
    : rule(rule), outputs(outputs), inputs(inputs),
      numExplicitInputs(numExplicitInputs),
      numImplicitInputs(numImplicitInputs),
      executionPool(nullptr), depsStyle(unsigned(DepsStyleKind::None)),
      isGenerator(0), shouldRestat(0)
  {
    assert(outputs.size() > 0);
    assert(numExplicitInputs + numImplicitInputs <= inputs.size());
  }

  const class Rule* getRule() const { return rule; }

  const std::vector<Node*>& getOutputs() const { return outputs; }

  const std::vector<Node*>& getInputs() const { return inputs; }

  const std::vector<Node*>::const_iterator explicitInputs_begin() const {
    return inputs.begin();
  }
  const std::vector<Node*>::const_iterator explicitInputs_end() const {
    return explicitInputs_begin() + getNumExplicitInputs();
  }

  const std::vector<Node*>::const_iterator implicitInputs_begin() const {
    return explicitInputs_end();
  }
  const std::vector<Node*>::const_iterator implicitInputs_end() const {
    return implicitInputs_begin() + getNumImplicitInputs();
  }

  const std::vector<Node*>::const_iterator orderOnlyInputs_begin() const {
    return implicitInputs_end();
  }
  const std::vector<Node*>::const_iterator orderOnlyInputs_end() const {
    return inputs.end();
  }

  unsigned getNumExplicitInputs() const { return numExplicitInputs; }
  unsigned getNumImplicitInputs() const { return numImplicitInputs; }
  unsigned getNumOrderOnlyInputs() const {
    return inputs.size() - getNumExplicitInputs() - getNumImplicitInputs();
  }

  llvm::StringMap<std::string>& getParameters() {
    return parameters;
  }
  const llvm::StringMap<std::string>& getParameters() const {
    return parameters;
  }

  /// @name Attributes
  /// @{

  /// Get the effective description.
  const std::string& getEffectiveDescription() const {
    return getDescription().empty() ? getCommandString() : getDescription();
  }

  /// Get the shell command to execute to run this command.
  const std::string& getCommandString() const {
    return commandString;
  }
  void setCommandString(StringRef value) {
    commandString = value;
  }

  /// Get the description to use when running this command.
  const std::string& getDescription() const {
    return description;
  }
  void setDescription(StringRef value) {
    description = value;
  }

  /// Get the style of implicit dependencies used by this command.
  DepsStyleKind getDepsStyle() const {
    return DepsStyleKind(depsStyle);
  }
  void setDepsStyle(DepsStyleKind value) {
    depsStyle = unsigned(value);
    assert(depsStyle == unsigned(value));
  }

  /// Get the dependency output file to use, for some implicit dependencies
  /// styles.
  const std::string& getDepsFile() const {
    return depsFile;
  }
  void setDepsFile(StringRef value) {
    depsFile = value;
  }

  /// Check whether this command should be treated as a generator command.
  bool hasGeneratorFlag() const {
    return isGenerator;
  }
  void setGeneratorFlag(bool value) {
    isGenerator = value;
  }

  /// Check whether this command should restat outputs after execution to
  /// determine if downstream commands still need to run.
  bool hasRestatFlag() const {
    return shouldRestat;
  }
  void setRestatFlag(bool value) {
    shouldRestat = value;
  }

  /// Get the pool to use when running this command.
  Pool* getExecutionPool() const {
    return executionPool;
  }
  void setExecutionPool(class Pool* value) {
    executionPool = value;
  }

  /// @}
};

/// A rule represents a template which can be expanded to produce a particular
/// command.
class Rule {
  /// The name of the rule.
  std::string name;

  /// The rule parameters, which are all unexpanded string exprs.
  //
  // FIXME: It would be nice to optimize this more, and the common case is that
  // we have a fixed set of values which are never dynamically expanded for most
  // parameters *other* than the command.
  llvm::StringMap<std::string> parameters;

public:
  explicit Rule(StringRef name) : name(name) {}

  const std::string& getName() const { return name; }

  llvm::StringMap<std::string>& getParameters() {
    return parameters;
  }
  const llvm::StringMap<std::string>& getParameters() const {
    return parameters;
  }

  /// Check whether the given string is a valid rule parameter.
  static bool isValidParameterName(StringRef name);
};

/// A manifest represents the complete set of rules and commands used to perform
/// a build.
class Manifest {
  /// The pool allocator used for manifest objects.
  llvm::BumpPtrAllocator allocator;
  
  /// The top level variable bindings.
  BindingSet bindings;

  /// The nodes in the manifest, stored as a map on the node name.
  //
  // FIXME: This is an inefficent map, given that the string is contained
  // inside the node.
  typedef llvm::StringMap<Node*> node_set;
  node_set nodes;

  /// The commands in the manifest.
  std::vector<Command*> commands;

  /// The pools in the manifest, stored as a map on the pool name.
  //
  // FIXME: This is an inefficent map, given that the string is contained
  // inside the pool.
  typedef llvm::StringMap<Pool*> pool_set;
  pool_set pools;

  /// The rules in the manifest, stored as a map on the rule name.
  //
  // FIXME: This is an inefficent map, given that the string is contained
  // inside the rule.
  typedef llvm::StringMap<Rule*> rule_set;
  rule_set rules;

  /// The default targets, if specified.
  std::vector<Node*> defaultTargets;

  /// The built-in console pool.
  Pool* consolePool;

  /// The built-in phony rule.
  Rule* phonyRule;

public:
  explicit Manifest();

  /// Get the allocator to use for manifest objects.
  llvm::BumpPtrAllocator& getAllocator() { return allocator; }

  /// Get the final set of top level variable bindings.
  BindingSet& getBindings() { return bindings; }
  /// Get the final set of top level variable bindings.
  const BindingSet& getBindings() const { return bindings; }

  node_set& getNodes() {
    return nodes;
  }
  const node_set& getNodes() const {
    return nodes;
  }

  /// Get or create the unique node for the given path.
  Node* getOrCreateNode(StringRef path);

  std::vector<Command*>& getCommands() {
    return commands;
  }
  const std::vector<Command*>& getCommands() const {
    return commands;
  }

  pool_set& getPools() {
    return pools;
  }
  const pool_set& getPools() const {
    return pools;
  }

  rule_set& getRules() {
    return rules;
  }
  const rule_set& getRules() const {
    return rules;
  }

  std::vector<Node*>& getDefaultTargets() {
    return defaultTargets;
  }
  const std::vector<Node*>& getDefaultTargets() const {
    return defaultTargets;
  }

  Pool* getConsolePool() const {
    return consolePool;
  }

  Rule* getPhonyRule() const {
    return phonyRule;
  }
};

}
}

#endif
