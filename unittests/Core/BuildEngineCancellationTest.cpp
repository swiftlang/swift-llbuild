//===- unittests/Core/BuildEngineCancellationTest.cpp ---------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2017-2018 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/Core/BuildEngine.h"

#include "llbuild/Core/BuildDB.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/Support/ErrorHandling.h"

#include "gtest/gtest.h"

#include <unordered_map>
#include <vector>

using namespace llbuild;
using namespace llbuild::core;

namespace {

class SimpleBuildEngineDelegate : public core::BuildEngineDelegate {
public:
  bool expectError = false;
private:
  virtual core::Rule lookupRule(const core::KeyType& key) override {
    // We never expect dynamic rule lookup.
    fprintf(stderr, "error: unexpected rule lookup for \"%s\"\n",
            key.c_str());
    abort();
    return core::Rule();
  }

  virtual void cycleDetected(const std::vector<core::Rule*>& items) override {
    error("unexpected cycle detected");
  }

  virtual void error(const Twine& message) override {
    fprintf(stderr, "error: %s\n", message.str().c_str());
    if (!expectError)
      abort();
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

// Simple task implementation which takes a fixed set of dependencies, evaluates
// them all, and then provides the output.
class SimpleTask : public Task {
public:
  typedef std::function<std::vector<KeyType>()> InputListingFnType;
  typedef std::function<int(const std::vector<int>&)> ComputeFnType;

private:
  InputListingFnType listInputs;
  std::vector<int> inputValues;
  ComputeFnType compute;

public:
  SimpleTask(InputListingFnType listInputs, ComputeFnType compute)
      : listInputs(listInputs), compute(compute)
  {
  }

  virtual void start(BuildEngine& engine) override {
    // Compute the list of inputs.
    auto inputs = listInputs();

    // Request all of the inputs.
    inputValues.resize(inputs.size());
    for (int i = 0, e = inputs.size(); i != e; ++i) {
      engine.taskNeedsInput(this, inputs[i], i);
    }
  }

  virtual void provideValue(BuildEngine&, uintptr_t inputID,
                            const ValueType& value) override {
    // Update the input values.
    assert(inputID < inputValues.size());
    inputValues[inputID] = intFromValue(value);
  }

  virtual void inputsAvailable(core::BuildEngine& engine) override {
    engine.taskIsComplete(this, intToValue(compute(inputValues)));
  }
};

// Helper function for creating a simple action.
typedef std::function<Task*(BuildEngine&)> ActionFn;

static ActionFn simpleAction(const std::vector<KeyType>& inputs,
                             SimpleTask::ComputeFnType compute) {
  return [=] (BuildEngine& engine) {
    return new SimpleTask([inputs]{ return inputs; }, compute);
  };
}

static ActionFn simpleActionExternalTask(const std::vector<KeyType>& inputs,
                             SimpleTask::ComputeFnType compute, Task** task) {
  return [=] (BuildEngine& engine) {
    *task = new SimpleTask([inputs]{ return inputs; }, compute);
    return *task; };
}

TEST(BuildEngineCancellationTest, basic) {
  std::vector<std::string> builtKeys;
  SimpleBuildEngineDelegate delegate;
  core::BuildEngine engine(delegate);
  bool cancelIt = false;
  engine.addRule({
      "value-A", {}, simpleAction({}, [&] (const std::vector<int>& inputs) {
          builtKeys.push_back("value-A");
          fprintf(stderr, "building A (and cancelling ? %d)\n", cancelIt);
          if (cancelIt) {
            engine.cancelBuild();
          }
          return 2; }) });
  engine.addRule({
      "result", {},
      simpleAction({"value-A"},
                   [&] (const std::vector<int>& inputs) {
                     EXPECT_EQ(1U, inputs.size());
                     EXPECT_EQ(2, inputs[0]);
                     builtKeys.push_back("result");
                     return inputs[0] * 3;
                   }) });

  // Build the result, cancelling during the first task.
  cancelIt = true;
  auto result = engine.build("result");
  EXPECT_EQ(0U, result.size());
  EXPECT_EQ(1U, builtKeys.size());
  EXPECT_EQ("value-A", builtKeys[0]);

  // Build again, without cancelling; both tasks should run.
  cancelIt = false;
  EXPECT_EQ(2 * 3, intFromValue(engine.build("result")));
  EXPECT_EQ(2U, builtKeys.size());
  EXPECT_EQ("value-A", builtKeys[0]);
  EXPECT_EQ("result", builtKeys[1]);
}

TEST(BuildEngineCancellationDueToDuplicateTaskTest, basic) {
  std::vector<std::string> builtKeys;
  SimpleBuildEngineDelegate delegate;
  core::BuildEngine engine(delegate);
  Task* taskA = nullptr;
  engine.addRule({
     "value-A", {}, simpleActionExternalTask({"value-B"},
        [&] (const std::vector<int>& inputs) {
          builtKeys.push_back("value-A");
          fprintf(stderr, "building A\n");
          return 2;
        },
        &taskA)
    });
  engine.addRule({
     "value-B", {}, simpleAction({}, [&] (const std::vector<int>& inputs) {
       builtKeys.push_back("value-B");
       fprintf(stderr, "building B (and reporting A complete early)\n");
       engine.taskIsComplete(taskA, intToValue(2));
       return 3; }) });
  engine.addRule({
      "result", {},
      simpleAction({"value-A", "value-B"},
                   [&] (const std::vector<int>& inputs) {
                     EXPECT_EQ(2U, inputs.size());
                     EXPECT_EQ(2, inputs[0]);
                     EXPECT_EQ(3, inputs[1]);
                     builtKeys.push_back("result");
                     return inputs[0] * 3 + inputs[1];
                   }) });

  // Build the result, expecting error
  //
  // This test is triggering the cancellation behavior through what is
  // clearly broken client behavior (reporting a task complete that should
  // not have started yet). The engine should cleanly cancel and report the
  // error, which is what this test is checking. However, the question remains
  // are there 'legitimate'/non-broken client pathways that could also trigger
  // that error?
  delegate.expectError = true;
  auto result = engine.build("result");
  EXPECT_EQ(0U, result.size());
  EXPECT_EQ(1U, builtKeys.size());
  EXPECT_EQ("value-B", builtKeys[0]);
}

}
