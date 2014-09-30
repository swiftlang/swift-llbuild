//===-- ManifestLoader.cpp ------------------------------------------------===//
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

#include "llbuild/Ninja/ManifestLoader.h"

using namespace llbuild;
using namespace llbuild::ninja;

#pragma mark - ManifestLoaderActions

ManifestLoaderActions::~ManifestLoaderActions() {
}

#pragma mark - ManifestLoader Implementation

namespace {

class ManifestLoaderImpl {
  std::string MainFilename;
  ManifestLoaderActions &Actions;

public:
  ManifestLoaderImpl(std::string MainFilename, ManifestLoaderActions &Actions)
    : MainFilename(MainFilename), Actions(Actions)
  {
  }

  std::unique_ptr<Manifest> load() {
    return nullptr;
  }
};

}

#pragma mark - ManifestLoader

ManifestLoader::ManifestLoader(std::string Filename,
                               ManifestLoaderActions &Actions)
  : Impl(static_cast<void*>(new ManifestLoaderImpl(Filename, Actions)))
{
}

ManifestLoader::~ManifestLoader() {
  delete static_cast<ManifestLoaderImpl*>(Impl);
}

std::unique_ptr<Manifest> ManifestLoader::load() {
  return static_cast<ManifestLoaderImpl*>(Impl)->load();
}
