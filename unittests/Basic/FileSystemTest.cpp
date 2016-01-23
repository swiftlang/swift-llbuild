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

#include "llbuild/Basic/FileSystem.h"
#include "llbuild/Basic/LLVM.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
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

}
