//===- unittests/Core/DependencyGraphTest.cpp -----------------------------===//
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

#include "gtest/gtest.h"

using namespace llbuild;

namespace {

TEST(DependencyGraphTest, Basic)
{
    core::DependencyGraph * gph = new core::DependencyGraph();
    std::cout << gph << std::endl;
    core::DependencyNode * nd1 = gph->makeNode("/");
    std::cout << nd1 << std::endl;
    core::DependencyNode * nd2 = gph->makeNode("abc");
    std::cout << nd2 << std::endl;
    core::DependencyNode * nd3 = gph->makeNode("/");
    std::cout << nd3 << std::endl;
    core::DependencyCommand * cmd1 = gph->makeCommand([](){});
    std::cout << cmd1 << std::endl;
    core::DependencyCommand * cmd2 = gph->makeCommand([](){
        std::cout << "Hello" << std::endl;
    });
    std::cout << cmd2 << "\n";
    (*cmd2)();
    delete gph;
}

}
