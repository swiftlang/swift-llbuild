//===- unittests/CAPI/C-API.cpp -------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2018 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/llbuild.h"

#include "gtest/gtest.h"

namespace {

/// We should support decoding an empty value without crashing.
TEST(CAPI, GetAPIVersion) {
  auto version = llb_get_api_version();
  EXPECT_EQ(version, LLBUILD_C_API_VERSION);
}

}
