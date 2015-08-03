//===- BuildExecutionQueue.h ------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BUILDSYSTEM_BUILDEXECUTIONQUEUE_H
#define LLBUILD_BUILDSYSTEM_BUILDEXECUTIONQUEUE_H

#include "llbuild/Basic/Compiler.h"

#include <functional>

namespace llbuild {
namespace buildsystem {

class Command;

/// Wrapper for individual pieces of work that are added to the execution queue.
///
/// All work on the exeuction queue is always done on behalf of some command in
/// the build system it is executing.
class QueueJob {
  /// The command this job is running on behalf of.
  Command* forCommand = nullptr;

  /// The function to execute to do the work.
  std::function<void()> work;
  
public:
  /// Default constructor, for use as a sentinel.
  QueueJob() {}

  /// General constructor.
  QueueJob(Command* forCommand, std::function<void()> work)
      : forCommand(forCommand), work(work) {}

  /// Support copy and move.
  QueueJob(const QueueJob& rhs) : forCommand(rhs.forCommand), work(rhs.work) {}
  QueueJob(QueueJob&& rhs)
      : forCommand(rhs.forCommand), work(std::move(rhs.work)) {
    rhs.forCommand = nullptr;
  }
  void operator=(const QueueJob& rhs) {
    if (this != &rhs) {
      forCommand = std::move(rhs.forCommand);
      work = std::move(rhs.work);
    }
  }
  QueueJob& operator=(QueueJob&& rhs) {
    if (this != &rhs) {
      forCommand = rhs.forCommand;
      work = rhs.work;
    }
    return *this;
  }

  Command* getForCommand() const { return forCommand; }

  void execute() { work(); }
};

/// This abstact class encapsulates the interface needed by the build system for
/// contributing work which needs to be executed to perform a particular build.
class BuildExecutionQueue {
  // DO NOT COPY
  BuildExecutionQueue(const BuildExecutionQueue&)
    LLBUILD_DELETED_FUNCTION;
  void operator=(const BuildExecutionQueue&)
    LLBUILD_DELETED_FUNCTION;
  BuildExecutionQueue &operator=(BuildExecutionQueue&& rhs)
    LLBUILD_DELETED_FUNCTION;
  
public:
  BuildExecutionQueue() {}
  virtual ~BuildExecutionQueue();

  /// Add a job to be executed.
  virtual void addJob(QueueJob job) = 0;
};

}
}

#endif
