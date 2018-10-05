//===- MockBuildSystemDelegate.h --------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/BuildSystem/BuildDescription.h"
#include "llbuild/BuildSystem/BuildSystem.h"

#include "llbuild/Basic/ExecutionQueue.h"
#include "llbuild/Basic/FileSystem.h"
#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/SourceMgr.h"

#include <memory>
#include <mutex>

using namespace llvm;
using namespace llbuild;
using namespace llbuild::basic;
using namespace llbuild::buildsystem;

namespace llbuild {
namespace unittests {

class MockExecutionQueueDelegate : public ExecutionQueueDelegate {
public:
  MockExecutionQueueDelegate();

private:
  virtual void queueJobStarted(JobDescriptor*) override {}

  virtual void queueJobFinished(JobDescriptor*) override {}

  virtual void processStarted(ProcessContext*, ProcessHandle handle) override {}

  virtual void processHadError(ProcessContext*, ProcessHandle handle,
                               const Twine& message) override {}

  virtual void processHadOutput(ProcessContext*, ProcessHandle handle,
                                StringRef data) override {}
  
  virtual void processFinished(ProcessContext*, ProcessHandle handle,
                               const ProcessResult& result) override {}
};
  
class MockBuildSystemDelegate : public BuildSystemDelegate {
  std::vector<std::string> messages;
  std::mutex messagesMutex;
  
  MockExecutionQueueDelegate executionQueueDelegate;

  bool trackAllMessages;
  
public:
    MockBuildSystemDelegate(bool trackAllMessages = false);

  std::vector<std::string> getMessages() {
    {
      std::unique_lock<std::mutex> lock(messagesMutex);
      return messages;
    }
  }
  
  virtual void setFileContentsBeingParsed(StringRef buffer) {}

  virtual void error(StringRef filename,
                     const Token& at,
                     const Twine& message) {
    llvm::errs() << "error: " << filename.str() << ": " << message.str() << "\n";
    {
      std::unique_lock<std::mutex> lock(messagesMutex);
      messages.push_back(message.str());
    }
  }

  virtual std::unique_ptr<Tool> lookupTool(StringRef name) {
    return nullptr;
  }

  virtual std::unique_ptr<ExecutionQueue> createExecutionQueue() {
    return std::unique_ptr<ExecutionQueue>(
        createLaneBasedExecutionQueue(executionQueueDelegate, /*numLanes=*/1,
                                      SchedulerAlgorithm::NamePriority,
                                      /*environment=*/nullptr));
  }
  
  virtual void hadCommandFailure() {
    if (trackAllMessages) {
      std::unique_lock<std::mutex> lock(messagesMutex);
      messages.push_back("hadCommandFailure");
    }
  }

  virtual void commandStatusChanged(Command*, CommandStatusKind) { }

  virtual void commandPreparing(Command* command) {
    if (trackAllMessages) {
      std::unique_lock<std::mutex> lock(messagesMutex);
      messages.push_back(
          ("commandPreparing(" + command->getName() + ")").str());
    }
  }

  virtual bool shouldCommandStart(Command*) { return true; }

  virtual void commandStarted(Command* command) {
    if (trackAllMessages) {
      std::unique_lock<std::mutex> lock(messagesMutex);
      messages.push_back(
          ("commandStarted(" + command->getName() + ")").str());
    }
  }

  virtual void commandHadError(Command* command, StringRef data) {
    llvm::errs() << "error: " << command->getName() << ": " << data.str() << "\n";
    {
      std::unique_lock<std::mutex> lock(messagesMutex);
      messages.push_back(data.str());
    }
  }

  virtual void commandHadNote(Command* command, StringRef data) {
    if (trackAllMessages) {
      std::unique_lock<std::mutex> lock(messagesMutex);
      messages.push_back(("commandNote(" + command->getName() + ") " + data).str());
    }
  }

  virtual void commandHadWarning(Command* command, StringRef data) {
    if (trackAllMessages) {
      std::unique_lock<std::mutex> lock(messagesMutex);
      messages.push_back(("commandWarning(" + command->getName() + ") " + data).str());
    }
  }

  virtual void commandFinished(Command* command, ProcessStatus result) {
    if (trackAllMessages) {
      std::unique_lock<std::mutex> lock(messagesMutex);
      messages.push_back(
          ("commandFinished(" + command->getName() + ": " + std::to_string((int)result) + ")").str());
    }
  }

  virtual void commandCannotBuildOutputDueToMissingInputs(Command * command, Node *output,
                                                          SmallPtrSet<Node *, 1> inputs) {
    std::string message = "cannot build '" + output->getName().str() + "' due to missing input: '" +
      (*inputs.begin())->getName().str() + "'";
    llvm::errs() << "error: " << command->getName() << ": " << message << "\n";
    {
      std::unique_lock<std::mutex> lock(messagesMutex);
      messages.push_back(message);
    }
  }

  virtual void cannotBuildNodeDueToMultipleProducers(Node *output,
                                                     std::vector<Command*> commands) {
    std::string message = "cannot build '" + output->getName().str() + "' node is produced "
    "by multiple commands; e.g. '" + (*commands.begin())->getName().str() + "'";
    llvm::errs() << "error: " << message << "\n";
    {
      std::unique_lock<std::mutex> lock(messagesMutex);
      messages.push_back(message);
    }
  }
};

}
}
