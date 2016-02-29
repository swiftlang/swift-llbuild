//===-- LaneBasedExecutionQueue.cpp ---------------------------------------===//
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

#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <memory>
#include <thread>
#include <vector>
#include <string>

#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

using namespace llbuild;
using namespace llbuild::buildsystem;

extern "C" {
  extern char **environ;
}

namespace {

struct LaneBasedExecutionQueueJobContext {
  QueueJob& job;
};

/// Build execution queue.
//
// FIXME: Consider trying to share this with the Ninja implementation.
class LaneBasedExecutionQueue : public BuildExecutionQueue {
  /// The number of lanes the queue was configured with.
  unsigned numLanes;

  /// A thread for each lane.
  std::vector<std::unique_ptr<std::thread>> lanes;

  /// The ready queue of jobs to execute.
  std::deque<QueueJob> readyJobs;
  std::mutex readyJobsMutex;
  std::condition_variable readyJobsCondition;
  
  void executeLane(unsigned laneNumber) {
    // Execute items from the queue until shutdown.
    while (true) {
      // Take a job from the ready queue.
      QueueJob job{};
      {
        std::unique_lock<std::mutex> lock(readyJobsMutex);

        // While the queue is empty, wait for an item.
        while (readyJobs.empty()) {
          readyJobsCondition.wait(lock);
        }

        // Take an item according to the chosen policy.
        job = readyJobs.front();
        readyJobs.pop_front();
      }

      // If we got an empty job, the queue is shutting down.
      if (!job.getForCommand())
        break;

      // Process the job.
      LaneBasedExecutionQueueJobContext context{ job };
      getDelegate().commandJobStarted(job.getForCommand());
      job.execute(reinterpret_cast<QueueJobContext*>(&context));
      getDelegate().commandJobFinished(job.getForCommand());
    }
  }

public:
  LaneBasedExecutionQueue(BuildExecutionQueueDelegate& delegate,
                          unsigned numLanes)
      : BuildExecutionQueue(delegate), numLanes(numLanes)
  {
    for (unsigned i = 0; i != numLanes; ++i) {
      lanes.push_back(std::unique_ptr<std::thread>(
                          new std::thread(
                              &LaneBasedExecutionQueue::executeLane, this, i)));
    }
  }
  
  virtual ~LaneBasedExecutionQueue() {
    // Shut down the lanes.
    for (unsigned i = 0; i != numLanes; ++i) {
      addJob({});
    }
    for (unsigned i = 0; i != numLanes; ++i) {
      lanes[i]->join();
    }
  }

  virtual void addJob(QueueJob job) override {
    std::lock_guard<std::mutex> guard(readyJobsMutex);
    readyJobs.push_back(job);
    readyJobsCondition.notify_one();
  }

  virtual bool
  executeProcess(QueueJobContext* opaqueContext,
                 ArrayRef<StringRef> commandLine,
                 ArrayRef<std::pair<StringRef,
                                    StringRef>> environment) override {
    // Assign a process handle, which just needs to be unique for as long as we
    // are communicating with the delegate.
    struct BuildExecutionQueueDelegate::ProcessHandle handle;
    handle.id = reinterpret_cast<uintptr_t>(&handle);

    // Whether or not we are capturing output.
    const bool shouldCaptureOutput = true;

    LaneBasedExecutionQueueJobContext& context =
      *reinterpret_cast<LaneBasedExecutionQueueJobContext*>(opaqueContext);
    getDelegate().commandProcessStarted(context.job.getForCommand(), handle);
    
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
    for (int i = 1; i < SIGUNUSED; ++i) {
      if (i == SIGKILL || i == SIGSTOP) continue;
      sigaddset(&mostSignals, i);
    }
    posix_spawnattr_setsigdefault(&attributes, &mostSignals);
#else
    sigset_t allSignals;
    sigfillset(&allSignals);
    posix_spawnattr_setsigdefault(&attributes, &allSignals);
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

    posix_spawnattr_setflags(&attributes, flags);

    // Setup the file actions.
    posix_spawn_file_actions_t fileActions;
    posix_spawn_file_actions_init(&fileActions);

    // Open /dev/null as stdin.
    posix_spawn_file_actions_addopen(
        &fileActions, 0, "/dev/null", O_RDONLY, 0);

    // If we are capturing output, create a pipe and appropriate spawn actions.
    int outputPipe[2]{ -1, -1 };
    if (shouldCaptureOutput) {
      if (::pipe(outputPipe) < 0) {
        getDelegate().commandProcessHadError(
            context.job.getForCommand(), handle,
            Twine("unable to open output pipe (") + strerror(errno) + ")");
        getDelegate().commandProcessFinished(context.job.getForCommand(),
                                             handle, -1);
        return false;
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

    // Form the complete C-string command line.
    std::vector<std::string> argsStorage(
        commandLine.begin(), commandLine.end());
    std::vector<const char*> args(argsStorage.size() + 1);
    for (size_t i = 0; i != argsStorage.size(); ++i) {
      args[i] = argsStorage[i].c_str();
    }
    args[argsStorage.size()] = nullptr;

    // Form the complete environment.
    std::vector<std::string> envStorage;
    for (const auto& entry: environment) {
      SmallString<256> assignment;
      assignment += entry.first;
      assignment += '=';
      assignment += entry.second;
      assignment += '\0';
      envStorage.emplace_back(assignment.str());
    }
    std::vector<const char*> env(environment.size() + 1);
    char* const* envp = nullptr;
    if (environment.empty()) {
      envp = ::environ;
    } else {
      for (size_t i = 0; i != envStorage.size(); ++i) {
        env[i] = envStorage[i].c_str();
      }
      env[envStorage.size()] = nullptr;
      envp = const_cast<char**>(env.data());
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
    //
    // FIXME: Need to track spawned processes for the purposes of cancellation.
    
    pid_t pid;
    if (posix_spawn(&pid, args[0], /*file_actions=*/&fileActions,
                    /*attrp=*/&attributes, const_cast<char**>(args.data()),
                    envp) != 0) {
      getDelegate().commandProcessHadError(
          context.job.getForCommand(), handle,
          Twine("unable to spawn process (") + strerror(errno) + ")");
      getDelegate().commandProcessFinished(context.job.getForCommand(), handle,
                                           -1);
      return false;
    }

    posix_spawn_file_actions_destroy(&fileActions);
    posix_spawnattr_destroy(&attributes);

    // Read the command output, if capturing.
    SmallString<1024> outputData;
    if (shouldCaptureOutput) {
      // Close the write end of the output pipe.
      ::close(outputPipe[1]);

      // Read all the data from the output pipe.
      while (true) {
        char buf[4096];
        ssize_t numBytes = read(outputPipe[0], buf, sizeof(buf));
        if (numBytes < 0) {
          getDelegate().commandProcessHadError(
              context.job.getForCommand(), handle,
              Twine("unable to read process output (") + strerror(errno) + ")");
          break;
        }

        if (numBytes == 0)
          break;

        outputData.insert(outputData.end(), &buf[0], &buf[numBytes]);
      }

      // Close the read end of the pipe.
      ::close(outputPipe[0]);
    }
    
    // Wait for the command to complete.
    int status, result = waitpid(pid, &status, 0);
    while (result == -1 && errno == EINTR)
      result = waitpid(pid, &status, 0);
    if (result == -1) {
      getDelegate().commandProcessHadError(
          context.job.getForCommand(), handle,
          Twine("unable to wait for process (") + strerror(errno) + ")");
      getDelegate().commandProcessFinished(context.job.getForCommand(), handle,
                                           -1);
      return false;
    }

    // Notify the client of the output, if buffering.
    if (shouldCaptureOutput) {
      getDelegate().commandProcessHadOutput(context.job.getForCommand(), handle,
                                            outputData);
    }
    
    // Notify of the process completion.
    //
    // FIXME: Need to communicate more information on the process exit status.
    getDelegate().commandProcessFinished(context.job.getForCommand(), handle,
                                         status);
    return (status == 0);
  }
};

}

BuildExecutionQueue*
llbuild::buildsystem::createLaneBasedExecutionQueue(
    BuildExecutionQueueDelegate& delegate, int numLanes) {
  return new LaneBasedExecutionQueue(delegate, numLanes);
}
