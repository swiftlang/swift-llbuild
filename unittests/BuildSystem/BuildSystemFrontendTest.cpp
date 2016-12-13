//===- unittests/BuildSystem/BuildSystemFrontendTest.cpp ------------------===//
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

#include "llbuild/Basic/FileSystem.h"
#include "llbuild/BuildSystem/BuildSystemFrontend.h"
#include "llbuild/BuildSystem/BuildFile.h"

#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Path.h"
#include "llvm/ADT/StringRef.h"

#include "gtest/gtest.h"

#include <unordered_set>
#include <mutex>

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


// TODO Move this into some kind of libtestSupport?
// Cribbed from llvm, where it's been since removed.
namespace {

using namespace std;
using namespace llvm::sys::fs;

error_code _remove_all_r(StringRef path, file_type ft, uint32_t &count) {
  if (ft == file_type::directory_file) {
    error_code ec;
    directory_iterator i(path, ec);
    if (ec)
      return ec;

    for (directory_iterator e; i != e; i.increment(ec)) {
      if (ec)
        return ec;

      file_status st;

      if (error_code ec = i->status(st))
        return ec;

      if (error_code ec = _remove_all_r(i->path(), st.type(), count))
        return ec;
    }

    if (error_code ec = remove(path, false))
      return ec;

    ++count; // Include the directory itself in the items removed.
  } else {
    if (error_code ec = remove(path, false))
      return ec;

    ++count;
  }

  return error_code();
}

error_code remove_all(const Twine &path, uint32_t &num_removed) {
  SmallString<128> path_storage;
  StringRef p = path.toStringRef(path_storage);

  file_status fs;
  if (error_code ec = status(path, fs))
    return ec;
  num_removed = 0;
  return _remove_all_r(p, fs.type(), num_removed);
}

error_code remove_all(const Twine &path) {
  uint32_t num_removed = 0;
  return remove_all(path, num_removed);
}

}

namespace {

// TODO Move this into some kind of libtestSupport?
/// Creates a temporary directory in its constructor and removes it in its
/// destructor. Makes it available via str() and c_str().
class TmpDir {
private:
  TmpDir(const TmpDir&) = delete;
  TmpDir& operator=(const TmpDir&) = delete;

  SmallString<256> tempDir;

public:
  TmpDir(StringRef namePrefix = "") {
    SmallString<256> tempDirPrefix;
    llvm::sys::path::system_temp_directory(true, tempDirPrefix);
    llvm::sys::path::append(tempDirPrefix, namePrefix);

    std::error_code ec = llvm::sys::fs::createUniqueDirectory
      (Twine(tempDirPrefix),
       tempDir);
    assert(!ec);
    (void)ec;
  }

  ~TmpDir() {
    std::error_code ec = remove_all(Twine(tempDir));
    assert(!ec);
    (void)ec;
  }

  const char *c_str() { return tempDir.c_str(); }
  std::string str() const { return tempDir.str(); }
};


/// Records delegate callbacks and makes them available via ``getTrace()``
/// and ``checkTrace()``.
///
/// Allows for command skipping via ``commandsToSkip``.
class TestBuildSystemFrontendDelegate : public BuildSystemFrontendDelegate {
  using super = BuildSystemFrontendDelegate;
  FileSystem& fs;

  std::mutex traceMutex;
  std::string traceData;
  raw_string_ostream traceStream;

public:
  TestBuildSystemFrontendDelegate(SourceMgr& sourceMgr,
                                  const BuildSystemInvocation& invocation,
                                  FileSystem& fs):
      BuildSystemFrontendDelegate(sourceMgr, invocation, "client", 0),
      fs(fs),
      traceStream(traceData)
  {
  }

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

      errs() << "Diff:\n";
      system(("diff '" + outputActual + "' '"+ outputExpected +"'").c_str());
    }

    return result;
  }

  virtual void hadCommandFailure() override {
    {
      std::lock_guard<std::mutex> lock(traceMutex);
      traceStream << __FUNCTION__ << "\n";
    }
    super::hadCommandFailure();
  }

  virtual void commandPreparing(Command* command) override {
    {
      std::lock_guard<std::mutex> lock(traceMutex);
      traceStream << __FUNCTION__ << ": " << command->getName() << "\n";
    }
    super::commandPreparing(command);
  }

  virtual bool shouldCommandStart(Command* command) override {
    {
      std::lock_guard<std::mutex> lock(traceMutex);
      traceStream << __FUNCTION__ << ": " << command->getName() << "\n";
    }

    if (commandsToSkip.find(command->getName()) != commandsToSkip.end()) {
      return false;
    }

    return super::shouldCommandStart(command);
  }

  virtual void commandStarted(Command* command) override {
    {
      std::lock_guard<std::mutex> lock(traceMutex);
      traceStream << __FUNCTION__ << ": " << command->getName() << "\n";
    }
    super::commandStarted(command);
  }

  virtual void commandFinished(Command* command) override {
    {
      std::lock_guard<std::mutex> lock(traceMutex);
      traceStream << __FUNCTION__ << ": " << command->getName() << "\n";
    }
    super::commandFinished(command);
  }

  virtual void commandProcessStarted(Command* command, ProcessHandle handle) override {
    {
      std::lock_guard<std::mutex> lock(traceMutex);
      traceStream << __FUNCTION__ << ": " << command->getName() << "\n";
    }
    super::commandProcessStarted(command, handle);
  }

  virtual void commandProcessHadError(Command* command, ProcessHandle handle,
                                      const Twine& message) override {
    {
      std::lock_guard<std::mutex> lock(traceMutex);
      traceStream << __FUNCTION__ << ": " << command->getName() << ": " << message << "\n";
    }
    super::commandProcessHadError(command, handle, message);
  }

  virtual void commandProcessFinished(Command* command, ProcessHandle handle,
                                      int exitStatus) override {
    {
        std::lock_guard<std::mutex> lock(traceMutex);
        traceStream << __FUNCTION__ << ": " << command->getName() << ": " << exitStatus << "\n";
    }
    super::commandProcessFinished(command, handle, exitStatus);
  }

  virtual FileSystem& getFileSystem() override {
    return fs;
  }

  virtual std::unique_ptr<Tool> lookupTool(StringRef name) override {
    return nullptr;
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
    TestBuildSystemFrontendDelegate delegate(sourceMgr, invocation, *fs);
    delegate.commandsToSkip.insert("2");

    BuildSystemFrontend frontend(delegate, invocation);
    ASSERT_TRUE(frontend.build(""));

    ASSERT_TRUE(delegate.checkTrace(R"END(
commandPreparing: 3
commandPreparing: 2
commandPreparing: 1
shouldCommandStart: 1
commandStarted: 1
commandProcessStarted: 1
commandProcessFinished: 1: 0
commandFinished: 1
shouldCommandStart: 2
commandFinished: 2
shouldCommandStart: 3
commandStarted: 3
commandProcessStarted: 3
commandProcessFinished: 3: 0
commandFinished: 3
)END"));

    ASSERT_FALSE(fs->getFileInfo(tempDir.str() + "/1").isMissing());
    ASSERT_TRUE(fs->getFileInfo(tempDir.str() + "/2").isMissing());
    ASSERT_FALSE(fs->getFileInfo(tempDir.str() + "/3").isMissing());
  }

  // If we rebuild incrementally without skipping, we expect to run 2 and
  // re-run 3. 1 doesn't have to run at all, so we don't expect to get asked
  // if it should start.
  {
    TestBuildSystemFrontendDelegate delegate(sourceMgr, invocation, *fs);

    BuildSystemFrontend frontend(delegate, invocation);
    ASSERT_TRUE(frontend.build(""));

    ASSERT_TRUE(delegate.checkTrace(R"END(
commandPreparing: 2
shouldCommandStart: 2
commandStarted: 2
commandProcessStarted: 2
commandProcessFinished: 2: 0
commandFinished: 2
commandPreparing: 3
shouldCommandStart: 3
commandStarted: 3
commandProcessStarted: 3
commandProcessFinished: 3: 0
commandFinished: 3
)END"));

    ASSERT_FALSE(fs->getFileInfo(tempDir.str() + "/2").isMissing());
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
    TestBuildSystemFrontendDelegate delegate(sourceMgr, invocation, *fs);
    delegate.commandsToSkip.insert("1");

    BuildSystemFrontend frontend(delegate, invocation);
    ASSERT_FALSE(frontend.build(""));
    ASSERT_EQ(1u, delegate.getNumFailedCommands());

    ASSERT_TRUE(delegate.checkTrace(R"END(
commandPreparing: 2
commandPreparing: 1
shouldCommandStart: 1
commandFinished: 1
shouldCommandStart: 2
commandStarted: 2
commandProcessStarted: 2
commandProcessFinished: 2: 256
commandFinished: 2
hadCommandFailure
)END"));

    ASSERT_TRUE(fs->getFileInfo(tempDir.str() + "/1").isMissing());
    ASSERT_TRUE(fs->getFileInfo(tempDir.str() + "/2").isMissing());
  }

  // If we rebuild incrementally without skipping, we expect to run 1 and 2.
  {
    TestBuildSystemFrontendDelegate delegate(sourceMgr, invocation, *fs);

    BuildSystemFrontend frontend(delegate, invocation);
    ASSERT_TRUE(frontend.build(""));

    ASSERT_TRUE(delegate.checkTrace(R"END(
commandPreparing: 2
commandPreparing: 1
shouldCommandStart: 1
commandStarted: 1
commandProcessStarted: 1
commandProcessFinished: 1: 0
commandFinished: 1
shouldCommandStart: 2
commandStarted: 2
commandProcessStarted: 2
commandProcessFinished: 2: 0
commandFinished: 2
)END"));

    ASSERT_FALSE(fs->getFileInfo(tempDir.str() + "/1").isMissing());
    ASSERT_FALSE(fs->getFileInfo(tempDir.str() + "/2").isMissing());
  }

}

}
