//===-- BuildFile.cpp -----------------------------------------------------===//
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

#include "llbuild/BuildFile/BuildFile.h"

using namespace llbuild;
using namespace llbuild::buildfile;

#pragma mark - BuildEngine implementation

namespace {

class BuildFileImpl {
  BuildFile& buildFile;

  /// The name of the main input file.
  std::string mainFilename;

  /// The delegate the BuildFile was configured with.
  BuildFileDelegate& delegate;

public:
  BuildFileImpl(class BuildFile& buildFile,
                const std::string& mainFilename,
                BuildFileDelegate& delegate)
    : buildFile(buildFile), mainFilename(mainFilename), delegate(delegate) {}

  ~BuildFileImpl() {}

  BuildFileDelegate* getDelegate() {
    return &delegate;
  }

  /// @name Parse Actions
  /// @{

  void load() {
    (void)buildFile;
  }

  /// @}
};

}

#pragma mark - BuildFile

BuildFile::BuildFile(const std::string& mainFilename,
                     BuildFileDelegate& delegate)
  : impl(new BuildFileImpl(*this, mainFilename, delegate)) 
{
}

BuildFile::~BuildFile() {
  delete static_cast<BuildFileImpl*>(impl);
}

BuildFileDelegate* BuildFile::getDelegate() {
  return static_cast<BuildFileImpl*>(impl)->getDelegate();
}

void BuildFile::load() {
  return static_cast<BuildFileImpl*>(impl)->load();
}
