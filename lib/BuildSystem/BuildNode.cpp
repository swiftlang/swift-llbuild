//===-- BuildNode.cpp -----------------------------------------------------===//
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

#include "llbuild/BuildSystem/BuildNode.h"

#include "llbuild/Basic/FileInfo.h"
#include "llbuild/BuildSystem/BuildFile.h"

#include "llvm/ADT/Twine.h"

using namespace llbuild;
using namespace llbuild::basic;
using namespace llbuild::buildsystem;

bool BuildNode::configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  StringRef value) {
  if (name == "is-virtual") {
    if (value == "true") {
      virtualNode = true;
    } else if (value == "false") {
      virtualNode = false;
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

FileInfo BuildNode::getFileInfo() const {
  assert(!isVirtual());
  return FileInfo::getInfoForPath(getName());
}
