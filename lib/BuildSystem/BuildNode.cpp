//===-- BuildNode.cpp -----------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2019 Apple Inc. and the Swift project authors
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
#include "llbuild/BuildSystem/Command.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Twine.h"

using namespace llbuild;
using namespace llbuild::basic;
using namespace llbuild::buildsystem;

bool BuildNode::configureAttribute(const ConfigureContext& ctx, StringRef name,
                                   StringRef value) {
  if (name == "type") {
    if (value == "plain") {
      type = NodeType::Plain;
    } else if (value == "directory") {
      type = NodeType::Plain;
    } else if (value == "directory-structure") {
      type = NodeType::DirectoryStructure;
    } else if (value == "virtual") {
      type = NodeType::Virtual;
    } else {
      ctx.error("invalid value: '" + value + "' for attribute '"
                + name + "'");
      return false;
    }
    return true;
  } else if (name == "is-directory") { // Note: Deprecated in favor of 'type'.
    if (value == "true") {
      type = NodeType::Directory;
    } else if (value == "false") {
      if (type == NodeType::Directory)
        type = NodeType::Plain;
    } else {
      ctx.error("invalid value: '" + value + "' for attribute '"
                + name + "'");
      return false;
    }
    return true;
  } else if (name == "is-directory-structure") { // Note: Deprecated in favor of 'type'.
    if (value == "true") {
      type = NodeType::DirectoryStructure;
    } else if (value == "false") {
      if (type == NodeType::DirectoryStructure)
        type = NodeType::Plain;
    } else {
      ctx.error("invalid value: '" + value + "' for attribute '"
                + name + "'");
      return false;
    }
    return true;
  } else if (name == "is-virtual") { // Note: Deprecated in favor of 'type'.
    if (value == "true") {
      type = NodeType::Virtual;
    } else if (value == "false") {
      if (type == NodeType::Virtual)
        type = NodeType::Plain;
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
      type = NodeType::Virtual;
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
  } else if (name == "content-exclusion-patterns") {
    exclusionPatterns = basic::StringList(value);
    return true;
  }
    
  // We don't support any other custom attributes.
  ctx.error("unexpected attribute: '" + name + "'");
  return false;
}

bool BuildNode::configureAttribute(const ConfigureContext& ctx, StringRef name,
                                   ArrayRef<StringRef> values) {
  if (name == "content-exclusion-patterns") {
    exclusionPatterns = basic::StringList(values);
    return true;
  }

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

basic::CommandSignature BuildNode::getSignature() const {
  basic::CommandSignature sig;
  sig.combine(static_cast<unsigned int>(type));
  // We include the name of all producer rules in the signature to ensure that
  // we properly pick up changes in build graph structure.  For example, a node
  // that was previously a plain input that has changed to become a produced
  // node.
  for (auto* producer : getProducers()) {
    sig.combine(producer->getName());
  }
  return sig;
}


FileInfo StatNode::getFileInfo(basic::FileSystem& fileSystem) const {
  return fileSystem.getFileInfo(name);
}

FileInfo StatNode::getLinkInfo(basic::FileSystem& fileSystem) const {
  return fileSystem.getLinkInfo(name);
}
