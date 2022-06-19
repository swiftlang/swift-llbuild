//===- unittests/BuildSystem/BuildSystemFrontendTest.cpp ------------------===//
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

#include "TempDir.h"

#include "llbuild/Basic/FileSystem.h"
#include "llbuild/BuildSystem/BuildDescription.h"
#include "llbuild/BuildSystem/BuildFile.h"
#include "llbuild/BuildSystem/BuildKey.h"
#include "llbuild/BuildSystem/BuildSystemFrontend.h"
#include "llbuild/BuildSystem/Command.h"
#include "llbuild/BuildSystem/Tool.h"

#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Path.h"
#include "llvm/ADT/StringRef.h"

#include "gtest/gtest.h"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_set>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

using namespace llbuild;
using namespace llbuild::basic;
using namespace llbuild::buildsystem;
using namespace llvm;


// TODO Move this into some kind of libtestSupport?
#define ASSERT_NO_ERROR(code)                       \
  do {                                              \
    std::error_code __ec;                           \
    ASSERT_FALSE(__ec = (code)) << __ec.message();  \
  } while (0)

namespace {

/// Records delegate callbacks and makes them available via ``getTrace()``
/// and ``checkTrace()``.
///
/// Allows for command skipping via ``commandsToSkip``.
class TestBuildSystemFrontendDelegate : public BuildSystemFrontendDelegate {
  using super = BuildSystemFrontendDelegate;

  std::mutex traceMutex;
  std::string traceData;
  raw_string_ostream traceStream;
  int currentlyRunningTasks = 0;
  int maxRunningTasks = 0;

public:
  TestBuildSystemFrontendDelegate(SourceMgr& sourceMgr)
    : BuildSystemFrontendDelegate(sourceMgr, "client", 0),
      traceStream(traceData)
  {
  }

  int maxTaskParallism() { return maxRunningTasks; }

  std::unordered_set<std::string> commandsToSkip;

  void clearTrace() {
    std::lock_guard<std::mutex> lock(traceMutex);
    traceStream.flush();
    traceData.clear();
  }

  const std::string& getTrace() {
    std::lock_guard<std::mutex> lock(traceMutex);
    return traceStream.str();
  }

  bool checkTrace(StringRef expected) {
    expected = expected.ltrim();

    bool result = expected == getTrace();
    if (!result) {
      errs() << "error: failed to match expected trace.\n\n"
             << "Actual:\n" << getTrace() << "\n"
             << "Expected:\n" << expected << "\n";

      TmpDir tmpDir;

      std::string outputActual = tmpDir.str() + "/actual.log";
      {
        std::error_code ec;
        llvm::raw_fd_ostream os(outputActual, ec, llvm::sys::fs::F_Text);
        assert(!ec);
        os << getTrace();
        os.close();
        assert(!os.has_error());
      }

      std::string outputExpected = tmpDir.str() + "/expected.log";
      {
        std::error_code ec;
        llvm::raw_fd_ostream os(outputExpected, ec, llvm::sys::fs::F_Text);
        assert(!ec);
        os << expected;
        os.close();
        assert(!os.has_error());
      }

#if !TARGET_OS_IPHONE
      errs() << "Diff:\n";
      system(("diff '" + outputActual + "' '"+ outputExpected +"'").c_str());
#endif
    }

    return result;
  }

  virtual void hadCommandFailure() override {
    {
      std::lock_guard<std::mutex> lock(traceMutex);
      traceStream << __func__ << "\n";
    }
    super::hadCommandFailure();
  }

  virtual void commandPreparing(Command* command) override {
    {
      std::lock_guard<std::mutex> lock(traceMutex);
      traceStream << __func__ << ": " << command->getName() << "\n";
    }
    super::commandPreparing(command);
  }

  virtual bool shouldCommandStart(Command* command) override {
    {
      std::lock_guard<std::mutex> lock(traceMutex);
      traceStream << __func__ << ": " << command->getName() << "\n";
    }

    if (commandsToSkip.find(command->getName()) != commandsToSkip.end()) {
      return false;
    }

    return super::shouldCommandStart(command);
  }

  virtual void commandStarted(Command* command) override {
    {
      std::lock_guard<std::mutex> lock(traceMutex);
      traceStream << __func__ << ": " << command->getName() << "\n";
    }
    super::commandStarted(command);
  }

  virtual void commandFinished(Command* command, ProcessStatus result) override {
    {
      std::lock_guard<std::mutex> lock(traceMutex);
      traceStream << __func__ << ": " << command->getName() << ": "
                  << (int)result << "\n";
    }
    super::commandFinished(command, result);
  }

  virtual void commandProcessStarted(Command* command, ProcessHandle handle) override {
    {
      std::lock_guard<std::mutex> lock(traceMutex);

      // Keep track of max build parallelism
      currentlyRunningTasks++;
      if (currentlyRunningTasks > maxRunningTasks)
        maxRunningTasks = currentlyRunningTasks;

      traceStream << __func__ << ": " << command->getName() << "\n";
    }
    super::commandProcessStarted(command, handle);
  }

  virtual void commandProcessHadError(Command* command, ProcessHandle handle,
                                      const Twine& message) override {
    {
      std::lock_guard<std::mutex> lock(traceMutex);
      traceStream << __func__ << ": " << command->getName() << ": " << message
                  << "\n";
    }
    super::commandProcessHadError(command, handle, message);
  }

  virtual void commandProcessFinished(Command* command, ProcessHandle handle,
                                      const ProcessResult& result) override {
    {
      std::lock_guard<std::mutex> lock(traceMutex);
      currentlyRunningTasks--;
      traceStream << __func__ << ": " << command->getName() << ": "
                  << result.exitCode << "\n";
    }
    super::commandProcessFinished(command, handle, result);
  }

  virtual std::unique_ptr<Tool> lookupTool(StringRef name) override {
    return nullptr;
  }

  virtual void cycleDetected(const std::vector<core::Rule*>& items) override { }

  virtual void error(StringRef filename, const Token& at, const Twine& message) override {
    std::lock_guard<std::mutex> lock(traceMutex);
    traceStream << __func__ << ": " << message << "\n";
  }
};


/// Sets up state and provides utils for build system frontend tests.
class BuildSystemFrontendTest : public ::testing::Test {
protected:
  TmpDir tempDir{"FrontendTest"};
  std::unique_ptr<FileSystem> fs = createLocalFileSystem();
  SourceMgr sourceMgr;
  BuildSystemInvocation invocation;

  virtual void SetUp() override {
    invocation.chdirPath = tempDir.str();
    invocation.traceFilePath = tempDir.str() + "/trace.log";
  }

  void writeBuildFile(StringRef s) {
    std::error_code ec;
    raw_fd_ostream os(std::string(tempDir.str()) + "/build.llbuild", ec,
                      llvm::sys::fs::F_Text);
    ASSERT_NO_ERROR(ec);

    os << s;
  }
};


// We have commands { 3 -> 2 -> 1 }. We'd like to skip 2, but we still
// want 1 and 3 to run.
TEST_F(BuildSystemFrontendTest, commandSkipping) {
  writeBuildFile(R"END(
client:
    name: client

targets:
    "": ["3"]

commands:
    1:
        tool: shell
        outputs: ["1"]
        args: touch 1

    2:
        tool: shell
        inputs: ["1"]
        outputs: ["2"]
        args: touch 2

    3:
        tool: shell
        inputs: ["2"]
        outputs: ["3"]
        args: touch 3
)END");

  {
    TestBuildSystemFrontendDelegate delegate(sourceMgr);
    delegate.commandsToSkip.insert("2");

    BuildSystemFrontend frontend(delegate, invocation, createLocalFileSystem());
    ASSERT_TRUE(frontend.build(""));

    ASSERT_TRUE(delegate.checkTrace(R"END(
commandPreparing: 3
commandPreparing: 2
commandPreparing: 1
shouldCommandStart: 1
commandStarted: 1
commandProcessStarted: 1
commandProcessFinished: 1: 0
commandFinished: 1: 0
shouldCommandStart: 2
commandFinished: 2: 3
shouldCommandStart: 3
commandStarted: 3
commandProcessStarted: 3
commandProcessFinished: 3: 0
commandFinished: 3: 0
)END"));

    ASSERT_FALSE(fs->getFileInfo(tempDir.str() + "/1").isMissing());
    ASSERT_TRUE(fs->getFileInfo(tempDir.str() + "/2").isMissing());
    ASSERT_FALSE(fs->getFileInfo(tempDir.str() + "/3").isMissing());
  }

  // If we rebuild incrementally without skipping, we expect to run 2 and
  // re-run 3. 1 doesn't have to run at all, so we don't expect to get asked
  // if it should start.
  {
    TestBuildSystemFrontendDelegate delegate(sourceMgr);

    BuildSystemFrontend frontend(delegate, invocation, createLocalFileSystem());
    ASSERT_TRUE(frontend.build(""));

    ASSERT_TRUE(delegate.checkTrace(R"END(
commandPreparing: 2
shouldCommandStart: 2
commandStarted: 2
commandProcessStarted: 2
commandProcessFinished: 2: 0
commandFinished: 2: 0
commandPreparing: 3
shouldCommandStart: 3
commandStarted: 3
commandProcessStarted: 3
commandProcessFinished: 3: 0
commandFinished: 3: 0
)END"));

    ASSERT_FALSE(fs->getFileInfo(tempDir.str() + "/2").isMissing());
  }

}


// We have a dependency tree such that three branches, left, middle, and right,
// should be able to run in parallel. The tree contains both real dependcies on
// the products and virtual/phony dependencies. This tests what parallism we
// actually achieve in both a clean build and subsequent incremental builds.
TEST_F(BuildSystemFrontendTest, dependencyScanOrderBuildParallelism) {
  writeBuildFile(R"END(
client:
    name: client

targets:
    "<all>": ["top"]

commands:
    top:
        tool: shell
        inputs: ["left", "middle", "right", "<left>", "<middle>", "<right>"]
        outputs: ["top"]
        args: touch top

    gate-left:
        tool: phony
        inputs: ["left"]
        outputs: ["<left>"]
    left:
        tool: shell
        inputs: ["base", "<base>"]
        outputs: ["left"]
        args: sleep 1 && touch left

    gate-middle:
        tool: phony
        inputs: ["middle"]
        outputs: ["<middle>"]
    middle:
        tool: shell
        inputs: ["base", "<base>"]
        outputs: ["middle"]
        args: sleep 1 && touch middle

    gate-right:
        tool: phony
        inputs: ["right"]
        outputs: ["<right>"]
    right:
        tool: shell
        inputs: ["base", "<base>"]
        outputs: ["right"]
        args: sleep 1 && touch right

    gate-base:
        tool: phony
        inputs: ["base"]
        outputs: ["<base>"]
    base:
        tool: shell
        outputs: ["base"]
        args: touch base
        always-out-of-date: true
)END");

  // Test a clean build
  {
    TestBuildSystemFrontendDelegate delegate(sourceMgr);
    BuildSystemFrontend frontend(delegate, invocation, createLocalFileSystem());
    ASSERT_TRUE(frontend.build("<all>"));

    ASSERT_EQ(delegate.maxTaskParallism(), 3);
  }

  // Test an 'incremental' build where we have an existing build database
  {
    TestBuildSystemFrontendDelegate delegate(sourceMgr);
    BuildSystemFrontend frontend(delegate, invocation, createLocalFileSystem());
    ASSERT_TRUE(frontend.build("<all>"));

    ASSERT_EQ(delegate.maxTaskParallism(), 3);
  }
}


TEST_F(BuildSystemFrontendTest, missingShellArguments) {
  writeBuildFile(R"END(
client:
    name: basic

commands:
    C0:
        tool: shell
        args: []
)END");

  TestBuildSystemFrontendDelegate delegate(sourceMgr);

  BuildSystemFrontend frontend(delegate, invocation, createLocalFileSystem());
  ASSERT_FALSE(frontend.build(""));

  ASSERT_TRUE(delegate.checkTrace(R"END(
error: unable to configure client
error: unable to load build file
)END"));
}



class TestCancellationFrontendDelegate : public TestBuildSystemFrontendDelegate {
  using super = TestBuildSystemFrontendDelegate;

  std::mutex shouldCancelMutex;
  std::condition_variable shouldCancelCond;
  std::atomic<bool> shouldCancel{false};
  std::atomic<bool> done{false};

  std::thread cancelWorker;

public:
  TestCancellationFrontendDelegate(SourceMgr& sourceMgr)
  : TestBuildSystemFrontendDelegate(sourceMgr),
    cancelWorker(&TestCancellationFrontendDelegate::cancelHandler, this)
  {
  }

  ~TestCancellationFrontendDelegate() {
    done = true;
    shouldCancelCond.notify_all();
    cancelWorker.join();
  }

  std::atomic<bool> cancelAtStart{false};
  std::atomic<bool> cancelAtCommand{false};

  virtual void commandPreparing(Command* command) override {
    super::commandPreparing(command);

    if (cancelAtStart) {
      shouldCancel = true;
      shouldCancelCond.notify_all();
    }
  }

  virtual void commandProcessStarted(Command* command, ProcessHandle handle) override {
    super::commandProcessStarted(command, handle);

    if (cancelAtCommand) {
      shouldCancel = true;
      shouldCancelCond.notify_all();
    }
  }

private:
  void cancelHandler() {
    std::unique_lock<std::mutex> lock(shouldCancelMutex);
    while (!done) {
      if (shouldCancel) {
        cancel();
        shouldCancel = false;
      }
      shouldCancelCond.wait(lock);
    }
  }
};


TEST_F(BuildSystemFrontendTest, cancellation) {
  writeBuildFile(R"END(
client:
    name: client

targets:
    "": ["output"]

commands:
    C0:
        tool: shell
        outputs: ["output"]
        args: yes >/dev/null
)END");

  TestCancellationFrontendDelegate delegate(sourceMgr);
  BuildSystemFrontend frontend(delegate, invocation, createLocalFileSystem());

  // FIXME: This is a good candidate for a 'long running' testing mode. Increase
  // the number of reps to catch subtle race conditions. For example, some of
  // the issues fixed when this test was developed sometimes took thousands of
  // iterations to trigger.
  for (int i = 0; i < 100; ++i) {
    delegate.cancelAtStart = true;
    delegate.cancelAtCommand = false;
    frontend.build("");
    delegate.clearTrace();

    delegate.cancelAtStart = false;
    delegate.cancelAtCommand = true;
    frontend.build("");
    delegate.clearTrace();
  }
}



// We have commands { 2 -> 1 }, where two requires 1's output. We'd like to
// skip 1, we expect 2 to run and fail because 1 did not produce output.
TEST_F(BuildSystemFrontendTest, commandSkippingFailure) {
  writeBuildFile(R"END(
client:
    name: client

targets:
    "": ["2"]

commands:
    1:
        tool: shell
        outputs: ["1"]
        args: touch 1

    2:
        tool: shell
        inputs: ["1"]
        outputs: ["2"]
        args: cp 1 2
)END");

  {
    TestBuildSystemFrontendDelegate delegate(sourceMgr);
    delegate.commandsToSkip.insert("1");

    BuildSystemFrontend frontend(delegate, invocation, createLocalFileSystem());
    ASSERT_FALSE(frontend.build(""));
    ASSERT_EQ(1u, delegate.getNumFailedCommands());

#if defined(_WIN32)
    ASSERT_TRUE(delegate.checkTrace(R"END(
commandPreparing: 2
commandPreparing: 1
shouldCommandStart: 1
commandFinished: 1: 3
shouldCommandStart: 2
commandStarted: 2
commandProcessStarted: 2
commandProcessFinished: 2: 1
commandFinished: 2: 1
hadCommandFailure
)END"));
#else
    ASSERT_TRUE(delegate.checkTrace(R"END(
commandPreparing: 2
commandPreparing: 1
shouldCommandStart: 1
commandFinished: 1: 3
shouldCommandStart: 2
commandStarted: 2
commandProcessStarted: 2
commandProcessFinished: 2: 256
commandFinished: 2: 1
hadCommandFailure
)END"));
#endif
    ASSERT_TRUE(fs->getFileInfo(tempDir.str() + "/1").isMissing());
    ASSERT_TRUE(fs->getFileInfo(tempDir.str() + "/2").isMissing());
  }

  // If we rebuild incrementally without skipping, we expect to run 1 and 2.
  {
    TestBuildSystemFrontendDelegate delegate(sourceMgr);

    BuildSystemFrontend frontend(delegate, invocation, createLocalFileSystem());
    ASSERT_TRUE(frontend.build(""));

    ASSERT_TRUE(delegate.checkTrace(R"END(
commandPreparing: 2
commandPreparing: 1
shouldCommandStart: 1
commandStarted: 1
commandProcessStarted: 1
commandProcessFinished: 1: 0
commandFinished: 1: 0
shouldCommandStart: 2
commandStarted: 2
commandProcessStarted: 2
commandProcessFinished: 2: 0
commandFinished: 2: 0
)END"));

    ASSERT_FALSE(fs->getFileInfo(tempDir.str() + "/1").isMissing());
    ASSERT_FALSE(fs->getFileInfo(tempDir.str() + "/2").isMissing());
  }

}

// Built-in commands don't process skipped inputs the same way as external
// commands (for now). Add explicit tests to make sure they behave as expected.
TEST_F(BuildSystemFrontendTest, commandSkipping_NonExternalCommands_mkdir) {
  writeBuildFile(R"END(
client:
    name: client

targets:
    "": ["2"]

commands:
    1:
        tool: mkdir
        outputs: ["1"]
    2:
        tool: mkdir
        inputs: ["1"]
        outputs: ["2"]
)END");

  TestBuildSystemFrontendDelegate delegate(sourceMgr);
  delegate.commandsToSkip.insert("1");
  
  BuildSystemFrontend frontend(delegate, invocation, createLocalFileSystem());
  ASSERT_TRUE(frontend.build(""));

  ASSERT_TRUE(delegate.checkTrace(R"END(
commandPreparing: 2
commandPreparing: 1
shouldCommandStart: 1
commandFinished: 1: 3
shouldCommandStart: 2
commandStarted: 2
commandFinished: 2: 0
)END"));
}

// Built-in commands don't process skipped inputs the same way as external
// commands (for now). Add explicit tests to make sure they behave as expected.
TEST_F(BuildSystemFrontendTest, commandSkipping_NonExternalCommands_symlink) {
  writeBuildFile(R"END(
client:
    name: client

targets:
    "": ["3"]

commands:
    1:
        tool: symlink
        outputs: ["1"]
        contents: "x"
    2:
        tool: symlink
        inputs: ["1"]
        outputs: ["2"]
        contents: "y"
    3:
        tool: shell
        inputs: ["2"]
        outputs: ["3"]
)END" +
#if defined(_WIN32)
                 std::string("        args: del 2")
#else
                 std::string("        args: rm 2")
#endif
  );
  // We need to delete the symlink ourselves, because llvm's remove()
  // currently refuses to delete symlinks.

  TestBuildSystemFrontendDelegate delegate(sourceMgr);
  delegate.commandsToSkip.insert("1");
  
  BuildSystemFrontend frontend(delegate, invocation, createLocalFileSystem());
  ASSERT_TRUE(frontend.build(""));

  ASSERT_TRUE(delegate.checkTrace(R"END(
commandPreparing: 3
commandPreparing: 2
commandPreparing: 1
shouldCommandStart: 1
commandFinished: 1: 3
shouldCommandStart: 2
commandStarted: 2
commandFinished: 2: 0
shouldCommandStart: 3
commandStarted: 3
commandProcessStarted: 3
commandProcessFinished: 3: 0
commandFinished: 3: 0
)END"));
}

TEST_F(BuildSystemFrontendTest, singleNodeBuildLogsMissingInputs) {
  writeBuildFile(R"END(
client:
  name: client
)END");

  TestBuildSystemFrontendDelegate delegate(sourceMgr);
  BuildSystemFrontend frontend(delegate, invocation, createLocalFileSystem());

  ASSERT_FALSE(frontend.buildNode("/missing"));
  ASSERT_TRUE(delegate.checkTrace("error: missing input '/missing' and no rule to build it\n"));
}


TEST(BuildSystemInvocationTest, formatCycle) {
  BuildSystemInvocation invocation;

  class NullRule : public core::Rule {
  public:
    NullRule(const KeyType& key) : Rule(key) { }
    Task* createTask(core::BuildEngine&) override { return nullptr; }
    bool isResultValid(core::BuildEngine&, const core::ValueType&) override {
      return true;
    }
  };

  NullRule command{BuildKey::makeCommand("c").getKeyData()};
  NullRule customtask{BuildKey::makeCustomTask("c","t").getKeyData()};
  NullRule dircontents{BuildKey::makeDirectoryContents("/").getKeyData()};
  NullRule filtdircontents{BuildKey::makeFilteredDirectoryContents("/", {}).getKeyData()};
  NullRule dirtree{BuildKey::makeDirectoryTreeSignature("/", {}).getKeyData()};
  NullRule dirtreestruct{BuildKey::makeDirectoryTreeStructureSignature("/", {}).getKeyData()};
  NullRule node{BuildKey::makeNode("n").getKeyData()};
  NullRule stat{BuildKey::makeStat("f").getKeyData()};
  NullRule target{BuildKey::makeTarget("t").getKeyData()};

  std::vector<core::Rule*> cycle{
    &command,
    &customtask,
    &dircontents,
    &filtdircontents,
    &dirtree,
    &dirtreestruct,
    &node,
    &stat,
    &target
  };


  auto cyclestr = invocation.formatDetectedCycle(cycle);
  ASSERT_EQ(cyclestr, "cycle detected while building: command 'c' -> custom task 'c' -> directory-contents '/' -> filtered-directory-contents '/' -> directory-tree-signature '/' -> directory-tree-structure-signature '/' -> node 'n' -> stat 'f' -> target 't'");
}

}
