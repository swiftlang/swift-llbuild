//===- CommandResult.h -----------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BUILDSYSTEM_COMMAND_RESULT_H
#define LLBUILD_BUILDSYSTEM_COMMAND_RESULT_H

namespace llbuild {
namespace buildsystem {

/// Result of a command execution.
enum class CommandResult {
  Succeeded = 0,
  Failed,
  Cancelled,
  Skipped,
};

}
}

#endif
