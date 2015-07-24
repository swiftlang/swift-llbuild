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

#include <string>
#include <utility>
#include <vector>

namespace llbuild {
namespace buildsystem {

/// The type used to pass parsed properties to the delegate.
typedef std::vector<std::pair<std::string, std::string>> property_list_type;

class BuildFileDelegate {
public:
  virtual ~BuildFileDelegate();

  /// Called by the build file loader to report an error.
  //
  // FIXME: Support better diagnostics by passing a token of some kind.
  virtual void error(const std::string& filename,
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
};

/// The BuildFile class supports the "LLBuild"-native build description file
/// format.
class BuildFile {
  void *impl;

public:
  /// Create a build engine with the given delegate.
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
};

}
}

#endif
