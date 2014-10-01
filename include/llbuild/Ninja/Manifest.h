//===- Manifest.h -----------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_NINJA_MANIFEST_H
#define LLBUILD_NINJA_MANIFEST_H

#include <string>
#include <unordered_map>
namespace llbuild {
namespace ninja {

/// This class represents a set of name to value variable bindings.
class BindingSet {
public:
  /// The parent binding scope, if any.
  BindingSet *ParentScope;

  /// The actual bindings, mapping from Name to Value.
  std::unordered_map<std::string, std::string> Bindings;
};

/// A manifest represents the complete set of rules and commands used to perform
/// a build.
class Manifest {
public:
  /// The top level variable bindings.
  BindingSet Bindings;
};

}
}

#endif
