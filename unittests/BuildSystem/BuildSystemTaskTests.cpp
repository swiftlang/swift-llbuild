//===- BuildSystemTaskTests.cpp -------------------------------------------===//
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

#include "MockBuildSystemDelegate.h"
#include "TempDir.h"

#include "llbuild/Basic/LLVM.h"
#include "llbuild/BuildSystem/BuildDescription.h"
#include "llbuild/BuildSystem/BuildFile.h"
#include "llbuild/BuildSystem/BuildKey.h"
#include "llbuild/BuildSystem/BuildValue.h"
#include "llbuild/BuildSystem/BuildSystem.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <thread>

#include "gtest/gtest.h"

using namespace llvm;
using namespace llbuild;
using namespace llbuild::basic;
using namespace llbuild::buildsystem;
using namespace llbuild::unittests;

namespace {

class LoggingFileSystem : public FileSystem {
private:
  std::mutex deletedPathsMutex;
  std::vector<std::string> deletedPaths;
  std::mutex missingPathsMutex;
  std::vector<std::string> missingPaths;

  std::unique_ptr<FileSystem> realFS = createLocalFileSystem();

public:
  std::vector<std::string> getDeletedPaths() {
    std::unique_lock<std::mutex> lock(deletedPathsMutex);
    return deletedPaths;
  }

  std::vector<std::string> getMissingPaths() {
    std::unique_lock<std::mutex> lock(missingPathsMutex);
    return missingPaths;
  }

  LoggingFileSystem() {}

  virtual bool createDirectory(const std::string& path) override {
    return realFS->createDirectory(path);
  }

  virtual std::unique_ptr<llvm::MemoryBuffer> getFileContents(const std::string& path) override {
    return realFS->getFileContents(path);
  }

  virtual bool remove(const std::string& path) override {
    std::unique_lock<std::mutex> lock(deletedPathsMutex);
    deletedPaths.push_back(path);
    return true;
  }

  virtual FileInfo getFileInfo(const std::string& path) override {
    FileInfo info = realFS->getFileInfo(path);
    if (info.isMissing()) {
      std::unique_lock<std::mutex> lock(missingPathsMutex);
      missingPaths.push_back(path);
    }
    return info;
  }

  virtual FileInfo getLinkInfo(const std::string& path) override {
    return realFS->getLinkInfo(path);
  }
};

/// Check that we evaluate a path key properly.
TEST(BuildSystemTaskTests, basics) {
  TmpDir tempDir{ __FUNCTION__ };

  // Create a sample file.
  SmallString<256> path{ tempDir.str() };
  sys::path::append(path, "a.txt");
  auto testString = StringRef("Hello, world!\n");
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(path, ec, llvm::sys::fs::F_None);
    assert(!ec);
    os << testString;
  }

  // Create the build system.
  auto description = llvm::make_unique<BuildDescription>();
  MockBuildSystemDelegate delegate;
  BuildSystem system(delegate);
  system.loadDescription(std::move(description));

  // Build a specific key.
  auto result = system.build(BuildKey::makeNode(path));
  ASSERT_TRUE(result.hasValue());
  ASSERT_TRUE(result.getValue().isExistingInput());
  ASSERT_EQ(result.getValue().getOutputInfo().size, testString.size());
}


/// Check the evaluation of directory contents.
TEST(BuildSystemTaskTests, directoryContents) {
  TmpDir tempDir{ __FUNCTION__ };

  // Create a directory with sample files.
  SmallString<256> fileA{ tempDir.str() };
  sys::path::append(fileA, "fileA");
  SmallString<256> fileB{ tempDir.str() };
  sys::path::append(fileB, "fileB");
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(fileA, ec, llvm::sys::fs::F_Text);
    assert(!ec);
    os << "fileA";
  }
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(fileB, ec, llvm::sys::fs::F_Text);
    assert(!ec);
    os << "fileB";
  }
  
  // Create the build system.
  auto description = llvm::make_unique<BuildDescription>();
  MockBuildSystemDelegate delegate;
  BuildSystem system(delegate);
  system.loadDescription(std::move(description));

  // Build a specific key.
  {
    auto result = system.build(BuildKey::makeDirectoryContents(tempDir.str()));
    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(result->isDirectoryContents());
    ASSERT_EQ(result->getDirectoryContents(), std::vector<StringRef>({
                  StringRef("fileA"), StringRef("fileB") }));
  }

  // Check that a missing directory behaves properly.
  {
    auto result = system.build(BuildKey::makeDirectoryContents(
                                   tempDir.str() + "/missing-subpath"));
    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(result->isMissingInput());
  }
}


/// Check the evaluation of directory signatures.
TEST(BuildSystemTaskTests, directorySignature) {
  TmpDir tempDir{ __FUNCTION__ };
  auto localFS = createLocalFileSystem();
  
  // Create a directory with sample files.
  SmallString<256> fileA{ tempDir.str() };
  sys::path::append(fileA, "fileA");
  SmallString<256> fileB{ tempDir.str() };
  sys::path::append(fileB, "fileB");
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(fileA, ec, llvm::sys::fs::F_Text);
    assert(!ec);
    os << "fileA";
  }
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(fileB, ec, llvm::sys::fs::F_Text);
    assert(!ec);
    os << "fileB";
  } 
  SmallString<256> subdirA{ tempDir.str() };
  sys::path::append(subdirA, "subdirA");
  (void) llvm::sys::fs::create_directories(subdirA.str());
  SmallString<256> subdirFileA{ subdirA };
  sys::path::append(subdirFileA, "subdirFileA");
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(subdirFileA, ec, llvm::sys::fs::F_Text);
    assert(!ec);
    os << "subdirFileA";
  }
  
  // Create the build system.
  auto keyToBuild = BuildKey::makeDirectoryTreeSignature(tempDir.str());
  auto description = llvm::make_unique<BuildDescription>();
  MockBuildSystemDelegate delegate;
  BuildSystem system(delegate);
  system.loadDescription(std::move(description));

  // Build an initial value.
  auto resultA = system.build(keyToBuild);
  ASSERT_TRUE(resultA.hasValue() && resultA->isDirectoryTreeSignature());

  // Modify the immediate directory and rebuild.
  SmallString<256> fileC{ tempDir.str() };
  sys::path::append(fileC, "fileC");

  // We must write the file in a loop until the directory actually changes (we
  // currently assume we can trust the directory file info to detect changes,
  // which is not always strictly true).
  auto dirFileInfo = localFS->getFileInfo(tempDir.str());
  do {
    std::error_code ec;
    llvm::sys::fs::remove(fileC.str());
    llvm::raw_fd_ostream os(fileC, ec, llvm::sys::fs::F_Text);
    assert(!ec);
    os << "fileC";
  } while (dirFileInfo == localFS->getFileInfo(tempDir.str()));
  
  auto resultB = system.build(keyToBuild);
  ASSERT_TRUE(resultB.hasValue() && resultB->isDirectoryTreeSignature());
  ASSERT_TRUE(resultA->toData() != resultB->toData());

  // Modify the subdirectory and rebuild.
  SmallString<256> subdirFileD{ subdirA };
  sys::path::append(subdirFileD, "fileD");

  // We must write the file in a loop until the directory actually changes (we
  // currently assume we can trust the directory file info to detect changes,
  // which is not always strictly true).
  dirFileInfo = localFS->getFileInfo(subdirA.str());
  do {
    std::error_code ec;
    llvm::sys::fs::remove(subdirFileD.str());
    llvm::raw_fd_ostream os(subdirFileD, ec, llvm::sys::fs::F_Text);
    assert(!ec);
    os << "fileD";
  } while (dirFileInfo == localFS->getFileInfo(subdirA.str()));
  
  auto resultC = system.build(keyToBuild);
  ASSERT_TRUE(resultC.hasValue() && resultC->isDirectoryTreeSignature());
  ASSERT_TRUE(resultA->toData() != resultB->toData());
  ASSERT_TRUE(resultA->toData() != resultC->toData());
}

TEST(BuildSystemTaskTests, doesNotProcessDependenciesAfterCancellation) {
  TmpDir tempDir{ __FUNCTION__ };

  std::string outputFile = tempDir.str() + "/output.txt";

  SmallString<256> manifest{ tempDir.str() };
  sys::path::append(manifest, "manifest.llbuild");
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(manifest, ec, llvm::sys::fs::F_Text);
    assert(!ec);

    os << "client:\n"
"  name: mock\n"
"\n"
"commands:\n"
"  WAIT:\n"
"    tool: shell\n"
"    deps: \"/tmp/deps.info\"\n"
"    deps-style: dependency-info\n"
"    inputs: [\"<cleanup>\"]\n";

    os << "    outputs: [\"" << outputFile << "\"]\n";
    os << "    description: \"WAIT\"\n"
"    args:\n";

    os << "      touch " << outputFile << "\n";
    os << "      sleep 9999\n";
  }

  auto keyToBuild = BuildKey::makeCommand("WAIT");
  MockBuildSystemDelegate delegate;
  BuildSystem system(delegate);
  bool loadingResult = system.loadDescription(manifest);
  ASSERT_TRUE(loadingResult);

  std::unique_ptr<llbuild::basic::FileSystem> fs = llbuild::basic::createLocalFileSystem();
  std::thread cancelThread([&] {
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

    system.cancel();
  });

  auto result = system.build(keyToBuild);

  cancelThread.join();
  // This is what we are testing for, if dependencies were processed, an error would occur during the build
  ASSERT_EQ(delegate.getMessages().size(), 0U);
}


/// Check that cancellation applies to enqueued jobs.
TEST(BuildSystemTaskTests, cancelAllInQueue) {
// Disabled: <rdar://problem/32142112> BuildSystem/BuildSystemTests/BuildSystemTaskTests.cancelAllInQueue FAILED
#ifdef false
  TmpDir tempDir{ __FUNCTION__ };

  SmallString<256> manifest{ tempDir.str() };
  sys::path::append(manifest, "manifest.llbuild");
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(manifest, ec, llvm::sys::fs::F_Text);
    assert(!ec);

    os << R"END(
client:
  name: mock

commands:
  ALL:
    tool: phony
    inputs: ["<C1>", "<C2>"]
  C1:
    tool: shell
    outputs: ["<C1>"]
    args: sleep 60
  C2:
    tool: shell
    outputs: ["<C2>"]
    args: sleep 60
)END";
  }
    
  auto keyToBuild = BuildKey::makeCommand("ALL");
  MockBuildSystemDelegate delegate(/*trackAllMessages=*/true);
  BuildSystem system(delegate);
  bool loadingResult = system.loadDescription(manifest);
  ASSERT_TRUE(loadingResult);

  std::unique_ptr<llbuild::basic::FileSystem> fs = llbuild::basic::createLocalFileSystem();
  std::thread cancelThread([&] {
      // Cancel the build once it appears to have started.
      time_t start = ::time(NULL);
      while (::time(NULL) < start + 5) {
        auto messages = delegate.getMessages();
        if (std::find(messages.begin(), messages.end(),
                      "commandStarted(C1)") != messages.end()) {
          system.cancel();
          return;
        }
      }
      // If we got here, we timed out waiting for the start.
      abort();
  });

  auto result = system.build(keyToBuild);
  cancelThread.join();

  ASSERT_EQ(std::vector<std::string>({
          "commandPreparing(ALL)",
          "commandPreparing(C2)",
          "commandPreparing(C1)",
          "commandStarted(C1)",
          "commandFinished(C1)",
          }), delegate.getMessages());
#endif
}

// Tests the behaviour of StaleFileRemovalTool
TEST(BuildSystemTaskTests, staleFileRemoval) {
  TmpDir tempDir{ __FUNCTION__ };

  SmallString<256> manifest{ tempDir.str() };
  sys::path::append(manifest, "manifest.llbuild");
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(manifest, ec, llvm::sys::fs::F_Text);
    assert(!ec);

    os << R"END(
client:
  name: mock

commands:
    C.1:
      tool: stale-file-removal
      description: STALE-FILE-REMOVAL
      expectedOutputs: ["a.out"]
)END";
  }

  auto keyToBuild = BuildKey::makeCommand("C.1");

  SmallString<256> builddb{ tempDir.str() };
  sys::path::append(builddb, "build.db");

  {
    MockBuildSystemDelegate delegate(/*trackAllMessages=*/true);
    BuildSystem system(delegate);
    system.attachDB(builddb.c_str(), nullptr);

    bool loadingResult = system.loadDescription(manifest);
    ASSERT_TRUE(loadingResult);

    auto result = system.build(keyToBuild);

    ASSERT_TRUE(result.getValue().isStaleFileRemoval());
    ASSERT_EQ(result.getValue().getStaleFileList().size(), 1UL);
    ASSERT_TRUE(strcmp(result.getValue().getStaleFileList()[0].str().c_str(), "a.out") == 0);

    ASSERT_EQ(std::vector<std::string>({
      "commandPreparing(C.1)",
      "commandStarted(C.1)",
      "commandFinished(C.1: 0)",
    }), delegate.getMessages());
  }

  {
    std::error_code ec;
    llvm::raw_fd_ostream os(manifest, ec, llvm::sys::fs::F_Text);
    assert(!ec);

    os << R"END(
client:
  name: mock

commands:
  C.1:
    tool: stale-file-removal
    description: STALE-FILE-REMOVAL
    expectedOutputs: ["b.out"]
)END";
  }

  auto mockFS = std::make_shared<LoggingFileSystem>();
  MockBuildSystemDelegate delegate(/*trackAllMessages=*/true, mockFS);
  BuildSystem system(delegate);
  system.attachDB(builddb.c_str(), nullptr);
  bool loadingResult = system.loadDescription(manifest);
  ASSERT_TRUE(loadingResult);
  auto result = system.build(keyToBuild);

  ASSERT_TRUE(result.getValue().isStaleFileRemoval());
  ASSERT_EQ(result.getValue().getStaleFileList().size(), 1UL);
  ASSERT_TRUE(strcmp(result.getValue().getStaleFileList()[0].str().c_str(), "b.out") == 0);

  ASSERT_EQ(std::vector<std::string>({ "a.out" }), mockFS->getDeletedPaths());

  ASSERT_EQ(std::vector<std::string>({
    "commandPreparing(C.1)",
    "commandStarted(C.1)",
    "commandNote(C.1) Removed stale file 'a.out'\n",
    "commandFinished(C.1: 0)",
  }), delegate.getMessages());
}

TEST(BuildSystemTaskTests, staleFileRemovalWithRoots) {
  TmpDir tempDir{ __FUNCTION__ };

  SmallString<256> manifest{ tempDir.str() };
  sys::path::append(manifest, "manifest.llbuild");
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(manifest, ec, llvm::sys::fs::F_Text);
    assert(!ec);

    os << R"END(
client:
  name: mock

commands:
    C.1:
      tool: stale-file-removal
      description: STALE-FILE-REMOVAL
      expectedOutputs: ["/bar/a.out", "/foo", "/foobar.txt"]
)END";
  }

  auto keyToBuild = BuildKey::makeCommand("C.1");

  SmallString<256> builddb{ tempDir.str() };
  sys::path::append(builddb, "build.db");

  {
    MockBuildSystemDelegate delegate(/*trackAllMessages=*/true);
    BuildSystem system(delegate);
    system.attachDB(builddb.c_str(), nullptr);

    bool loadingResult = system.loadDescription(manifest);
    ASSERT_TRUE(loadingResult);

    auto result = system.build(keyToBuild);

    ASSERT_TRUE(result.getValue().isStaleFileRemoval());
    ASSERT_EQ(result.getValue().getStaleFileList().size(), 3UL);
    ASSERT_TRUE(strcmp(result.getValue().getStaleFileList()[0].str().c_str(), "/bar/a.out") == 0);
    ASSERT_TRUE(strcmp(result.getValue().getStaleFileList()[1].str().c_str(), "/foo") == 0);
    ASSERT_TRUE(strcmp(result.getValue().getStaleFileList()[2].str().c_str(), "/foobar.txt") == 0);

    ASSERT_EQ(std::vector<std::string>({
      "commandPreparing(C.1)",
      "commandStarted(C.1)",
      "commandFinished(C.1: 0)",
    }), delegate.getMessages());
  }

  {
    std::error_code ec;
    llvm::raw_fd_ostream os(manifest, ec, llvm::sys::fs::F_Text);
    assert(!ec);

    os << R"END(
client:
  name: mock

commands:
    C.1:
      tool: stale-file-removal
      description: STALE-FILE-REMOVAL
      expectedOutputs: ["/bar/b.out"]
      roots: ["/foo"]
)END";
  }

  auto mockFS = std::make_shared<LoggingFileSystem>();
  MockBuildSystemDelegate delegate(/*trackAllMessages=*/true, mockFS);
  BuildSystem system(delegate);
  system.attachDB(builddb.c_str(), nullptr);
  bool loadingResult = system.loadDescription(manifest);
  ASSERT_TRUE(loadingResult);
  auto result = system.build(keyToBuild);

  ASSERT_TRUE(result.getValue().isStaleFileRemoval());
  ASSERT_EQ(result.getValue().getStaleFileList().size(), 1UL);
  ASSERT_TRUE(strcmp(result.getValue().getStaleFileList()[0].str().c_str(), "/bar/b.out") == 0);

  auto messages = delegate.getMessages();
  std::sort(messages.begin(), messages.end());

  ASSERT_EQ(std::vector<std::string>({ "/foo" }), mockFS->getDeletedPaths());

  ASSERT_EQ(std::vector<std::string>({
    "commandFinished(C.1: 0)",
    "commandNote(C.1) Removed stale file '/foo'\n",
    "commandPreparing(C.1)",
    "commandStarted(C.1)",
    "commandWarning(C.1) Stale file '/bar/a.out' is located outside of the allowed root paths.\n",
    "commandWarning(C.1) Stale file '/foobar.txt' is located outside of the allowed root paths.\n",
  }), messages);
}

TEST(BuildSystemTaskTests, staleFileRemovalWithRootsEnforcesAbsolutePaths) {
  TmpDir tempDir{ __FUNCTION__ };

  SmallString<256> manifest{ tempDir.str() };
  sys::path::append(manifest, "manifest.llbuild");
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(manifest, ec, llvm::sys::fs::F_Text);
    assert(!ec);

    os << R"END(
client:
  name: mock

commands:
    C.1:
      tool: stale-file-removal
      description: STALE-FILE-REMOVAL
      expectedOutputs: ["a.out"]
)END";
  }

  auto keyToBuild = BuildKey::makeCommand("C.1");

  SmallString<256> builddb{ tempDir.str() };
  sys::path::append(builddb, "build.db");

  {
    MockBuildSystemDelegate delegate(/*trackAllMessages=*/true);
    BuildSystem system(delegate);
    system.attachDB(builddb.c_str(), nullptr);

    bool loadingResult = system.loadDescription(manifest);
    ASSERT_TRUE(loadingResult);

    auto result = system.build(keyToBuild);

    ASSERT_TRUE(result.getValue().isStaleFileRemoval());
    ASSERT_EQ(result.getValue().getStaleFileList().size(), 1UL);
    ASSERT_TRUE(strcmp(result.getValue().getStaleFileList()[0].str().c_str(), "a.out") == 0);

    ASSERT_EQ(std::vector<std::string>({
      "commandPreparing(C.1)",
      "commandStarted(C.1)",
      "commandFinished(C.1: 0)",
    }), delegate.getMessages());
  }

  {
    std::error_code ec;
    llvm::raw_fd_ostream os(manifest, ec, llvm::sys::fs::F_Text);
    assert(!ec);

    os << R"END(
client:
  name: mock

commands:
    C.1:
      tool: stale-file-removal
      description: STALE-FILE-REMOVAL
      expectedOutputs: ["/bar/b.out"]
      roots: ["/foo"]
)END";
  }

  MockBuildSystemDelegate delegate(/*trackAllMessages=*/true);
  BuildSystem system(delegate);
  system.attachDB(builddb.c_str(), nullptr);
  bool loadingResult = system.loadDescription(manifest);
  ASSERT_TRUE(loadingResult);
  auto result = system.build(keyToBuild);

  ASSERT_TRUE(result.getValue().isStaleFileRemoval());
  ASSERT_EQ(result.getValue().getStaleFileList().size(), 1UL);
  ASSERT_TRUE(strcmp(result.getValue().getStaleFileList()[0].str().c_str(), "/bar/b.out") == 0);

  ASSERT_EQ(std::vector<std::string>({
    "commandPreparing(C.1)",
    "commandStarted(C.1)",
    "commandWarning(C.1) Stale file 'a.out' has a relative path. This is invalid in combination with the root path attribute.\n",
    "commandFinished(C.1: 0)",
  }), delegate.getMessages());
}

TEST(BuildSystemTaskTests, staleFileRemovalWithRootsIsAgnosticToTrailingPathSeparator) {
  TmpDir tempDir{ __FUNCTION__ };

  SmallString<256> manifest{ tempDir.str() };
  sys::path::append(manifest, "manifest.llbuild");
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(manifest, ec, llvm::sys::fs::F_Text);
    assert(!ec);

    os << R"END(
client:
  name: mock

commands:
    C.1:
      tool: stale-file-removal
      description: STALE-FILE-REMOVAL
      expectedOutputs: ["a.out", "/foo/"]
)END";
  }

  auto keyToBuild = BuildKey::makeCommand("C.1");

  SmallString<256> builddb{ tempDir.str() };
  sys::path::append(builddb, "build.db");

  {
    MockBuildSystemDelegate delegate(/*trackAllMessages=*/true);
    BuildSystem system(delegate);
    system.attachDB(builddb.c_str(), nullptr);

    bool loadingResult = system.loadDescription(manifest);
    ASSERT_TRUE(loadingResult);

    auto result = system.build(keyToBuild);

    ASSERT_TRUE(result.getValue().isStaleFileRemoval());
    ASSERT_EQ(result.getValue().getStaleFileList().size(), 2UL);
    ASSERT_TRUE(strcmp(result.getValue().getStaleFileList()[0].str().c_str(), "a.out") == 0);

    ASSERT_EQ(std::vector<std::string>({
      "commandPreparing(C.1)",
      "commandStarted(C.1)",
      "commandFinished(C.1: 0)",
    }), delegate.getMessages());
  }

  {
    std::error_code ec;
    llvm::raw_fd_ostream os(manifest, ec, llvm::sys::fs::F_Text);
    assert(!ec);

    os << R"END(
client:
  name: mock

commands:
    C.1:
      tool: stale-file-removal
      description: STALE-FILE-REMOVAL
      expectedOutputs: ["/bar/b.out"]
      roots: ["/foo/"]
)END";
  }

  auto mockFS = std::make_shared<LoggingFileSystem>();
  MockBuildSystemDelegate delegate(/*trackAllMessages=*/true, mockFS);
  BuildSystem system(delegate);
  system.attachDB(builddb.c_str(), nullptr);
  bool loadingResult = system.loadDescription(manifest);
  ASSERT_TRUE(loadingResult);
  auto result = system.build(keyToBuild);

  ASSERT_TRUE(result.getValue().isStaleFileRemoval());
  ASSERT_EQ(result.getValue().getStaleFileList().size(), 1UL);
  ASSERT_TRUE(strcmp(result.getValue().getStaleFileList()[0].str().c_str(), "/bar/b.out") == 0);

  ASSERT_EQ(std::vector<std::string>({ "/foo/" }), mockFS->getDeletedPaths());

  auto messages = delegate.getMessages();
  std::sort(messages.begin(), messages.end());

  ASSERT_EQ(std::vector<std::string>({
    "commandFinished(C.1: 0)",
    "commandNote(C.1) Removed stale file '/foo/'\n",
    "commandPreparing(C.1)",
    "commandStarted(C.1)",
    "commandWarning(C.1) Stale file 'a.out' has a relative path. This is invalid in combination with the root path attribute.\n",
  }), messages);
}

TEST(BuildSystemTaskTests, staleFileRemovalWithManyFiles) {
  TmpDir tempDir{ __FUNCTION__ };

  SmallString<256> manifest{ tempDir.str() };
  sys::path::append(manifest, "manifest.llbuild");
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(manifest, ec, llvm::sys::fs::F_Text);
    assert(!ec);

    os << R"END(
client:
  name: mock

commands:
    C.1:
      tool: stale-file-removal
      expectedOutputs: ["", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/Objects-normal/x86_64/empty.o", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/A/Headers/CoreBasic-extra-header.h", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/A/Headers/CoreBasic.h", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/A/Resources/CoreBasic-extra-resource.txt", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/A/Resources/CoreBasic-resource.txt", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/A/Modules/module.modulemap", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/A/CoreBasic, /var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/A", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/A/Headers", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/A/Resources", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/A/Resources/Info.plist", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/CoreBasic", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Headers", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Modules", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Resources", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/Current", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/CoreBasic-all-non-framework-target-headers.hmap", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/CoreBasic-all-target-headers.hmap", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/CoreBasic-generated-files.hmap", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/CoreBasic-own-target-headers.hmap", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/CoreBasic-project-headers.hmap", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/CoreBasic.hmap", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/Objects-normal/x86_64/CoreBasic.LinkFileList", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/all-product-headers.yaml", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/module.modulemap"]
      roots: ["/tmp/basic.dst", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products"]
)END";
  }

  auto keyToBuild = BuildKey::makeCommand("C.1");

  SmallString<256> builddb{ tempDir.str() };
  sys::path::append(builddb, "build.db");

  // We will check that the same file is present in both file lists, so it should not show up in the difference.
  std::string linkFileList = "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/Objects-normal/x86_64/CoreBasic.LinkFileList";

  {
    MockBuildSystemDelegate delegate(/*trackAllMessages=*/true);
    BuildSystem system(delegate);
    system.attachDB(builddb.c_str(), nullptr);

    bool loadingResult = system.loadDescription(manifest);
    ASSERT_TRUE(loadingResult);

    auto result = system.build(keyToBuild);

    ASSERT_TRUE(result.getValue().isStaleFileRemoval());
    ASSERT_EQ(result.getValue().getStaleFileList().size(), 50UL);

    // Check that `LinkFileList` is present in list of files of initial build
    bool hasLinkFileList = false;
    for (auto staleFile : result.getValue().getStaleFileList()) {
      if (staleFile == linkFileList) {
        hasLinkFileList = true;
      }
    }
    ASSERT_TRUE(hasLinkFileList);

    ASSERT_EQ(std::vector<std::string>({
      "commandPreparing(C.1)",
      "commandStarted(C.1)",
      "commandFinished(C.1: 0)",
    }), delegate.getMessages());
  }

  {
    std::error_code ec;
    llvm::raw_fd_ostream os(manifest, ec, llvm::sys::fs::F_Text);
    assert(!ec);

    os << R"END(
client:
  name: mock

commands:
    C.1:
      tool: stale-file-removal
      expectedOutputs: ["/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/Objects-normal/x86_64/empty.o", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/A/Headers/CoreBasic.h", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/A/Resources/CoreBasic-resource.txt", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/A/CoreBasic", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/A", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/A/Headers", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/A/Resources", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/A/Resources/Info.plist", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/CoreBasic", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Headers", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Resources", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products/Debug/CoreBasic.framework/Versions/Current", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/CoreBasic-all-non-framework-target-headers.hmap", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/CoreBasic-all-target-headers.hmap", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/CoreBasic-generated-files.hmap", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/CoreBasic-own-target-headers.hmap", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/CoreBasic-project-headers.hmap", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/CoreBasic.hmap", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/Objects-normal/x86_64/CoreBasic.LinkFileList", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates/basic.build/Debug/CoreBasic.build/all-product-headers.yaml"]
      roots: ["/tmp/basic.dst", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Intermediates", "/var/folders/1f/r6txd12925s9kytl1_pckt_w0000gp/T/Tests/Build/Products"]
)END";
  }

  MockBuildSystemDelegate delegate(/*trackAllMessages=*/true);
  BuildSystem system(delegate);
  system.attachDB(builddb.c_str(), nullptr);
  bool loadingResult = system.loadDescription(manifest);
  ASSERT_TRUE(loadingResult);
  auto result = system.build(keyToBuild);

  ASSERT_TRUE(result.getValue().isStaleFileRemoval());
  ASSERT_EQ(result.getValue().getStaleFileList().size(), 22UL);

  // Check that `LinkFileList` is present in list of files of second build
  bool hasLinkFileList = false;
  for (auto staleFile : result.getValue().getStaleFileList()) {
    if (staleFile == linkFileList) {
      hasLinkFileList = true;
    }
  }
  ASSERT_TRUE(hasLinkFileList);

  // Check that `LinkFileList` is not present in the diff
  std::vector<std::string> messages = delegate.getMessages();
  ASSERT_FALSE(std::find(messages.begin(), messages.end(), "cannot remove stale file '" + linkFileList + "': No such file or directory") != messages.end());
}

TEST(BuildSystemTaskTests, staleFileRemovalPathIsPrefixedByPath) {
  ASSERT_TRUE(pathIsPrefixedByPath("/foo/bar", "/foo"));
  ASSERT_TRUE(pathIsPrefixedByPath("/foo", "/foo"));
  ASSERT_TRUE(pathIsPrefixedByPath("/foo/", "/foo"));
  ASSERT_TRUE(pathIsPrefixedByPath("/foo", "/foo/"));

  ASSERT_FALSE(pathIsPrefixedByPath("/bar", "/foo"));
  ASSERT_FALSE(pathIsPrefixedByPath("/foobar", "/foo"));
}

}
