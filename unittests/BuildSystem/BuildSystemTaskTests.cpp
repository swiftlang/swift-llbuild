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

  // Create a smaple file.
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
  system.enableTracing("/tmp/x.trace", nullptr);
  system.loadDescription(std::move(description));

  // Build a specific key.
  auto key = BuildKey::makeNode(path);
  auto result = system.build(key);
  ASSERT_TRUE(result.hasValue());
  ASSERT_TRUE(result.getValue().isExistingInput());
  ASSERT_EQ(result.getValue().getOutputInfo().size, testString.size());
}

}
