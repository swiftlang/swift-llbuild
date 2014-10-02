//===-- DependencyNode.cpp ------------------------------------------------===//
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

DependencyNode::DependencyNode(Number number, std::string name)
    : _number(number), _name(name)
{
    std::cout << __PRETTY_FUNCTION__ << std::endl;
}

DependencyNode::~DependencyNode()
{
    std::cout << __PRETTY_FUNCTION__ << std::endl;
}
