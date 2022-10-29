//===-- SerialQueue.cpp ---------------------------------------------------===//
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

#include "llbuild/Basic/SerialQueue.h"

#include "llbuild/Basic/ExecutionQueue.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <random>
#include <thread>

#include <signal.h>

using namespace llbuild;
using namespace llbuild::basic;

namespace {

/// A basic serial queue.
///
/// This implementation has been optimized for simplicity over performance.
//
// FIXME: Optimize.
class SerialQueueImpl {
  /// The thread executing the operations.
  std::unique_ptr<std::thread> operationsThread;
  
  /// The queue of operations.
  std::deque<std::function<void(void)>> operations;

  /// The mutex protecting access to the queue.
  std::mutex operationsMutex;

  /// Condition variable used to signal when operations are available.
  std::condition_variable readyOperationsCondition;
  
  /// Thread function to execute operations.
  void run() {
    while (true) {
      // Get the next operation from the queue.
      std::function<void(void)> fn;
      {
        std::unique_lock<std::mutex> lock(operationsMutex);

        // While the queue is empty, wait for an item.
        while (operations.empty()) {
          readyOperationsCondition.wait(lock);
        }

        fn = operations.front();
        operations.pop_front();
      }

      // If we got a nil function, the queue is shutting down.
      if (!fn)
        break;
      
      // Execute the operation.
      fn();
    }
  }

  void addOperation(std::function<void(void)>&& fn) {
    std::lock_guard<std::mutex> guard(operationsMutex);
    operations.push_back(fn);
    readyOperationsCondition.notify_one();
  }
  
public:
  SerialQueueImpl() {
    // Ensure the queue is fully initialized before creating the worker thread.
    operationsThread = std::make_unique<std::thread>(
        &SerialQueueImpl::run, this);
  }

  ~SerialQueueImpl() {
    // Signal the worker to shut down.
    addOperation({});

    // Wait for the worker to complete.
    operationsThread->join();
  }
  
  void sync(std::function<void(void)> fn) {
    assert(fn);
    
    // Add an operation which will execute the function and signal its
    // completion.
    std::condition_variable cv{};
    std::mutex isCompleteMutex{};
    bool isComplete = false;
    addOperation([&]() {
        fn();
        {
          std::unique_lock<std::mutex> lock(isCompleteMutex);
          isComplete = true;
          cv.notify_one();
        }
      });

    // Wait for the operation to complete.
    std::unique_lock<std::mutex> lock(isCompleteMutex);
    while (!isComplete) {
      cv.wait(lock);
    }
  }

  void async(std::function<void(void)> fn) {
    assert(fn);
    
    // Add the operation.
    addOperation(std::move(fn));
  }
};

}

SerialQueue::SerialQueue()
    : impl(new SerialQueueImpl())
{
}

SerialQueue::~SerialQueue() {
  delete static_cast<SerialQueueImpl*>(impl);
}

void SerialQueue::sync(std::function<void(void)> fn) {
  static_cast<SerialQueueImpl*>(impl)->sync(fn);
}

void SerialQueue::async(std::function<void(void)> fn) {
  static_cast<SerialQueueImpl*>(impl)->async(fn);
}


/// An execution queue based on a serial operation queue.
class SerialExecutionQueue : public ExecutionQueue {
  /// (Random) build identifier
  uint32_t buildID;

  /// Underlying queue implementation
  SerialQueueImpl* queue;

  /// The base environment.
  const char* const* environment;

  struct SerialContext: public basic::QueueJobContext {
    uint64_t jobID;
    QueueJob& job;

    SerialContext(uint64_t jobID, QueueJob& job)
      : jobID(jobID), job(job) { }

    unsigned laneID() const override { return 0; }
  };

  uint64_t jobCount{0};
  std::atomic<bool> cancelled { false };

  ProcessGroup spawnedProcesses;

  std::mutex killAfterTimeoutThreadMutex;
  std::unique_ptr<std::thread> killAfterTimeoutThread = nullptr;
  std::condition_variable queueCompleteCondition;
  std::mutex queueCompleteMutex;
  bool queueComplete { false };

  void killAfterTimeout() {
    std::unique_lock<std::mutex> lock(queueCompleteMutex);

    if (!queueComplete) {
      // Shorten timeout if in testing context
      if (getenv("LLBUILD_TEST") != nullptr) {
        queueCompleteCondition.wait_for(lock, std::chrono::milliseconds(1000));
      } else {
        queueCompleteCondition.wait_for(lock, std::chrono::seconds(10));
      }

#if _WIN32
      spawnedProcesses.signalAll(SIGTERM);
#else
      spawnedProcesses.signalAll(SIGKILL);
#endif
    }
  }

public:
  SerialExecutionQueue(ExecutionQueueDelegate& delegate,
                       const char* const* environment)
  : ExecutionQueue(delegate), buildID(std::random_device()()), queue(new SerialQueueImpl), environment(environment)
  {
  }

  virtual ~SerialExecutionQueue()
  {
    delete queue;
    queue = nullptr;

    std::lock_guard<std::mutex> guard(killAfterTimeoutThreadMutex);
    if (killAfterTimeoutThread) {
      {
        std::unique_lock<std::mutex> lock(queueCompleteMutex);
        queueComplete = true;
        queueCompleteCondition.notify_all();
      }
      killAfterTimeoutThread->join();
    }
  }


  virtual void addJob(QueueJob job, QueueJobPriority) override {
    uint64_t jobID = ++jobCount;
    queue->async([jobID, job]() mutable {
      SerialContext ctx(jobID, job);
      job.execute(&ctx);
    });
  }

  virtual void cancelAllJobs() override {
    {
      std::lock_guard<std::mutex> guard(spawnedProcesses.mutex);
      if (cancelled) return;
      cancelled = true;
      spawnedProcesses.close();
    }

    spawnedProcesses.signalAll(SIGINT);
    {
      std::lock_guard<std::mutex> guard(killAfterTimeoutThreadMutex);
      killAfterTimeoutThread = std::make_unique<std::thread>(
          &SerialExecutionQueue::killAfterTimeout, this);
    }
  }

  virtual void executeProcess(
      QueueJobContext* opaqueContext,
      ArrayRef<StringRef> commandLine,
      ArrayRef<std::pair<StringRef, StringRef>> environment,
      ProcessAttributes attributes,
      llvm::Optional<ProcessCompletionFn> completionFn,
      ProcessDelegate* delegate
  ) override {

    SerialContext& context = *reinterpret_cast<SerialContext*>(opaqueContext);

    // Do not execute new processes anymore after cancellation.
    if (cancelled) {
      if (completionFn.hasValue())
        completionFn.getValue()(ProcessResult::makeCancelled());
      return;
    }

    // Form the complete environment.
    //
    // NOTE: We construct the environment in order of precedence, so
    // overridden keys should be defined first.
    POSIXEnvironment posixEnv;

    // Export lane ID to subprocesses.
    posixEnv.setIfMissing("LLBUILD_BUILD_ID", Twine(buildID).str());
    posixEnv.setIfMissing("LLBUILD_LANE_ID", Twine(0).str());

    // Add the requested environment.
    for (const auto& entry: environment) {
      posixEnv.setIfMissing(entry.first, entry.second);
    }

    // Inherit the base environment, if desired.
    //
    // FIXME: This involves a lot of redundant allocation, currently. We could
    // cache this for the common case of a directly inherited environment.
    if (attributes.inheritEnvironment) {
      for (const char* const* p = this->environment; *p != nullptr; ++p) {
        auto pair = StringRef(*p).split('=');
        posixEnv.setIfMissing(pair.first, pair.second);
      }
    }

    // Assign a process handle, which just needs to be unique for as long as we
    // are communicating with the delegate.
    ProcessHandle handle;
    handle.id = context.jobID;

    ProcessReleaseFn releaseFn = [](std::function<void()>&& processWait) {
      // not allowed to release, call wait directly
      processWait();
    };

    ProcessCompletionFn laneCompletionFn{
      [completionFn](ProcessResult result) mutable {
        if (completionFn.hasValue())
          completionFn.getValue()(result);
      }
    };

    spawnProcess(
        delegate ? *delegate : getDelegate(),
        reinterpret_cast<ProcessContext*>(context.job.getDescriptor()),
        spawnedProcesses,
        handle,
        commandLine,
        posixEnv,
        attributes,
        std::move(releaseFn),
        std::move(laneCompletionFn)
    );
  }
};


#if !defined(_WIN32)
extern "C" {
  extern char **environ;
}
#endif

std::unique_ptr<ExecutionQueue> llbuild::basic::createSerialQueue(
    ExecutionQueueDelegate& delegate, const char* const* environment
) {
  if (!environment) {
    environment = const_cast<const char* const*>(environ);
  }
  return std::make_unique<SerialExecutionQueue>(delegate, environment);

}
