//===-- Subprocess.cpp ----------------------------------------------------===//
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

#include "llbuild/Basic/Subprocess.h"

#include "llbuild/Basic/PlatformUtility.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"

#include <atomic>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <pthread/spawn.h>

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

using namespace llbuild;
using namespace llbuild::basic;

namespace {

  static std::atomic<QualityOfService> defaultQualityOfService{
    QualityOfService::Normal };

#if defined(__APPLE__)
  qos_class_t _getDarwinQOSClass(QualityOfService level) {
    switch (getDefaultQualityOfService()) {
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
      _getDarwinQOSClass(getDefaultQualityOfService()), 0);
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
    ::kill(-it.first, signal);
  }
}


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

// Helper function to collect subprocess output
static void captureExecutedProcessOutput(
    ProcessDelegate& delegate,
    int outputPipe,
    ProcessHandle handle,
    ProcessContext* ctx
) {
  // Read all the data from the output pipe.
  while (true) {
    char buf[4096];
    ssize_t numBytes = ::read(outputPipe, buf, sizeof(buf));
    if (numBytes < 0) {
      int err = errno;
      delegate.processHadError(ctx, handle,
          Twine("unable to read process output (") + strerror(err) + ")");
      break;
    }

    if (numBytes == 0)
      break;

    // Notify the client of the output.
    delegate.processHadOutput(ctx, handle, StringRef(buf, numBytes));
  }

  // We have receieved the zero byte read that indicates an EOF. Go ahead and
  // close the pipe.
  ::close(outputPipe);
}

// Helper function for cleaning up after a process has finished in
// executeProcess
static void cleanUpExecutedProcess(
    ProcessDelegate& delegate,
    ProcessGroup& pgrp,
    llbuild_pid_t pid,
    ProcessHandle handle,
    ProcessContext* ctx,
    ProcessCompletionFn&& completionFn,
    int releaseFd
) {
  // Wait for the command to complete.
  struct rusage usage;
  int exitCode, result = wait4(pid, &exitCode, 0, &usage);
  while (result == -1 && errno == EINTR)
    result = wait4(pid, &exitCode, 0, &usage);

  // Close the release pipe
  //
  // Note: We purposely hold this open until after the process has finished as
  // it simplifies client implentation. If we close it early, clients need to be
  // aware of and potentially handle a SIGPIPE.
  if (releaseFd >= 0) {
    ::close(releaseFd);
  }

  // Update the set of spawned processes.
  pgrp.remove(pid);

  if (result == -1) {
    auto result = ProcessResult::makeFailed(exitCode);
    delegate.processHadError(ctx, handle,
                             Twine("unable to wait for process (") + strerror(errno) + ")");
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
  delegate.processFinished(ctx, handle, processResult);
  completionFn(processResult);
}


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

  // Whether or not we are capturing output.
  const bool shouldCaptureOutput = true;

  delegate.processStarted(ctx, handle);

  if (commandLine.size() == 0) {
    auto result = ProcessResult::makeFailed();
    delegate.processHadError(ctx, handle, Twine("no arguments for command"));
    delegate.processFinished(ctx, handle, result);
    completionFn(result);
    return;
  }

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
#endif

  // Establish a separate process group.
  posix_spawnattr_setpgroup(&attributes, 0);

  // Set the attribute flags.
  unsigned flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
  flags |= POSIX_SPAWN_SETPGROUP;

  // Close all other files by default.
  //
  // FIXME: Note that this is an Apple-specific extension, and we will have to
  // do something else on other platforms (and unfortunately, there isn't
  // really an easy answer other than using a stub executable).
#ifdef __APPLE__
  flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif

  // On Darwin, set the QOS of launched processes to the global default.
#ifdef __APPLE__
  posix_spawnattr_set_qos_class_np(
      &attributes, _getDarwinQOSClass(defaultQualityOfService));
#endif

  posix_spawnattr_setflags(&attributes, flags);

  // Setup the file actions.
  posix_spawn_file_actions_t fileActions;
  posix_spawn_file_actions_init(&fileActions);

  // Open /dev/null as stdin.
  posix_spawn_file_actions_addopen(
      &fileActions, 0, "/dev/null", O_RDONLY, 0);


  // Create a pipe for the process to (potentially) release the lane while
  // still running.
  int controlPipe[2]{ -1, -1 };
  if (attr.controlEnabled) {
    if (basic::sys::pipe(controlPipe) < 0) {
      delegate.processHadError(ctx, handle,
          Twine("unable to open control pipe (") + strerror(errno) + ")");
      delegate.processFinished(ctx, handle, ProcessResult::makeFailed());
      completionFn(ProcessStatus::Failed);
      return;
    }
  #ifdef __APPLE__
    posix_spawn_file_actions_addinherit_np(&fileActions, controlPipe[1]);
  #else
    posix_spawn_file_actions_adddup2(&fileActions, controlPipe[1], controlPipe[1]);
  #endif
    posix_spawn_file_actions_addclose(&fileActions, controlPipe[0]);
  }

  // If we are capturing output, create a pipe and appropriate spawn actions.
  int outputPipe[2]{ -1, -1 };
  if (shouldCaptureOutput) {
    if (basic::sys::pipe(outputPipe) < 0) {
      auto result = ProcessResult::makeFailed();
      delegate.processHadError(ctx, handle,
          Twine("unable to open output pipe (") + strerror(errno) + ")");
      delegate.processFinished(ctx, handle, result);
      completionFn(result);
      return;
    }

    // Open the write end of the pipe as stdout and stderr.
    posix_spawn_file_actions_adddup2(&fileActions, outputPipe[1], 1);
    posix_spawn_file_actions_adddup2(&fileActions, outputPipe[1], 2);

    // Close the read and write ends of the pipe.
    posix_spawn_file_actions_addclose(&fileActions, outputPipe[0]);
    posix_spawn_file_actions_addclose(&fileActions, outputPipe[1]);
  } else {
    // Otherwise, propagate the current stdout/stderr.
    posix_spawn_file_actions_adddup2(&fileActions, 1, 1);
    posix_spawn_file_actions_adddup2(&fileActions, 2, 2);
  }

  // Form the complete C string command line.
  std::vector<std::string> argsStorage(
      commandLine.begin(), commandLine.end());
  std::vector<const char*> args(argsStorage.size() + 1);
  for (size_t i = 0; i != argsStorage.size(); ++i) {
    args[i] = argsStorage[i].c_str();
  }
  args[argsStorage.size()] = nullptr;

  // Export a task ID to subprocesses.
  auto taskID = Twine::utohexstr(handle.id);
  environment.setIfMissing("LLBUILD_TASK_ID", taskID.str());
  if (attr.controlEnabled) {
    environment.setIfMissing("LLBUILD_CONTROL_FD", Twine(controlPipe[1]).str());
  }

  // Resolve the executable path, if necessary.
  //
  // FIXME: This should be cached.
  if (!llvm::sys::path::is_absolute(args[0])) {
    auto res = llvm::sys::findProgramByName(args[0]);
    if (!res.getError()) {
      argsStorage[0] = *res;
      args[0] = argsStorage[0].c_str();
    }
  }

  // Spawn the command.
  llbuild_pid_t pid = -1;
  bool wasCancelled;
  {
    // We need to hold the spawn processes lock when we spawn, to ensure that
    // we don't create a process in between when we are cancelled.
    std::lock_guard<std::mutex> guard(pgrp.mutex);
    wasCancelled = pgrp.isClosed();

    // If we have been cancelled since we started, do nothing.
    if (!wasCancelled) {
      int result = 0;

#ifdef __APPLE__
      thread_local std::string threadWorkingDir;

      if (attr.workingDir.empty()) {
        if (!threadWorkingDir.empty()) {
          pthread_fchdir_np(-1);
          threadWorkingDir.clear();
        }
      } else {
        if (threadWorkingDir != attr.workingDir) {
          const auto workingDir = attr.workingDir.str();
          if (pthread_chdir_np(workingDir.c_str()) == -1) {
            result = errno;
          } else {
            threadWorkingDir = attr.workingDir;
          }
        }
      }
#else
      assert(attr.workingDir.empty() &&
             "setting process working directory unsupported");
#endif

      if (result == 0) {
        result = posix_spawn(&pid, args[0], /*file_actions=*/&fileActions,
                             /*attrp=*/&attributes, const_cast<char**>(args.data()),
                             const_cast<char* const*>(environment.getEnvp()));
      }

      if (result != 0) {
        auto processResult = ProcessResult::makeFailed();
        delegate.processHadError(ctx, handle,
            Twine("unable to spawn process (") + strerror(result) + ")");
        delegate.processFinished(ctx, handle, processResult);
        pid = -1;
      } else {
        ProcessInfo info{ attr.canSafelyInterrupt };
        pgrp.add(std::move(guard), pid, info);
      }
    }
  }

  posix_spawn_file_actions_destroy(&fileActions);
  posix_spawnattr_destroy(&attributes);

  // Close the write end of the release pipe
  if (attr.controlEnabled) {
    ::close(controlPipe[1]);
  }

  // If we failed to launch a process, clean up and abort.
  if (pid == -1) {
    if (attr.controlEnabled) {
      ::close(controlPipe[0]);
    }

    if (shouldCaptureOutput) {
      ::close(outputPipe[1]);
      ::close(outputPipe[0]);
    }
    auto result = wasCancelled ? ProcessResult::makeCancelled() : ProcessResult::makeFailed();
    completionFn(result);
    return;
  }

  // Set up our select() structures
  pollfd readfds[] = {
    { controlPipe[0], 0, 0 },
    { outputPipe[0], 0, 0 }
  };
  const int nfds = 2;
  ControlProtocolState control(taskID.str());
  std::function<bool (StringRef)> readCbs[] = {
    // control callback handle
    [&delegate, &control, ctx, handle](StringRef buf) mutable -> bool {
      std::string errstr;
      int ret = control.read(buf, &errstr);
      if (ret < 0) {
        delegate.processHadError(ctx, handle,
                                 Twine("control protocol error" + errstr));
      }
      return (ret == 0);
    },
    // output capture callback
    [&delegate, ctx, handle](StringRef buf) -> bool {
      // Notify the client of the output.
      delegate.processHadOutput(ctx, handle, buf);
      return true;
    }
  };

  int activeEvents = 0;

  if (attr.controlEnabled) {
    readfds[0].events = POLLIN;
    activeEvents |= POLLIN;
  }

  // Read the command output, if capturing.
  if (shouldCaptureOutput) {
    readfds[1].events = POLLIN;
    activeEvents |= POLLIN;

    // Close the write end of the output pipe.
    ::close(outputPipe[1]);
  }

  while (activeEvents) {
    char buf[4096];
    activeEvents = 0;

    if (poll(readfds, nfds, -1) == -1) {
      int err = errno;
      delegate.processHadError(ctx, handle,
          Twine("failed to poll (") + strerror(err) + ")");
      break;
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
      activeEvents |= readfds[i].events;
    }

    if (control.shouldRelease()) {
      releaseFn([
                 &delegate, &pgrp, pid, handle, ctx,
                 outputFd=outputPipe[0],
                 controlFd=controlPipe[0],
                 completionFn=std::move(completionFn)
                 ]() mutable {
        if (shouldCaptureOutput)
          captureExecutedProcessOutput(delegate, outputFd, handle, ctx);

        cleanUpExecutedProcess(delegate, pgrp, pid, handle, ctx,
                               std::move(completionFn), controlFd);
      });
      return;
    }
  }

  if (shouldCaptureOutput) {
    // If we have reached here, both the control and read pipes have given us
    // the requisite EOF/hang-up events. Safe to close the read end of the
    // output pipe.
    ::close(outputPipe[0]);
  }

  cleanUpExecutedProcess(delegate, pgrp, pid, handle, ctx,
                         std::move(completionFn), controlPipe[0]);
}

