//===- BuildNode.h ----------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BUILDSYSTEM_BUILDNODE_H
#define LLBUILD_BUILDSYSTEM_BUILDNODE_H

#include "BuildDescription.h"

#include "llbuild/Basic/LLVM.h"
#include "llbuild/BuildSystem/BuildFile.h"

#include "llvm/ADT/StringRef.h"

namespace llbuild {
namespace basic {
  
struct FileInfo;
class FileSystem;

}
  
namespace buildsystem {
  
// FIXME: Figure out how this is going to be organized.
class BuildNode : public Node {
  /// Whether or not this node represents a full directory.
  //
  // FIXME: We need a type enumeration here.
  bool directory;

  /// Whether or not this node represents the full directory structure.
  //
  // FIXME: We need a type enumeration here.
  bool directoryStructure;
  
  /// Whether or not this node is "virtual" (i.e., not a filesystem path).
  bool virtualNode;

  /// Whether this node represents a "command timestamp".
  ///
  /// Such nodes should always also be virtual.
  bool commandTimestamp;

  /// Whether this node is mutated by the build.
  ///
  /// This flag cannot currently be honored to provide a strongly consistent
  /// build, but it is used to detect when the file system information on a node
  /// cannot be safely used to track *output* file state.
  bool mutated;

public:
  explicit BuildNode(StringRef name, bool isDirectory,
                     bool isDirectoryStructure, bool isVirtual,
                     bool isCommandTimestamp, bool isMutated)
      : Node(name), directory(isDirectory),
        directoryStructure(isDirectoryStructure), virtualNode(isVirtual),
        commandTimestamp(isCommandTimestamp), mutated(isMutated) {}

  /// Check whether this is a "virtual" (non-filesystem related) node.
  bool isVirtual() const { return virtualNode; }

  /// Check whether this node is intended to represent a directory's contents
  /// recursively.
  bool isDirectory() const { return directory; }

  /// Check whether this node is intended to represent a directory's structure
  /// recursively.
  bool isDirectoryStructure() const { return directoryStructure; }

  bool isCommandTimestamp() const { return commandTimestamp; }

  bool isMutated() const { return mutated; }

  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  StringRef value) override;
  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  ArrayRef<StringRef> values) override;
  virtual bool configureAttribute(
      const ConfigureContext& ctx, StringRef name,
      ArrayRef<std::pair<StringRef, StringRef>> values) override;

  basic::FileInfo getFileInfo(basic::FileSystem&) const;
  basic::FileInfo getLinkInfo(basic::FileSystem&) const;
};

}
}

#endif
