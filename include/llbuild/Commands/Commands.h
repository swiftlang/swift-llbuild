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

#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/StringRef.h"

#include <string>
#include <vector>

namespace llbuild {
namespace commands {

/// Register the program name.
void setProgramName(StringRef name);

/// Get the registered program name.
const char* getProgramName();

int executeNinjaCommand(const std::vector<std::string> &args);
int executeBuildEngineCommand(const std::vector<std::string> &args);
int executeBuildSystemCommand(const std::vector<std::string> &args);

}
}

#endif
