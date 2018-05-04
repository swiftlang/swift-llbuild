//===-- LaneBasedExecutionQueue.cpp ---------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/BuildSystem/BuildExecutionQueue.h"

#include "POSIXEnvironment.h"

#include "llbuild/Basic/CrossPlatformCompatibility.h"
#include "llbuild/Basic/LLVM.h"
#include "llbuild/Basic/PlatformUtility.h"
#include "llbuild/Basic/Tracing.h"

#include "llbuild/BuildSystem/BuildDescription.h"
#include "llbuild/BuildSystem/BuildSystem.h"
#include "llbuild/BuildSystem/CommandResult.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>

#ifdef __APPLE__
#include <pthread/spawn.h>
#endif

using namespace llbuild;
using namespace llbuild::buildsystem;

struct QueueJobLess {
  bool operator()(const llbuild::buildsystem::QueueJob &__x, const llbuild::buildsystem::QueueJob &__y) const {
    return __x.getForCommand()->getName() < __y.getForCommand()->getName();
  }
};

namespace {

static std::atomic<QualityOfService> defaultQualityOfService{
  llbuild::buildsystem::QualityOfService::normal };

static std::atomic<SchedulerAlgorithm> defaultAlgorithm {
  llbuild::buildsystem::SchedulerAlgorithm::commandNamePriority };

static std::atomic<uint32_t> defaultLaneWidth { 0 };

#if defined(__APPLE__)
qos_class_t _getDarwinQOSClass(QualityOfService level) {
  switch (llbuild::buildsystem::getDefaultQualityOfService()) {
  case llbuild::buildsystem::QualityOfService::normal:
    return QOS_CLASS_DEFAULT;
  case llbuild::buildsystem::QualityOfService::userInitiated:
    return QOS_CLASS_USER_INITIATED;
  case llbuild::buildsystem::QualityOfService::utility:
    return QOS_CLASS_UTILITY;
  case llbuild::buildsystem::QualityOfService::background:
    return QOS_CLASS_BACKGROUND;
  default:
    assert(0 && "unknown command result");
    return QOS_CLASS_DEFAULT;
  }
}

#endif

struct LaneBasedExecutionQueueJobContext {
  uint32_t laneNumber;
  
  QueueJob& job;
};

struct ProcessInfo {
  /// Whether the process can be safely interrupted.
  bool canSafelyInterrupt;
};


class Scheduler {
public:
  virtual ~Scheduler() { }

  virtual void addJob(QueueJob job) = 0;
  virtual QueueJob getNextJob() = 0;
  virtual bool empty() const = 0;
  virtual uint64_t size() const = 0;

  static std::unique_ptr<Scheduler> make();
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
  std::unique_ptr<Scheduler> readyJobs;
  std::mutex readyJobsMutex;
  std::condition_variable readyJobsCondition;
  bool cancelled { false };
  bool shutdown { false };
  
  /// The set of spawned processes to terminate if we get cancelled.
  std::unordered_map<llbuild_pid_t, ProcessInfo> spawnedProcesses;
  std::mutex spawnedProcessesMutex;

  /// Management of cancellation and SIGKILL escalation
  std::unique_ptr<std::thread> killAfterTimeoutThread = nullptr;
  std::condition_variable queueCompleteCondition;
  std::mutex queueCompleteMutex;
  bool queueComplete { false };

  /// The base environment.
  const char* const* environment;
  
  void executeLane(unsigned laneNumber) {
    // Set the thread name, if available.
#if defined(__APPLE__)
    pthread_setname_np(
        (llvm::Twine("org.swift.llbuild Lane-") +
         llvm::Twine(laneNumber)).str().c_str());
#elif defined(__linux__)
    pthread_setname_np(
        pthread_self(),
        (llvm::Twine("org.swift.llbuild Lane-") +
         llvm::Twine(laneNumber)).str().c_str());
#endif

    // Set the QoS class, if available.
#if defined(__APPLE__)
    pthread_set_qos_class_self_np(
        _getDarwinQOSClass(defaultQualityOfService), 0);
#endif
    
    // Execute items from the queue until shutdown.
    while (true) {
      // Take a job from the ready queue.
      QueueJob job{};
      uint64_t readyJobsCount;
      {
        std::unique_lock<std::mutex> lock(readyJobsMutex);

        // While the queue is empty, wait for an item.
        while (!shutdown && readyJobs->empty()) {
          readyJobsCondition.wait(lock);
        }
        if (shutdown && readyJobs->empty())
          return;

        // Take an item according to the chosen policy.
        job = readyJobs->getNextJob();
        readyJobsCount = readyJobs->size();
      }

      // If we got an empty job, the queue is shutting down.
      if (!job.getForCommand())
        break;

      // Process the job.
      LaneBasedExecutionQueueJobContext context{ laneNumber, job };
      {
        TracingPoint(TraceEventKind::ExecutionQueueDepth, readyJobsCount);
        TracingString commandNameID(
            TraceEventKind::ExecutionQueueJob,
            job.getForCommand()->getName());
        TracingInterval i(TraceEventKind::ExecutionQueueJob,
                          context.laneNumber, commandNameID);
        getDelegate().commandJobStarted(job.getForCommand());
        job.execute(reinterpret_cast<QueueJobContext*>(&context));
        getDelegate().commandJobFinished(job.getForCommand());
      }
    }
  }

  void killAfterTimeout() {
    std::unique_lock<std::mutex> lock(queueCompleteMutex);

    if (!queueComplete) {
      // Shorten timeout if in testing context
      if (getenv("LLBUILD_TEST") != nullptr) {
        queueCompleteCondition.wait_for(lock, std::chrono::milliseconds(1000));
      } else {
        queueCompleteCondition.wait_for(lock, std::chrono::seconds(10));
      }

      sendSignalToProcesses(SIGKILL);
    }
  }

  void sendSignalToProcesses(int signal) {
    std::unique_lock<std::mutex> lock(spawnedProcessesMutex);

    for (const auto& it: spawnedProcesses) {
      // If we are interrupting, only interupt processes which are believed to
      // be safe to interrupt.
      if (signal == SIGINT && !it.second.canSafelyInterrupt)
        continue;
      
      // We are killing the whole process group here, this depends on us
      // spawning each process in its own group earlier.
      ::kill(-it.first, signal);
    }
  }

public:
  LaneBasedExecutionQueue(BuildExecutionQueueDelegate& delegate,
                          unsigned numLanes,
                          const char* const* environment)
      : BuildExecutionQueue(delegate), numLanes(numLanes),
        readyJobs(Scheduler::make()), environment(environment)
  {
    for (unsigned i = 0; i != numLanes; ++i) {
      lanes.push_back(std::unique_ptr<std::thread>(
                          new std::thread(
                              &LaneBasedExecutionQueue::executeLane, this, i)));
    }
  }
  
  virtual ~LaneBasedExecutionQueue() {
    // Shut down the lanes.
    {
      std::unique_lock<std::mutex> lock(readyJobsMutex);
      shutdown = true;
      readyJobsCondition.notify_all();
    }

    for (unsigned i = 0; i != numLanes; ++i) {
      lanes[i]->join();
    }

    if (killAfterTimeoutThread) {
      {
        std::unique_lock<std::mutex> lock(queueCompleteMutex);
        queueComplete = true;
        queueCompleteCondition.notify_all();
      }
      killAfterTimeoutThread->join();
    }
  }

  virtual void addJob(QueueJob job) override {
    uint64_t readyJobsCount;
    {
      std::lock_guard<std::mutex> guard(readyJobsMutex);
      readyJobs->addJob(job);
      readyJobsCondition.notify_one();
      readyJobsCount = readyJobs->size();
    }
    TracingPoint(TraceEventKind::ExecutionQueueDepth, readyJobsCount);
  }

  virtual void cancelAllJobs() override {
    {
      std::lock_guard<std::mutex> lock(readyJobsMutex);
      std::lock_guard<std::mutex> guard(spawnedProcessesMutex);
      if (cancelled) return;
      cancelled = true;
      readyJobsCondition.notify_all();
    }

    sendSignalToProcesses(SIGINT);
    killAfterTimeoutThread = llvm::make_unique<std::thread>(
        &LaneBasedExecutionQueue::killAfterTimeout, this);
  }

  virtual CommandResult
  executeProcess(QueueJobContext* opaqueContext,
                 ArrayRef<StringRef> commandLine,
                 ArrayRef<std::pair<StringRef,
                                    StringRef>> environment,
                 bool inheritEnvironment,
                 bool canSafelyInterrupt) override {
    LaneBasedExecutionQueueJobContext& context =
      *reinterpret_cast<LaneBasedExecutionQueueJobContext*>(opaqueContext);
    TracingInterval subprocessInterval(TraceEventKind::ExecutionQueueSubprocess,
                                       context.laneNumber);

    {
      std::unique_lock<std::mutex> lock(readyJobsMutex);
      // Do not execute new processes anymore after cancellation.
      if (cancelled) {
        return CommandResult::Cancelled;
      }
    }

    // Assign a process handle, which just needs to be unique for as long as we
    // are communicating with the delegate.
    struct BuildExecutionQueueDelegate::ProcessHandle handle;
    handle.id = reinterpret_cast<uintptr_t>(&handle);

    // Whether or not we are capturing output.
    const bool shouldCaptureOutput = true;

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

    // If we are capturing output, create a pipe and appropriate spawn actions.
    int outputPipe[2]{ -1, -1 };
    if (shouldCaptureOutput) {
      if (basic::sys::pipe(outputPipe) < 0) {
        getDelegate().commandProcessHadError(
            context.job.getForCommand(), handle,
            Twine("unable to open output pipe (") + strerror(errno) + ")");
        getDelegate().commandProcessFinished(context.job.getForCommand(),
                                             handle, CommandExtendedResult::makeFailed());
        return CommandResult::Failed;
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

    // Form the complete environment.
    //
    // NOTE: We construct the environment in order of precedence, so
    // overridden keys should be defined first.
    POSIXEnvironment posixEnv;

    // Export a task ID to subprocesses.
    //
    // We currently only export the lane ID, but eventually will export a unique
    // task ID for SR-6053.
    posixEnv.setIfMissing("LLBUILD_TASK_ID", Twine(context.laneNumber).str());
                          
    // Add the requested environment.
    for (const auto& entry: environment) {
      posixEnv.setIfMissing(entry.first, entry.second);
    }
      
    // Inherit the base environment, if desired.
    //
    // FIXME: This involves a lot of redundant allocation, currently. We could
    // cache this for the common case of a directly inherited environment.
    if (inheritEnvironment) {
      for (const char* const* p = this->environment; *p != nullptr; ++p) {
        auto pair = StringRef(*p).split('=');
        posixEnv.setIfMissing(pair.first, pair.second);
      }
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
      std::lock_guard<std::mutex> guard(spawnedProcessesMutex);
      wasCancelled = cancelled;
      
      // If we have been cancelled since we started, do nothing.
      if (!wasCancelled) {
        int result = posix_spawn(&pid, args[0], /*file_actions=*/&fileActions,
                                 /*attrp=*/&attributes, const_cast<char**>(args.data()),
                                 const_cast<char* const*>(posixEnv.getEnvp()));
        if (result != 0) {
          getDelegate().commandProcessHadError(
              context.job.getForCommand(), handle,
              Twine("unable to spawn process (") + strerror(result) + ")");
          getDelegate().commandProcessFinished(context.job.getForCommand(), handle,
                                               CommandExtendedResult::makeFailed());
          pid = -1;
        } else {
          ProcessInfo info{ canSafelyInterrupt };
          spawnedProcesses.emplace(std::make_pair(pid, info));
        }
      }
    }

    posix_spawn_file_actions_destroy(&fileActions);
    posix_spawnattr_destroy(&attributes);
    
    // If we failed to launch a process, clean up and abort.
    if (pid == -1) {
      if (shouldCaptureOutput) {
        ::close(outputPipe[1]);
        ::close(outputPipe[0]);
      }
      return wasCancelled ? CommandResult::Cancelled : CommandResult::Failed;
    }

    // Read the command output, if capturing.
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

        // Notify the client of the output.
        getDelegate().commandProcessHadOutput(
            context.job.getForCommand(), handle,
            StringRef(buf, numBytes));
      }

      // Close the read end of the pipe.
      ::close(outputPipe[0]);
    }
    
    // Wait for the command to complete.
    struct rusage usage;
    int status, result = wait4(pid, &status, 0, &usage);
    while (result == -1 && errno == EINTR)
      result = wait4(pid, &status, 0, &usage);

    // Update the set of spawned processes.
    {
        std::lock_guard<std::mutex> guard(spawnedProcessesMutex);
        spawnedProcesses.erase(pid);
    }

    if (result == -1) {
      getDelegate().commandProcessHadError(
          context.job.getForCommand(), handle,
          Twine("unable to wait for process (") + strerror(errno) + ")");
      getDelegate().commandProcessFinished(context.job.getForCommand(), handle,
                                           CommandExtendedResult::makeFailed(status));
      return CommandResult::Failed;
    }

    // We report additional info in the tracing interval
    //   arg2: user time, in us
    //   arg3: sys time, in us
    //   arg4: memory usage, in bytes
    uint64_t utime = (uint64_t(usage.ru_utime.tv_sec) * 1000000000 +
                      uint64_t(usage.ru_utime.tv_usec) * 1000);
    uint64_t stime = (uint64_t(usage.ru_stime.tv_sec) * 1000000000 +
                      uint64_t(usage.ru_stime.tv_usec) * 1000);

    subprocessInterval.arg2 = utime;
    subprocessInterval.arg3 = stime;
    subprocessInterval.arg4 = usage.ru_maxrss;
    
    // FIXME: We should report a statistic for how much output we read from the
    // subprocess (probably as a new point sample).
    
    // Notify of the process completion.
    bool cancelled = WIFSIGNALED(status) && (WTERMSIG(status) == SIGINT || WTERMSIG(status) == SIGKILL);
    CommandResult commandResult = cancelled ? CommandResult::Cancelled : (status == 0) ? CommandResult::Succeeded : CommandResult::Failed;
    CommandExtendedResult extendedResult(commandResult, status, pid, utime, stime,
                                         usage.ru_maxrss);
    getDelegate().commandProcessFinished(context.job.getForCommand(), handle,
                                         extendedResult);
    return commandResult;
  }
};

class PriorityQueueScheduler : public Scheduler {
private:
  std::priority_queue<QueueJob, std::vector<QueueJob>, QueueJobLess> jobs;

public:
  void addJob(QueueJob job) override {
    jobs.push(job);
  }

  QueueJob getNextJob() override {
    QueueJob job = jobs.top();
    jobs.pop();
    return job;
  }

  bool empty() const override {
    return jobs.empty();
  }

  uint64_t size() const override {
    return jobs.size();
  }
};

class FifoScheduler : public Scheduler {
private:
  std::deque<QueueJob> jobs;

public:
  void addJob(QueueJob job) override {
    jobs.push_back(job);
  }

  QueueJob getNextJob() override {
    QueueJob job = jobs.front();
    jobs.pop_front();
    return job;
  }

  bool empty() const override {
    return jobs.empty();
  }

  uint64_t size() const override {
    return jobs.size();
  }
};

std::unique_ptr<Scheduler> Scheduler::make() {
  switch (defaultAlgorithm) {
    case llbuild::buildsystem::SchedulerAlgorithm::commandNamePriority:
      return std::unique_ptr<Scheduler>(new PriorityQueueScheduler);
    case llbuild::buildsystem::SchedulerAlgorithm::fifo:
      return std::unique_ptr<Scheduler>(new FifoScheduler);
    default:
      assert(0 && "unknown scheduler algorithm");
      return std::unique_ptr<Scheduler>(nullptr);
  }
}

}

#if !defined(_WIN32)
extern "C" {
  extern char **environ;
}
#endif

BuildExecutionQueue*
llbuild::buildsystem::createLaneBasedExecutionQueue(
    BuildExecutionQueueDelegate& delegate, int numLanes,
    const char* const* environment) {
  if (!environment) {
    environment = const_cast<const char* const*>(environ);
  }
  return new LaneBasedExecutionQueue(delegate, numLanes, environment);
}

QualityOfService llbuild::buildsystem::getDefaultQualityOfService() {
  return defaultQualityOfService;
}

void llbuild::buildsystem::setDefaultQualityOfService(QualityOfService level) {
  defaultQualityOfService = level;
}

SchedulerAlgorithm llbuild::buildsystem::getSchedulerAlgorithm() {
  return defaultAlgorithm;
}

void llbuild::buildsystem::setSchedulerAlgorithm(SchedulerAlgorithm algorithm) {
  defaultAlgorithm = algorithm;
}

uint32_t llbuild::buildsystem::getSchedulerLaneWidth() {
  return defaultLaneWidth;
}

void llbuild::buildsystem::setSchedulerLaneWidth(uint32_t width) {
  defaultLaneWidth = width;
}
