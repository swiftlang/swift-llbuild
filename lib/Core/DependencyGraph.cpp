//===-- DependencyGraph.cpp -----------------------------------------------===//
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

#include "llbuild/Core/DependencyGraph.h"

#include "llbuild/Core/DependencyNode.h"
#include "llbuild/Core/DependencyCommand.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>

using namespace llbuild;
using namespace llbuild::core;

DependencyGraph::DependencyGraph()
{
}

DependencyGraph::~DependencyGraph()
{
    // The 'commands' vector is the one data structure that owns the commands.
    for (auto & c : _commands) {
        delete c;
    }
    _commands.clear();
    
    // The 'namesToNodes' map doesn't own the nodes.
    _namesToNodes.clear();

    // The 'nodes' vector is the one data structure that owns the nodes.
    for (auto & n : _nodes) {
        delete n;
    }
    _nodes.clear();
}

DependencyNode * DependencyGraph::makeNode(std::string name)
{
    // Look up the node, creating a slot for it if needed.
    DependencyNode * & result = _namesToNodes[name];
    if (result == nullptr) {
        // Create a new node, giving it the next available number.  Note that
        // 'result' is a reference, so assigning the new node pointer to it
        // puts it into the table.
        result = new DependencyNode(_nodes.size(), name);
        
        // Add the pointer to the 'nodes' vector, which is what owns it.
        _nodes.push_back(result);
    }
    return result;
}

DependencyNode * DependencyGraph::findNode(std::string name)
{
    // Look up the node, returning nullptr if it isn't found.
    const auto result = _namesToNodes.find(name);
    return (result == _namesToNodes.end()) ? nullptr : result->second;
}

DependencyCommand * DependencyGraph::makeCommand(const std::function<void(void)> & func)
{
    // Create a new command, giving it the next available number.
    DependencyCommand * result = new DependencyCommand(_commands.size(), func);
    
    // Add the pointer to the 'command' vector, which is what owns it.
    _commands.push_back(result);
    
    // Finally return it.
    return result;
}
