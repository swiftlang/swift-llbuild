//===-- Subprocess.cpp ----------------------------------------------------===//
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

#include "llbuild/Basic/Subprocess.h"

#include "llbuild/Basic/CrossPlatformCompatibility.h"
#include "llbuild/Basic/PlatformUtility.h"
#include "llbuild/Basic/ShellUtility.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Config/config.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Compiler.h"

#include <atomic>
#include <thread>
#include <memory>

#include <fcntl.h>
#if !defined(_WIN32)
#include <poll.h>
#endif
#include <signal.h>
#if defined(_WIN32)
#include <process.h>
#include <psapi.h>
#include <windows.h>
#else
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <pthread/spawn.h>
#include "TargetConditionals.h"
#if !TARGET_OS_IPHONE
extern "C" {
  // Provided by System.framework's libsystem_kernel interface
  extern int __pthread_chdir(const char *path);
  extern int __pthread_fchdir(int fd);
}

/// Set the thread specific working directory to the given path.
int pthread_chdir_np(const char *path)
{
  return __pthread_chdir(path);
}

/// Set the thread specific working directory to that of the given file
/// descriptor. Passing -1 clears the thread specific working directory,
/// returning it to the process level working directory.
int pthread_fchdir_np(int fd)
{
  return __pthread_fchdir(fd);
}
#endif
#endif

#ifndef __GLIBC_PREREQ
#define __GLIBC_PREREQ(maj, min) 0
#endif

#if !defined(_WIN32) && defined(HAVE_POSIX_SPAWN)
// Implementation mostly copied from _CFPosixSpawnFileActionsChdir in swift-corelibs-foundation
static int posix_spawn_file_actions_addchdir_polyfill(posix_spawn_file_actions_t * __restrict file_actions,
                                                      const char * __restrict path) {
#if (defined(__GLIBC__) && !__GLIBC_PREREQ(2, 29)) || (defined(__OpenBSD__)) || (defined(__ANDROID__) && __ANDROID_API__ < 34) || (defined(__QNX__))
  // Glibc versions prior to 2.29 don't support posix_spawn_file_actions_addchdir_np, impacting:
  //  - Amazon Linux 2 (EoL mid-2025)
  // Currently missing as of:
  //  - OpenBSD 7.5 (April 2024)
  //  - QNX 8 (December 2023)
  //  - Android <= 14
  return ENOSYS;
#elif defined(__APPLE__) && defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && __MAC_OS_X_VERSION_MIN_REQUIRED < 101500
  // Conditionally available on macOS if building with a deployment target older than 10.15
  if (__builtin_available(macOS 10.15, *)) {
    return posix_spawn_file_actions_addchdir_np(file_actions, path);
  }
  return ENOSYS;
#elif defined(__GLIBC__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__ANDROID__) || defined(__musl__)
  // Pre-standard posix_spawn_file_actions_addchdir_np version available in:
  //  - Solaris 11.3 (October 2015)
  //  - Glibc 2.29 (February 2019)
  //  - macOS 10.15 (October 2019)
  //  - musl 1.1.24 (October 2019)
  //  - FreeBSD 13.1 (May 2022)
  //  - Android 14 (October 2023)
  return posix_spawn_file_actions_addchdir_np((posix_spawn_file_actions_t *)file_actions, path);
#else
  // Standardized posix_spawn_file_actions_addchdir version (POSIX.1-2024, June 2024) available in:
  //  - Solaris 11.4 (August 2018)
  //  - NetBSD 10.0 (March 2024)
  return posix_spawn_file_actions_addchdir((posix_spawn_file_actions_t *)file_actions, path);
#endif
}
#endif

using namespace llbuild;
using namespace llbuild::basic;

namespace {

  static std::atomic<QualityOfService> defaultQualityOfService{
    QualityOfService::Normal };

#if defined(__APPLE__)
  qos_class_t _getDarwinQOSClass(QualityOfService level) {
    switch (level) {
      case QualityOfService::Normal:
        return QOS_CLASS_DEFAULT;
      case QualityOfService::UserInitiated:
        return QOS_CLASS_USER_INITIATED;
      case QualityOfService::Utility:
        return QOS_CLASS_UTILITY;
      case QualityOfService::Background:
        return QOS_CLASS_BACKGROUND;
      default:
        assert(0 && "unknown command result");
        return QOS_CLASS_DEFAULT;
    }
  }

#endif

}

QualityOfService llbuild::basic::getDefaultQualityOfService() {
  return defaultQualityOfService;
}

void llbuild::basic::setDefaultQualityOfService(QualityOfService level) {
  defaultQualityOfService = level;
}

void llbuild::basic::setCurrentThreadQualityOfService(QualityOfService level) {
#if defined(__APPLE__)
  pthread_set_qos_class_self_np(
      _getDarwinQOSClass(level), 0);
#endif

}

ProcessDelegate::~ProcessDelegate() {
}


ProcessGroup::~ProcessGroup() {
  // Wait for all processes in the process group to terminate
  std::unique_lock<std::mutex> lock(mutex);
  while (!processes.empty()) {
    processesCondition.wait(lock);
  }
}

void ProcessGroup::signalAll(int signal) {
  std::lock_guard<std::mutex> lock(mutex);

  for (const auto& it: processes) {
    // If we are interrupting, only interupt processes which are believed to
    // be safe to interrupt.
    if (signal == SIGINT && !it.second.canSafelyInterrupt)
      continue;

    // We are killing the whole process group here, this depends on us
    // spawning each process in its own group earlier.
#if defined(_WIN32)
    TerminateProcess(it.first, signal);
#else
    ::kill(-it.first, signal);
#endif
  }
}

/// Remember to automatically close the descriptor when it goes out of scope.
/// This helps to keep the file descriptor alive until forwarded to the process.
/// After that we don't need to keep it around.
class ManagedDescriptor {
public:

  /// A short-hand type for a platform-independent descriptor.
  using FileDescriptor = sys::FileDescriptorTraits<>::DescriptorType;

private:

  /// Open the trait namespace to shorten the code.
  using fdTraits = sys::FileDescriptorTraits<>;

  /// Underlying file descriptor.
  FileDescriptor _descriptor = fdTraits::InvalidDescriptor;

#ifndef  NDEBUG
  /// Whether the descriptor has been properly closed.
  bool _closedProperly = true;

  /// File and line number this descriptor was allocated at.
  const char *_file = nullptr;
  unsigned _line = 0;
#endif

public:

  /// Create the descriptor which doesn't describe anything.
  ManagedDescriptor() : _descriptor(fdTraits::InvalidDescriptor) { }

  /// Store and retrieve the source code information.
  /// Useful for tracking leaking descriptors.
#ifndef NDEBUG
  ManagedDescriptor(const char *file, unsigned line)
    : _file(file), _line(line) { (void)_file; (void)_line; }
  const char *file() { return _file; }
  unsigned line() { return _line; }
#else
  ManagedDescriptor(const char *file, unsigned line) { }
  const char *file() { return __FILE__; }
  unsigned line() { return 0; }
#endif

  /// Create the descriptor which autocloses if it goes out of scope.
  ManagedDescriptor(FileDescriptor &fd) { reset(fd); }

  /// Must not ever copy to avoid double-closure.
  ManagedDescriptor(const ManagedDescriptor &) LLBUILD_DELETED_FUNCTION;

  /// Can move descriptors just fine.
  ManagedDescriptor(ManagedDescriptor&& other) {
    _descriptor = other._descriptor;
    other._descriptor = fdTraits::InvalidDescriptor;
#ifndef NDEBUG
    assert(_closedProperly);
    _closedProperly = isValid() ? false : other._closedProperly;
    other._closedProperly = true;
    _file = other._file;
    _line = other._line;
#endif
  }

  /// Close the file descriptor.
  /// Safely autocloses in release mode.
  /// Asserts if the descriptors was about to be autoclosed.
  ~ManagedDescriptor() {
    assert(!isValid() && _closedProperly);
    close();
  }

  /// Copy the underlying descriptor out.
  FileDescriptor unsafeDescriptor() const {
    return _descriptor;
  }

  /// Whether descriptor has been initialized to a valid value and not closed.
  bool isValid() const {
    return fdTraits::IsValid(_descriptor);
  }

  /// Replace the existing descriptor with a given one,
  /// invalidating the passed descriptor.
  ManagedDescriptor &reset(FileDescriptor &fd) {
    close();
    _descriptor = fd;
#ifndef NDEBUG
    _closedProperly = false;
#endif
    fd = fdTraits::InvalidDescriptor;
    return *this;
  }

  /// Set inheritability of a given file descriptor.
  /// true  - Ensure the descriptor is inherited by the child process.
  /// false - Prevent leaking the descriptor into a child process.
  ManagedDescriptor &childMayInherit(bool yes) {
    if (isValid()) {
      return *this;
    }

    auto fd = _descriptor;

#if defined(_WIN32)
    SetHandleInformation(fd, HANDLE_FLAG_INHERIT, yes ? TRUE : FALSE);
#else
    if (yes) {
      fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
    } else {
      fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) & ~FD_CLOEXEC);
    }
#endif
    return *this;
  }

  /// Explicitly close the descriptor.
  bool close() {
    if (!isValid()) {
      return false;
    }

    auto fd = _descriptor;
    _descriptor = fdTraits::InvalidDescriptor;
#ifndef NDEBUG
    _closedProperly = true;
#endif
    fdTraits::Close(fd);
    return true;
  }
};

// Manage the state of a control protocol channel
//
// FIXME: This really should move out of subprocess and up a layer or two. The
// process code should primarily handle reading file descriptors and pushing the
// data up. For now, though, the goal is to move the code out of build system
// and into a reusable layer.
class ControlProtocolState {
  std::string controlID;

  bool negotiated = false;
  std::string partialMsg;
  bool releaseSeen = false;

  const size_t maxLength = 16;

public:
  ControlProtocolState(const std::string& controlID) : controlID(controlID) {}

  /// Reads incoming control message buffer
  ///
  /// \return 0 on success, 1 on completion, -1 on error.
  int read(StringRef buf, std::string* errstr = nullptr) {
    while (buf.size()) {
      size_t nl = buf.find('\n');
      if (nl == StringRef::npos) {
        if (partialMsg.size() + buf.size() > maxLength) {
          // protocol fault, msg length exceeded maximum
          partialMsg.clear();
          if (errstr) {
            *errstr = "excessive message length";
          }
          return -1;
        }

        // incomplete msg, store and continue
        partialMsg += buf.str();
        return 0;
      }

      partialMsg += buf.slice(0, nl);

      if (!negotiated) {
        // negotiate protocol version
        if (partialMsg != "llbuild.1") {
          // incompatible protocol version
          if (errstr) {
            *errstr = "unsupported protocol: " + partialMsg;
          }
          partialMsg.clear();
          return -1;
        }
        negotiated = true;
      } else {
        // check for supported control message
        if (partialMsg == controlID) {
          releaseSeen = true;
        }

        // We halt receiving anything after the first control message
        if (errstr) {
          *errstr = "bad ID";
        }
        partialMsg.clear();
        return 1;
      }

      partialMsg.clear();
      buf = buf.drop_front(nl + 1);
    }
    return 0;
  }

  bool shouldRelease() const { return releaseSeen; }
};

#if !defined(_WIN32) && defined(HAVE_POSIX_SPAWN)
// Helper function to collect subprocess output.
// Consumes and closes the outputPipe descriptor.
static void captureExecutedProcessOutput(ProcessDelegate& delegate,
                                         ManagedDescriptor& outputPipe,
                                         ProcessHandle handle,
                                         ProcessContext* ctx) {
  while (true) {
    char buf[4096];
    ssize_t numBytes =
        sys::FileDescriptorTraits<>::Read(outputPipe.unsafeDescriptor(), buf, sizeof(buf));
    if (numBytes < 0) {
      int err = errno;
      delegate.processHadError(ctx, handle,
                               Twine("unable to read process output (") +
                                   sys::strerror(err) + ")");
      break;
    }

    if (numBytes == 0)
      break;

    // Notify the client of the output.
    delegate.processHadOutput(ctx, handle, StringRef(buf, numBytes));
  }
  // We have receieved the zero byte read that indicates an EOF.
  // Go ahead and close the pipe (it was going to be closed automatically).
  outputPipe.close();
}
#endif

#if defined(_WIN32) || defined(HAVE_POSIX_SPAWN)
// Helper function for cleaning up after a process has finished in
// executeProcess
static void cleanUpExecutedProcess(ProcessDelegate& delegate,
                                   ProcessGroup& pgrp, llbuild_pid_t pid,
                                   ProcessHandle handle, ProcessContext* ctx,
                                   ProcessCompletionFn&& completionFn,
                                   ManagedDescriptor& releaseFd) {
#if defined(_WIN32)
  FILETIME creationTime;
  FILETIME exitTime;
  FILETIME utimeTicks;
  FILETIME stimeTicks;
  int waitResult = WaitForSingleObject(pid, INFINITE);
  int err = GetLastError();
  DWORD exitCode = 0;
  GetExitCodeProcess(pid, &exitCode);

  if (waitResult == WAIT_FAILED || waitResult == WAIT_ABANDONED) {
    releaseFd.close();
    auto result = ProcessResult::makeFailed(exitCode);
    delegate.processHadError(ctx, handle,
                             Twine("unable to wait for process (") +
                                 sys::strerror(GetLastError()) + ")");
    delegate.processFinished(ctx, handle, result);
    completionFn(result);
    return;
  }
#else
  // Wait for the command to complete.
  struct rusage usage;
  int exitCode, result = wait4(pid, &exitCode, 0, &usage);
  while (result == -1 && errno == EINTR)
    result = wait4(pid, &exitCode, 0, &usage);
#endif
  // Close the release pipe
  //
  // Note: We purposely hold this open until after the process has finished as
  // it simplifies client implentation. If we close it early, clients need to be
  // aware of and potentially handle a SIGPIPE.
  releaseFd.close();

  // Update the set of spawned processes.
  pgrp.remove(pid);
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters;
  bool res =
      GetProcessTimes(pid, &creationTime, &exitTime, &stimeTicks, &utimeTicks);
  if (!res) {
    auto result = ProcessResult::makeCancelled();
    delegate.processHadError(ctx, handle,
                             Twine("unable to get statistics for process: ") +
                                 sys::strerror(GetLastError()) + ")");
    delegate.processFinished(ctx, handle, result);
    return;
  }
  // Each tick is 100ns
  uint64_t utime =
      ((uint64_t)utimeTicks.dwHighDateTime << 32 | utimeTicks.dwLowDateTime) /
      10;
  uint64_t stime =
      ((uint64_t)stimeTicks.dwHighDateTime << 32 | stimeTicks.dwLowDateTime) /
      10;
  GetProcessMemoryInfo(pid, &counters, sizeof(counters));

  // We report additional info in the tracing interval
  //   - user time, in µs
  //   - sys time, in µs
  //   - memory usage, in bytes

  // FIXME: We should report a statistic for how much output we read from the
  // subprocess (probably as a new point sample).

  // Notify of the process completion.
  ProcessStatus processStatus =
      (exitCode == 0) ? ProcessStatus::Succeeded : ProcessStatus::Failed;
  ProcessResult processResult(processStatus, exitCode, pid, utime, stime,
                              counters.PeakWorkingSetSize);
#else  // !defined(_WIN32)
  if (result == -1) {
    auto result = ProcessResult::makeFailed(exitCode);
    delegate.processHadError(ctx, handle,
                             Twine("unable to wait for process (") +
                                 strerror(errno) + ")");
    delegate.processFinished(ctx, handle, result);
    completionFn(result);
    return;
  }

  // We report additional info in the tracing interval
  //   - user time, in µs
  //   - sys time, in µs
  //   - memory usage, in bytes
  uint64_t utime = (uint64_t(usage.ru_utime.tv_sec) * 1000000 +
                    uint64_t(usage.ru_utime.tv_usec));
  uint64_t stime = (uint64_t(usage.ru_stime.tv_sec) * 1000000 +
                    uint64_t(usage.ru_stime.tv_usec));

  // FIXME: We should report a statistic for how much output we read from the
  // subprocess (probably as a new point sample).

  // Notify of the process completion.
  bool cancelled = WIFSIGNALED(exitCode) && (WTERMSIG(exitCode) == SIGINT || WTERMSIG(exitCode) == SIGKILL);
  ProcessStatus processStatus = cancelled ? ProcessStatus::Cancelled : (exitCode == 0) ? ProcessStatus::Succeeded : ProcessStatus::Failed;
  ProcessResult processResult(processStatus, exitCode, pid, utime, stime,
                              usage.ru_maxrss);
#endif // else !defined(_WIN32)
  delegate.processFinished(ctx, handle, processResult);
  completionFn(processResult);
}
#endif

#if defined(_WIN32) || defined(HAVE_POSIX_SPAWN)
#if defined(_WIN32)
  using PlatformSpecificPipesConfig = STARTUPINFOW;
#else
  using PlatformSpecificPipesConfig = posix_spawn_file_actions_t;
#endif

/// Create all or no communication pipes.
enum class CommunicationPipesCreationError {
  ERROR_NONE,
  OUTPUT_PIPE_FAILED,
  CONTROL_PIPE_FAILED
};
static std::pair<CommunicationPipesCreationError, int> createCommunicationPipes(const ProcessAttributes &attr,
        PlatformSpecificPipesConfig& pipesConfig,
        ManagedDescriptor& outputPipeParentEnd,
        ManagedDescriptor& outputPipeChildEnd,
        ManagedDescriptor& controlPipeParentEnd,
        ManagedDescriptor& controlPipeChildEnd) {
#if defined(_WIN32)
  STARTUPINFOW& startupInfo = pipesConfig;
  startupInfo.dwFlags = STARTF_USESTDHANDLES;
  if (attr.connectToConsole) {
    // Connect to the current stdout/stderr.
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  } else {
    // Set NUL as stdin
    HANDLE nul =
      CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE outputPipe[2]{NULL, NULL};
    SECURITY_ATTRIBUTES secAttrs{sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    if (CreatePipe(&outputPipe[0], &outputPipe[1], &secAttrs, 0) == 0) {
      return std::make_pair(CommunicationPipesCreationError::OUTPUT_PIPE_FAILED, errno);
    }
    startupInfo.hStdInput = nul;
    startupInfo.hStdOutput = outputPipe[1];
    startupInfo.hStdError = outputPipe[1];
    outputPipeParentEnd.reset(outputPipe[0]).childMayInherit(false);
    outputPipeChildEnd.reset(outputPipe[1]);
  }

  if (attr.controlEnabled) {
    HANDLE controlPipe[2]{NULL, NULL};
    SECURITY_ATTRIBUTES secAttrs{sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    if (CreatePipe(&controlPipe[0], &controlPipe[1], &secAttrs, 0) == 0) {
      outputPipeParentEnd.close();
      outputPipeChildEnd.close();
      return std::make_pair(CommunicationPipesCreationError::OUTPUT_PIPE_FAILED, errno);
    }
    controlPipeParentEnd.reset(controlPipe[0]).childMayInherit(false);
    controlPipeChildEnd.reset(controlPipe[1]);
  }
#else

  posix_spawn_file_actions_t& fileActions = pipesConfig;

  // If we are capturing output, create a pipe and appropriate spawn actions.
  if (attr.connectToConsole) {
#ifdef __APPLE__
    posix_spawn_file_actions_addinherit_np(&fileActions, STDIN_FILENO);
#else
    posix_spawn_file_actions_adddup2(&fileActions, STDIN_FILENO, STDIN_FILENO);
#endif
    // Propagate the current stdout/stderr.
    posix_spawn_file_actions_adddup2(&fileActions, STDOUT_FILENO, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fileActions, STDERR_FILENO, STDERR_FILENO);
  } else {
    // Open /dev/null as stdin.
    posix_spawn_file_actions_addopen(&fileActions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);

    int outputPipe[2]{ -1, -1 };
    if (basic::sys::pipe(outputPipe) < 0) {
      return std::make_pair(CommunicationPipesCreationError::OUTPUT_PIPE_FAILED, errno);
    }
    outputPipeParentEnd.reset(outputPipe[0]).childMayInherit(false);
    outputPipeChildEnd.reset(outputPipe[1]);

    // Open the write end of the pipe as stdout and stderr.
    // The code is safe.
    posix_spawn_file_actions_adddup2(&fileActions, outputPipeChildEnd.unsafeDescriptor(), STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fileActions, outputPipeChildEnd.unsafeDescriptor(), STDERR_FILENO);

    // Close the child end of the pipe known under a different number.
    posix_spawn_file_actions_addclose(&fileActions, outputPipeChildEnd.unsafeDescriptor());
  }

  // Create a pipe for the process to (potentially) release the lane while
  // still running.
  if (attr.controlEnabled) {
    int controlPipe[2]{ -1, -1 };
    if (basic::sys::pipe(controlPipe) < 0) {
      outputPipeParentEnd.close();
      outputPipeChildEnd.close();
      return std::make_pair(CommunicationPipesCreationError::CONTROL_PIPE_FAILED, errno);
    }

    controlPipeParentEnd.reset(controlPipe[0]).childMayInherit(false);
    controlPipeChildEnd.reset(controlPipe[1]);

    // Make sure that the descriptor is properly inherited by the child.
    // The code is safe.
#ifdef __APPLE__
    posix_spawn_file_actions_addinherit_np(&fileActions, controlPipeChildEnd.unsafeDescriptor());
#else
    posix_spawn_file_actions_adddup2(&fileActions, controlPipeChildEnd.unsafeDescriptor(), controlPipeChildEnd.unsafeDescriptor());
#endif
  }

#endif

  return std::make_pair(CommunicationPipesCreationError::ERROR_NONE, 0);
}
#endif

void llbuild::basic::spawnProcess(
    ProcessDelegate& delegate,
    ProcessContext* ctx,
    ProcessGroup& pgrp,
    ProcessHandle handle,
    ArrayRef<StringRef> commandLine,
    POSIXEnvironment environment,
    ProcessAttributes attr,
    ProcessReleaseFn&& releaseFn,
    ProcessCompletionFn&& completionFn
) {
  llbuild_pid_t pid = (llbuild_pid_t)-1;
  
#if !defined(_WIN32) && !defined(HAVE_POSIX_SPAWN)
  auto result = ProcessResult::makeFailed();
  delegate.processStarted(ctx, handle, pid);
  delegate.processHadError(ctx, handle, Twine("process spawning is unavailable"));
  delegate.processFinished(ctx, handle, result);
  completionFn(result);
  return;
#else
  // Don't use lane release feature for console workloads.
  if (attr.connectToConsole) {
    attr.controlEnabled = false;
  }

#if defined(_WIN32)
  // Control channel support is broken (thread-unsafe) on Windows.
  attr.controlEnabled = false;
#endif

  if (commandLine.size() == 0) {
    auto result = ProcessResult::makeFailed();
    delegate.processStarted(ctx, handle, pid);
    delegate.processHadError(ctx, handle, Twine("no arguments for command"));
    delegate.processFinished(ctx, handle, result);
    completionFn(result);
    return;
  }

  // Form the complete C string command line.
  std::vector<std::string> argsStorage(commandLine.begin(), commandLine.end());
#if defined(_WIN32)
  std::string args = llbuild::basic::formatWindowsCommandString(argsStorage);

  // Convert the command line string to utf16
  llvm::SmallVector<llvm::UTF16, 20> u16Executable;
  llvm::SmallVector<llvm::UTF16, 20> u16CmdLine;
  llvm::convertUTF8ToUTF16String(argsStorage[0], u16Executable);
  llvm::convertUTF8ToUTF16String(args, u16CmdLine);
#else
  std::vector<const char*> args(argsStorage.size() + 1);
  for (size_t i = 0; i != argsStorage.size(); ++i) {
    args[i] = argsStorage[i].c_str();
  }
  args[argsStorage.size()] = nullptr;
#endif

#if defined(_WIN32)
  DWORD creationFlags = NORMAL_PRIORITY_CLASS |
                        CREATE_UNICODE_ENVIRONMENT;
  PROCESS_INFORMATION processInfo = {0};

#else
  // Initialize the spawn attributes.
  posix_spawnattr_t attributes;
  posix_spawnattr_init(&attributes);

  // Unmask all signals.
  sigset_t noSignals;
  sigemptyset(&noSignals);
  posix_spawnattr_setsigmask(&attributes, &noSignals);

  // Reset all signals to default behavior.
  //
  // On Linux, this can only be used to reset signals that are legal to
  // modify, so we have to take care about the set we use.
#if defined(__linux__)
  sigset_t mostSignals;
  sigemptyset(&mostSignals);
  for (int i = 1; i < SIGSYS; ++i) {
    if (i == SIGKILL || i == SIGSTOP) continue;
    sigaddset(&mostSignals, i);
  }
  posix_spawnattr_setsigdefault(&attributes, &mostSignals);
#else
  sigset_t mostSignals;
  sigfillset(&mostSignals);
  sigdelset(&mostSignals, SIGKILL);
  sigdelset(&mostSignals, SIGSTOP);
  posix_spawnattr_setsigdefault(&attributes, &mostSignals);
#endif // else !defined(_WIN32)

  // Establish a separate process group.
  posix_spawnattr_setpgroup(&attributes, 0);

  // Set the attribute flags.
  unsigned flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
  if (!attr.connectToConsole) {
    flags |= POSIX_SPAWN_SETPGROUP;
  }

  // Close all other files by default.
  //
  // FIXME: Note that this is an Apple-specific extension, and we will have to
  // do something else on other platforms (and unfortunately, there isn't
  // really an easy answer other than using a stub executable).
#ifdef __APPLE__
  flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif

  // On Darwin, set the QoS of launched processes to one of the current thread.
#ifdef __APPLE__
  posix_spawnattr_set_qos_class_np(&attributes, qos_class_self());
#endif

  posix_spawnattr_setflags(&attributes, flags);

  // Setup the file actions.
  posix_spawn_file_actions_t fileActions;
  posix_spawn_file_actions_init(&fileActions);

  bool usePosixSpawnChdirFallback = true;
  const auto workingDir = attr.workingDir.str();
  if (!workingDir.empty() &&
      posix_spawn_file_actions_addchdir_polyfill(&fileActions, workingDir.c_str()) != ENOSYS) {
    usePosixSpawnChdirFallback = false;
  }

#endif

#if defined(_WIN32)
  /// Process startup information for Windows.
  STARTUPINFOW startupInfo = {0};
  PlatformSpecificPipesConfig& pipesConfig = startupInfo;
#else
  PlatformSpecificPipesConfig& pipesConfig = fileActions;
#endif

  // Automatically managed (released) descriptors for output and control pipes.
  // The child ends are forwarded to the child (and quickly released in the
  // parent). The parent ends are retained and read/written by the parent.
  ManagedDescriptor outputPipeParentEnd{__FILE__, __LINE__};
  ManagedDescriptor controlPipeParentEnd{__FILE__, __LINE__};

#if defined(_WIN32)
  llvm::SmallVector<llvm::UTF16, 20> u16Cwd;
  std::string workingDir = attr.workingDir.str();
  if (!workingDir.empty()) {
    llvm::convertUTF8ToUTF16String(workingDir, u16Cwd);
  }
#endif

  // Export a task ID to subprocesses.
  auto taskID = Twine::utohexstr(handle.id);
  environment.setIfMissing("LLBUILD_TASK_ID", taskID.str());

  // Resolve the executable path, if necessary.
  //
  // FIXME: This should be cached.
  if (!llvm::sys::path::is_absolute(argsStorage[0])) {
    auto res = llvm::sys::findProgramByName(argsStorage[0]);
    if (!res.getError()) {
      argsStorage[0] = *res;
#if defined(_WIN32)
      u16Executable.clear();
      llvm::convertUTF8ToUTF16String(argsStorage[0], u16Executable);
#else
      args[0] = argsStorage[0].c_str();
#endif
    }
  }

  // Spawn the command.
  bool wasCancelled;
  do {
      // We need to hold the spawn processes lock when we spawn, to ensure that
      // we don't create a process in between when we are cancelled.
      std::lock_guard<std::mutex> guard(pgrp.mutex);
      wasCancelled = pgrp.isClosed();

      // If we have been cancelled since we started, skip startup.
      if (wasCancelled) { break; }

      // The partf of the control pipes that are inherited by the child.
      ManagedDescriptor outputPipeChildEnd{__FILE__, __LINE__};
      ManagedDescriptor controlPipeChildEnd{__FILE__, __LINE__};

      // Open the communication channel under the mutex to avoid
      // leaking the wrong channel into other children started concurrently.
      auto errorPair = createCommunicationPipes(attr, pipesConfig, outputPipeParentEnd, outputPipeChildEnd, controlPipeParentEnd, controlPipeChildEnd);
      if (errorPair.first != CommunicationPipesCreationError::ERROR_NONE) {
        std::string whatPipe = errorPair.first == CommunicationPipesCreationError::OUTPUT_PIPE_FAILED ? "output pipe" : "control pipe";
#if !defined(_WIN32)
        posix_spawn_file_actions_destroy(&fileActions);
        posix_spawnattr_destroy(&attributes);
#endif
        delegate.processStarted(ctx, handle, pid);
        delegate.processHadError(ctx, handle,
            Twine("unable to open " + whatPipe + " (") + strerror(errorPair.second) + ")");
        delegate.processFinished(ctx, handle, ProcessResult::makeFailed());
        completionFn(ProcessResult(ProcessStatus::Failed));
        return;
      }

      if (controlPipeChildEnd.isValid()) {
        long long controlFd = (long long)controlPipeChildEnd.unsafeDescriptor();
        environment.setIfMissing("LLBUILD_CONTROL_FD", Twine(controlFd).str());
      }

      int result = 0;

      bool workingDirectoryUnsupported = false;

#if !defined(_WIN32)
      if (usePosixSpawnChdirFallback) {
#if defined(__APPLE__)
        thread_local std::string threadWorkingDir;

        if (workingDir.empty()) {
          if (!threadWorkingDir.empty()) {
            pthread_fchdir_np(-1);
            threadWorkingDir.clear();
          }
        } else {
          if (threadWorkingDir != workingDir) {
            if (pthread_chdir_np(workingDir.c_str()) == -1) {
              result = errno;
            } else {
              threadWorkingDir = workingDir;
            }
          }
        }
#else
        if (!workingDir.empty()) {
          workingDirectoryUnsupported = true;
          result = -1;
        }
#endif // if defined(__APPLE__)
      }
#endif // else !defined(_WIN32)

      if (result == 0) {
#if defined(_WIN32)
        auto unicodeEnv = environment.getWindowsEnvp();
        result = !CreateProcessW(
            /*lpApplicationName=*/(LPWSTR)u16Executable.data(),
            (LPWSTR)u16CmdLine.data(),
            /*lpProcessAttributes=*/NULL,
            /*lpThreadAttributes=*/NULL,
            /*bInheritHandles=*/TRUE, creationFlags,
            /*lpEnvironment=*/unicodeEnv.get(),
            /*lpCurrentDirectory=*/u16Cwd.empty() ? NULL
                                                  : (LPWSTR)u16Cwd.data(),
            &startupInfo, &processInfo);
#else
        result =
            posix_spawn(&pid, args[0], /*file_actions=*/&fileActions,
                        /*attrp=*/&attributes, const_cast<char**>(args.data()),
                        const_cast<char* const*>(environment.getEnvp()));
#endif
      }
    
      delegate.processStarted(ctx, handle, pid);

      if (result != 0) {
        auto processResult = ProcessResult::makeFailed();
#if defined(_WIN32)
        result = GetLastError();
#endif
        delegate.processHadError(
            ctx, handle,
            workingDirectoryUnsupported
                ? Twine("working-directory unsupported on this platform")
                : Twine("unable to spawn process '") + argsStorage[0] + "' (" + sys::strerror(result) +
                      ")");
        delegate.processFinished(ctx, handle, processResult);
        pid = (llbuild_pid_t)-1;
      } else {
#if defined(_WIN32)
        pid = processInfo.hProcess;
#endif
        ProcessInfo info{ attr.canSafelyInterrupt };
        pgrp.add(std::move(guard), pid, info);
      }

    // Close the child ends of the forwarded output and control pipes.
    controlPipeChildEnd.close();
    outputPipeChildEnd.close();
  } while(false);

#if !defined(_WIN32)
  posix_spawn_file_actions_destroy(&fileActions);
  posix_spawnattr_destroy(&attributes);
#endif

  // If we failed to launch a process, clean up and abort.
  if (pid == (llbuild_pid_t)-1) {
    // Manually close to avoid triggering debug-time leak check.
    outputPipeParentEnd.close();
    controlPipeParentEnd.close();
    auto result = wasCancelled ? ProcessResult::makeCancelled() : ProcessResult::makeFailed();
    completionFn(result);
    return;
  }

#if !defined(_WIN32)
  // Set up our poll() structures. We use assert() to ensure
  // the file descriptors are alive.
  pollfd readfds[] = {
    { outputPipeParentEnd.unsafeDescriptor(), 0, 0 },
    { controlPipeParentEnd.unsafeDescriptor(), 0, 0 }
  };
  bool activeEvents = false;
#endif
  const int nfds = 2;
  ControlProtocolState control(taskID.str());
  std::function<bool (StringRef)> readCbs[] = {
    // output capture callback
    [&delegate, ctx, handle](StringRef buf) -> bool {
      // Notify the client of the output.
      delegate.processHadOutput(ctx, handle, buf);
      return true;
    },
    // control callback handle
    [&delegate, &control, ctx, handle](StringRef buf) mutable -> bool {
      std::string errstr;
      int ret = control.read(buf, &errstr);
      if (ret < 0) {
        delegate.processHadError(ctx, handle,
                                 Twine("control protocol error" + errstr));
      }
      return (ret == 0);
    }
  };
#if defined(_WIN32)
  struct threadData {
    std::function<void(void*)> reader;
    const ManagedDescriptor& handle;
    std::function<bool(StringRef)> cb;
  };
  HANDLE readers[2] = {NULL, NULL};
  auto reader = [&delegate, handle, ctx](void* lpArgs) {
    threadData* args = (threadData*)lpArgs;
    for (;;) {
      char buf[4096];
      DWORD numBytes;
      bool result = ReadFile(args->handle.unsafeDescriptor(), buf, sizeof(buf), &numBytes, NULL);

      if (!result || numBytes == 0) {
        if (GetLastError() == ERROR_BROKEN_PIPE) {
          // Pipe done, exit
          return;
        } else {
          delegate.processHadError(ctx, handle,
                                   Twine("unable to read process output (") +
                                       sys::strerror(GetLastError()) + ")");
        }
      }

      if (numBytes <= 0 || !args->cb(StringRef(buf, numBytes))) {
        continue;
      }
    }
  };

  struct threadData outputThreadParams = {reader, outputPipeParentEnd, readCbs[0]};
  struct threadData controlThreadParams = {reader, controlPipeParentEnd, readCbs[1]};
  HANDLE threads[2] = {NULL, NULL};
#endif // defined(_WIN32)

  int threadCount = 0;

  // Read the command output, if capturing instead of pass-throughing.
  if (!attr.connectToConsole) {
#if defined(_WIN32)
    threads[threadCount++] = (HANDLE)_beginthread(
        [](LPVOID lpParams) { ((threadData*)lpParams)->reader(lpParams); }, 0,
        &outputThreadParams);
#else
    readfds[0].events = POLLIN;
    activeEvents = true;
    (void)threadCount;
#endif
  }

  // Process the control channel input.
  if (attr.controlEnabled) {
#if defined(_WIN32)
    threads[threadCount++] = (HANDLE)_beginthread(
        [](LPVOID lpParams) { ((threadData*)lpParams)->reader(lpParams); }, 0,
        &controlThreadParams);
#else
    readfds[1].events = POLLIN;
    activeEvents = true;
#endif
  }

#if defined(_WIN32)
  DWORD waitResult = WaitForMultipleObjects(threadCount, threads,
                                            /*bWaitAll=*/false,
                                            /*dwMilliseconds=*/INFINITE);
  if (WAIT_FAILED == waitResult || WAIT_TIMEOUT == waitResult) {
    int err = GetLastError();
    delegate.processHadError(
        ctx, handle, Twine("failed to poll (") + sys::strerror(err) + ")");
  }
#else  // !defined(_WIN32)
  while (activeEvents) {
    char buf[4096];
    activeEvents = false;

    // Ensure we haven't autoclosed the file descriptors,
    // or move()d somewhere they could be autoclosed in.
    assert(readfds[0].fd == outputPipeParentEnd.unsafeDescriptor());
    assert(readfds[1].fd == controlPipeParentEnd.unsafeDescriptor());

    while (poll(readfds, nfds, -1) == -1) {
        int err = errno;

        if (err == EAGAIN || err == EINTR) {
          continue;
        } else {
          delegate.processHadError(ctx, handle,
            Twine("failed to poll (") + strerror(err) + ")"); 
          return;
        }
    }

    for (int i = 0; i < nfds; i++) {
      if (readfds[i].revents & (POLLIN | POLLERR | POLLHUP)) {
        ssize_t numBytes = read(readfds[i].fd, buf, sizeof(buf));
        if (numBytes < 0) {
          int err = errno;
          delegate.processHadError(ctx, handle,
              Twine("unable to read process output (") + strerror(err) + ")");
        }
        if (numBytes <= 0 || !readCbs[i](StringRef(buf, numBytes))) {
          readfds[i].events = 0;
          continue;
        }
      }
      activeEvents |= readfds[i].events != 0;
    }

    if (control.shouldRelease()) {
      std::shared_ptr<ManagedDescriptor> outputFdShared
        = std::make_shared<ManagedDescriptor>(std::move(outputPipeParentEnd));
      std::shared_ptr<ManagedDescriptor> controlFdShared
        = std::make_shared<ManagedDescriptor>(std::move(controlPipeParentEnd));
      releaseFn([&delegate, &pgrp, pid, handle, ctx,
                 outputFdShared, controlFdShared,
                 completionFn=std::move(completionFn)]() mutable {
        if (outputFdShared->isValid()) {
          captureExecutedProcessOutput(delegate, *outputFdShared, handle, ctx);
        }
        cleanUpExecutedProcess(delegate, pgrp, pid, handle, ctx,
                               std::move(completionFn), *controlFdShared);
      });
      return;
    }

  }
#endif // else !defined(_WIN32)
  // If we have reached here, both the control and read pipes have given us
  // the requisite EOF/hang-up events. Safe to close the read end of the
  // output pipe.
  outputPipeParentEnd.close();
  cleanUpExecutedProcess(delegate, pgrp, pid, handle, ctx,
                         std::move(completionFn), controlPipeParentEnd);
#endif
}
