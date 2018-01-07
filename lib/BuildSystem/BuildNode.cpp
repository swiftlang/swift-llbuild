//===-- BuildNode.cpp -----------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/BuildSystem/BuildNode.h"

#include "llbuild/Basic/FileInfo.h"
#include "llbuild/Basic/FileSystem.h"
#include "llbuild/BuildSystem/BuildFile.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Twine.h"

using namespace llbuild;
using namespace llbuild::basic;
using namespace llbuild::buildsystem;

bool BuildNode::configureAttribute(const ConfigureContext& ctx, StringRef name,
                                   StringRef value) {
  if (name == "is-directory") {
    if (value == "true") {
      directory = true;
      directoryStructure = virtualNode = false;
    } else if (value == "false") {
      directory = false;
    } else {
      ctx.error("invalid value: '" + value + "' for attribute '"
                + name + "'");
      return false;
    }
    return true;
  } else if (name == "is-directory-structure") {
    if (value == "true") {
      directoryStructure = true;
      directory = virtualNode = false;
    } else if (value == "false") {
      directory = false;
    } else {
      ctx.error("invalid value: '" + value + "' for attribute '"
                + name + "'");
      return false;
    }
    return true;
  } else if (name == "is-virtual") {
    if (value == "true") {
      virtualNode = true;
      directory = directoryStructure = false;
    } else if (value == "false") {
      virtualNode = false;
      commandTimestamp = false;
    } else {
      ctx.error("invalid value: '" + value + "' for attribute '"
                + name + "'");
      return false;
    }
    return true;
  } else if (name == "is-command-timestamp") {
    if (value == "true") {
      commandTimestamp = true;
      virtualNode = true;
      directory = directoryStructure = false;
    } else if (value == "false") {
      commandTimestamp = false;
    } else {
      ctx.error("invalid value: '" + value + "' for attribute '"
                + name + "'");
      return false;
    }
    return true;
  } else if (name == "is-mutated") {
    if (value == "true") {
      mutated = true;
    } else if (value == "false") {
      mutated = false;
    } else {
      ctx.error("invalid value: '" + value + "' for attribute '"
                + name + "'");
      return false;
    }
    return true;
  }
    
  // We don't support any other custom attributes.
  ctx.error("unexpected attribute: '" + name + "'");
  return false;
}

bool BuildNode::configureAttribute(const ConfigureContext& ctx, StringRef name,
                                   ArrayRef<StringRef> values) {
  // We don't support any other custom attributes.
  ctx.error("unexpected attribute: '" + name + "'");
  return false;
}

bool BuildNode::configureAttribute(
    const ConfigureContext& ctx, StringRef name,
    ArrayRef<std::pair<StringRef, StringRef>> values) {
  // We don't support any other custom attributes.
  ctx.error("unexpected attribute: '" + name + "'");
  return false;
}

FileInfo BuildNode::getFileInfo(basic::FileSystem& fileSystem) const {
  assert(!isVirtual());
  return fileSystem.getFileInfo(getName());
}

FileInfo BuildNode::getLinkInfo(basic::FileSystem& fileSystem) const {
  assert(!isVirtual());
  return fileSystem.getLinkInfo(getName());
}
