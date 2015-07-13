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

#ifndef LLBUILD_BUILDFILE_BUILDFILE_H
#define LLBUILD_BUILDFILE_BUILDFILE_H

#include <string>

namespace llbuild {
namespace buildfile {

class BuildFileDelegate {
public:
  virtual ~BuildFileDelegate();
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

  void load();

  /// @}
};

}
}

#endif
