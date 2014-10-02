//===- DependencyNode.h ----------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_CORE_DEPENDENCYNODE_H
#define LLBUILD_CORE_DEPENDENCYNODE_H

#include "llbuild/Core/DependencyGraph.h"

#include <string>
#include <vector>

namespace llbuild {
namespace core {

class DependencyNode
{
  public:
    /// Small positive integer that uniquely identifies a node in the graph.
    typedef uint32_t Number;

  private:
    /// Our number (immutable, and unique in the dependency graph).
    Number  _number;
    
    /// Our name (immutable, and never empty).
    std::string  _name;

  public:
    explicit DependencyNode(Number number, std::string name);
    ~DependencyNode();
    
    Number number() const { return _number; }
    
    std::string name() const { return _name; }
};

}
}

#endif
