//===-- BuildExecutionQueue.cpp -------------------------------------------===//
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

#include "llbuild/BuildSystem/BuildExecutionQueue.h"
#include "llbuild/BuildSystem/CommandResult.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"

#include <vector>

using namespace llbuild;
using namespace llbuild::buildsystem;

BuildExecutionQueue::BuildExecutionQueue(BuildExecutionQueueDelegate& delegate)
    : delegate(delegate)
{
}

BuildExecutionQueue::~BuildExecutionQueue() {
}

CommandResult BuildExecutionQueue::executeProcess(
    QueueJobContext* context, ArrayRef<StringRef> commandLine) {
  return executeProcess(context, commandLine, {});
}

bool BuildExecutionQueue::executeShellCommand(QueueJobContext* context,
                                              StringRef command) {
  SmallString<1024> commandStorage(command);
  std::vector<StringRef> commandLine(
      { "/bin/sh", "-c", commandStorage.c_str() });
  return executeProcess(context, commandLine) == CommandResult::Succeeded;
}

BuildExecutionQueueDelegate::~BuildExecutionQueueDelegate() {
}
