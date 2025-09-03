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
#include "llvm/Support/ConvertUTF.h"

#include "gtest/gtest.h"

#include <chrono>
#include <vector>

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
    os << "Hello, world!";
    os.close();
  }

  auto missingFileInfo = fs->getFileInfo("/does/not/exists");
  EXPECT_TRUE(missingFileInfo.isMissing());
  
  auto ourFileInfo = fs->getFileInfo(tempPath.str());
  EXPECT_FALSE(ourFileInfo.isMissing());

  auto missingFileContents = fs->getFileContents("/does/not/exist");
  EXPECT_EQ(missingFileContents.get(), nullptr);

  auto ourFileContents = fs->getFileContents(tempPath.str());
  EXPECT_EQ(ourFileContents->getBuffer().str(), "Hello, world!");

  // Remote the temporary file.
  auto ec = llvm::sys::fs::remove(tempPath.str());
  EXPECT_FALSE(ec);
}

TEST(FileSystemTest, testRecursiveRemoval) {
  TmpDir rootTempDir(__func__);

  SmallString<256> tempDir { rootTempDir.str() };
  llvm::sys::path::append(tempDir, "root");
  sys::mkdir(tempDir.c_str());

  SmallString<256> file{ tempDir.str() };
  llvm::sys::path::append(file, "test.txt");
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(file.str(), ec, llvm::sys::fs::F_Text);
    EXPECT_FALSE(ec);
    os << "Hello, world!";
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
    os << "Hello, world!";
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
  TmpDir rootTempDir(__func__);

  SmallString<256> file{ rootTempDir.str() };
  llvm::sys::path::append(file, "test.txt");
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(file.str(), ec, llvm::sys::fs::F_Text);
    EXPECT_FALSE(ec);
    os << "Hello, world!";
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
    os << "Hello, world!";
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

TEST(DeviceAgnosticFileSystemTest, basic) {
  // Check basic sanity of the local filesystem object.
  auto fs = DeviceAgnosticFileSystem::from(createLocalFileSystem());

  // Write a temp file.
  SmallString<256> tempPath;
  llvm::sys::fs::createTemporaryFile("FileSystemTests", "txt", tempPath);
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(tempPath.str(), ec, llvm::sys::fs::F_Text);
    EXPECT_FALSE(ec);
    os << "Hello, world!";
    os.close();
  }

  auto missingFileInfo = fs->getFileInfo("/does/not/exists");
  EXPECT_TRUE(missingFileInfo.isMissing());

  auto ourFileInfo = fs->getFileInfo(tempPath.str());
  EXPECT_FALSE(ourFileInfo.isMissing());

  EXPECT_EQ(ourFileInfo.device, 0ull);
  EXPECT_EQ(ourFileInfo.inode, 0ull);

  auto missingFileContents = fs->getFileContents("/does/not/exist");
  EXPECT_EQ(missingFileContents.get(), nullptr);

  auto ourFileContents = fs->getFileContents(tempPath.str());
  EXPECT_EQ(ourFileContents->getBuffer().str(), "Hello, world!");
  // Remove the temporary file.
  auto ec = llvm::sys::fs::remove(tempPath.str());
  EXPECT_FALSE(ec);
}

TEST(ChecksumOnlyFileSystem, basic) {
  // Check basic sanity of the local filesystem object.
  auto fs = ChecksumOnlyFileSystem::from(createLocalFileSystem());

  // Write a temp file.
  SmallString<256> tempPath;
  llvm::sys::fs::createTemporaryFile("FileSystemTests", "txt", tempPath);
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(tempPath.str(), ec, llvm::sys::fs::F_Text);
    EXPECT_FALSE(ec);
    os << "Hello, world!";
    os.close();
  }

  auto missingFileInfo = fs->getFileInfo("/does/not/exists");
  EXPECT_TRUE(missingFileInfo.isMissing());

  auto ourFileInfo = fs->getFileInfo(tempPath.str());
  EXPECT_FALSE(ourFileInfo.isMissing());

  EXPECT_EQ(ourFileInfo.device, 0ull);
  EXPECT_EQ(ourFileInfo.inode, 0ull);

  auto missingFileContents = fs->getFileContents("/does/not/exist");
  EXPECT_EQ(missingFileContents.get(), nullptr);

  auto ourFileContents = fs->getFileContents(tempPath.str());
  EXPECT_EQ(ourFileContents->getBuffer().str(), "Hello, world!");
  // Remove the temporary file.
  auto ec = llvm::sys::fs::remove(tempPath.str());
  EXPECT_FALSE(ec);
}

#if defined(_WIN32)
TEST(FileSystemTest, widenPathPerformance) {
  // Performance test for widenPath function on Windows
  using namespace std::chrono;
  
  // Test data: various path types that exercise different code paths
  std::vector<std::string> testPaths = {
    // Short absolute paths (should hit early return optimization)
    "C:\\Windows\\System32\\kernel32.dll",
    "C:\\Program Files\\test.exe",
    "D:\\temp\\file.txt",
    
    // Relative paths (require current directory lookup)
    "..\\..\\test.txt",
    "src\\main.cpp",
    "build\\debug\\output.exe",
    
    // Long paths that require \\?\ prefix (must be > 248 characters)
    "C:\\very\\long\\directory\\name\\that\\definitely\\exceeds\\the\\maximum\\path\\length\\supported\\by\\traditional\\windows\\apis\\and\\will\\require\\the\\extended\\long\\path\\prefix\\to\\function\\properly\\on\\modern\\windows\\systems\\with\\long\\path\\support\\enabled\\in\\the\\registry\\or\\application\\manifest\\settings\\for\\proper\\operation\\and\\compatibility\\with\\deep\\directory\\structures\\and\\very\\long\\filenames\\that\\are\\common\\in\\modern\\development\\environments\\file.txt",
    
    // Paths with . and .. components (require canonicalization)
    "C:\\Windows\\..\\Program Files\\..\\Windows\\System32\\kernel32.dll",
    "..\\src\\..\\build\\..\\src\\main.cpp",
    
    // UNC paths
    "\\\\server\\share\\file.txt",
    
    // Already prefixed paths
    "\\\\?\\C:\\already\\prefixed\\path.txt"
  };
  
  const int iterations = 1000;
  
  // Warm up
  for (const auto& path : testPaths) {
    llvm::SmallVector<wchar_t, 260> result;
    llvm::sys::path::widenPath(path, result);
  }
  
  // Measure performance
  auto start = high_resolution_clock::now();
  
  for (int i = 0; i < iterations; ++i) {
    for (const auto& path : testPaths) {
      llvm::SmallVector<wchar_t, 260> result;
      std::error_code ec = llvm::sys::path::widenPath(path, result);
      EXPECT_FALSE(ec) << "Failed to widen path: " << path;
    }
  }
  
  auto end = high_resolution_clock::now();
  auto duration = duration_cast<microseconds>(end - start);
  
  double avgTimePerCall = static_cast<double>(duration.count()) / (iterations * testPaths.size());
  
  // Print performance results
  std::cout << "\nwidenPath Performance Results:\n";
  std::cout << "Total iterations: " << iterations << "\n";
  std::cout << "Test paths: " << testPaths.size() << "\n";
  std::cout << "Total calls: " << (iterations * testPaths.size()) << "\n";
  std::cout << "Total time: " << duration.count() << " microseconds\n";
  std::cout << "Average time per call: " << avgTimePerCall << " microseconds\n";
  
  // Performance expectations (these are reasonable targets)
  EXPECT_LT(avgTimePerCall, 50.0) << "widenPath is taking too long on average";
  
  // Test correctness of results
  for (const auto& path : testPaths) {
    llvm::SmallVector<wchar_t, 260> result;
    std::error_code ec = llvm::sys::path::widenPath(path, result);
    EXPECT_FALSE(ec) << "widenPath failed for: " << path;
    EXPECT_GT(result.size(), 0) << "widenPath returned empty result for: " << path;
  }
}

TEST(FileSystemTest, widenPathCorrectnessAndOptimizations) {
  // Test that optimizations don't break correctness
  
  // Test early return for short absolute paths
  {
    std::string shortAbsPath = "C:\\Windows\\System32\\test.exe";
    llvm::SmallVector<wchar_t, 260> result;
    std::error_code ec = llvm::sys::path::widenPath(shortAbsPath, result);
    EXPECT_FALSE(ec);
    EXPECT_GT(result.size(), 0);
    
    // Convert back to check correctness
    llvm::SmallVector<char, 260> backConverted;
    llvm::sys::windows::UTF16ToUTF8(result.data(), result.size(), backConverted);
    std::string converted(backConverted.data(), backConverted.size());
    EXPECT_EQ(converted, shortAbsPath);
  }
  
  // Test relative path handling
  {
    std::string relPath = "..\\test.txt";
    llvm::SmallVector<wchar_t, 260> result;
    std::error_code ec = llvm::sys::path::widenPath(relPath, result);
    EXPECT_FALSE(ec);
    EXPECT_GT(result.size(), 0);
  }
  
  // Test long path with \\?\ prefix
  {
    // Create a path that definitely exceeds MAX_PATH - 12 (248 characters)
    // MAX_PATH is 260, so we need more than 248 characters to trigger long path support
    std::string longPath = "C:\\very\\long\\directory\\name\\that\\definitely\\exceeds\\the\\maximum\\path\\length\\supported\\by\\traditional\\windows\\apis\\and\\will\\require\\the\\extended\\long\\path\\prefix\\to\\function\\properly\\on\\modern\\windows\\systems\\with\\long\\path\\support\\enabled\\in\\the\\registry\\or\\application\\manifest\\settings\\for\\proper\\operation\\and\\compatibility\\with\\deep\\directory\\structures\\and\\very\\long\\filenames\\that\\are\\common\\in\\modern\\development\\environments\\file.txt";
    
    // Verify the path is actually long enough (should be > 248 characters)
    EXPECT_GT(longPath.length(), 248) << "Test path should exceed MAX_PATH - 12 limit";
    
    llvm::SmallVector<wchar_t, 1024> result;
    std::error_code ec = llvm::sys::path::widenPath(longPath, result);
    EXPECT_FALSE(ec);
    EXPECT_GT(result.size(), 0);
    
    // Should have \\?\ prefix for long paths
    if (result.size() >= 4) {
      std::wstring prefix(result.data(), 4);
      EXPECT_EQ(prefix, L"\\\\?\\") << "Long path should have \\\\?\\ prefix, path length: " << longPath.length();
    }
  }
  
  // Test path canonicalization
  {
    std::string pathWithDots = "C:\\Windows\\..\\Program Files\\..\\Windows\\System32\\kernel32.dll";
    llvm::SmallVector<wchar_t, 1024> result;
    std::error_code ec = llvm::sys::path::widenPath(pathWithDots, result);
    EXPECT_FALSE(ec);
    EXPECT_GT(result.size(), 0);
  }
}
#endif // _WIN32

}
