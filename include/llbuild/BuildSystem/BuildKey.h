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
    key.reserve(str.size() + 1);
    key.push_back(kindCode);
    key.append(str.begin(), str.end());
  }

public:
  /// @name Construction Functions
  /// @{

  static BuildKey makeCommand(StringRef name) {
    return BuildKey('C', name);
  }

  static BuildKey makeNode(StringRef path) {
    return BuildKey('N', path);
  }

  static BuildKey makeNode(const Node* node) {
    return BuildKey('N', node->getName());
  }

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
    default:
      return Kind::Unknown;
    }
  }

  bool isCommand() const { return getKind() == Kind::Command; }
  bool isNode() const { return getKind() == Kind::Node; }
  bool isTarget() const { return getKind() == Kind::Target; }

  StringRef getCommandName() const {
    return StringRef(key.data()+1, key.size()-1);
  }

  StringRef getNodeName() const {
    return StringRef(key.data()+1, key.size()-1);
  }

  StringRef getTargetName() const {
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
