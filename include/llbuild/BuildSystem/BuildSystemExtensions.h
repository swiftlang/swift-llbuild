//===- BuildSystemExtensions.h ----------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_BUILDSYSTEM_BUILDSYSTEMEXTENSIONS_H
#define LLBUILD_BUILDSYSTEM_BUILDSYSTEMEXTENSIONS_H

#include "llbuild/BuildSystem/BuildSystemHandlers.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

#include <memory>
#include <mutex>

namespace llbuild {
namespace buildsystem {

class BuildSystemExtension;
class ShellCommand;

/// Management of the loading and registration of build system extensions;
///
/// NOTE: This class *is* thread-safe.
class BuildSystemExtensionManager {
  /// Mutex to protect extensions map.
  std::mutex extensionsLock;

  /// The map of discovered extensions (or nullptr, for negative lookups).
  llvm::StringMap<std::unique_ptr<BuildSystemExtension>> extensions;
  
public:
  BuildSystemExtensionManager() {}

  /// Find a registered extension for the given command path.
  BuildSystemExtension* lookupByCommandPath(StringRef path);
};

/// A concrete build system extension.
class BuildSystemExtension {
public:
  explicit BuildSystemExtension();
  virtual ~BuildSystemExtension();

  virtual std::unique_ptr<ShellCommandHandler>
  resolveShellCommandHandler(ShellCommand*) = 0;
};

}
}

#endif
