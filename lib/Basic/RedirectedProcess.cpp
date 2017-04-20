//===-- RedirectedProcess.cpp ---------------------------------------------===//
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

#include "llbuild/Basic/RedirectedProcess.h"

#include <signal.h>

#include <fcntl.h>
#include <unistd.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>

using namespace llbuild;
using namespace llbuild::basic;
using namespace llbuild::basic::sys;

struct llbuild::basic::sys::ProcessInfo {
  pid_t processId;
  int readHandle;
  int writeHandle;
};

RedirectedProcess::RedirectedProcess(bool shouldCaptureOutput) {
  this->shouldCaptureOutput = shouldCaptureOutput;
  this->innerProcessInfo = new ProcessInfo();
}

bool RedirectedProcess::openPipe() {
  if (!shouldCaptureOutput) {
    return true;
  }

  int pipeHandles[2] = {-1, -1};
  if (::pipe(pipeHandles) < 0) {
    return false;
  }

  innerProcessInfo->readHandle = pipeHandles[0];
  innerProcessInfo->writeHandle = pipeHandles[1];

  return true;
}

bool RedirectedProcess::execute(const char *path, bool setGroupFlags,
                                char *const *args, char *const *envp,
                                std::mutex &spawnedProcessesMutex) {
  // Initialize the spawn attributes.
  posix_spawnattr_t attributes;
  posix_spawnattr_init(&attributes);

  // Unmask all signals
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
  for (int i = 1; i < SIGUNUSED; ++i) {
    if (i == SIGKILL || i == SIGSTOP)
      continue;
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
  if (setGroupFlags)
    flags |= POSIX_SPAWN_SETPGROUP;

// Close all other files by default.
//
// FIXME: This is an Apple-specific extension, and we will have to do
// something else on other platforms (and unfortunately, there isn't
// really an easy answer other than using a stub executable).
#ifdef __APPLE__
  flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif

  posix_spawnattr_setflags(&attributes, flags);

  // Setup the file actions.
  posix_spawn_file_actions_t fileActions;
  posix_spawn_file_actions_init(&fileActions);

  // Open /dev/null as stdin.
  posix_spawn_file_actions_addopen(&fileActions, 0, "/dev/null", O_RDONLY, 0);

  // Create a pipe to use to read the command output, if necessary.
  if (shouldCaptureOutput) {
    // Open the write end of the pipe as stdout and stderr.
    posix_spawn_file_actions_adddup2(&fileActions, innerProcessInfo->writeHandle, 1);
    posix_spawn_file_actions_adddup2(&fileActions, innerProcessInfo->writeHandle, 2);

    // Close the read and write ends of the pipe.
    posix_spawn_file_actions_addclose(&fileActions, innerProcessInfo->readHandle);
    posix_spawn_file_actions_addclose(&fileActions, innerProcessInfo->writeHandle);
  } else {
    posix_spawn_file_actions_adddup2(&fileActions, 1, 1);
    posix_spawn_file_actions_adddup2(&fileActions, 2, 2);
  }

  // Spawn the command.
  pid_t pid;
  {
    // We need to hold the spawn processes lock when we spawn, to ensure that
    // we don't create a process in between when we are cancelled.
    std::lock_guard<std::mutex> guard(spawnedProcessesMutex);

    if (posix_spawn(&pid, path, /*file_actions=*/&fileActions,
                    /*attrp=*/&attributes, args, envp) != 0) {
      return false;
    }
  }

  posix_spawn_file_actions_destroy(&fileActions);
  posix_spawnattr_destroy(&attributes);

  innerProcessInfo->processId = pid;

  return true;
}

bool RedirectedProcess::readPipe(llvm::SmallString<1024> &output) {
  if (!shouldCaptureOutput) {
    return true;
  }

  // Close the write end of the output pipe.
  ::close(innerProcessInfo->writeHandle);

  // Read all the data from the output pipe.
  while (true) {
    char buf[4096];
    ssize_t numBytes = ::read(innerProcessInfo->readHandle, buf, sizeof(buf));
    if (numBytes < 0) {
      return false;
    }

    if (numBytes == 0)
      break;

    output.insert(output.end(), &buf[0], &buf[numBytes]);
  }

  // Close the read end of the pipe.
  ::close(innerProcessInfo->readHandle);

  return true;
}

bool RedirectedProcess::waitForCompletion(int *exitStatus) {
  int result = waitpid(innerProcessInfo->processId, exitStatus, 0);
  while (result == -1 && errno == EINTR)
    result = waitpid(innerProcessInfo->processId, exitStatus, 0);

  return result != -1;
}

bool RedirectedProcess::kill(int signal) {
  int result = ::kill(-(innerProcessInfo->processId), signal);
  return result == 0;
}

bool RedirectedProcess::operator==(const RedirectedProcess &rhs) const {
  return innerProcessInfo->processId == rhs.innerProcessInfo->processId &&
         innerProcessInfo->readHandle == rhs.innerProcessInfo->readHandle &&
         innerProcessInfo->writeHandle == rhs.innerProcessInfo->writeHandle;
}

size_t RedirectedProcess::hash() const {
  return innerProcessInfo->processId;
}

RedirectedProcess::~RedirectedProcess() {
  // TODO: this causes tens of errors at runtime running the tests:
  // "*** Error in `/mnt/c/Users/hughb/Documents/GitHub/swift-linux/build/bin/llbuild': double free or corruption (fasttop): 0x00007f1450000940 ***"
  // Surely we allocated innerProcessInfo (this->innerProcessInfo = new ProcessInfo();), so have
  // to delete it? This may need investigation to make sure we don't leak memory.
  // Or I'm probably wrong, and should delete this destructor.

  //delete innerProcessInfo;
}


int RedirectedProcess::sigkill() {
  return SIGKILL;
}

bool RedirectedProcess::isProcessCancelledStatus(int status) {
  return WIFSIGNALED(status) &&
    (WTERMSIG(status) == SIGINT || WTERMSIG(status) == SIGKILL);
}
