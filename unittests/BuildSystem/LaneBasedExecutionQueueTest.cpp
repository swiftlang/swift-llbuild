//===- unittests/BuildSystem/LaneBasedExecutionQueueTest.cpp --------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/BuildSystem/BuildExecutionQueue.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"

#include "gtest/gtest.h"

using namespace llbuild;
using namespace llbuild::buildsystem;

namespace {
  class DummyDelegate : public BuildExecutionQueueDelegate {
  public:
    DummyDelegate() {}

    virtual void commandJobStarted(Command* command) override {}
    virtual void commandJobFinished(Command* command) override {}
    virtual void commandProcessStarted(Command* command, ProcessHandle handle) override {}
    virtual void commandProcessHadError(Command* command, ProcessHandle handle, const Twine& message) override {}
    virtual void commandProcessHadOutput(Command* command, ProcessHandle handle, StringRef data) override {}
    virtual void commandProcessFinished(Command* command, ProcessHandle handle, int exitStatus) override {}
  };

  TEST(LaneBasedExecutionQueueTest, basic) {
    DummyDelegate delegate;
    auto queue = std::unique_ptr<BuildExecutionQueue>(createLaneBasedExecutionQueue(delegate, 2));

    auto fn = [&queue](QueueJobContext* context) {
      std::vector<StringRef> commandLine;
      commandLine.push_back("/usr/bin/yes");
      queue->executeProcess(context, commandLine);
    };

    queue->addJob(QueueJob((Command*)0x1, fn));
    ::usleep(10); // there's a tiny race, until executeProcess() has called executeCommand()

    queue->cancelAllJobs();
    queue.reset();
  }
    
}
