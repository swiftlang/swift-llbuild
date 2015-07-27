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

#include "llbuild/BuildSystem/BuildFile.h"

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
};

class BuildSystemImpl {
  /// The delegate the BuildSystem was configured with.
  BuildSystemDelegate& delegate;

  /// The name of the main input file.
  std::string mainFilename;

public:
  BuildSystemImpl(class BuildSystem& buildSystem,
                  BuildSystemDelegate& delegate,
                  const std::string& mainFilename)
      : delegate(delegate), mainFilename(mainFilename) {}

  BuildSystemDelegate& getDelegate() {
    return delegate;
  }

  /// @name Actions
  /// @{

  bool build(const std::string& target) {
    // Load the build file.
    BuildSystemFileDelegate fileDelegate(*this);
    BuildFile buildFile(mainFilename, fileDelegate);
    buildFile.load();
    
    return false;
  }

  /// @}
};

}

#pragma mark - BuildSystemFileDelegate

void BuildSystemFileDelegate::error(const std::string& filename,
                                    const std::string& message) {
  // Delegate to the system delegate.
  system.getDelegate().error(filename, message);
}

bool
BuildSystemFileDelegate::configureClient(const std::string& name,
                                         uint32_t version,
                                         const property_list_type& properties) {
  return false;
}

std::unique_ptr<Tool>
BuildSystemFileDelegate::lookupTool(const std::string& name) {
  // FIXME: Implement.
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
  // FIXME: Implement.
  return nullptr;
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
