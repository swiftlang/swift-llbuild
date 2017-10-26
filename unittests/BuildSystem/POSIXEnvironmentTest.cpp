//===- unittests/BuildSystem/POSIXEnvironmentTest.cpp ---------------------===//
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

#include "../../lib/BuildSystem/POSIXEnvironment.h"

#include "gtest/gtest.h"

using namespace llbuild;
using namespace llbuild::buildsystem;

namespace {
  TEST(POSIXEnvironmentTest, basic) {
    POSIXEnvironment env;
    env.setIfMissing("a", "aValue");
    env.setIfMissing("b", "bValue");
    env.setIfMissing("a", "NOT HERE");

    auto result = env.getEnvp();
    EXPECT_EQ(StringRef(result[0]), "a=aValue");
    EXPECT_EQ(StringRef(result[1]), "b=bValue");
    EXPECT_EQ(result[2], nullptr);
  }
}
