//===- Commands.h -----------------------------------------------*- C++ -*-===//
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
//
// This header describes the interfaces in the Commands llbuild library, which
// contains all of the command line tool implementations.
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_COMMANDS_H
#define LLBUILD_COMMANDS_H

#include <string>
#include <vector>

namespace llbuild {
namespace commands {

int executeNinjaCommand(const std::vector<std::string> &args);
int executeBuildEngineCommand(const std::vector<std::string> &args);

}
}

#endif
