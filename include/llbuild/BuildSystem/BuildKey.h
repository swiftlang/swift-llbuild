//===- BuildKey.h -----------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BUILDSYSTEM_BUILDKEY_H
#define LLBUILD_BUILDSYSTEM_BUILDKEY_H

#include "llbuild/Basic/Compiler.h"
#include "llbuild/Basic/LLVM.h"
#include "llbuild/Core/BuildEngine.h"
#include "llbuild/BuildSystem/BuildDescription.h"

#include "llvm/ADT/StringRef.h"

namespace llbuild {
namespace buildsystem {

/// The BuildKey encodes the key space used by the BuildSystem when using the
/// core BuildEngine.
class BuildKey {
  using KeyType = core::KeyType;
  
public:
  enum class Kind {
    /// A key used to identify a command.
    Command,

    /// A key used to identify a custom task.
    CustomTask,

    /// A key used to identify directory contents.
    DirectoryContents,

    /// A key used to identify the signature of a complete directory tree.
    DirectoryTreeSignature,

    /// A key used to identify the signature of a complete directory tree
    /// structure.
    DirectoryTreeStructureSignature,

    /// A key used to identify a node.
    Node,

    /// A key used to identify a target.
    Target,

    /// An invalid key kind.
    Unknown,
  };
  static StringRef stringForKind(Kind);

private:
  /// The actual key data.
  KeyType key;

private:
  BuildKey(const KeyType& key) : key(key) {}
  BuildKey(char kindCode, StringRef str) {
    key.reserve(1 + str.size());
    key.push_back(kindCode);
    key.append(str.begin(), str.end());
  }
  BuildKey(char kindCode, StringRef name, StringRef data) {
    // FIXME: We need good support infrastructure for binary encoding.
    uint32_t nameSize = name.size();
    uint32_t dataSize = data.size();
    key.resize(1 + sizeof(uint32_t) + nameSize + dataSize);
    uint32_t pos = 0;
    key[pos] = kindCode; pos += 1;
    memcpy(&key[pos], &nameSize, sizeof(uint32_t));
    pos += sizeof(uint32_t);
    memcpy(&key[pos], name.data(), nameSize);
    pos += nameSize;
    memcpy(&key[pos], data.data(), dataSize);
    pos += dataSize;
    assert(key.size() == pos);
  }

public:
  /// @name Construction Functions
  /// @{

  /// Create a key for computing a command result.
  static BuildKey makeCommand(StringRef name) {
    return BuildKey('C', name);
  }

  /// Create a key for computing a custom task (manged by a particular command).
  static BuildKey makeCustomTask(StringRef name, StringRef taskData) {
    return BuildKey('X', name, taskData);
  }

  /// Create a key for computing the contents of a directory.
  static BuildKey makeDirectoryContents(StringRef path) {
    return BuildKey('D', path);
  }

  /// Create a key for computing the contents of a directory.
  static BuildKey makeDirectoryTreeSignature(StringRef path) {
    return BuildKey('S', path);
  }

  /// Create a key for computing the structure of a directory.
  static BuildKey makeDirectoryTreeStructureSignature(StringRef path) {
    return BuildKey('s', path);
  }

  /// Create a key for computing a node result.
  static BuildKey makeNode(StringRef path) {
    return BuildKey('N', path);
  }

  /// Create a key for computing a node result.
  static BuildKey makeNode(const Node* node) {
    return BuildKey('N', node->getName());
  }

  /// Createa a key for computing a target.
  static BuildKey makeTarget(StringRef name) {
    return BuildKey('T', name);
  }

  /// @}
  /// @name Accessors
  /// @{

  const KeyType& getKeyData() const { return key; }

  Kind getKind() const {
    switch (key[0]) {
    case 'C': return Kind::Command;
    case 'D': return Kind::DirectoryContents;
    case 'N': return Kind::Node;
    case 'S': return Kind::DirectoryTreeSignature;
    case 's': return Kind::DirectoryTreeStructureSignature;
    case 'T': return Kind::Target;
    case 'X': return Kind::CustomTask;
    default:
      return Kind::Unknown;
    }
  }

  bool isCommand() const { return getKind() == Kind::Command; }
  bool isCustomTask() const { return getKind() == Kind::CustomTask; }
  bool isDirectoryContents() const {
    return getKind() == Kind::DirectoryContents;
  }
  bool isDirectoryTreeSignature() const {
    return getKind() == Kind::DirectoryTreeSignature;
  }
  bool isDirectoryTreeStructureSignature() const {
    return getKind() == Kind::DirectoryTreeStructureSignature;
  }
  bool isNode() const { return getKind() == Kind::Node; }
  bool isTarget() const { return getKind() == Kind::Target; }

  StringRef getCommandName() const {
    assert(isCommand());
    return StringRef(key.data()+1, key.size()-1);
  }

  StringRef getCustomTaskName() const {
    assert(isCustomTask());
    uint32_t nameSize;
    memcpy(&nameSize, &key[1], sizeof(uint32_t));
    return StringRef(&key[1 + sizeof(uint32_t)], nameSize);
  }

  StringRef getCustomTaskData() const {
    assert(isCustomTask());
    uint32_t nameSize;
    memcpy(&nameSize, &key[1], sizeof(uint32_t));
    uint32_t dataSize = key.size() - 1 - sizeof(uint32_t) - nameSize;
    return StringRef(&key[1 + sizeof(uint32_t) + nameSize], dataSize);
  }

  StringRef getDirectoryContentsPath() const {
    assert(isDirectoryContents());
    return StringRef(key.data()+1, key.size()-1);
  }

  StringRef getDirectoryTreeSignaturePath() const {
    assert(isDirectoryTreeSignature());
    return StringRef(key.data()+1, key.size()-1);
  }

  StringRef getDirectoryTreeStructureSignaturePath() const {
    assert(isDirectoryTreeStructureSignature());
    return StringRef(key.data()+1, key.size()-1);
  }

  StringRef getNodeName() const {
    assert(isNode());
    return StringRef(key.data()+1, key.size()-1);
  }

  StringRef getTargetName() const {
    assert(isTarget());
    return StringRef(key.data()+1, key.size()-1);
  }

  /// @}

  /// @name Conversion to core ValueType.
  /// @{

  static BuildKey fromData(const KeyType& key) {
    auto result = BuildKey(key);
    assert(result.getKind() != Kind::Unknown && "invalid key");
    return result;
  }

  const core::KeyType toData() const { return getKeyData(); }

  /// @}

  /// @name Debug Support
  /// @{

  void dump(raw_ostream& OS) const;

  /// @}
};

}
}

#endif
