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

#include "llvm/ADT/STLExtras.h"

#include <cassert>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

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
    operationsThread = llvm::make_unique<std::thread>(
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
