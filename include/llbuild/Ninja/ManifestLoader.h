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

#include "llbuild/Ninja/Manifest.h"

#include <string>
#include <utility>

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
  virtual void initialize(ManifestLoader* Loader) = 0;

  virtual void error(std::string Filename, std::string Message,
                     const Token& At) = 0;

  /// Called by the loader to request the contents of a manifest file be loaded.
  ///
  /// \param Filename The name of the file to load.
  ///
  /// \param ForToken If non-null, the token triggering the file load, for use
  /// in diagnostics.
  ///
  /// \param Data_Out On success, the contents of the file.
  ///
  /// \param Length_Out On success, the length of the data in the file.
  ///
  /// \returns True on success. On failure, the action is assumed to have
  /// produced an appropriate error.
  virtual bool readFileContents(const std::string& FromFilename,
                                const std::string& Filename,
                                const Token* ForToken,
                                std::unique_ptr<char[]> *Data_Out,
                                uint64_t *Length_Out) = 0;
};

/// Interface for loading Ninja build manifests.
class ManifestLoader {
  void *Impl;

public:
  ManifestLoader(std::string MainFilename, ManifestLoaderActions& Actions);
  ~ManifestLoader();

  /// Load the manifest.
  std::unique_ptr<Manifest> load();

  /// Get the current underlying manifest parser.
  const Parser* getCurrentParser() const;
};

}
}

#endif
