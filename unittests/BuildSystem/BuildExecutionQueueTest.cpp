//===- unittests/buildsystem/buildexecutionqueuetest.cpp ------------------===//
//
// this source file is part of the swift.org open source project
//
// copyright (c) 2014 - 2015 apple inc. and the swift project authors
// licensed under apache license v2.0 with runtime library exception
//
// see http://swift.org/license.txt for license information
// see http://swift.org/contributors.txt for the list of swift project authors
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Path.h"

#include "llbuild/BuildSystem/BuildExecutionQueue.h"
#include "llbuild/BuildSystem/ExternalCommand.h"

#include "gtest/gtest.h"

using namespace llbuild;
using namespace llbuild::core;
using namespace llbuild::buildsystem;

namespace {
class PhonyCommand : public ExternalCommand {
  public:
    using ExternalCommand::ExternalCommand;

    virtual bool shouldShowStatus() override { return false; }

    virtual void getShortDescription(SmallVectorImpl<char> &result) override {
      llvm::raw_svector_ostream(result) << getName();
    }

    virtual void getVerboseDescription(SmallVectorImpl<char> &result) override {
      llvm::raw_svector_ostream(result) << getName();
    }

    virtual bool executeExternalCommand(BuildSystemCommandInterface& bsci,
        Task* task,
        QueueJobContext* context) override {
      return true;
    }
};
  
class SimpleBuildExecutionQueueDelegate : public BuildExecutionQueueDelegate {

  public:
    SimpleBuildExecutionQueueDelegate() { 
    }

    virtual void commandJobStarted(Command* command) override {
    }

    virtual void commandJobFinished(Command* command) override {
    }

    virtual void commandProcessStarted(Command* command,
        ProcessHandle handle) override {
    }

    virtual void commandProcessHadError(Command* command, ProcessHandle handle,
        const Twine& message) override {
    }

    virtual void commandProcessFinished(Command* command, ProcessHandle handle,
        int exitStatus) override {
    }
};

struct LaneBasedExecutionQueueJobContext {
    QueueJob& job;
};

class BuildExecutionQueueTest : public testing::Test {
  protected:
    SimpleBuildExecutionQueueDelegate delegate;
    std::shared_ptr<BuildExecutionQueue> queue;
    std::unique_ptr<PhonyCommand> phony;
    std::string stdoutString;
    std::string stderrString;
    std::string expectedStdout;
    std::string expectedStderr;

    BuildExecutionQueueTest() {
      queue = std::shared_ptr<BuildExecutionQueue>(createLaneBasedExecutionQueue(delegate, 1));
      phony = llvm::make_unique<PhonyCommand>("Phony");
    }

    virtual ~BuildExecutionQueueTest() {
      EXPECT_EQ(stderrString, expectedStderr);
      EXPECT_EQ(stdoutString, expectedStdout);
    }

    void executeProcessTester(std::string exe, StringRef output, StringRef error) {
      expectedStdout = output;
      expectedStderr = error;
      // HACK.
      auto job = QueueJob { phony.get(), [](auto context){} };
      LaneBasedExecutionQueueJobContext context{ job }; 
      queue->executeProcess(reinterpret_cast<QueueJobContext*>(&context), {exe}, {}, 
          [&](StringRef output){ stdoutString.append(output); },
          [&](StringRef error){ stderrString.append(error); });
    }
};

std::string fixturePath(StringRef name) {
  SmallString<16> basePath = llvm::sys::path::parent_path(__FILE__);
  auto pathFromEnv = getenv("LLBUILD_BUILDSYSTEM_UNITTEST_PATH");
  if (pathFromEnv)
    basePath = pathFromEnv;
  llvm::sys::path::append(basePath, "/Inputs/BuildExecutionQueueExecuteProcessInputs/");
  llvm::sys::path::append(basePath, name); 
  return basePath.str();
}

TEST_F(BuildExecutionQueueTest, simple) {
  std::string commandLine = fixturePath("simple-output");
  executeProcessTester(commandLine, "simple output\n", "simple error");
}

TEST_F(BuildExecutionQueueTest, stdoutOnly) {
  std::string commandLine = fixturePath("only-stdout");
  executeProcessTester(commandLine, "simple output", "");
}

TEST_F(BuildExecutionQueueTest, stderrOnly) {
  std::string commandLine = fixturePath("only-stderr");
  executeProcessTester(commandLine, "", "simple error");
}

TEST_F(BuildExecutionQueueTest, longRunning) {
  std::string commandLine = fixturePath("long-output");
  std::string output, error;
  for (int i=0; i < 16 * 1024; i++) {
    output.append("1");
    error.append("2");
  }
  executeProcessTester(commandLine, output, error);
}

TEST_F(BuildExecutionQueueTest, deadlockForNonblockingIO) {
  std::string commandLine = fixturePath("deadlock-if-blocking-io");
  std::string output, error;
  for (int i=0; i < 1024 * 1024; i++) {
    output.append("1");
    error.append("2");
  }
  executeProcessTester(commandLine, output, error);
}

}
