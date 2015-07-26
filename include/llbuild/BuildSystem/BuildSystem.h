//===- BuildSystem.h --------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BUILDSYSTEM_BUILDSYSTEM_H
#define LLBUILD_BUILDSYSTEM_BUILDSYSTEM_H

#include "llbuild/Basic/Compiler.h"

#include <string>

namespace llbuild {
namespace buildsystem {

class BuildSystemDelegate {
  std::string name;
  
public:
  BuildSystemDelegate(const std::string& name) : name(name) {}
  virtual ~BuildSystemDelegate();

  /// Called by the build system to get the client name.
  const std::string& getName() const { return name; }
};

/// The BuildSystem class is used to perform builds using the native build
/// system.
class BuildSystem {
private:
  void *impl;

public:
  /// Create a build system with the given delegate.
  ///
  /// \arg mainFilename The path of the main build file.
  explicit BuildSystem(BuildSystemDelegate& delegate,
                       const std::string& mainFilename);
  ~BuildSystem();

  /// Return the delegate the engine was configured with.
  BuildSystemDelegate* getDelegate();

  /// @name Actions
  /// @{

  /// Build the named target.
  ///
  /// \returns True on success.
  bool build(const std::string& target);

  /// @}
};

}
}

#endif
