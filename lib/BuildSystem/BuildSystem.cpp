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

using namespace llbuild;
using namespace llbuild::buildsystem;

BuildSystemDelegate::~BuildSystemDelegate() {}

#pragma mark - BuildSystem implementation

namespace {

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

  BuildSystemDelegate* getDelegate() {
    return &delegate;
  }

  /// @name Actions
  /// @{

  bool build(const std::string& target) {
    return false;
  }

  /// @}
};

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

BuildSystemDelegate* BuildSystem::getDelegate() {
  return static_cast<BuildSystemImpl*>(impl)->getDelegate();
}

bool BuildSystem::build(const std::string& name) {
  return static_cast<BuildSystemImpl*>(impl)->build(name);
}
