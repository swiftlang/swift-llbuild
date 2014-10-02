//===- DependencyGraph.h ----------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_CORE_DEPENDENCYGRAPH_H
#define LLBUILD_CORE_DEPENDENCYGRAPH_H

#include <unordered_map>
#include <string>
#include <vector>

namespace llbuild {
namespace core {
    
class DependencyNode;
class DependencyCommand;

class DependencyGraph
{
  private:
    /// Stores owned pointers to the nodes in the graph (indexable by the node
    /// number minus one). This is the canonical owner of the Node objects.
    std::vector<DependencyNode *>  _nodes;
    
    /// Maps node names to node pointers.  The nodes themselves are allocated
    /// in the heap.  This map doesn't own the nodes; the 'nodes' vector does.
    std::unordered_map<std::string, DependencyNode *>  _namesToNodes;
    
    /// Stores owned pointers to the commands in the graph (indexable by the
    /// command number minus one).  This is the canonical owner of the Command
    /// objects.
    std::vector<DependencyCommand *>  _commands;

  public:
    /// Constructor and Destructor
    explicit DependencyGraph();
    ~DependencyGraph();
    
    /// Returns either an existing node or a new node with the given name.  If
    /// a new node is created, subsequenct lookups of the same name will return
    /// the same node.  Node names are case sensitive.
    DependencyNode * makeNode(std::string name);
    
    /// Looks up a node by name and returns a pointer to it, or nullptr if not
    /// found.  Node names are case sensitive.
    DependencyNode * findNode(std::string name);
    
    /// Creates and returns a new command.  When run, the command will invoke
    /// the given function or closure.
    DependencyCommand * makeCommand(const std::function<void(void)> & func);
};

}
}

#endif
