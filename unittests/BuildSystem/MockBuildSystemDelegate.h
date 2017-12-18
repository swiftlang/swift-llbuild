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

#include "llbuild/BuildSystem/BuildExecutionQueue.h"
#include "llbuild/BuildSystem/BuildDescription.h"
#include "llbuild/BuildSystem/BuildSystem.h"

#include "llbuild/Basic/FileSystem.h"
#include "llbuild/Basic/LLVM.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/SourceMgr.h"

#include <memory>
#include <mutex>

using namespace llvm;
using namespace llbuild;
using namespace llbuild::buildsystem;

namespace llbuild {
namespace unittests {

class MockExecutionQueueDelegate : public BuildExecutionQueueDelegate {
public:
  MockExecutionQueueDelegate();

private:
  virtual void commandJobStarted(Command*) {}

  virtual void commandJobFinished(Command*) {}

  virtual void commandProcessStarted(Command*, ProcessHandle handle) {}

  virtual void commandProcessHadError(Command*, ProcessHandle handle,
                                      const Twine& message) {}

  virtual void commandProcessHadOutput(Command*, ProcessHandle handle,
                                       StringRef data) {}
  
  virtual void commandProcessFinished(Command*, ProcessHandle handle,
                                      CommandResult result,
                                      int exitStatus) {}
};
  
class MockBuildSystemDelegate : public BuildSystemDelegate {
  std::shared_ptr<basic::FileSystem> fileSystem;
  std::vector<std::string> messages;
  std::mutex messagesMutex;
  
  MockExecutionQueueDelegate executionQueueDelegate;

  bool trackAllMessages;
  
public:
    MockBuildSystemDelegate(bool trackAllMessages = false,
                            std::shared_ptr<basic::FileSystem> fileSystem = basic::createLocalFileSystem());

  std::vector<std::string> getMessages() {
    {
      std::unique_lock<std::mutex> lock(messagesMutex);
      return messages;
    }
  }
  
  virtual basic::FileSystem& getFileSystem() { return *fileSystem; }
  
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

  virtual std::unique_ptr<BuildExecutionQueue> createExecutionQueue() {
    return std::unique_ptr<BuildExecutionQueue>(
        createLaneBasedExecutionQueue(executionQueueDelegate, /*numLanes=*/1,
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

  virtual void commandFinished(Command* command, CommandResult result) {
    if (trackAllMessages) {
      std::unique_lock<std::mutex> lock(messagesMutex);
      messages.push_back(
          ("commandFinished(" + command->getName() + ": " + std::to_string((int)result) + ")").str());
    }
  }
};

}
}
