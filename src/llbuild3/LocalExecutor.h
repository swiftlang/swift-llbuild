//===- LocalExecutor.h ------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2018 - 2025 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD3_LOCALEXECUTOR_H
#define LLBUILD3_LOCALEXECUTOR_H

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <llbuild3/Result.hpp>

#include "llbuild3/Action.pb.h"
#include "llbuild3/Error.pb.h"

#if defined(_WIN32)
// Ignore the conflicting min/max defined in windows.h
#define NOMINMAX
#include <windows.h>
#else
#include <inttypes.h>
#if __has_include(<sys/cdefs.h>)
#include <sys/cdefs.h>
#endif
#include <sys/resource.h>
#include <unistd.h>
#if defined(__linux__) || defined(__GNU__)
#include <termios.h>
#else
#include <sys/types.h>
#endif // defined(__linux__) || defined(__GNU__)
#endif // _WIN32

namespace llbuild3 {

#if defined(_WIN32)
typedef HANDLE llbuild_pid_t;
typedef HANDLE FD;
typedef int llbuild_rlim_t;
#define PATH_MAX MAX_PATH
#else
typedef pid_t llbuild_pid_t;
typedef int FD;
typedef rlim_t llbuild_rlim_t;
#endif


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

// MARK: Process Execution

/// Status of a process execution.
enum class ProcessStatus {
  Unknown = -1,
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

  ProcessResult(
    ProcessStatus status = ProcessStatus::Unknown,
    int exitCode = -1,
    llbuild_pid_t pid = (llbuild_pid_t)-1,
    uint64_t utime = 0,
    uint64_t stime = 0,
    uint64_t maxrss = 0
  ) : status(status), exitCode(exitCode), pid(pid), utime(utime),
  stime(stime), maxrss(maxrss) { }

  static ProcessResult makeFailed(int exitCode = -1) {
    return ProcessResult(ProcessStatus::Failed, exitCode);
  }

  static ProcessResult makeCancelled(int exitCode = -1) {
    return ProcessResult(ProcessStatus::Cancelled, exitCode);
  }
};

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
  /// \param pid - The subprocess' identifier, can be -1 for failure reasons.
  virtual void processStarted(ProcessContext* ctx, ProcessHandle handle, llbuild_pid_t pid) = 0;

  /// Called to report an error in the management of a command process.
  ///
  /// \param ctx - Opaque context passed on to the delegate
  /// \param handle - The process handle.
  /// \param message - The error message.
  virtual void processHadError(ProcessContext* ctx, ProcessHandle handle,
                               const Error& message) = 0;

  /// Called to report a command processes' (merged) standard output and error.
  ///
  /// \param ctx - Opaque context passed on to the delegate
  /// \param handle - The process handle.
  /// \param data - The process output.
  virtual void processHadOutput(ProcessContext* ctx, ProcessHandle handle,
                                std::string_view data) = 0;

  /// Called when a command's job has finished executing an external process.
  ///
  /// \param ctx - Opaque context passed on to the delegate
  /// \param handle - The handle used to identify the process. This handle
  ///  will become invalid as soon as the client returns from this API call.
  /// \param result - Whether the process succeeded, failed or was cancelled.
  virtual void processFinished(ProcessContext* ctx, ProcessHandle handle,
                               const ProcessResult& result) = 0;
};


typedef std::function<void(ProcessResult)> ProcessCompletionFn;


struct ProcessAttributes {
  /// If true, whether it is safe to attempt to SIGINT the process to cancel
  /// it. If false, the process won't be interrupted during cancellation and
  /// will be given a chance to complete (if it fails to complete it will
  /// ultimately be sent a SIGKILL).
  bool canSafelyInterrupt;

  /// Whether to connect the spawned process directly to the console.
  bool connectToConsole = false;

  /// If set, the working directory to change into before spawning (support
  /// not guaranteed on all platforms).
  std::string workingDir = {};

  /// If true, the supplied environment will be overlayed on top base
  /// environment supplied when creating the queue.
  /// If false, only the supplied environment will be passed
  /// to the subprocess.
  bool inheritEnvironment = true;

  /// If true, exposes a control file descriptor that may be used to
  /// communicate with the build system.
  bool controlEnabled = true;
};

class LocalSandbox {
public:
  virtual ~LocalSandbox() = 0;

  virtual std::filesystem::path workingDir() = 0;

  virtual std::vector<std::pair<std::string, std::string>> environment() = 0;

  virtual std::optional<Error> prepareInput(std::string path, FileType type,
                                            CASID objID) = 0;

  virtual result<std::vector<FileObject>, Error>
  collectOutputs(std::vector<std::string> paths) = 0;

  virtual void release() = 0;
};

class LocalSandboxProvider {
public:
  virtual ~LocalSandboxProvider() = 0;

  virtual result<std::shared_ptr<LocalSandbox>, Error> create(ProcessHandle) = 0;
};



class LocalExecutor {
private:
  void* impl;

  LocalExecutor(const LocalExecutor&) = delete;
  void operator=(const LocalExecutor&) = delete;

public:
  LocalExecutor(std::shared_ptr<LocalSandboxProvider> sandboxProvider);
  ~LocalExecutor();

  result<std::shared_ptr<LocalSandbox>, Error> createSandbox(ProcessHandle);

  void executeProcess(std::vector<std::string_view>& commandLine,
                      std::vector<std::pair<std::string_view, std::string_view>>& environment,
                      ProcessDelegate& delegate,
                      ProcessContext* ctx,
                      ProcessHandle handle,
                      ProcessAttributes attributes = {true},
                      std::optional<ProcessCompletionFn> completionFn = {});

  void cancelAllJobs();
};

}

#endif
