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
#include "llbuild/BuildSystem/BuildKey.h"
#include "llbuild/BuildSystem/BuildValue.h"
#include "llbuild/BuildSystem/BuildSystem.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include "gtest/gtest.h"

using namespace llvm;
using namespace llbuild;
using namespace llbuild::buildsystem;
using namespace llbuild::unittests;

namespace {

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
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(fileC, ec, llvm::sys::fs::F_Text);
    assert(!ec);
    os << "fileC";
  }
  auto resultB = system.build(keyToBuild);
  ASSERT_TRUE(resultB.hasValue() && resultB->isDirectoryTreeSignature());
  ASSERT_TRUE(resultA->toData() != resultB->toData());

  // Modify the subdirectory and rebuild.
  SmallString<256> subdirFileD{ subdirA };
  sys::path::append(subdirFileD, "fileD");
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(subdirFileD, ec, llvm::sys::fs::F_Text);
    assert(!ec);
    os << "fileD";
  }
  auto resultC = system.build(keyToBuild);
  ASSERT_TRUE(resultC.hasValue() && resultC->isDirectoryTreeSignature());
  ASSERT_TRUE(resultA->toData() != resultB->toData());
  ASSERT_TRUE(resultA->toData() != resultC->toData());
}

}
