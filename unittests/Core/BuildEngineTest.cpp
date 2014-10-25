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
  // Check a trivial build graph.
  std::vector<std::string> BuiltKeys;
  core::BuildEngine Engine;
  Engine.addRule({
      "value-A", simpleAction({}, [&] (const std::vector<ValueType>& Inputs) {
          BuiltKeys.push_back("value-A");
          return 2; }) });
  Engine.addRule({
      "value-B", simpleAction({}, [&] (const std::vector<ValueType>& Inputs) {
          BuiltKeys.push_back("value-B");
          return 3; }) });
  Engine.addRule({
      "result",
      simpleAction({"value-A", "value-B"},
                   [&] (const std::vector<ValueType>& Inputs) {
                     EXPECT_EQ(2U, Inputs.size());
                     EXPECT_EQ(2, Inputs[0]);
                     EXPECT_EQ(3, Inputs[1]);
                     BuiltKeys.push_back("result");
                     return Inputs[0] * Inputs[1] * 5;
                   }) });

  // Build the result.
  EXPECT_EQ(2 * 3 * 5, Engine.build("result"));
  EXPECT_EQ(3U, BuiltKeys.size());
  EXPECT_EQ("value-A", BuiltKeys[0]);
  EXPECT_EQ("value-B", BuiltKeys[1]);
  EXPECT_EQ("result", BuiltKeys[2]);

  // Check that we can get results for already built nodes, without building
  // anything.
  BuiltKeys.clear();
  EXPECT_EQ(2, Engine.build("value-A"));
  EXPECT_TRUE(BuiltKeys.empty());
  BuiltKeys.clear();
  EXPECT_EQ(3, Engine.build("value-B"));
  EXPECT_TRUE(BuiltKeys.empty());
}

TEST(BuildEngineTest, BasicWithSharedInput) {
  // Check a build graph with a input key shared by multiple rules.
  //
  // Dependencies:
  //   value-C: (value-A, value-B)
  //   value-R: (value-A, value-C)
  std::vector<std::string> BuiltKeys;
  core::BuildEngine Engine;
  Engine.addRule({
      "value-A", simpleAction({}, [&] (const std::vector<ValueType>& Inputs) {
          BuiltKeys.push_back("value-A");
          return 2; }) });
  Engine.addRule({
      "value-B", simpleAction({}, [&] (const std::vector<ValueType>& Inputs) {
          BuiltKeys.push_back("value-B");
          return 3; }) });
  Engine.addRule({
      "value-C",
      simpleAction({"value-A", "value-B"},
                   [&] (const std::vector<ValueType>& Inputs) {
                     EXPECT_EQ(2U, Inputs.size());
                     EXPECT_EQ(2, Inputs[0]);
                     EXPECT_EQ(3, Inputs[1]);
                     BuiltKeys.push_back("value-C");
                     return Inputs[0] * Inputs[1] * 5;
                   }) });
  Engine.addRule({
      "value-R",
      simpleAction({"value-A", "value-C"},
                   [&] (const std::vector<ValueType>& Inputs) {
                     EXPECT_EQ(2U, Inputs.size());
                     EXPECT_EQ(2, Inputs[0]);
                     EXPECT_EQ(2 * 3 * 5, Inputs[1]);
                     BuiltKeys.push_back("value-R");
                     return Inputs[0] * Inputs[1] * 7;
                   }) });

  // Build the result.
  EXPECT_EQ(2 * 2 * 3 * 5 * 7, Engine.build("value-R"));
  EXPECT_EQ(4U, BuiltKeys.size());
  EXPECT_EQ("value-A", BuiltKeys[0]);
  EXPECT_EQ("value-B", BuiltKeys[1]);
  EXPECT_EQ("value-C", BuiltKeys[2]);
  EXPECT_EQ("value-R", BuiltKeys[3]);
}

TEST(BuildEngineTest, BasicIncremental) {
  // Check a build graph responds to incremental changes appropriately.
  //
  // Dependencies:
  //   value-C: (value-A, value-B)
  //   value-R: (value-A, value-C)
  //   value-D: (value-R)
  //   value-R2: (value-D)
  //
  // If value-A or value-B change, then value-C and value-R are recomputed when
  // value-R is built, but value-R2 is not recomputed.
  //
  // If value-R2 is built, then value-B changes and value-R is built, then when
  // value-R2 is built again only value-D and value-R2 are rebuilt.
  std::vector<std::string> BuiltKeys;
  core::BuildEngine Engine;
  int ValueA = 2;
  int ValueB = 3;
  Engine.addRule({
      "value-A", simpleAction({}, [&] (const std::vector<ValueType>& Inputs) {
          BuiltKeys.push_back("value-A");
          return ValueA; }),
      [&](const Rule& rule, const ValueType Value) {
          // FIXME: Once we have custom ValueType objects, we would like to have
          // timestamps on the value and just compare to a timestamp (similar to
          // what we would do for a file).
          return ValueA != Value;
      } });
  Engine.addRule({
      "value-B", simpleAction({}, [&] (const std::vector<ValueType>& Inputs) {
          BuiltKeys.push_back("value-B");
          return ValueB; }),
      [&](const Rule& rule, const ValueType Value) {
          // FIXME: Once we have custom ValueType objects, we would like to have
          // timestamps on the value and just compare to a timestamp (similar to
          // what we would do for a file).
          return ValueB != Value;
      } });
  Engine.addRule({
      "value-C",
      simpleAction({"value-A", "value-B"},
                   [&] (const std::vector<ValueType>& Inputs) {
                     EXPECT_EQ(2U, Inputs.size());
                     EXPECT_EQ(ValueA, Inputs[0]);
                     EXPECT_EQ(ValueB, Inputs[1]);
                     BuiltKeys.push_back("value-C");
                     return Inputs[0] * Inputs[1] * 5;
                   }) });
  Engine.addRule({
      "value-R",
      simpleAction({"value-A", "value-C"},
                   [&] (const std::vector<ValueType>& Inputs) {
                     EXPECT_EQ(2U, Inputs.size());
                     EXPECT_EQ(ValueA, Inputs[0]);
                     EXPECT_EQ(ValueA * ValueB * 5, Inputs[1]);
                     BuiltKeys.push_back("value-R");
                     return Inputs[0] * Inputs[1] * 7;
                   }) });
  Engine.addRule({
      "value-D",
      simpleAction({"value-R"},
                   [&] (const std::vector<ValueType>& Inputs) {
                     EXPECT_EQ(1U, Inputs.size());
                     EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7,
                               Inputs[0]);
                     BuiltKeys.push_back("value-D");
                     return Inputs[0] * 11;
                   }) });
  Engine.addRule({
      "value-R2",
      simpleAction({"value-D"},
                   [&] (const std::vector<ValueType>& Inputs) {
                     EXPECT_EQ(1U, Inputs.size());
                     EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7 * 11,
                               Inputs[0]);
                     BuiltKeys.push_back("value-R2");
                     return Inputs[0] * 13;
                   }) });

  // Build the first result.
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7, Engine.build("value-R"));
  EXPECT_EQ(4U, BuiltKeys.size());
  EXPECT_EQ("value-A", BuiltKeys[0]);
  EXPECT_EQ("value-B", BuiltKeys[1]);
  EXPECT_EQ("value-C", BuiltKeys[2]);
  EXPECT_EQ("value-R", BuiltKeys[3]);

  // Mark value-A as having changed, then rebuild and sanity check.
  ValueA = 17;
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7, Engine.build("value-R"));
  EXPECT_EQ(3U, BuiltKeys.size());
  EXPECT_EQ("value-A", BuiltKeys[0]);
  EXPECT_EQ("value-C", BuiltKeys[1]);
  EXPECT_EQ("value-R", BuiltKeys[2]);

  // Mark value-B as having changed, then rebuild and sanity check.
  ValueB = 19;
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7, Engine.build("value-R"));
  EXPECT_EQ(3U, BuiltKeys.size());
  EXPECT_EQ("value-B", BuiltKeys[0]);
  EXPECT_EQ("value-C", BuiltKeys[1]);
  EXPECT_EQ("value-R", BuiltKeys[2]);

  // Build value-R2 for the first time.
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7 * 11 * 13,
            Engine.build("value-R2"));
  EXPECT_EQ(2U, BuiltKeys.size());
  EXPECT_EQ("value-D", BuiltKeys[0]);
  EXPECT_EQ("value-R2", BuiltKeys[1]);

  // Now mark value-B as having changed, then rebuild value-R, then build
  // value-R2 and sanity check.
  ValueB = 23;
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7, Engine.build("value-R"));
  EXPECT_EQ(3U, BuiltKeys.size());
  EXPECT_EQ("value-B", BuiltKeys[0]);
  EXPECT_EQ("value-C", BuiltKeys[1]);
  EXPECT_EQ("value-R", BuiltKeys[2]);
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7 * 11 * 13,
            Engine.build("value-R2"));
  EXPECT_EQ(2U, BuiltKeys.size());
  EXPECT_EQ("value-D", BuiltKeys[0]);
  EXPECT_EQ("value-R2", BuiltKeys[1]);

  // Final sanity check.
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7, Engine.build("value-R"));
  EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7 * 11 * 13,
            Engine.build("value-R2"));
  EXPECT_EQ(0U, BuiltKeys.size());
}

}
