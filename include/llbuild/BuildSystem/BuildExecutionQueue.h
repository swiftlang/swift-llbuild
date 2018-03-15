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
#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <functional>

namespace llbuild {
namespace buildsystem {

class BuildExecutionQueueDelegate;
class Command;
enum class CommandResult;

/// Opaque type which allows the queue implementation to maintain additional
/// state and associate subsequent requests (e.g., \see executeProcess()) with
/// the dispatching job.
struct QueueJobContext;

/// Wrapper for individual pieces of work that are added to the execution queue.
///
/// All work on the exeuction queue is always done on behalf of some command in
/// the build system it is executing.
class QueueJob {
  /// The command this job is running on behalf of.
  Command* forCommand = nullptr;

  /// The function to execute to do the work.
  typedef std::function<void(QueueJobContext*)> work_fn_ty;
  work_fn_ty work;
  
public:
  /// Default constructor, for use as a sentinel.
  QueueJob() {}

  /// General constructor.
  QueueJob(Command* forCommand, work_fn_ty work)
      : forCommand(forCommand), work(work) {}

  Command* getForCommand() const { return forCommand; }

  void execute(QueueJobContext* context) { work(context); }
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

  BuildExecutionQueueDelegate& delegate;
  
public:
  BuildExecutionQueue(BuildExecutionQueueDelegate& delegate);
  virtual ~BuildExecutionQueue();

  /// @name Accessors
  /// @{

  BuildExecutionQueueDelegate& getDelegate() { return delegate; }
  const BuildExecutionQueueDelegate& getDelegate() const { return delegate; }

  /// @}

  /// Add a job to be executed.
  virtual void addJob(QueueJob job) = 0;

  /// Cancel all jobs and subprocesses of this queue.
  virtual void cancelAllJobs() = 0;

  /// @name Execution Interfaces
  ///
  /// These are additional interfaces provided by the execution queue which can
  /// be invoked by the individual \see QueueJob::execute() to perform
  /// particular actions. The benefit of delegating to the execution queue to
  /// perform these actions is that the queue can potentially do a better job of
  /// scheduling activities.
  ///
  /// @{

  /// Execute the given command line.
  ///
  /// This will launch and execute the given command line and wait for it to
  /// complete.
  ///
  /// \param context The context object passed to the job's worker function.
  ///
  /// \param commandLine The command line to execute.
  ///
  /// \param environment The environment to launch with.
  ///
  /// \param inheritEnvironment If true, the supplied environment will be
  /// overlayed on top base environment supplied when creating the queue. If
  /// false, only the supplied environment will be passed to the subprocess.
  ///
  /// \returns Result of the process execution.
  //
  // FIXME: This interface will need to get more complicated, and provide the
  // command result and facilities for dealing with the output.
  virtual CommandResult
  executeProcess(QueueJobContext* context,
                 ArrayRef<StringRef> commandLine,
                 ArrayRef<std::pair<StringRef, StringRef>> environment,
                 bool inheritEnvironment = true) = 0;

  /// @}

  /// Execute the given command, using an inherited environment.
  CommandResult executeProcess(QueueJobContext* context,
                      ArrayRef<StringRef> commandLine);

  /// Execute the given command using "/bin/sh".
  ///
  /// This will launch and execute the given command line and wait for it to
  /// complete.
  ///
  /// \param context The context object passed to the job's worker function.
  /// \param command The command to execute.
  /// \returns True on success.
  //
  // FIXME: This interface will need to get more complicated, and provide the
  // command result and facilities for dealing with the output.
  bool executeShellCommand(QueueJobContext* context, StringRef command);

};

/// Delegate interface for execution queue status.
///
/// All delegate interfaces are invoked synchronously by the execution queue,
/// and should defer any long running operations to avoid blocking the queue
/// unnecessarily.
///
/// NOTE: The delegate *MUST* be thread-safe with respect to all calls, which
/// will arrive concurrently and without any specified thread.
class BuildExecutionQueueDelegate {
  // DO NOT COPY
  BuildExecutionQueueDelegate(const BuildExecutionQueueDelegate&)
    LLBUILD_DELETED_FUNCTION;
  void operator=(const BuildExecutionQueueDelegate&)
    LLBUILD_DELETED_FUNCTION;
  BuildExecutionQueueDelegate &operator=(BuildExecutionQueueDelegate&& rhs)
    LLBUILD_DELETED_FUNCTION;

public:
  /// Handle used to communicate information about a launched process.
  struct ProcessHandle {
    /// Opaque ID.
    uintptr_t id;
  };
  
public:
  BuildExecutionQueueDelegate() {}
  virtual ~BuildExecutionQueueDelegate();

  /// Called when a command's job has been started.
  ///
  /// The queue guarantees that any commandStarted() call will be paired with
  /// exactly one \see commandFinished() call.
  //
  // FIXME: We may eventually want to allow the individual job to provide some
  // additional context here, for complex commands.
  //
  // FIXME: Design a way to communicate the "lane" here, for use in "super
  // console" like UIs.
  virtual void commandJobStarted(Command*) = 0;

  /// Called when a command's job has been finished.
  ///
  /// NOTE: This callback is invoked by the quee without any understanding of
  /// how the command is tied to the engine. In particular, it is almost always
  /// the case that the command will have already completed from the perspective
  /// of the low-level engine (and its dependents may have started
  /// executing). Clients which want to understand when a command is complete
  /// before the engine has been notified as such should use \see
  /// BuildSystem::commandFinished().
  virtual void commandJobFinished(Command*) = 0;

  /// Called when a command's job has started executing an external process.
  ///
  /// The queue guarantees that any commandProcessStarted() call will be paired
  /// with exactly one \see commandProcessFinished() call.
  ///
  /// \param handle - A unique handle used in subsequent delegate calls to
  /// identify the process. This handle should only be used to associate
  /// different status calls relating to the same process. It is only guaranteed
  /// to be unique from when it has been provided here to when it has been
  /// provided to the \see commandProcessFinished() call.
  virtual void commandProcessStarted(Command*, ProcessHandle handle) = 0;

  /// Called to report an error in the management of a command process.
  ///
  /// \param handle - The process handle.
  /// \param message - The error message.
  //
  // FIXME: Need to move to more structured error handling.
  virtual void commandProcessHadError(Command*, ProcessHandle handle,
                                      const Twine& message) = 0;

  /// Called to report a command processes' (merged) standard output and error.
  ///
  /// \param handle - The process handle.
  /// \param data - The process output.
  virtual void commandProcessHadOutput(Command*, ProcessHandle handle,
                                       StringRef data) = 0;
  
  /// Called when a command's job has finished executing an external process.
  ///
  /// \param handle - The handle used to identify the process. This handle will
  /// become invalid as soon as the client returns from this API call.
  ///
  /// \param result - Whether the process suceeded, failed or was cancelled.
  /// \param exitStatus - The raw exit status of the process, or -1 if an error
  /// was encountered.
  //
  // FIXME: Need to include additional information on the status here, e.g., the
  // signal status, and the process output (if buffering).
  virtual void commandProcessFinished(Command*, ProcessHandle handle,
                                      CommandResult result,
                                      int exitStatus) = 0;
};

/// Create an execution queue that schedules jobs to individual lanes with a
/// capped limit on the number of concurrent lanes.
BuildExecutionQueue *createLaneBasedExecutionQueue(
    BuildExecutionQueueDelegate& delegate, int numLanes,
    const char* const* environment);

}
}

#endif
