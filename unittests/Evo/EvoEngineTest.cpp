//===- unittests/Core/BuildEngineTest.cpp ---------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/Evo/EvoEngine.h"

#include "llbuild/Basic/ExecutionQueue.h"
#include "llbuild/Core/BuildDB.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"

#include "gtest/gtest.h"

#include <future>
#include <condition_variable>
#include <unordered_map>
#include <vector>

using namespace llbuild;
using namespace llbuild::evo;

namespace {

class SimpleBuildEngineDelegate : public core::BuildEngineDelegate, public basic::ExecutionQueueDelegate {
public:
  /// The cycle, if one was detected during building.
  std::vector<std::string> cycle;
  bool resolveCycle = false;

  std::vector<std::string> errors;
  bool expectedError = false;

private:
  std::unique_ptr<core::Rule> lookupRule(const core::KeyType& key) override {
    // We never expect dynamic rule lookup.
    fprintf(stderr, "error: unexpected rule lookup for \"%s\"\n",
            key.c_str());
    abort();
    return nullptr;
  }

  void cycleDetected(const std::vector<core::Rule*>& items) override {
    cycle.clear();
    std::transform(items.begin(), items.end(), std::back_inserter(cycle),
                   [](auto rule) { return rule->key.str(); });
  }

  bool shouldResolveCycle(const std::vector<core::Rule*>& items,
                                  core::Rule* candidateRule,
                                  core::Rule::CycleAction action) override {
    return resolveCycle;
  }

  void error(const Twine& message) override {
    errors.push_back(message.str());
    if (!expectedError) {
      fprintf(stderr, "error: %s\n", message.str().c_str());
      abort();
    }
  }

  void processStarted(basic::ProcessContext*, basic::ProcessHandle) override { }
  void processHadError(basic::ProcessContext*, basic::ProcessHandle, const Twine&) override { }
  void processHadOutput(basic::ProcessContext*, basic::ProcessHandle, StringRef) override { }
  void processFinished(basic::ProcessContext*, basic::ProcessHandle, const basic::ProcessResult&) override { }
  void queueJobStarted(basic::JobDescriptor*) override { }
  void queueJobFinished(basic::JobDescriptor*) override { }

  std::unique_ptr<basic::ExecutionQueue> createExecutionQueue() override {
    return createSerialQueue(*this, nullptr);
  }
};

static int32_t intFromValue(const core::ValueType& value) {
  assert(value.size() == 4);
  return ((value[0] << 0) |
          (value[1] << 8) |
          (value[2] << 16) |
          (value[3] << 24));
}
static core::ValueType intToValue(int32_t value) {
  std::vector<uint8_t> result(4);
  result[0] = (value >> 0) & 0xFF;
  result[1] = (value >> 8) & 0xFF;
  result[2] = (value >> 16) & 0xFF;
  result[3] = (value >> 24) & 0xFF;
  return result;
}


TEST(EvoEngineTest, basic) {
  // Check a trivial build graph.
  std::vector<std::string> builtKeys;
  SimpleBuildEngineDelegate delegate;
  core::BuildEngine engine(delegate);

  class StaticValueRule : public EvoRule {
  private:
    std::vector<std::string>& builtKeys;
    int value;
  public:
    StaticValueRule(const core::KeyType& key, int value,
                    std::vector<std::string>& builtKeys)
      : EvoRule(key), builtKeys(builtKeys), value(value) { }

    core::ValueType run(EvoEngine&) override {
      builtKeys.push_back(key.str());
      return intToValue(value);
    }
    bool isResultValid(core::BuildEngine&, const core::ValueType&) override {
      return true;
    }
  };

  class CalculatedValueRule : public EvoRule {
  private:
    std::vector<std::string>& builtKeys;
  public:
    CalculatedValueRule(const core::KeyType& key,
                        std::vector<std::string>& builtKeys)
      : EvoRule(key), builtKeys(builtKeys) { }

    core::ValueType run(EvoEngine& engine) override {
      auto a = engine.request("value-A");
      auto b = engine.request("value-B");

      auto aVal = intFromValue(engine.wait(a));
      auto bVal = intFromValue(engine.wait(b));

      EXPECT_EQ(2, aVal);
      EXPECT_EQ(3, bVal);

      builtKeys.push_back(key.str());

      return intToValue(aVal * bVal * 5);
    }
    bool isResultValid(core::BuildEngine&, const core::ValueType&) override {
      return true;
    }
  };

  engine.addRule(std::unique_ptr<core::Rule>(new StaticValueRule("value-A", 2, builtKeys)));
  engine.addRule(std::unique_ptr<core::Rule>(new StaticValueRule("value-B", 3, builtKeys)));
  engine.addRule(std::unique_ptr<core::Rule>(new CalculatedValueRule("result", builtKeys)));

  // Build the result.
  EXPECT_EQ(2 * 3 * 5, intFromValue(engine.build("result")));
  EXPECT_EQ(3U, builtKeys.size());
  EXPECT_TRUE("value-A" == builtKeys[0] || "value-A" == builtKeys[1]);
  EXPECT_TRUE("value-B" == builtKeys[0] || "value-B" == builtKeys[1]);
  EXPECT_EQ("result", builtKeys[2]);

  // Check that we can get results for already built nodes, without building
  // anything.
  builtKeys.clear();
  EXPECT_EQ(2, intFromValue(engine.build("value-A")));
  EXPECT_TRUE(builtKeys.empty());
  builtKeys.clear();
  EXPECT_EQ(3, intFromValue(engine.build("value-B")));
  EXPECT_TRUE(builtKeys.empty());
}

}
