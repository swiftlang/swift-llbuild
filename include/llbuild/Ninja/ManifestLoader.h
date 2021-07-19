//===- ManifestLoader.h ----------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_NINJA_MANIFESTLOADER_H
#define LLBUILD_NINJA_MANIFESTLOADER_H

#include "llbuild/Basic/LLVM.h"
#include "llbuild/Ninja/Manifest.h"

#include "llvm/ADT/StringRef.h"

#include <memory>

namespace llvm {
class MemoryBuffer;
}

namespace llbuild {
namespace ninja {

class Manifest;
class ManifestLoader;
class Parser;
struct Token;

/// Delegate interface for loader behavior.
class ManifestLoaderActions {
public:
  virtual ~ManifestLoaderActions();

  /// Called at the beginning of loading, to register the manifest loader.
  virtual void initialize(ManifestLoader* loader) = 0;

  virtual void error(StringRef filename, StringRef message, const Token& at) = 0;

  /// Called by the loader to request the contents of a manifest file be loaded.
  ///
  /// \param path Absolute path of the file to load.
  ///
  /// \param forFilename If non-empty, the name of the file triggering the file
  /// load (for use in diagnostics).
  ///
  /// \param forToken If non-null, the token triggering the file load (for use
  /// in diagnostics).
  ///
  /// \returns The loaded file on success, or a nullptr. On failure, the action
  /// is assumed to have produced an appropriate error.
  virtual std::unique_ptr<llvm::MemoryBuffer> readFile(
      StringRef path, StringRef forFilename, const Token* forToken) = 0;
};

/// Interface for loading Ninja build manifests.
class ManifestLoader {
  class ManifestLoaderImpl;
  std::unique_ptr<ManifestLoaderImpl> impl;

public:
  ManifestLoader(StringRef workingDirectory, StringRef mainFilename,
                 ManifestLoaderActions& actions);
  ~ManifestLoader();

  /// Load the manifest.
  std::unique_ptr<Manifest> load();

  /// Get the current underlying manifest parser.
  const Parser* getCurrentParser() const;
};

}
}

#endif
