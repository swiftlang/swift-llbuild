//===- BuildFile.h ----------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BUILDSYSTEM_BUILDFILE_H
#define LLBUILD_BUILDSYSTEM_BUILDFILE_H

#include "llbuild/Basic/Compiler.h"

#include "llvm/ADT/StringRef.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace llbuild {
namespace core {

class Task;

}

namespace buildsystem {

/// The type used to pass parsed properties to the delegate.
typedef std::vector<std::pair<std::string, std::string>> property_list_type;

class BuildValue;
class Command;
class BuildSystemCommandInterface;

/// Abstract tool definition used by the build file.
class Tool {
  // DO NOT COPY
  Tool(const Tool&) LLBUILD_DELETED_FUNCTION;
  void operator=(const Tool&) LLBUILD_DELETED_FUNCTION;
  Tool &operator=(Tool&& rhs) LLBUILD_DELETED_FUNCTION;
    
  std::string name;

public:
  explicit Tool(const std::string& name) : name(name) {}
  virtual ~Tool();

  const std::string& getName() const { return name; }

  /// Called by the build file loader to configure a specified tool property.
  virtual bool configureAttribute(const std::string& name,
                                  const std::string& value) = 0;

  /// Called by the build file loader to create a command which uses this tool.
  ///
  /// \param name - The name of the command.
  virtual std::unique_ptr<Command> createCommand(const std::string& name) = 0;
};

/// Each Target declares a name that can be used to reference it, and a list of
/// the top-level nodes which must be built to bring that target up to date.
class Target {
  /// The name of the target.
  std::string name;

  /// The list of node names that should be computed to build this target.
  std::vector<std::string> nodeNames;

public:
  explicit Target(std::string name) : name(name) { }

  const std::string& getName() const { return name; }

  std::vector<std::string>& getNodeNames() { return nodeNames; }
  const std::vector<std::string>& getNodeNames() const { return nodeNames; }
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
  explicit Node(const std::string& name) : name(name) {}
  virtual ~Node();

  const std::string& getName() const { return name; }
  
  std::vector<Command*>& getProducers() { return producers; }

  const std::vector<Command*>& getProducers() const { return producers; }
  
  /// Called by the build file loader to configure a specified node property.
  virtual bool configureAttribute(const std::string& name,
                                  const std::string& value) = 0;
};

/// Abstract command definition used by the build file.
class Command {
  // DO NOT COPY
  Command(const Command&) LLBUILD_DELETED_FUNCTION;
  void operator=(const Command&) LLBUILD_DELETED_FUNCTION;
  Command &operator=(Command&& rhs) LLBUILD_DELETED_FUNCTION;
    
  std::string name;

public:
  explicit Command(const std::string& name) : name(name) {}
  virtual ~Command();

  const std::string& getName() const { return name; }

  /// @name File Loading
  /// @{
  
  /// Called by the build file loader to pass the list of input nodes.
  virtual void configureInputs(const std::vector<Node*>& inputs) = 0;

  /// Called by the build file loader to pass the list of output nodes.
  virtual void configureOutputs(const std::vector<Node*>& outputs) = 0;
                               
  /// Called by the build file loader to configure a specified command property.
  virtual bool configureAttribute(const std::string& name,
                                  const std::string& value) = 0;

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

  virtual bool isResultValid(const BuildValue& value) = 0;
  
  virtual void start(BuildSystemCommandInterface&, core::Task*) = 0;

  virtual void providePriorValue(BuildSystemCommandInterface&, core::Task*,
                                 const BuildValue& value) = 0;

  virtual void provideValue(BuildSystemCommandInterface&, core::Task*,
                            uintptr_t inputID, const BuildValue& value) = 0;

  virtual void inputsAvailable(BuildSystemCommandInterface&, core::Task*) = 0;
  
  /// @}
};

class BuildFileDelegate {
public:
  /// Minimal token object representing the range where a diagnostic occurred.
  struct Token {
    const char* start;
    unsigned length;
  };

  virtual ~BuildFileDelegate();

  /// Called by the build file loader to register the current file contents.
  virtual void setFileContentsBeingParsed(llvm::StringRef buffer) = 0;

  /// Called by the build file loader to report an error.
  ///
  /// \param filename The file the error occurred in.
  ///
  /// \param at The token at which the error occurred. The token will be null if
  /// no location is associated.
  ///
  /// \param message The diagnostic message.
  virtual void error(const std::string& filename,
                     const Token& at,
                     const std::string& message) = 0;
  
  /// Called by the build file loader after the 'client' file section has been
  /// loaded.
  ///
  /// \param name The expected client name.
  /// \param version The client version specified in the file.
  /// \param properties The list of additional properties passed to the client.
  ///
  /// \returns True on success.
  virtual bool configureClient(const std::string& name,
                               uint32_t version,
                               const property_list_type& properties) = 0;

  /// Called by the build file loader to get a tool definition.
  ///
  /// \param name The name of the tool to lookup.
  /// \returns The tool to use on success, or otherwise nil.
  virtual std::unique_ptr<Tool> lookupTool(const std::string& name) = 0;

  /// Called by the build file loader to inform the client that a target
  /// definition has been loaded.
  virtual void loadedTarget(const std::string& name, const Target& target) = 0;

  /// Called by the build file loader to inform the client that a command
  /// has been fully loaded.
  virtual void loadedCommand(const std::string& name, const Command& command) = 0;

  /// Called by the build file loader to get a node.
  ///
  /// \param name The name of the node to lookup.
  ///
  /// \param isImplicit Whether the node is an implicit one (created as a side
  /// effect of being declared by a command).
  virtual std::unique_ptr<Node> lookupNode(const std::string& name,
                                           bool isImplicit=false) = 0;
};

/// The BuildFile class supports the "llbuild"-native build description file
/// format.
class BuildFile {
public:
  // FIXME: This is an inefficent map, the string is duplicated.
  typedef std::unordered_map<std::string, std::unique_ptr<Node>> node_set;
  
  // FIXME: This is an inefficent map, the string is duplicated.
  typedef std::unordered_map<std::string, std::unique_ptr<Target>> target_set;

  // FIXME: This is an inefficent map, the string is duplicated.
  typedef std::unordered_map<std::string, std::unique_ptr<Command>> command_set;
  
  // FIXME: This is an inefficent map, the string is duplicated.
  typedef std::unordered_map<std::string, std::unique_ptr<Tool>> tool_set;

private:
  void *impl;

public:
  /// Create a build file with the given delegate.
  ///
  /// \arg mainFilename The path of the main build file.
  explicit BuildFile(const std::string& mainFilename,
                     BuildFileDelegate& delegate);
  ~BuildFile();

  /// Return the delegate the engine was configured with.
  BuildFileDelegate* getDelegate();

  /// @name Parse Actions
  /// @{

  /// Load the build file from the provided filename.
  ///
  /// This method should only be called once on the BuildFile, and it should be
  /// called before any other operations.
  ///
  /// \returns True on success.
  bool load();

  /// @}
  /// @name Accessors
  /// @{

  /// Get the set of declared nodes for the file.
  const node_set& getNodes() const;

  /// Get the set of declared targets for the file.
  const target_set& getTargets() const;

  /// Get the set of declared commands for the file.
  const command_set& getCommands() const;

  /// Get the set of all tools used by the file.
  const tool_set& getTools() const;

  /// @}
};

}
}

#endif
