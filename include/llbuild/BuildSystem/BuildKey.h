//===- BuildKey.h -----------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BUILDSYSTEM_BUILDKEY_H
#define LLBUILD_BUILDSYSTEM_BUILDKEY_H

#include "llbuild/Basic/Compiler.h"
#include "llbuild/Basic/LLVM.h"
#include "llbuild/Core/BuildEngine.h"

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

    /// A key used to identify a node.
    Node,

    /// A key used to identify a target.
    Target,

    /// An invalid key kind.
    Unknown,
  };

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
  BuildKey(char kindCode, StringRef commandName, StringRef taskName,
           StringRef taskData) {
    // FIXME: We need good support infrastructure for binary encoding.
    uint32_t commandNameSize = commandName.size();
    uint32_t taskNameSize = taskName.size();
    uint32_t taskDataSize = taskData.size();
    key.resize(1 + 2*sizeof(uint32_t) + commandNameSize + taskNameSize +
               taskDataSize);
    uint32_t pos = 0;
    key[pos] = kindCode; pos += 1;
    memcpy(&key[pos], &commandNameSize, sizeof(uint32_t));
    pos += sizeof(uint32_t);
    memcpy(&key[pos], &taskNameSize, sizeof(uint32_t));
    pos += sizeof(uint32_t);
    memcpy(&key[pos], commandName.data(), commandNameSize);
    pos += commandNameSize;
    memcpy(&key[pos], taskName.data(), taskNameSize);
    pos += taskNameSize;
    memcpy(&key[pos], taskData.data(), taskDataSize);
    pos += taskDataSize;
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
  static BuildKey makeCustomTask(StringRef commandName, StringRef taskName,
                                 StringRef taskData) {
    return BuildKey('X', commandName, taskName, taskData);
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
    case 'N': return Kind::Node;
    case 'T': return Kind::Target;
    case 'X': return Kind::CustomTask;
    default:
      return Kind::Unknown;
    }
  }

  bool isCommand() const { return getKind() == Kind::Command; }
  bool isNode() const { return getKind() == Kind::Node; }
  bool isCustomTask() const { return getKind() == Kind::CustomTask; }
  bool isTarget() const { return getKind() == Kind::Target; }

  StringRef getCommandName() const {
    assert(isCommand());
    return StringRef(key.data()+1, key.size()-1);
  }

  StringRef getCustomTaskCommandName() const {
    assert(isCustomTask());
    uint32_t commandSize;
    memcpy(&commandSize, &key[1], sizeof(uint32_t));
    return StringRef(&key[1 + 2*sizeof(uint32_t)], commandSize);
  }

  StringRef getCustomTaskName() const {
    assert(isCustomTask());
    uint32_t commandSize;
    memcpy(&commandSize, &key[1], sizeof(uint32_t));
    uint32_t taskNameSize;
    memcpy(&taskNameSize, &key[1 + sizeof(uint32_t)], sizeof(uint32_t));
    return StringRef(&key[1 + 2*sizeof(uint32_t) + commandSize], taskNameSize);
  }

  StringRef getCustomTaskData() const {
    assert(isCustomTask());
    uint32_t commandSize;
    memcpy(&commandSize, &key[1], sizeof(uint32_t));
    uint32_t taskNameSize;
    memcpy(&taskNameSize, &key[1 + sizeof(uint32_t)], sizeof(uint32_t));
    uint32_t taskDataSize =
      key.size() - 1 - 2*sizeof(uint32_t) - commandSize - taskNameSize;
    return StringRef(&key[1 + 2*sizeof(uint32_t) + commandSize + taskNameSize],
                     taskDataSize);
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
};

}
}

#endif
