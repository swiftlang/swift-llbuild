//===-- BuildSystem.cpp ---------------------------------------------------===//
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

#include "llbuild/BuildSystem/BuildSystem.h"
#include "llbuild/BuildSystem/BuildSystemCommandInterface.h"

#include "llbuild/Basic/FileInfo.h"
#include "llbuild/Basic/FileSystem.h"
#include "llbuild/Basic/Hashing.h"
#include "llbuild/Basic/LLVM.h"
#include "llbuild/Core/BuildDB.h"
#include "llbuild/Core/BuildEngine.h"
#include "llbuild/Core/MakefileDepsParser.h"
#include "llbuild/BuildSystem/BuildExecutionQueue.h"
#include "llbuild/BuildSystem/BuildFile.h"
#include "llbuild/BuildSystem/BuildKey.h"
#include "llbuild/BuildSystem/BuildNode.h"
#include "llbuild/BuildSystem/BuildValue.h"
#include "llbuild/BuildSystem/ExternalCommand.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>

using namespace llbuild;
using namespace llbuild::basic;
using namespace llbuild::core;
using namespace llbuild::buildsystem;

BuildSystemDelegate::~BuildSystemDelegate() {}

BuildSystemCommandInterface::~BuildSystemCommandInterface() {}

#pragma mark - BuildSystem implementation

namespace {

class BuildSystemImpl;

/// The delegate used to load the build file for use by a build system.
class BuildSystemFileDelegate : public BuildFileDelegate {
  BuildSystemImpl& system;

public:
  BuildSystemFileDelegate(BuildSystemImpl& system)
      : BuildFileDelegate(), system(system) {}

  BuildSystemDelegate& getSystemDelegate();

  /// @name Delegate Implementation
  /// @{

  virtual FileSystem& getFileSystem() override {
    return getSystemDelegate().getFileSystem();
  }
  
  virtual void setFileContentsBeingParsed(StringRef buffer) override;
  
  virtual void error(StringRef filename,
                     const BuildFileToken& at,
                     const Twine& message) override;

  virtual bool configureClient(const ConfigureContext&, StringRef name,
                               uint32_t version,
                               const property_list_type& properties) override;

  virtual std::unique_ptr<Tool> lookupTool(StringRef name) override;

  virtual void loadedTarget(StringRef name,
                            const Target& target) override;

  virtual void loadedCommand(StringRef name,
                             const Command& target) override;

  virtual std::unique_ptr<Node> lookupNode(StringRef name,
                                           bool isImplicit=false) override;

  /// @}
};

/// The delegate used to build a loaded build file.
class BuildSystemEngineDelegate : public BuildEngineDelegate {
  BuildSystemImpl& system;

  // FIXME: This is an inefficent map, the string is duplicated.
  std::unordered_map<std::string, std::unique_ptr<BuildNode>> dynamicNodes;

  /// The custom tasks which are owned by the build system.
  std::vector<std::unique_ptr<Command>> customTasks;

  BuildFile& getBuildFile();

  virtual Rule lookupRule(const KeyType& keyData) override;
  virtual void cycleDetected(const std::vector<Rule*>& items) override;

public:
  BuildSystemEngineDelegate(BuildSystemImpl& system) : system(system) {}

  BuildSystemImpl& getBuildSystem() {
    return system;
  }
};

class BuildSystemImpl : public BuildSystemCommandInterface {
  BuildSystem& buildSystem;

  /// The delegate the BuildSystem was configured with.
  BuildSystemDelegate& delegate;

  /// The name of the main input file.
  std::string mainFilename;

  /// The delegate used for the loading the build file.
  BuildSystemFileDelegate fileDelegate;

  /// The build file the system is building.
  BuildFile buildFile;

  /// The delegate used for building the file contents.
  BuildSystemEngineDelegate engineDelegate;

  /// The build engine.
  BuildEngine buildEngine;

  /// The execution queue.
  std::unique_ptr<BuildExecutionQueue> executionQueue;

  /// @name BuildSystemCommandInterface Implementation
  /// @{

  virtual BuildEngine& getBuildEngine() override {
    return buildEngine;
  }
  
  virtual BuildExecutionQueue& getExecutionQueue() override {
    return *executionQueue;
  }

  virtual void taskNeedsInput(core::Task* task, const BuildKey& key,
                              uintptr_t inputID) override {
    return buildEngine.taskNeedsInput(task, key.toData(), inputID);
  }

  virtual void taskMustFollow(core::Task* task, const BuildKey& key) override {
    return buildEngine.taskMustFollow(task, key.toData());
  }

  virtual void taskDiscoveredDependency(core::Task* task,
                                        const BuildKey& key) override {
    return buildEngine.taskDiscoveredDependency(task, key.toData());
  }

  virtual void taskIsComplete(core::Task* task, const BuildValue& value,
                              bool forceChange) override {
    return buildEngine.taskIsComplete(task, value.toData(), forceChange);
  }

  virtual void addJob(QueueJob&& job) override {
    executionQueue->addJob(std::move(job));
  }

  /// @}

public:
  BuildSystemImpl(class BuildSystem& buildSystem,
                  BuildSystemDelegate& delegate,
                  StringRef mainFilename)
      : buildSystem(buildSystem), delegate(delegate),
        mainFilename(mainFilename),
        fileDelegate(*this), buildFile(mainFilename, fileDelegate),
        engineDelegate(*this), buildEngine(engineDelegate),
        executionQueue(delegate.createExecutionQueue()) {}

  BuildSystem& getBuildSystem() {
    return buildSystem;
  }

  BuildSystemDelegate& getDelegate() override {
    return delegate;
  }

  StringRef getMainFilename() {
    return mainFilename;
  }

  BuildSystemCommandInterface& getCommandInterface() {
    return *this;
  }

  BuildFile& getBuildFile() {
    return buildFile;
  }

  void error(StringRef filename, const Twine& message) {
    getDelegate().error(filename, {}, message);
  }

  void error(StringRef filename, const BuildSystemDelegate::Token& at,
             const Twine& message) {
    getDelegate().error(filename, at, message);
  }

  std::unique_ptr<BuildNode> lookupNode(StringRef name,
                                        bool isImplicit);

  /// @name Client API
  /// @{

  bool attachDB(StringRef filename, std::string* error_out) {
    // FIXME: How do we pass the client schema version here, if we haven't
    // loaded the file yet.
    std::unique_ptr<core::BuildDB> db(
        core::createSQLiteBuildDB(filename, delegate.getVersion(),
                                  error_out));
    if (!db)
      return false;

    buildEngine.attachDB(std::move(db));
    return true;
  }

  bool enableTracing(StringRef filename, std::string* error_out) {
    return buildEngine.enableTracing(filename, error_out);
  }

  bool build(StringRef target);

  /// @}
};

#pragma mark - BuildSystem engine integration

#pragma mark - Task implementations

static BuildSystemImpl& getBuildSystem(BuildEngine& engine) {
  return static_cast<BuildSystemEngineDelegate*>(
      engine.getDelegate())->getBuildSystem();
}
  
/// This is the task used to "build" a target, it translates between the request
/// for building a target key and the requests for all of its nodes.
class TargetTask : public Task {
  Target& target;
  
  // Build specific data.
  //
  // FIXME: We should probably factor this out somewhere else, so we can enforce
  // it is never used when initialized incorrectly.

  /// If true, the command had a missing input (this implies ShouldSkip is
  /// true).
  bool hasMissingInput = false;

  virtual void start(BuildEngine& engine) override {
    // Request all of the necessary system tasks.
    unsigned id = 0;
    for (auto it = target.getNodes().begin(),
           ie = target.getNodes().end(); it != ie; ++it, ++id) {
      engine.taskNeedsInput(this, BuildKey::makeNode(*it).toData(), id);
    }
  }

  virtual void providePriorValue(BuildEngine&,
                                 const ValueType& value) override {
    // Do nothing.
  }

  virtual void provideValue(BuildEngine& engine, uintptr_t inputID,
                            const ValueType& valueData) override {
    // Do nothing.
    auto value = BuildValue::fromData(valueData);

    if (value.isMissingInput()) {
      hasMissingInput = true;

      // FIXME: Design the logging and status output APIs.
      auto& system = getBuildSystem(engine);
      system.error(system.getMainFilename(),
                   (Twine("missing input '") +
                    target.getNodes()[inputID]->getName() +
                    "' and no rule to build it"));
    }
  }

  virtual void inputsAvailable(BuildEngine& engine) override {
    if (hasMissingInput) {
      // FIXME: Design the logging and status output APIs.
      auto& system = getBuildSystem(engine);
      system.error(system.getMainFilename(),
                   (Twine("cannot build target '") + target.getName() +
                    "' due to missing input"));
      
      // Report the command failure.
      system.getDelegate().hadCommandFailure();
    }
    
    // Complete the task immediately.
    engine.taskIsComplete(this, BuildValue::makeTarget().toData());
  }

public:
  TargetTask(Target& target) : target(target) {}

  static bool isResultValid(Target& node, const BuildValue& value) {
    // Always treat target tasks as invalid.
    return false;
  }
};

/// This is the task to "build" a node which represents pure raw input to the
/// system.
class InputNodeTask : public Task {
  BuildNode& node;

  virtual void start(BuildEngine& engine) override {
    assert(node.getProducers().empty());
  }

  virtual void providePriorValue(BuildEngine&,
                                 const ValueType& value) override {
  }

  virtual void provideValue(BuildEngine&, uintptr_t inputID,
                            const ValueType& value) override {
  }

  virtual void inputsAvailable(BuildEngine& engine) override {
    // Handle virtual nodes.
    if (node.isVirtual()) {
      engine.taskIsComplete(
          this, BuildValue::makeVirtualInput().toData());
      return;
    }
    
    // Get the information on the file.
    //
    // FIXME: This needs to delegate, since we want to have a notion of
    // different node types.
    auto info = node.getFileInfo();
    if (info.isMissing()) {
      engine.taskIsComplete(this, BuildValue::makeMissingInput().toData());
      return;
    }

    engine.taskIsComplete(
        this, BuildValue::makeExistingInput(info).toData());
  }

public:
  InputNodeTask(BuildNode& node) : node(node) {}

  static bool isResultValid(const BuildNode& node, const BuildValue& value) {
    // Virtual input nodes are always valid unless the value type is wrong.
    if (node.isVirtual())
      return value.isVirtualInput();
    
    // If the previous value wasn't for an existing input, always recompute.
    assert(value.isExistingInput() || value.isMissingInput());
    if (!value.isExistingInput())
      return false;

    // Otherwise, the result is valid if the path exists and the file
    // information remains the same.
    //
    // FIXME: This is inefficient, we will end up doing the stat twice, once
    // when we check the value for up to dateness, and once when we "build" the
    // output.
    //
    // We can solve this by caching ourselves but I wonder if it is something
    // the engine should support more naturally.
    auto info = node.getFileInfo();
    if (info.isMissing())
      return false;

    return value.getOutputInfo() == info;
  }
};


/// This is the task to "build" a node which is the product of some command.
///
/// It is responsible for selecting the appropriate producer command to run to
/// produce the node, and for synchronizing any external state the node depends
/// on.
class ProducedNodeTask : public Task {
  Node& node;
  BuildValue nodeResult;
  Command* producingCommand = nullptr;

  // Build specific data.
  //
  // FIXME: We should probably factor this out somewhere else, so we can enforce
  // it is never used when initialized incorrectly.

  // Whether this is a node we are unable to produce.
  bool isInvalid = false;
  
  virtual void start(BuildEngine& engine) override {
    // Request the producer command.
    if (node.getProducers().size() == 1) {
      producingCommand = node.getProducers()[0];
      engine.taskNeedsInput(this, BuildKey::makeCommand(
                                producingCommand->getName()).toData(),
                            /*InputID=*/0);
      return;
    }

    // We currently do not support building nodes which have multiple producers.
    auto producerA = node.getProducers()[0];
    auto producerB = node.getProducers()[1];
    getBuildSystem(engine).error(
        "", "unable to build node: '" + node.getName() + "' (node is produced "
        "by multiple commands; e.g., '" + producerA->getName() + "' and '" +
        producerB->getName() + "')");
    isInvalid = true;
  }

  virtual void providePriorValue(BuildEngine&,
                                 const ValueType& value) override {
  }

  virtual void provideValue(BuildEngine&, uintptr_t inputID,
                            const ValueType& valueData) override {
    auto value = BuildValue::fromData(valueData);

    // Extract the node result from the command.
    assert(producingCommand);
    nodeResult = producingCommand->getResultForOutput(&node, value);
  }

  virtual void inputsAvailable(BuildEngine& engine) override {
    if (isInvalid) {
      engine.taskIsComplete(this, BuildValue::makeFailedInput().toData());
      return;
    }
    
    assert(!nodeResult.isInvalid());
    
    // Complete the task immediately.
    engine.taskIsComplete(this, nodeResult.toData());
  }

public:
  ProducedNodeTask(Node& node)
      : node(node), nodeResult(BuildValue::makeInvalid()) {}
  
  static bool isResultValid(Node& node, const BuildValue& value) {
    // If the result was failure, we always need to rebuild (it may produce an
    // error).
    if (value.isFailedInput())
      return false;

    // The produced node result itself doesn't need any synchronization.
    return true;
  }
};

/// This is the task to actually execute a command.
class CommandTask : public Task {
  Command& command;

  virtual void start(BuildEngine& engine) override {
    command.start(getBuildSystem(engine).getCommandInterface(), this);
  }

  virtual void providePriorValue(BuildEngine& engine,
                                 const ValueType& valueData) override {
    BuildValue value = BuildValue::fromData(valueData);
    command.providePriorValue(
        getBuildSystem(engine).getCommandInterface(), this, value);
  }

  virtual void provideValue(BuildEngine& engine, uintptr_t inputID,
                            const ValueType& valueData) override {
    command.provideValue(
        getBuildSystem(engine).getCommandInterface(), this, inputID,
        BuildValue::fromData(valueData));
  }

  virtual void inputsAvailable(BuildEngine& engine) override {
    command.inputsAvailable(getBuildSystem(engine).getCommandInterface(), this);
  }

public:
  CommandTask(Command& command) : command(command) {}

  static bool isResultValid(Command& command, const BuildValue& value) {
    // Delegate to the command for further checking.
    return command.isResultValid(value);
  }
};

#pragma mark - BuildSystemEngineDelegate implementation

/// This is a synthesized task used to represent a missing command.
///
/// This command is used in cases where a command has been removed from the
/// manifest, but can still be found during an incremental rebuild. This command
/// is used to inject an invalid value thus forcing downstream clients to
/// rebuild.
class MissingCommandTask : public Task {
private:
  virtual void start(BuildEngine& engine) override { }
  virtual void providePriorValue(BuildEngine& engine,
                                 const ValueType& valueData) override { }

  virtual void provideValue(BuildEngine& engine, uintptr_t inputID,
                            const ValueType& valueData) override { }

  virtual void inputsAvailable(BuildEngine& engine) override {
    // A missing command always builds to an invalid value, and forces
    // downstream clients to be rebuilt (at which point they will presumably see
    // the command is no longer used).
    return engine.taskIsComplete(this, BuildValue::makeInvalid().toData(),
                                 /*forceChange=*/true);
  }

public:
  using Task::Task;
};

BuildFile& BuildSystemEngineDelegate::getBuildFile() {
  return system.getBuildFile();
}

Rule BuildSystemEngineDelegate::lookupRule(const KeyType& keyData) {
  // Decode the key.
  auto key = BuildKey::fromData(keyData);

  switch (key.getKind()) {
  case BuildKey::Kind::Unknown:
    break;
    
  case BuildKey::Kind::Command: {
    // Find the comand.
    auto it = getBuildFile().getCommands().find(key.getCommandName());
    if (it == getBuildFile().getCommands().end()) {
      // If there is no such command, produce an error task.
      return Rule{
        keyData,
        /*Action=*/ [](BuildEngine& engine) -> Task* {
          return engine.registerTask(new MissingCommandTask());
        },
        /*IsValid=*/ [](BuildEngine&, const Rule& rule,
                        const ValueType& value) -> bool {
          // The cached result for a missing command is never valid.
          return false;
        }
      };
    }

    // Create the rule for the command.
    Command* command = it->second.get();
    return Rule{
      keyData,
      /*Action=*/ [command](BuildEngine& engine) -> Task* {
        return engine.registerTask(new CommandTask(*command));
      },
      /*IsValid=*/ [command](BuildEngine&, const Rule& rule,
                             const ValueType& value) -> bool {
        return CommandTask::isResultValid(
            *command, BuildValue::fromData(value));
      }
    };
  }

  case BuildKey::Kind::CustomTask: {
    // Search for a tool which knows how to create the given custom task.
    //
    // FIXME: We should most likely have some kind of registration process so we
    // can do an efficient query here, but exactly how this should look isn't
    // clear yet.
    for (const auto& it: getBuildFile().getTools()) {
      auto result = it.second->createCustomCommand(key);
      if (!result) continue;

      // Save the custom command.
      customTasks.emplace_back(std::move(result));
      Command *command = customTasks.back().get();
      
      return Rule{
        keyData,
        /*Action=*/ [command](BuildEngine& engine) -> Task* {
          return engine.registerTask(new CommandTask(*command));
        },
        /*IsValid=*/ [command](BuildEngine&, const Rule& rule,
                               const ValueType& value) -> bool {
          return CommandTask::isResultValid(
              *command, BuildValue::fromData(value));
        }
      };
    }
    
    // We were unable to create an appropriate custom command, produce an error
    // task.
    return Rule{
      keyData,
      /*Action=*/ [](BuildEngine& engine) -> Task* {
        return engine.registerTask(new MissingCommandTask());
      },
      /*IsValid=*/ [](BuildEngine&, const Rule& rule,
                      const ValueType& value) -> bool {
        // The cached result for a missing command is never valid.
        return false;
      }
    };
  }
    
  case BuildKey::Kind::Node: {
    // Find the node.
    auto it = getBuildFile().getNodes().find(key.getNodeName());
    BuildNode* node;
    if (it != getBuildFile().getNodes().end()) {
      node = static_cast<BuildNode*>(it->second.get());
    } else {
      auto it = dynamicNodes.find(key.getNodeName());
      if (it != dynamicNodes.end()) {
        node = it->second.get();
      } else {
        // Create nodes on the fly for any unknown ones.
        auto nodeOwner = system.lookupNode(
            key.getNodeName(), /*isImplicit=*/true);
        node = nodeOwner.get();
        dynamicNodes[key.getNodeName()] = std::move(nodeOwner);
      }
    }

    // Create the rule used to construct this node.
    //
    // We could bypass this level and directly return the rule to run the
    // command, which would reduce the number of tasks in the system. For now we
    // do the uniform thing, but do differentiate between input and command
    // nodes.

    // Create an input node if there are no producers.
    if (node->getProducers().empty()) {
      return Rule{
        keyData,
        /*Action=*/ [node](BuildEngine& engine) -> Task* {
          return engine.registerTask(new InputNodeTask(*node));
        },
        /*IsValid=*/ [node](BuildEngine&, const Rule& rule,
                            const ValueType& value) -> bool {
          return InputNodeTask::isResultValid(
              *node, BuildValue::fromData(value));
        }
      };
    }

    // Otherwise, create a task for a produced node.
    return Rule{
      keyData,
      /*Action=*/ [node](BuildEngine& engine) -> Task* {
        return engine.registerTask(new ProducedNodeTask(*node));
      },
      /*IsValid=*/ [node](BuildEngine&, const Rule& rule,
                          const ValueType& value) -> bool {
        return ProducedNodeTask::isResultValid(
            *node, BuildValue::fromData(value));
      }
    };
  }

  case BuildKey::Kind::Target: {
    // Find the target.
    auto it = getBuildFile().getTargets().find(key.getTargetName());
    if (it == getBuildFile().getTargets().end()) {
      // FIXME: Invalid target name, produce an error.
      assert(0 && "FIXME: invalid target");
      abort();
    }

    // Create the rule to construct this target.
    Target* target = it->second.get();
    return Rule{
      keyData,
        /*Action=*/ [target](BuildEngine& engine) -> Task* {
        return engine.registerTask(new TargetTask(*target));
      },
      /*IsValid=*/ [target](BuildEngine&, const Rule& rule,
                            const ValueType& value) -> bool {
        return TargetTask::isResultValid(*target, BuildValue::fromData(value));
      }
    };
  }
  }

  assert(0 && "invalid key type");
  abort();
}

void BuildSystemEngineDelegate::cycleDetected(const std::vector<Rule*>& cycle) {
  // Compute a description of the cycle path.
  SmallString<256> message;
  llvm::raw_svector_ostream os(message);
  os << "cycle detected while building: ";
  bool first = true;
  for (const auto* rule: cycle) {
    if (!first)
      os << " -> ";

    // Convert to a build key.
    auto key = BuildKey::fromData(rule->key);
    switch (key.getKind()) {
    case BuildKey::Kind::Unknown:
      os << "((unknown))";
      break;
    case BuildKey::Kind::Command:
      os << "command '" << key.getCommandName() << "'";
      break;
    case BuildKey::Kind::CustomTask:
      os << "custom task '" << key.getCustomTaskName() << "'";
      break;
    case BuildKey::Kind::Node:
      os << "node '" << key.getNodeName() << "'";
      break;
    case BuildKey::Kind::Target:
      os << "target '" << key.getTargetName() << "'";
      break;
    }
    first = false;
  }
  
  system.error(system.getMainFilename(), os.str());
}

#pragma mark - BuildSystemImpl implementation

std::unique_ptr<BuildNode>
BuildSystemImpl::lookupNode(StringRef name, bool isImplicit) {
  bool isVirtual = !name.empty() && name[0] == '<' && name.back() == '>';
  return llvm::make_unique<BuildNode>(name, isVirtual);
}

bool BuildSystemImpl::build(StringRef target) {
  // Load the build file.
  //
  // FIXME: Eventually, we may want to support something fancier where we load
  // the build file in the background so we can immediately start building
  // things as they show up.
  //
  // FIXME: We need to load this only once.
  if (!getBuildFile().load()) {
    error(getMainFilename(), "unable to load build file");
    return false;
  }    

  // Build the target.
  getBuildEngine().build(BuildKey::makeTarget(target).toData());

  return true;
}

#pragma mark - PhonyTool implementation

class PhonyCommand : public ExternalCommand {
public:
  using ExternalCommand::ExternalCommand;

  virtual void getShortDescription(SmallVectorImpl<char> &result) override {
    llvm::raw_svector_ostream(result) << getName();
  }

  virtual void getVerboseDescription(SmallVectorImpl<char> &result) override {
    llvm::raw_svector_ostream(result) << getName();
  }

  virtual bool executeExternalCommand(BuildSystemCommandInterface& bsci,
                                      Task* task,
                                      QueueJobContext* context) override {
    // Nothing needs to be done for phony commands.
    return true;
  }
};

class PhonyTool : public Tool {
public:
  using Tool::Tool;

  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  StringRef value) override {
    // No supported configuration attributes.
    ctx.error("unexpected attribute: '" + name + "'");
    return false;
  }
  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  ArrayRef<StringRef> values) override {
    // No supported configuration attributes.
    ctx.error("unexpected attribute: '" + name + "'");
    return false;
  }

  virtual std::unique_ptr<Command> createCommand(StringRef name) override {
    return llvm::make_unique<PhonyCommand>(name);
  }
};

#pragma mark - ShellTool implementation

class ShellCommand : public ExternalCommand {
  std::vector<std::string> args;

  virtual uint64_t getSignature() override {
    uint64_t result = ExternalCommand::getSignature();
    for (const auto& arg: args) {
      result ^= basic::hashString(arg);
    }
    return result;
  }
  
public:
  using ExternalCommand::ExternalCommand;

  virtual void getShortDescription(SmallVectorImpl<char> &result) override {
    llvm::raw_svector_ostream(result) << getDescription();
  }

  virtual void getVerboseDescription(SmallVectorImpl<char> &result) override {
    llvm::raw_svector_ostream os(result);
    bool first = true;
    for (const auto& arg: args) {
      if (!first) os << " ";
      first = false;
      // FIXME: This isn't correct, we need utilities for doing shell quoting.
      if (arg.find(' ') != StringRef::npos) {
        os << '"' << arg << '"';
      } else {
        os << arg;
      }
    }
  }
  
  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  StringRef value) override {
    if (name == "args") {
      // When provided as a scalar string, we default to executing using the
      // shell.
      args.clear();
      args.push_back("/bin/sh");
      args.push_back("-c");
      args.push_back(value);
    } else {
      return ExternalCommand::configureAttribute(ctx, name, value);
    }

    return true;
  }
  
  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  ArrayRef<StringRef> values) override {
    if (name == "args") {
      // Diagnose missing arguments.
      if (values.empty()) {
        ctx.error("invalid arguments for command '" + getName() + "'");
        return false;
      }
      
      args = std::vector<std::string>(values.begin(), values.end());
    } else {
      return ExternalCommand::configureAttribute(ctx, name, values);
    }

    return true;
  }

  virtual bool executeExternalCommand(BuildSystemCommandInterface& bsci,
                                      Task* task,
                                      QueueJobContext* context) override {
    // Execute the command.
    return bsci.getExecutionQueue().executeProcess(
        context, std::vector<StringRef>(args.begin(), args.end()));
  }
};

class ShellTool : public Tool {
public:
  using Tool::Tool;

  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  StringRef value) override {
    // No supported attributes.
    ctx.error("unexpected attribute: '" + name + "'");
    return false;
  }
  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  ArrayRef<StringRef> values) override {
    // No supported attributes.
    ctx.error("unexpected attribute: '" + name + "'");
    return false;
  }

  virtual std::unique_ptr<Command> createCommand(StringRef name) override {
    return llvm::make_unique<ShellCommand>(name);
  }
};

#pragma mark - ClangTool implementation

class ClangShellCommand : public ExternalCommand {
  /// The compiler command to invoke.
  std::string args;
  
  /// The path to the dependency output file, if used.
  std::string depsPath;
  
  virtual uint64_t getSignature() override {
    uint64_t result = ExternalCommand::getSignature();
    result ^= basic::hashString(args);
    return result;
  }

  bool processDiscoveredDependencies(BuildSystemCommandInterface& bsci,
                                     Task* task,
                                     QueueJobContext* context) {
    // Read the dependencies file.
    auto input = bsci.getDelegate().getFileSystem().getFileContents(depsPath);
    if (!input) {
      getBuildSystem(bsci.getBuildEngine()).error(
          depsPath, "unable to open dependencies file (" + depsPath + ")");
      return false;
    }

    // Parse the output.
    //
    // We just ignore the rule, and add any dependency that we encounter in the
    // file.
    struct DepsActions : public core::MakefileDepsParser::ParseActions {
      BuildSystemCommandInterface& bsci;
      Task* task;
      ClangShellCommand* command;
      unsigned numErrors{0};

      DepsActions(BuildSystemCommandInterface& bsci, Task* task,
                  ClangShellCommand* command)
          : bsci(bsci), task(task), command(command) {}

      virtual void error(const char* message, uint64_t position) override {
        getBuildSystem(bsci.getBuildEngine()).error(
            command->depsPath,
            "error reading dependency file: " + std::string(message));
        ++numErrors;
      }

      virtual void actOnRuleDependency(const char* dependency,
                                       uint64_t length) override {
        bsci.taskDiscoveredDependency(
            task, BuildKey::makeNode(StringRef(dependency, length)));
      }

      virtual void actOnRuleStart(const char* name, uint64_t length) override {}
      virtual void actOnRuleEnd() override {}
    };

    DepsActions actions(bsci, task, this);
    core::MakefileDepsParser(input->getBufferStart(), input->getBufferSize(),
                             actions).parse();
    return actions.numErrors == 0;
  }

public:
  using ExternalCommand::ExternalCommand;

  virtual void getShortDescription(SmallVectorImpl<char> &result) override {
    llvm::raw_svector_ostream(result) << getDescription();
  }

  virtual void getVerboseDescription(SmallVectorImpl<char> &result) override {
    llvm::raw_svector_ostream(result) << args;
  }
  
  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  StringRef value) override {
    if (name == "args") {
      args = value;
    } else if (name == "deps") {
      depsPath = value;
    } else {
      return ExternalCommand::configureAttribute(ctx, name, value);
    }

    return true;
  }
  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  ArrayRef<StringRef> values) override {
    return ExternalCommand::configureAttribute(ctx, name, values);
  }

  virtual bool executeExternalCommand(BuildSystemCommandInterface& bsci,
                                      Task* task,
                                      QueueJobContext* context) override {
    // Execute the command.
    if (!bsci.getExecutionQueue().executeShellCommand(context, args)) {
      // If the command failed, there is no need to gather dependencies.
      return false;
    }

    // Otherwise, collect the discovered dependencies, if used.
    if (!depsPath.empty()) {
      if (!processDiscoveredDependencies(bsci, task, context)) {
        // If we were unable to process the dependencies output, report a
        // failure.
        return false;
      }
    }

    return true;
  }
};

class ClangTool : public Tool {
public:
  using Tool::Tool;

  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  StringRef value) override {
    // No supported attributes.
    ctx.error("unexpected attribute: '" + name + "'");
    return false;
  }
  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  ArrayRef<StringRef> values) override {
    // No supported attributes.
    ctx.error("unexpected attribute: '" + name + "'");
    return false;
  }

  virtual std::unique_ptr<Command> createCommand(StringRef name) override {
    return llvm::make_unique<ClangShellCommand>(name);
  }
};

#pragma mark - BuildSystemFileDelegate

BuildSystemDelegate& BuildSystemFileDelegate::getSystemDelegate() {
  return system.getDelegate();
}

void BuildSystemFileDelegate::setFileContentsBeingParsed(StringRef buffer) {
  getSystemDelegate().setFileContentsBeingParsed(buffer);
}

void BuildSystemFileDelegate::error(StringRef filename,
                                    const BuildFileToken& at,
                                    const Twine& message) {
  // Delegate to the system delegate.
  auto atSystemToken = BuildSystemDelegate::Token{at.start, at.length};
  system.error(filename, atSystemToken, message);
}

bool
BuildSystemFileDelegate::configureClient(const ConfigureContext&,
                                         StringRef name,
                                         uint32_t version,
                                         const property_list_type& properties) {
  // The client must match the configured name of the build system.
  if (name != getSystemDelegate().getName())
    return false;

  // The client version must match the configured version.
  //
  // FIXME: We should give the client the opportunity to support a previous
  // schema version (auto-upgrade).
  if (version != getSystemDelegate().getVersion())
    return false;

  return true;
}

std::unique_ptr<Tool>
BuildSystemFileDelegate::lookupTool(StringRef name) {
  // First, give the client an opportunity to create the tool.
  if (auto tool = getSystemDelegate().lookupTool(name)) {
    return tool;
  }

  // Otherwise, look for one of the builtin tool definitions.
  if (name == "shell") {
    return llvm::make_unique<ShellTool>(name);
  } else if (name == "phony") {
    return llvm::make_unique<PhonyTool>(name);
  } else if (name == "clang") {
    return llvm::make_unique<ClangTool>(name);
  }

  return nullptr;
}

void BuildSystemFileDelegate::loadedTarget(StringRef name,
                                           const Target& target) {
}

void BuildSystemFileDelegate::loadedCommand(StringRef name,
                                            const Command& command) {
}

std::unique_ptr<Node>
BuildSystemFileDelegate::lookupNode(StringRef name,
                                    bool isImplicit) {
  return system.lookupNode(name, isImplicit);
}

}

#pragma mark - BuildSystem

BuildSystem::BuildSystem(BuildSystemDelegate& delegate,
                         StringRef mainFilename)
    : impl(new BuildSystemImpl(*this, delegate, mainFilename))
{
}

BuildSystem::~BuildSystem() {
  delete static_cast<BuildSystemImpl*>(impl);
}

BuildSystemDelegate& BuildSystem::getDelegate() {
  return static_cast<BuildSystemImpl*>(impl)->getDelegate();
}

bool BuildSystem::attachDB(StringRef path,
                                std::string* error_out) {
  return static_cast<BuildSystemImpl*>(impl)->attachDB(path, error_out);
}

bool BuildSystem::enableTracing(StringRef path,
                                std::string* error_out) {
  return static_cast<BuildSystemImpl*>(impl)->enableTracing(path, error_out);
}

bool BuildSystem::build(StringRef name) {
  return static_cast<BuildSystemImpl*>(impl)->build(name);
}
