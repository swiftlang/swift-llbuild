//===- Subprocess.h ---------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2018 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_BASIC_SUBPROCESS_H
#define LLBUILD_BASIC_SUBPROCESS_H

#include "llbuild/Basic/Compiler.h"
#include "llbuild/Basic/CrossPlatformCompatibility.h"
#include "llbuild/Basic/LLVM.h"
#include "llbuild/Basic/POSIXEnvironment.h"

#include "llvm/ADT/Optional.h"

#include <inttypes.h>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace llbuild {
  namespace basic {

    // MARK: Quality of Service

    enum class QualityOfService {
      /// A default quality of service (i.e. what the system would use without
      /// other advisement, generally this would be comparable to what would be
      /// done by `make`, `ninja`, etc.)
      Normal,

      /// User-initiated, high priority work.
      UserInitiated,

      /// Batch work performed on behalf of the user.
      Utility,

      /// Background work that is not directly visible to the user.
      Background
    };

    QualityOfService getDefaultQualityOfService();
    void setDefaultQualityOfService(QualityOfService level);

    void setCurrentThreadQualityOfService(QualityOfService level);


    // MARK: Process Info

    /// Handle used to communicate information about a launched process.
    struct ProcessHandle {
      /// Opaque ID.
      uint64_t id;
    };

    struct ProcessInfo {
      /// Whether the process can be safely interrupted.
      bool canSafelyInterrupt;
    };


    // MARK: Process Group

    class ProcessGroup {
      ProcessGroup(const ProcessGroup&) LLBUILD_DELETED_FUNCTION;
      void operator=(const ProcessGroup&) LLBUILD_DELETED_FUNCTION;
      ProcessGroup& operator=(ProcessGroup&&) LLBUILD_DELETED_FUNCTION;

      std::unordered_map<llbuild_pid_t, ProcessInfo> processes;
      std::condition_variable processesCondition;
      bool closed = false;

    public:
      ProcessGroup() {}
      ~ProcessGroup();

      std::mutex mutex;

      void close() { closed = true; }
      bool isClosed() const { return closed; }

      void add(std::lock_guard<std::mutex>&& lock, llbuild_pid_t pid,
               ProcessInfo info) {
        processes.emplace(std::make_pair(pid, info));
      }

      void remove(llbuild_pid_t pid) {
        {
          std::lock_guard<std::mutex> lock(mutex);
          processes.erase(pid);
        }
        processesCondition.notify_all();
      }

      void signalAll(int signal);
    };


    // MARK: Process Execution

    /// Status of a process execution.
    enum class ProcessStatus {
      Succeeded = 0,
      Failed,
      Cancelled,
      Skipped,
    };

    /// Result of a process execution.
    struct ProcessResult {

      /// The final status of the command
      ProcessStatus status;

      /// Process exit code
      int exitCode;

      /// Process identifier (can be -1 for failure reasons)
      llbuild_pid_t pid;

      /// User time (in us)
      uint64_t utime;

      /// System time (in us)
      uint64_t stime;

      /// Max RSS (in bytes)
      uint64_t maxrss;

      ProcessResult(ProcessStatus status, int exitCode = -1,
                    llbuild_pid_t pid = (llbuild_pid_t)-1, uint64_t utime = 0,
                    uint64_t stime = 0, uint64_t maxrss = 0)
          : status(status), exitCode(exitCode), pid(pid), utime(utime),
            stime(stime), maxrss(maxrss) {}

      static ProcessResult makeFailed(int exitCode = -1) {
        return ProcessResult(ProcessStatus::Failed, exitCode);
      }

      static ProcessResult makeCancelled(int exitCode = -1) {
        return ProcessResult(ProcessStatus::Cancelled, exitCode);
      }
    };


    typedef std::function<void(std::function<void()>&&)> ProcessReleaseFn;
    typedef std::function<void(ProcessResult)> ProcessCompletionFn;


    /// Opaque context passed on to the delegate
    struct ProcessContext;

    /// Delegate interface for process execution.
    ///
    /// All delegate interfaces are invoked synchronously by the subprocess
    /// methods (as called by the execution queue, for example) and should defer
    /// any long running operations to avoid blocking the queue unnecessarily.
    ///
    /// NOTE: The delegate *MUST* be thread-safe with respect to all calls,
    /// which will arrive concurrently and without any specified thread.
    class ProcessDelegate {
      // DO NOT COPY
      ProcessDelegate(const ProcessDelegate&) LLBUILD_DELETED_FUNCTION;
      void operator=(const ProcessDelegate&) LLBUILD_DELETED_FUNCTION;
      ProcessDelegate& operator=(ProcessDelegate&& rhs) LLBUILD_DELETED_FUNCTION;

    public:
      ProcessDelegate() {}
      virtual ~ProcessDelegate();

      /// Called when the external process has started executing.
      ///
      /// The subprocess code guarantees that any processStarted() call will be
      /// paired with exactly one \see processFinished() call.
      ///
      /// \param ctx - Opaque context passed on to the delegate
      /// \param handle - A unique handle used in subsequent delegate calls to
      /// identify the process. This handle should only be used to associate
      /// different status calls relating to the same process. It is only
      /// guaranteed to be unique from when it has been provided here to when it
      /// has been provided to the \see processFinished() call.
      virtual void processStarted(ProcessContext* ctx, ProcessHandle handle) = 0;

      /// Called to report an error in the management of a command process.
      ///
      /// \param ctx - Opaque context passed on to the delegate
      /// \param handle - The process handle.
      /// \param message - The error message.
      //
      // FIXME: Need to move to more structured error handling.
      virtual void processHadError(ProcessContext* ctx, ProcessHandle handle,
                                   const Twine& message) = 0;

      /// Called to report a command processes' (merged) standard output and error.
      ///
      /// \param ctx - Opaque context passed on to the delegate
      /// \param handle - The process handle.
      /// \param data - The process output.
      virtual void processHadOutput(ProcessContext* ctx, ProcessHandle handle,
                                    StringRef data) = 0;

      /// Called when a command's job has finished executing an external process.
      ///
      /// \param ctx - Opaque context passed on to the delegate
      /// \param handle - The handle used to identify the process. This handle
      ///  will become invalid as soon as the client returns from this API call.
      /// \param result - Whether the process suceeded, failed or was cancelled.
      //
      // FIXME: Need to include additional information on the status here, e.g., the
      // signal status, and the process output (if buffering).
      virtual void processFinished(ProcessContext* ctx, ProcessHandle handle,
                                   const ProcessResult& result) = 0;
    };


    struct ProcessAttributes {
      /// If true, whether it is safe to attempt to SIGINT the process to cancel
      /// it. If false, the process won't be interrupted during cancellation and
      /// will be given a chance to complete (if it fails to complete it will
      /// ultimately be sent a SIGKILL).
      bool canSafelyInterrupt;

      /// If set, the working directory to change into before spawning (only
      /// supported on macOS)
      StringRef workingDir = {};

      /// If true, exposes a control file descriptor that may be used to
      /// communicate with the build system.
      bool controlEnabled = true;
    };

    /// Execute the given command line.
    ///
    /// This will launch and execute the given command line and wait for it to
    /// complete or release its execution lane.
    ///
    /// \param delegate The process delegate.
    ///
    /// \param ctx The context object passed to the delegate.
    ///
    /// \param pgrp The process group in which to track this process.
    ///
    /// \param handle The handle object passed to the delegate.
    ///
    /// \param commandLine The command line to execute.
    ///
    /// \param environment The environment to launch with.
    ///
    /// \param attributes Additional attributes for the process to be spawned.
    ///
    /// \param releaseFn Functional called when a process wishes to release its
    /// exclusive access to build system resources (namely an execution lane).
    ///
    /// \param completionFn An optional function that, if supplied, will be run
    /// following the completion of the process. This may be run asynchronously
    /// from another thread if the executed process asks the system to release
    /// its execution lane. Callers should put cleanup and notification work
    /// here.
    ///
    //
    // FIXME: This interface will need to get more complicated, and provide the
    // command result and facilities for dealing with the output.
    void spawnProcess(ProcessDelegate& delegate,
                      ProcessContext* ctx,
                      ProcessGroup& pgrp,
                      ProcessHandle handle,
                      ArrayRef<StringRef> commandLine,
                      POSIXEnvironment environment,
                      ProcessAttributes attributes,
                      ProcessReleaseFn&& releaseFn,
                      ProcessCompletionFn&& completionFn);

    /// @}

  }
}

#endif
