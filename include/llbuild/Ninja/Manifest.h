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

#include <cassert>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace llbuild {
namespace ninja {

class Pool;
class Rule;

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

/// A node represents a unique path as present in the manifest.
//
// FIXME: Figure out what the deal is with normalization.
class Node {
  std::string Path;

public:
  explicit Node(const std::string& Path) : Path(Path) {}

  const std::string& getPath() const { return Path; }
};

/// A pool represents a generic bucket for organizing commands.
class Pool {
  /// The name of the pool.
  std::string Name;

  /// The pool depth, or 0 if unspecified.
  uint32_t Depth = 0;

public:
  explicit Pool(const std::string& Name) : Name(Name) {}

  const std::string& getName() const { return Name; }

  uint32_t getDepth() const {
    return Depth;
  }
  void setDepth(uint32_t Value) {
    Depth = Value;
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
  Rule *Rule;

  /// The outputs of the command.
  std::vector<Node*> Outputs;

  /// The list of all inputs to the command, include explicit as well as
  /// implicit and order-only inputs (which are determined by their position in
  /// the array and the \see NumExplicitInputs and \see NumImplicitInputs
  /// variables).
  std::vector<Node*> Inputs;

  /// The number of explicit inputs, at the start of the \see Inputs array.
  unsigned NumExplicitInputs;

  /// The number of implicit inputs, immediately following the explicit inputs
  /// in the \see Inputs array. The remaining inputs are the order-only ones.
  unsigned NumImplicitInputs;

  /// The command parameters, which are used to evaluate the rule template.
  //
  // FIXME: It might be substantially better to evaluate all of these in the
  // context of the rule up-front (during loading).
  std::unordered_map<std::string, std::string> Parameters;

  Pool *ExecutionPool;

  std::string CommandString;
  std::string Description;
  std::string DepsFile;

  unsigned DepsStyle: 2;
  unsigned IsGenerator: 1;
  unsigned ShouldRestat: 1;

public:
  explicit Command(class Rule *Rule, std::vector<Node*> Outputs,
                   std::vector<Node*> Inputs, unsigned NumExplicitInputs,
                   unsigned NumImplicitInputs)
    : Rule(Rule), Outputs(Outputs), Inputs(Inputs),
      NumExplicitInputs(NumExplicitInputs),
      NumImplicitInputs(NumImplicitInputs),
      ExecutionPool(nullptr), DepsStyle(unsigned(DepsStyleKind::None)),
      IsGenerator(0), ShouldRestat(0)
  {
    assert(Outputs.size() > 0);
    assert(NumExplicitInputs + NumImplicitInputs <= Inputs.size());
  }

  const class Rule* getRule() const { return Rule; }

  const std::vector<Node*>& getOutputs() const { return Outputs; }

  const std::vector<Node*>& getInputs() const { return Inputs; }

  const std::vector<Node*>::const_iterator explicitInputs_begin() const {
    return Inputs.begin();
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
    return Inputs.end();
  }

  unsigned getNumExplicitInputs() const { return NumExplicitInputs; }
  unsigned getNumImplicitInputs() const { return NumImplicitInputs; }
  unsigned getNumOrderOnlyInputs() const {
    return Inputs.size() - getNumExplicitInputs() - getNumImplicitInputs();
  }

  std::unordered_map<std::string, std::string>& getParameters() {
    return Parameters;
  }
  const std::unordered_map<std::string, std::string>& getParameters() const {
    return Parameters;
  }

  /// @name Attributes
  /// @{

  /// Get the effective description.
  const std::string& getEffectiveDescription() const {
    return getDescription().empty() ? getCommandString() : getDescription();
  }

  /// Get the shell command to execute to run this command.
  const std::string& getCommandString() const {
    return CommandString;
  }
  void setCommandString(const std::string& Value) {
    CommandString = Value;
  }

  /// Get the description to use when running this command.
  const std::string& getDescription() const {
    return Description;
  }
  void setDescription(const std::string& Value) {
    Description = Value;
  }

  /// Get the style of implicit dependencies used by this command.
  DepsStyleKind getDepsStyle() const {
    return DepsStyleKind(DepsStyle);
  }
  void setDepsStyle(DepsStyleKind Value) {
    DepsStyle = unsigned(Value);
    assert(DepsStyle == unsigned(Value));
  }

  /// Get the dependency output file to use, for some implicit dependencies
  /// styles.
  const std::string& getDepsFile() const {
    return DepsFile;
  }
  void setDepsFile(const std::string& Value) {
    DepsFile = Value;
  }

  /// Check whether this command should be treated as a generator command.
  bool hasGeneratorFlag() const {
    return IsGenerator;
  }
  void setGeneratorFlag(bool Value) {
    IsGenerator = Value;
  }

  /// Check whether this command should restat outputs after execution to
  /// determine if downstream commands still need to run.
  bool hasRestatFlag() const {
    return ShouldRestat;
  }
  void setRestatFlag(bool Value) {
    ShouldRestat = Value;
  }

  /// Get the pool to use when running this command.
  Pool* getExecutionPool() const {
    return ExecutionPool;
  }
  void setExecutionPool(class Pool* Value) {
    ExecutionPool = Value;
  }

  /// @}
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

  /// The nodes in the manifest, stored as a map on the node name.
  //
  // FIXME: This is an inefficent map, given that the string is contained
  // inside the node.
  typedef std::unordered_map<std::string, std::unique_ptr<Node>> node_set;
  node_set Nodes;

  /// The commands in the manifest.
  std::vector<std::unique_ptr<Command>> Commands;

  /// The pools in the manifest, stored as a map on the pool name.
  //
  // FIXME: This is an inefficent map, given that the string is contained
  // inside the pool.
  typedef std::unordered_map<std::string, std::unique_ptr<Pool>> pool_set;
  pool_set Pools;

  /// The rules in the manifest, stored as a map on the rule name.
  //
  // FIXME: This is an inefficent map, given that the string is contained
  // inside the rule.
  typedef std::unordered_map<std::string, std::unique_ptr<Rule>> rule_set;
  rule_set Rules;

  /// The default targets, if specified.
  std::vector<Node*> DefaultTargets;

  /// The built-in console pool.
  Pool* ConsolePool;

  /// The built-in phony rule.
  Rule* PhonyRule;

public:
  explicit Manifest();

  /// Get the final set of top level variable bindings.
  BindingSet& getBindings() { return Bindings; }
  /// Get the final set of top level variable bindings.
  const BindingSet& getBindings() const { return Bindings; }

  node_set& getNodes() {
    return Nodes;
  }
  const node_set& getNodes() const {
    return Nodes;
  }

  /// Get or create the unique node for the given path.
  Node* getOrCreateNode(const std::string& Path);

  std::vector<std::unique_ptr<Command>>& getCommands() {
    return Commands;
  }
  const std::vector<std::unique_ptr<Command>>& getCommands() const {
    return Commands;
  }

  pool_set& getPools() {
    return Pools;
  }
  const pool_set& getPools() const {
    return Pools;
  }

  rule_set& getRules() {
    return Rules;
  }
  const rule_set& getRules() const {
    return Rules;
  }

  std::vector<Node*>& getDefaultTargets() {
    return DefaultTargets;
  }
  const std::vector<Node*>& getDefaultTargets() const {
    return DefaultTargets;
  }

  Pool* getConsolePool() const {
    return ConsolePool;
  }

  Rule* getPhonyRule() const {
    return PhonyRule;
  }
};

}
}

#endif
