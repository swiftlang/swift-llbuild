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

#include "llbuild/Basic/CrossPlatformCompatibility.h"
#include <inttypes.h>

#include <functional>

namespace llbuild {
namespace buildsystem {

/// Result of a command execution.
enum class CommandResult {
  Succeeded = 0,
  Failed,
  Cancelled,
  Skipped,
};

typedef std::function<void(CommandResult)> CommandCompletionFn;


/// Extended result of a command execution.
struct CommandExtendedResult {
  CommandResult result; /// The final status of the command
  int exitStatus;       /// The exit code

  uint64_t utime;     /// User time (in us)
  uint64_t stime;     /// Sys time (in us)
  uint64_t maxrss;    /// Max RSS (in bytes)
  llbuild_pid_t pid;  /// Process identifier (can be -1 for failure reasons)

  CommandExtendedResult(CommandResult result, int exitStatus, llbuild_pid_t pid, uint64_t utime = 0,
                uint64_t stime = 0, uint64_t maxrss = 0)
    : result(result), exitStatus(exitStatus), utime(utime)
    , stime(stime), maxrss(maxrss), pid(pid)
  {}

  static CommandExtendedResult makeFailed(int exitStatus = -1) {
    return CommandExtendedResult(CommandResult::Failed, exitStatus, -1);
  }

  static CommandExtendedResult makeCancelled(int exitStatus = -1) {
    return CommandExtendedResult(CommandResult::Cancelled, exitStatus, -1);
  }

};

}
}

#endif
