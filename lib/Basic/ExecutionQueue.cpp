//===-- ExecutionQueue.cpp ------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2018 - 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/Basic/ExecutionQueue.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"

#include <future>
#include <vector>

using namespace llbuild;
using namespace llbuild::basic;


JobDescriptor::~JobDescriptor() {
}

QueueJobContext::~QueueJobContext() {
}

ExecutionQueue::ExecutionQueue(ExecutionQueueDelegate& delegate)
  : delegate(delegate)
{
}

ExecutionQueue::~ExecutionQueue() {
}

ProcessStatus ExecutionQueue::executeProcess(QueueJobContext* context,
                                             ArrayRef<StringRef> commandLine) {
  std::promise<ProcessStatus> p;
  auto result = p.get_future();
  executeProcess(context, commandLine, {}, {true},
                 {[&p](ProcessResult result) mutable {
    p.set_value(result.status);
  }});
  return result.get();
}

bool ExecutionQueue::executeShellCommand(QueueJobContext* context,
                                         StringRef command) {
  SmallString<1024> commandStorage(command);
  std::vector<StringRef> commandLine(
#if defined(_WIN32)
      {"C:\\windows\\system32\\cmd.exe", "/C", commandStorage.c_str()});
#else
      {DefaultShellPath, "-c", commandStorage.c_str()});
#endif
  return executeProcess(context, commandLine) == ProcessStatus::Succeeded;
}

ExecutionQueueDelegate::~ExecutionQueueDelegate() {
}


