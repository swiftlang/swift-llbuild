//===- DependencyCommand.h --------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_CORE_DEPENDENCYCOMMAND_H
#define LLBUILD_CORE_DEPENDENCYCOMMAND_H

#include "llbuild/Core/DependencyGraph.h"

#include <string>
#include <functional>
#include <vector>

namespace llbuild {
namespace core {

class DependencyCommand
{
  public:
    /// Small positive integer that uniquely identifies a command in the graph.
    typedef uint32_t Number;

  private:
    /// Our number (immutable, and unique in the dependency graph).
    Number  _number;
    
    /// Our function or closure (immutable).
    std::function<void(void)>  _function;

  public:
    // public methods etc
    explicit DependencyCommand(Number number, const std::function<void(void)> & func);
    ~DependencyCommand();
    
    Number number() const { return _number; }
    
    void operator()(void) { _function(); }
};

}
}

#endif
