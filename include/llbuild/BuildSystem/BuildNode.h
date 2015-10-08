//===- BuildNode.h ----------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BUILDSYSTEM_BUILDNODE_H
#define LLBUILD_BUILDSYSTEM_BUILDNODE_H

#include "llbuild/Basic/LLVM.h"
#include "llbuild/BuildSystem/BuildFile.h"

#include "llvm/ADT/StringRef.h"

namespace llbuild {
namespace basic {
  
struct FileInfo;
  
}
  
namespace buildsystem {
  
// FIXME: Figure out how this is going to be organized.
class BuildNode : public Node {
  /// Whether or not this node is "virtual" (i.e., not a filesystem path).
  bool virtualNode;

public:
  explicit BuildNode(StringRef name, bool isVirtual)
      : Node(name), virtualNode(isVirtual) {}

  bool isVirtual() const { return virtualNode; }

  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  StringRef value) override;

  basic::FileInfo getFileInfo() const;
};

}
}

#endif
