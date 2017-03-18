//===-- FileSystem.cpp ----------------------------------------------------===//
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

#include "llbuild/Basic/FileSystem.h"
#include "llbuild/Basic/PlatformUtility.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/MemoryBuffer.h"

#include <cassert>
#include <cstring>

using namespace llbuild;
using namespace llbuild::basic;

FileSystem::~FileSystem() {}

bool FileSystem::createDirectories(const std::string& path) {
  // Attempt to create the final directory first, to optimize for the common
  // case where we don't need to recurse.
  if (createDirectory(path))
    return true;

  // If that failed, attempt to create the parent.
  StringRef parent = llvm::sys::path::parent_path(path);
  if (parent.empty())
    return false;
  return createDirectories(parent) && createDirectory(path);
}

namespace {

class LocalFileSystem : public FileSystem {
public:
  LocalFileSystem() {}

  virtual bool
  createDirectory(const std::string& path) override {
    if (!sys::mkdir(path.c_str())) {
      if (errno != EEXIST) {
        return false;
      }
    }
    return true;
  }

  virtual std::unique_ptr<llvm::MemoryBuffer>
  getFileContents(const std::string& path) override {
    auto result = llvm::MemoryBuffer::getFile(path);
    if (result.getError()) {
      return nullptr;
    }
    return std::unique_ptr<llvm::MemoryBuffer>(result->release());
  }
  
  virtual FileInfo getFileInfo(const std::string& path) override {
    return FileInfo::getInfoForPath(path);
  }
  
  virtual FileInfo getLinkInfo(const std::string& path) override {
    return FileInfo::getInfoForPath(path, /*isLink:*/ true);
  }
};
  
}

std::unique_ptr<FileSystem> basic::createLocalFileSystem() {
  return llvm::make_unique<LocalFileSystem>();
}
