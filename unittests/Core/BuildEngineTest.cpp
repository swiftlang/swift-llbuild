//===- unittests/Core/BuildEngineTest.cpp ---------------------------------===//
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

#include "llbuild/Core/BuildEngine.h"

#include "gtest/gtest.h"

#include <vector>

using namespace llbuild;
using namespace llbuild::core;

namespace {

// Simple task implementation which takes a fixed set of dependencies, evaluates
// them all, and then provides the output.
class SimpleTask : public Task {
public:
  typedef std::function<ValueType(const std::vector<ValueType>&)> ComputeFnType;

private:
  std::vector<KeyType> Inputs;
  std::vector<ValueType> InputValues;
  ComputeFnType Compute;

public:
  SimpleTask(const std::vector<KeyType>& Inputs, ComputeFnType Compute)
    : Task("SimpleTask"), Inputs(Inputs), Compute(Compute)
  {
    InputValues.resize(Inputs.size());
  }

  virtual void start(BuildEngine& Engine) override {
    // Request all of the inputs.
    for (int i = 0, e = Inputs.size(); i != e; ++i) {
      Engine.taskNeedsInput(this, Inputs[i], i);
    }
  }

  virtual void provideValue(BuildEngine&, uintptr_t InputID,
                            ValueType Value) override {
    // Update the input values.
    assert(InputID < InputValues.size());
    InputValues[InputID] = Value;
  }

  virtual ValueType finish() override {
    // Compute the result.
    return Compute(InputValues);
  }
};

// Helper function for creating a simple action.
typedef std::function<Task*(BuildEngine&)> ActionFn;

static ActionFn simpleAction(const std::vector<KeyType>& Inputs,
                             SimpleTask::ComputeFnType Compute) {
  return [=] (BuildEngine& engine) {
    return engine.registerTask(new SimpleTask(Inputs, Compute)); };
}

TEST(BuildEngineTest, Basic) {
  /// Check a trivial build graph.
  core::BuildEngine Engine;
  Engine.addRule({
      "value-A", simpleAction({}, [] (const std::vector<ValueType>& Inputs) {
          return 10; }) });
  Engine.addRule({
      "value-B", simpleAction({}, [] (const std::vector<ValueType>& Inputs) {
          return 20; }) });
  Engine.addRule({
      "result",
      simpleAction({"value-A", "value-B"},
                   [] (const std::vector<ValueType>& Inputs) {
                     EXPECT_EQ(2, Inputs.size());
                     EXPECT_EQ(10, Inputs[0]);
                     EXPECT_EQ(20, Inputs[1]);
                     return Inputs[0] + Inputs[1];
                   }) });

  EXPECT_EQ(30, Engine.build("result"));
  EXPECT_EQ(10, Engine.build("value-A"));
  EXPECT_EQ(20, Engine.build("value-B"));
}

}
