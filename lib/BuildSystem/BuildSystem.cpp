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

#include "llvm/ADT/StringRef.h"

#include "llbuild/Core/BuildEngine.h"
#include "llbuild/BuildSystem/BuildFile.h"

#include <memory>

using namespace llbuild;
using namespace llbuild::buildsystem;

BuildSystemDelegate::~BuildSystemDelegate() {}

#pragma mark - BuildSystem implementation

namespace {

class BuildSystemImpl;

class BuildSystemFileDelegate : public BuildFileDelegate {
  BuildSystemImpl& system;

public:
  BuildSystemFileDelegate(BuildSystemImpl& system)
      : BuildFileDelegate(), system(system) {}

  BuildSystemDelegate& getSystemDelegate();

  /// @name Delegate Implementation
  /// @{

  virtual void error(const std::string& filename,
                     const std::string& message) override;

  virtual bool configureClient(const std::string& name,
                               uint32_t version,
                               const property_list_type& properties) override;

  virtual std::unique_ptr<Tool> lookupTool(const std::string& name) override;

  virtual void loadedTarget(const std::string& name,
                            const Target& target) override;

  virtual void loadedTask(const std::string& name, const Task& target) override;

  virtual std::unique_ptr<Node> lookupNode(const std::string& name,
                                           bool isImplicit=false) override;

  /// @}
};

class BuildSystemImpl {
  BuildSystem& buildSystem;

  /// The delegate the BuildSystem was configured with.
  BuildSystemDelegate& delegate;

  /// The name of the main input file.
  std::string mainFilename;

public:
  BuildSystemImpl(class BuildSystem& buildSystem,
                  BuildSystemDelegate& delegate,
                  const std::string& mainFilename)
      : buildSystem(buildSystem), delegate(delegate),
        mainFilename(mainFilename) {}

  BuildSystem& getBuildSystem() {
    return buildSystem;
  }

  BuildSystemDelegate& getDelegate() {
    return delegate;
  }

  const std::string& getMainFilename() {
    return mainFilename;
  }

  /// @name Actions
  /// @{

  bool build(const std::string& target);

  /// @}
};

#pragma mark - BuildSystem engine integration

/// The system key defines the helpers for translating to and from the key space
/// used by the BuildSystem when using the core BuildEngine.
struct SystemKey {
  enum class Kind {
    /// A key used to identify a named target.
    Target,

    /// An invalid key kind.
    Unknown,
  };

  /// The actual key data.
  core::KeyType key;

private:
  SystemKey(const core::KeyType& key) : key(key) {}
  SystemKey(char kindCode, llvm::StringRef str) {
    key.reserve(str.size() + 1);
    key.push_back(kindCode);
    key.append(str.begin(), str.end());
  }
    
public:
  // Support copy and move.
  SystemKey(SystemKey&& rhs) : key(rhs.key) { }
  void operator=(const SystemKey& rhs) {
    if (this != &rhs)
      key = rhs.key;
  }
  SystemKey& operator=(SystemKey&& rhs) {
    if (this != &rhs)
      key = rhs.key;
    return *this;
  }

  // Allow implicit conversion to the contained key.
  operator const core::KeyType& () const { return getKeyData(); }

  /// @name Construction Functions
  /// @{

  static SystemKey fromKeyData(const core::KeyType& key) {
    auto result = SystemKey(key);
    assert(result.getKind() != Kind::Unknown && "invalid key");
    return result;
  }
  
  static SystemKey makeTarget(llvm::StringRef name) {
    return SystemKey('T', name);
  }

  /// @}
  /// @name Accessors
  /// @{

  const core::KeyType& getKeyData() const { return key; }
  
  Kind getKind() const {
    switch (key[0]) {
    case 'T': return Kind::Target;
    default:
      return Kind::Unknown;
    }
  }

  bool isTarget() const { return getKind() == Kind::Target; }

  llvm::StringRef getTargetName() const {
    return llvm::StringRef(key.data()+1, key.size()-1);
  }
  
  /// @}
};

class ToolBasedCoreTask : public core::Task {
  virtual void start(core::BuildEngine&) override {
  }
  
  virtual void providePriorValue(core::BuildEngine&,
                                 const core::ValueType& value) override {
  }
  
  virtual void provideValue(core::BuildEngine&, uintptr_t inputID,
                            const core::ValueType& value) override {
  }
  
  virtual void inputsAvailable(core::BuildEngine& engine) override {
    // Complete the task immediately.
    engine.taskIsComplete(this, core::ValueType());
  }
};

class BuildSystemEngineDelegate : public core::BuildEngineDelegate {
  BuildSystemImpl& system;
public:
  BuildSystemEngineDelegate(BuildSystemImpl& system) : system(system) {}

  virtual core::Rule lookupRule(const core::KeyType& keyData) override {
    // Decode the key.
    auto key = SystemKey::fromKeyData(keyData);

    switch (key.getKind()) {
    default:
      assert(0 && "invalid key");
      abort();

    case SystemKey::Kind::Target: {
      // FIXME: Return an appropriate rule.
      return core::Rule{
        key,
        /*Action=*/ [&](core::BuildEngine& engine) -> core::Task* {
          return engine.registerTask(new ToolBasedCoreTask());
        }
      };
    }
    }
  }

  virtual void cycleDetected(const std::vector<core::Rule*>& items) override {
    system.getDelegate().error(system.getMainFilename(),
                               "cycle detected while building");
  }
};

bool BuildSystemImpl::build(const std::string& target) {
  // Load the build file.
  //
  // FIXME: Eventually, we may want to support something fancier where we load
  // the build file in the background so we can immediately start building
  // things as they show up.
  BuildSystemFileDelegate fileDelegate(*this);
  BuildFile buildFile(mainFilename, fileDelegate);
  buildFile.load();

  // Create the engine to use for building.
  BuildSystemEngineDelegate engineDelegate(*this);
  core::BuildEngine engine(engineDelegate);

  // Build the target.
  engine.build(SystemKey::makeTarget(target));

  return false;
}

#pragma mark - BuildNode implementation

// FIXME: Figure out how this is going to be organized.
class BuildNode : public Node {
public:
  using Node::Node;

  virtual bool configureAttribute(const std::string& name,
                                  const std::string& value) override {
    // We don't support any custom attributes.
    return false;
  }
};

#pragma mark - ShellTool implementation

class ShellTask : public Task {
  BuildSystemImpl& system;
  std::vector<Node*> inputs;
  std::vector<Node*> outputs;
  std::string args;
  
public:
  ShellTask(BuildSystemImpl& system, const std::string& name)
      : Task(name), system(system) {}

  virtual void configureInputs(const std::vector<Node*>& value) override {
    inputs = value;
  }

  virtual void configureOutputs(const std::vector<Node*>& value) override {
    outputs = value;
  }

  virtual bool configureAttribute(const std::string& name,
                                  const std::string& value) override {
    if (name == "args") {
      args = value;
    } else {
      system.getDelegate().error(
          system.getMainFilename(),
          "unexpected attribute: '" + name + "'");
      return false;
    }

    return true;
  }
};

class ShellTool : public Tool {
  BuildSystemImpl& system;
  
public:
  ShellTool(BuildSystemImpl& system, const std::string& name)
      : Tool(name), system(system) {}

  virtual bool configureAttribute(const std::string& name,
                                  const std::string& value) override {
    system.getDelegate().error(
        system.getMainFilename(),
        "unexpected attribute: '" + name + "'");

    // No supported attributes.
    return false;
  }

  virtual std::unique_ptr<Task> createTask(const std::string& name) override {
    return std::make_unique<ShellTask>(system, name);
  }
};

#pragma mark - BuildSystemFileDelegate

BuildSystemDelegate& BuildSystemFileDelegate::getSystemDelegate() {
  return system.getDelegate();
}

void BuildSystemFileDelegate::error(const std::string& filename,
                                    const std::string& message) {
  // Delegate to the system delegate.
  getSystemDelegate().error(filename, message);
}

bool
BuildSystemFileDelegate::configureClient(const std::string& name,
                                         uint32_t version,
                                         const property_list_type& properties) {
  // The client must match the configured name of the build system.
  if (name != getSystemDelegate().getName())
    return false;

  // FIXME: Give the client an opportunity to respond to the schema version and
  // configuration the properties.

  return true;
}

std::unique_ptr<Tool>
BuildSystemFileDelegate::lookupTool(const std::string& name) {
  // First, give the client an opportunity to create the tool.
  auto tool = getSystemDelegate().lookupTool(name);
  if (tool)
    return std::move(tool);

  // Otherwise, look for one of the builtin tool definitions.
  if (name == "shell") {
    return std::make_unique<ShellTool>(system, name);
  }

  return nullptr;
}

void BuildSystemFileDelegate::loadedTarget(const std::string& name,
                                           const Target& target) {
}

void BuildSystemFileDelegate::loadedTask(const std::string& name,
                                         const Task& target) {
}

std::unique_ptr<Node>
BuildSystemFileDelegate::lookupNode(const std::string& name,
                                    bool isImplicit) {
  return std::make_unique<BuildNode>(name);
}

}

#pragma mark - BuildSystem

BuildSystem::BuildSystem(BuildSystemDelegate& delegate,
                         const std::string& mainFilename)
    : impl(new BuildSystemImpl(*this, delegate, mainFilename))
{
}

BuildSystem::~BuildSystem() {
  delete static_cast<BuildSystemImpl*>(impl);
}

BuildSystemDelegate& BuildSystem::getDelegate() {
  return static_cast<BuildSystemImpl*>(impl)->getDelegate();
}

bool BuildSystem::build(const std::string& name) {
  return static_cast<BuildSystemImpl*>(impl)->build(name);
}
