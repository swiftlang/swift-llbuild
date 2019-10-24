//===- unittests/Basic/LaneBasedExecutionQueueTest.cpp --------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/Basic/FileSystem.h"
#include "llbuild/Basic/ExecutionQueue.h"
#include "../BuildSystem/TempDir.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"

#include "gtest/gtest.h"

#include <atomic>
#include <condition_variable>
#include <ctime>
#include <future>
#include <mutex>

using namespace llbuild;
using namespace llbuild::basic;

namespace {
  class DummyDelegate : public ExecutionQueueDelegate {
  public:
    DummyDelegate() {}

    virtual void queueJobStarted(JobDescriptor*) override {}
    virtual void queueJobFinished(JobDescriptor*) override {}
    virtual void processStarted(ProcessContext*, ProcessHandle) override {}
    virtual void processHadError(ProcessContext*, ProcessHandle,
                                 const Twine& message) override {}
    virtual void processHadOutput(ProcessContext*, ProcessHandle,
                                  StringRef data) override {}
    virtual void processFinished(ProcessContext*, ProcessHandle,
                                 const ProcessResult& result) override {}
  };

  class DummyCommand : public JobDescriptor {
  public:
    DummyCommand() {}

    virtual StringRef getOrdinalName() const { return StringRef(""); }
    virtual void getShortDescription(SmallVectorImpl<char> &result) const {}
    virtual void getVerboseDescription(SmallVectorImpl<char> &result) const {}
  };

  TEST(LaneBasedExecutionQueueTest, basic) {
    DummyDelegate delegate;
    std::unique_ptr<FileSystem> fs = createLocalFileSystem();
    TmpDir tempDir{"LaneBasedExecutionQueueTest"};
    std::string outputFile = tempDir.str() + "/yes-output.txt";
    auto queue = std::unique_ptr<ExecutionQueue>(
        createLaneBasedExecutionQueue(delegate, 2,
                                      SchedulerAlgorithm::NamePriority,
                                      /*environment=*/nullptr));

    auto fn = [&outputFile, &queue](QueueJobContext* context) {
      queue->executeShellCommand(context, "yes >" + outputFile);
    };

    DummyCommand dummyCommand;
    queue->addJob(QueueJob(&dummyCommand, fn));

    // Busy wait until `outputFile` appears which indicates that `yes` is
    // running.
    time_t start = ::time(NULL);
    while (fs->getFileInfo(outputFile).isMissing()) {
      if (::time(NULL) > start + 5) {
        // We can't fail gracefully because the `LaneBasedExecutionQueue` will
        // always wait for spawned processes to exit
        abort();
      }
    }

    queue->cancelAllJobs();
    queue.reset();
  }

  TEST(LaneBasedExecutionQueueTest, workingDirectory) {
    DummyDelegate delegate;
    std::unique_ptr<FileSystem> fs = createLocalFileSystem();
    TmpDir tempDir{"LaneBasedExecutionQueueTest"};
    std::string outputFile = tempDir.str() + "/yes-output.txt";
    auto queue = std::unique_ptr<ExecutionQueue>(
        createLaneBasedExecutionQueue(delegate, 2,
                                      SchedulerAlgorithm::NamePriority,
                                      /*environment=*/nullptr));

    auto fn = [&tempDir, &queue](QueueJobContext* context) {
      std::string yescmd = "yes >yes-output.txt";
      std::vector<StringRef> commandLine(
                                         { DefaultShellPath, "-c", yescmd.c_str() });
      std::promise<ProcessStatus> p;
      auto result = p.get_future();
      queue->executeProcess(context, commandLine, {}, {true, false, tempDir.str()},
                     {[&p](ProcessResult result) mutable {
        p.set_value(result.status);
      }});
      result.get();
    };

    DummyCommand dummyCommand;
    queue->addJob(QueueJob(&dummyCommand, fn));

    // Busy wait until `outputFile` appears which indicates that `yes` is
    // running.
    time_t start = ::time(NULL);
    while (fs->getFileInfo(outputFile).isMissing()) {
      if (::time(NULL) > start + 5) {
        // We can't fail gracefully because the `LaneBasedExecutionQueue` will
        // always wait for spawned processes to exit
        abort();
      }
    }

    queue->cancelAllJobs();
    queue.reset();
  }

  TEST(LaneBasedExecutionQueueTest, exhaustsQueueAfterCancellation) {
    DummyDelegate delegate;
    auto queue = std::unique_ptr<ExecutionQueue>(
        createLaneBasedExecutionQueue(delegate, 1,
                                      SchedulerAlgorithm::NamePriority,
                                      /*environment=*/nullptr));

    bool buildStarted { false };
    std::condition_variable buildStartedCondition;
    std::mutex buildStartedMutex;
    std::atomic<int> executions { 0 };

    auto fn = [&buildStarted, &buildStartedCondition, &buildStartedMutex,
               &executions, &queue](QueueJobContext* context) {
      executions++;
      if (queue) { queue->cancelAllJobs(); }

      std::unique_lock<std::mutex> lock(buildStartedMutex);
      buildStarted = true;
      buildStartedCondition.notify_all();
    };

    DummyCommand dummyCommand1;
    queue->addJob(QueueJob(&dummyCommand1, fn));
    DummyCommand dummyCommand2;
    queue->addJob(QueueJob(&dummyCommand2, fn));

    {
      std::unique_lock<std::mutex> lock(buildStartedMutex);
      while (!buildStarted) {
        buildStartedCondition.wait(lock);
      }
    }

    queue.reset();

    // Busy wait until our executions are done, but also have a timeout in case they never finish
    time_t start = ::time(NULL);
    while (executions < 2) {
      if (::time(NULL) > start + 5) {
        break;
      }
    }

    EXPECT_EQ(executions, 2);
  }

}
