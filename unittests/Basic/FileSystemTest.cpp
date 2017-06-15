//===- unittests/Basic/FileSystemTest.cpp ---------------------------------===//
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

#include "../BuildSystem/TempDir.h"

#include "llbuild/Basic/FileSystem.h"
#include "llbuild/Basic/LLVM.h"
#include "llbuild/Basic/PlatformUtility.h"
#include "llbuild/Basic/Stat.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include "gtest/gtest.h"

using namespace llbuild;
using namespace llbuild::basic;

namespace {

TEST(FileSystemTest, basic) {
  // Check basic sanity of the local filesystem object.
  auto fs = createLocalFileSystem();

  // Write a temp file.
  SmallString<256> tempPath;
  llvm::sys::fs::createTemporaryFile("FileSystemTests", "txt", tempPath);
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(tempPath.str(), ec, llvm::sys::fs::F_Text);
    EXPECT_FALSE(ec);
    os << "Hello, world!\n";
    os.close();
  }

  auto missingFileInfo = fs->getFileInfo("/does/not/exists");
  EXPECT_TRUE(missingFileInfo.isMissing());
  
  auto ourFileInfo = fs->getFileInfo(tempPath.str());
  EXPECT_FALSE(ourFileInfo.isMissing());

  auto missingFileContents = fs->getFileContents("/does/not/exist");
  EXPECT_EQ(missingFileContents.get(), nullptr);

  auto ourFileContents = fs->getFileContents(tempPath.str());
  EXPECT_EQ(ourFileContents->getBuffer().str(), "Hello, world!\n");

  // Remote the temporary file.
  auto ec = llvm::sys::fs::remove(tempPath.str());
  EXPECT_FALSE(ec);
}

TEST(FileSystemTest, testRecursiveRemoval) {
  TmpDir rootTempDir{ __FUNCTION__ };

  SmallString<256> tempDir { rootTempDir.str() };
  llvm::sys::path::append(tempDir, "root");
  sys::mkdir(tempDir.c_str());

  SmallString<256> file{ tempDir.str() };
  llvm::sys::path::append(file, "test.txt");
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(file.str(), ec, llvm::sys::fs::F_Text);
    EXPECT_FALSE(ec);
    os << "Hello, world!\n";
    os.close();
  }

  SmallString<256> dir{ tempDir.str() };
  llvm::sys::path::append(dir, "subdir");
  sys::mkdir(dir.c_str());

  llvm::sys::path::append(dir, "file_in_subdir.txt");
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(dir.str(), ec, llvm::sys::fs::F_Text);
    EXPECT_FALSE(ec);
    os << "Hello, world!\n";
    os.close();
  }

  auto fs = createLocalFileSystem();
  bool result = fs->remove(tempDir.c_str());
  EXPECT_TRUE(result);

  sys::StatStruct statbuf;
  EXPECT_EQ(-1, sys::stat(tempDir.c_str(), &statbuf));
  EXPECT_EQ(ENOENT, errno);
}

TEST(FileSystemTest, testRecursiveRemovalDoesNotFollowSymlinks) {
  TmpDir rootTempDir{ __FUNCTION__ };

  SmallString<256> file{ rootTempDir.str() };
  llvm::sys::path::append(file, "test.txt");
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(file.str(), ec, llvm::sys::fs::F_Text);
    EXPECT_FALSE(ec);
    os << "Hello, world!\n";
    os.close();
  }

  SmallString<256> otherDir{ rootTempDir.str() };
  llvm::sys::path::append(otherDir, "other_dir");
  sys::mkdir(otherDir.c_str());

  SmallString<256> otherFile{ otherDir.str() };
  llvm::sys::path::append(otherFile, "test.txt");
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(otherFile.str(), ec, llvm::sys::fs::F_Text);
    EXPECT_FALSE(ec);
    os << "Hello, world!\n";
    os.close();
  }

  SmallString<256> tempDir { rootTempDir.str() };
  llvm::sys::path::append(tempDir, "root");
  sys::mkdir(tempDir.c_str());

  SmallString<256> linkPath { tempDir.str() };
  llvm::sys::path::append(linkPath, "link.txt");
  int res = sys::symlink(file.c_str(), linkPath.c_str());
  EXPECT_EQ(res, 0);

  SmallString<256> directoryLinkPath { tempDir.str() };
  llvm::sys::path::append(directoryLinkPath, "link_to_other_dir");
  res = sys::symlink(otherDir.c_str(), directoryLinkPath.c_str());
  EXPECT_EQ(res, 0);

  auto fs = createLocalFileSystem();
  bool result = fs->remove(tempDir.c_str());
  EXPECT_TRUE(result);

  sys::StatStruct  statbuf;
  EXPECT_EQ(-1, sys::stat(tempDir.c_str(), &statbuf));
  EXPECT_EQ(ENOENT, errno);
  // Verify that the symlink target still exists.
  EXPECT_EQ(0, sys::stat(file.c_str(), &statbuf));
  // Verify that we did not delete the symlinked directories contents.
  EXPECT_EQ(0, sys::stat(otherFile.c_str(), &statbuf));
}

}
