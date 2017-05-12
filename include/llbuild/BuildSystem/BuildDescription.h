//===- BuildDescription.h ---------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_BUILDSYSTEM_BUILDDESCRIPTION_H
#define LLBUILD_BUILDSYSTEM_BUILDDESCRIPTION_H

#include "llbuild/Basic/Compiler.h"
#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llbuild {

namespace core {

class Task;

}

namespace buildsystem {

/// The type used to pass parsed properties to the delegate.
typedef std::vector<std::pair<std::string, std::string>> property_list_type;

class BuildSystem;
class BuildSystemCommandInterface;
class BuildKey;
class BuildValue;
class Command;
class Node;
struct QueueJobContext;

/// Context for information that may be needed for a configuration action.
//
// FIXME: This is currently commingled with the build file loading, even though
// it should ideally be possible to create a build description decoupled
// entirely from the build file representation.
struct ConfigureContext;

/// Abstract tool definition used by the build file.
class Tool {
  // DO NOT COPY
  Tool(const Tool&) LLBUILD_DELETED_FUNCTION;
  void operator=(const Tool&) LLBUILD_DELETED_FUNCTION;
  Tool &operator=(Tool&& rhs) LLBUILD_DELETED_FUNCTION;
    
  std::string name;

public:
  explicit Tool(StringRef name) : name(name) {}
  virtual ~Tool();

  StringRef getName() const { return name; }

  /// Called by the build file loader to configure a specified tool property.
  virtual bool configureAttribute(const ConfigureContext&, StringRef name,
                                  StringRef value) = 0;
  /// Called by the build file loader to configure a specified tool property.
  virtual bool configureAttribute(const ConfigureContext&, StringRef name,
                                  ArrayRef<StringRef> values) = 0;
  /// Called by the build file loader to configure a specified node property.
  virtual bool configureAttribute(
      const ConfigureContext&, StringRef name,
      ArrayRef<std::pair<StringRef, StringRef>> values) = 0;

  /// Called by the build file loader to create a command which uses this tool.
  ///
  /// \param name - The name of the command.
  virtual std::unique_ptr<Command> createCommand(StringRef name) = 0;

  /// Called by the build system to create a custom command with the given name.
  ///
  /// The tool should return null if it does not understand how to create the
  /// a custom command for the given key.
  ///
  /// \param key - The custom build key to create a command for.
  /// \returns The command to use, or null.
  virtual std::unique_ptr<Command> createCustomCommand(const BuildKey& key);
};

/// Each Target declares a name that can be used to reference it, and a list of
/// the top-level nodes which must be built to bring that target up to date.
class Target {
  /// The name of the target.
  std::string name;

  /// The list of nodes that should be computed to build this target.
  std::vector<Node*> nodes;

public:
  explicit Target(std::string name) : name(name) { }

  StringRef getName() const { return name; }

  std::vector<Node*>& getNodes() { return nodes; }
  const std::vector<Node*>& getNodes() const { return nodes; }
};

/// Abstract definition for a Node used by the build file.
class Node {
  // DO NOT COPY
  Node(const Node&) LLBUILD_DELETED_FUNCTION;
  void operator=(const Node&) LLBUILD_DELETED_FUNCTION;
  Node &operator=(Node&& rhs) LLBUILD_DELETED_FUNCTION;
    
  /// The name used to identify the node.
  std::string name;

  /// The list of commands which can produce this node.
  //
  // FIXME: Optimize for single entry list.
  std::vector<Command*> producers;
  
public:
  explicit Node(StringRef name) : name(name) {}
  virtual ~Node();

  StringRef getName() const { return name; }
  
  std::vector<Command*>& getProducers() { return producers; }

  const std::vector<Command*>& getProducers() const { return producers; }
  
  /// Called by the build file loader to configure a specified node property.
  virtual bool configureAttribute(const ConfigureContext&, StringRef name,
                                  StringRef value) = 0;
  /// Called by the build file loader to configure a specified node property.
  virtual bool configureAttribute(const ConfigureContext&, StringRef name,
                                  ArrayRef<StringRef> values) = 0;
  /// Called by the build file loader to configure a specified node property.
  virtual bool configureAttribute(
      const ConfigureContext&, StringRef name,
      ArrayRef<std::pair<StringRef, StringRef>> values) = 0;
};

/// Abstract command definition used by the build file.
class Command {
  // DO NOT COPY
  Command(const Command&) LLBUILD_DELETED_FUNCTION;
  void operator=(const Command&) LLBUILD_DELETED_FUNCTION;
  Command &operator=(Command&& rhs) LLBUILD_DELETED_FUNCTION;
    
  std::string name;

public:
  explicit Command(StringRef name) : name(name) {}
  virtual ~Command();

  StringRef getName() const { return name; }

  /// @name Command Information
  /// @{
  //
  // FIXME: These probably don't belong here, clients generally can just manage
  // the information from their commands directly and our predefined interfaces
  // won't necessarily match what they want. However, we use them now to allow
  // extracting generic status information from the builtin commands. An
  // alternate solution would be to simply expose those command classes directly
  // and provide some kind of dynamic dispatching mechanism (llvm::cast<>, for
  // example) over commands.

  /// Controls whether the default status reporting shows status for the
  /// command.
  virtual bool shouldShowStatus() { return true; }
  
  /// Get a short description of the command, for use in status reporting.
  virtual void getShortDescription(SmallVectorImpl<char> &result) = 0;

  /// Get a verbose description of the command, for use in status reporting.
  virtual void getVerboseDescription(SmallVectorImpl<char> &result) = 0;
  
  /// @}

  /// @name File Loading
  /// @{

  /// Called by the build file loader to set the description.
  virtual void configureDescription(const ConfigureContext&,
                                    StringRef description) = 0;

  /// Called by the build file loader to pass the list of input nodes.
  virtual void configureInputs(const ConfigureContext&,
                               const std::vector<Node*>& inputs) = 0;

  /// Called by the build file loader to pass the list of output nodes.
  virtual void configureOutputs(const ConfigureContext&,
                                const std::vector<Node*>& outputs) = 0;
                               
  /// Called by the build file loader to configure a specified command property.
  virtual bool configureAttribute(const ConfigureContext&, StringRef name,
                                  StringRef value) = 0;
  /// Called by the build file loader to configure a specified command property.
  virtual bool configureAttribute(const ConfigureContext&, StringRef name,
                                  ArrayRef<StringRef> values) = 0;
  /// Called by the build file loader to configure a specified command property.
  virtual bool configureAttribute(
      const ConfigureContext&, StringRef name,
      ArrayRef<std::pair<StringRef, StringRef>> values) = 0;

  /// @}

  /// @name Node Interfaces
  ///
  /// @description These are the interfaces which allow the build system to
  /// coordinate between the abstract command and node objects.
  //
  // FIXME: This feels awkward, maybe this isn't the right way to manage
  // this. However, we want the system to be able to provide the plumbing
  // between pluggable comands and nodes, so it feels like it has to live
  // somewhere.
  //
  /// @{
  
  /// Get the appropriate output for a particular node (known to be produced by
  /// this command) given the command's result.
  virtual BuildValue getResultForOutput(Node* node,
                                        const BuildValue& value) = 0;

  /// @}
  
  /// @name Command Execution
  ///
  /// @description These APIs directly mirror the APIs available in the
  /// lower-level BuildEngine, but with additional services provided by the
  /// BuildSystem. See the BuildEngine documentation for more information.
  ///
  /// @{

  virtual bool isResultValid(BuildSystem& system, const BuildValue& value) = 0;
  
  virtual void start(BuildSystemCommandInterface&, core::Task*) = 0;

  virtual void providePriorValue(BuildSystemCommandInterface&, core::Task*,
                                 const BuildValue& value) = 0;

  virtual void provideValue(BuildSystemCommandInterface&, core::Task*,
                            uintptr_t inputID, const BuildValue& value) = 0;

  /// Execute the command, and return the value.
  ///
  /// This method will always be executed on the build execution queue.
  virtual BuildValue execute(BuildSystemCommandInterface&, core::Task*,
                             QueueJobContext* context) = 0;
  
  /// @}
};


/// A complete description of a build.
class BuildDescription {
public:
  // FIXME: This is an inefficent map, the string is duplicated.
  typedef llvm::StringMap<std::unique_ptr<Node>> node_set;
  
  // FIXME: This is an inefficent map, the string is duplicated.
  typedef llvm::StringMap<std::unique_ptr<Target>> target_set;

  // FIXME: This is an inefficent map, the string is duplicated.
  typedef llvm::StringMap<std::unique_ptr<Command>> command_set;
  
  // FIXME: This is an inefficent map, the string is duplicated.
  typedef llvm::StringMap<std::unique_ptr<Tool>> tool_set;

private:
  node_set nodes;

  target_set targets;

  command_set commands;
  
  tool_set tools;

  /// The default target.
  std::string defaultTarget;

public:
  /// @name Accessors
  /// @{

  /// Get the set of declared nodes for the file.
  node_set& getNodes() { return nodes; }

  /// Get the set of declared nodes for the file.
  const node_set& getNodes() const { return nodes; }

  /// Get the set of declared targets for the file.
  target_set& getTargets() { return targets; }

  /// Get the set of declared targets for the file.
  const target_set& getTargets() const { return targets; }

  /// Get the default target.
  std::string& getDefaultTarget() { return defaultTarget; }

  /// Get the default target.
  const std::string& getDefaultTarget() const { return defaultTarget; }

  /// Get the set of declared commands for the file.
  command_set& getCommands() { return commands; }

  /// Get the set of declared commands for the file.
  const command_set& getCommands() const { return commands; }

  /// Get the set of all tools used by the file.
  tool_set& getTools() { return tools; }

  /// Get the set of all tools used by the file.
  const tool_set& getTools() const { return tools; }

  /// @}
  /// @name Construction Helpers.
  /// @{

  Node& addNode(std::unique_ptr<Node> value) {
    auto& result = *value.get();
    getNodes()[value->getName()] = std::move(value);
    return result;
  }

  Target& addTarget(std::unique_ptr<Target> value) {
    auto& result = *value.get();
    getTargets()[value->getName()] = std::move(value);
    return result;
  }

  Command& addCommand(std::unique_ptr<Command> value) {
    auto& result = *value.get();
    getCommands()[value->getName()] = std::move(value);
    return result;
  }

  Tool& addTool(std::unique_ptr<Tool> value) {
    auto& result = *value.get();
    getTools()[value->getName()] = std::move(value);
    return result;
  }
  
  /// @}
};

}
}

#endif
