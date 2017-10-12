//===- unittests/Core/BuildEngineServerTest.cpp ---------------------------===//
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

#include "llbuild/Core/BuildEngineServer.h"

#include "llbuild/Core/BuildEngine.h"
#include "llbuild/Core/BuildEngineClient.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"

#include "gtest/gtest.h"

using namespace llbuild;
using namespace llbuild::core;

namespace {

class SimpleBuildEngineDelegate : public core::BuildEngineDelegate {
private:
  virtual core::Rule lookupRule(const core::KeyType& key) override {
    // We never expect dynamic rule lookup.
    fprintf(stderr, "error: unexpected rule lookup for \"%s\"\n",
            key.c_str());
    abort();
    return core::Rule();
  }

  virtual void cycleDetected(const std::vector<core::Rule*>& items) override {
  }

  virtual void error(const Twine& message) override {
    fprintf(stderr, "error: %s\n", message.str().c_str());
    abort();
  }
};

TEST(BuildEngineServerTest, basic) {
  // Create a temporary socket path.
  llvm::SmallString<256> sockPath;
  auto ec = llvm::sys::fs::createTemporaryFile(__FUNCTION__, "sock", sockPath);
  EXPECT_EQ(bool(ec), false);

  // Create a dummy engine.
  SimpleBuildEngineDelegate delegate;
  core::BuildEngine engine(delegate);

  // Create a server.
  core::BuildEngineServer server(engine, sockPath);
  std::string error;
  EXPECT_TRUE(server.start(&error));

  // Create multiple clients.
  for (const auto taskID: { "dummy1", "dummy2", "dummy3" }) {
    core::BuildEngineClient client(sockPath, taskID);
    EXPECT_TRUE(client.connect(&error));
    client.disconnect();
  }

  server.shutdown();
}

}
