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
#include <functional>
#include <iostream>
#include <iomanip>
#include <sstream>

using namespace llbuild;
using namespace llbuild::core;

DependencyCommand::DependencyCommand(Number number, const std::function<void(void)> & func)
    : _number(number), _function(func)
{
    std::cout << __PRETTY_FUNCTION__ << std::endl;
}

DependencyCommand::~DependencyCommand()
{
    std::cout << __PRETTY_FUNCTION__ << std::endl;
}
