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

#include "llbuild/Core/BuildDB.h"

#include "gtest/gtest.h"

#include <unordered_map>
#include <vector>

using namespace llbuild;
using namespace llbuild::core;

namespace {

class SimpleBuildEngineDelegate : public core::BuildEngineDelegate {
  virtual core::Rule lookupRule(const core::KeyType& Key) override {
    // We never expect dynamic rule lookup.
    fprintf(stderr, "error: %s: unexpected rule lookup for \"%s\"\n",
            getprogname(), Key.c_str());
    abort();
    return core::Rule();
  }
};

static int32_t IntFromValue(const core::ValueType& Value) {
  assert(Value.size() == 4);
  return ((Value[0] << 0) |
          (Value[1] << 8) |
          (Value[2] << 16) |
          (Value[3] << 24));
}
static core::ValueType IntToValue(int32_t Value) {
  std::vector<uint8_t> Result(4);
  Result[0] = (Value >> 0) & 0xFF;
  Result[1] = (Value >> 8) & 0xFF;
  Result[2] = (Value >> 16) & 0xFF;
  Result[3] = (Value >> 24) & 0xFF;
  return Result;
}

// Simple task implementation which takes a fixed set of dependencies, evaluates
// them all, and then provides the output.
class SimpleTask : public Task {
public:
  typedef std::function<int(const std::vector<int>&)> ComputeFnType;

private:
  std::vector<KeyType> Inputs;
  std::vector<int> InputValues;
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
                            const ValueType& Value) override {
    // Update the input values.
    assert(InputID < InputValues.size());
    InputValues[InputID] = IntFromValue(Value);
  }

  virtual void inputsAvailable(core::BuildEngine& Engine) override {
    Engine.taskIsComplete(this, IntToValue(Compute(InputValues)));
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
  SimpleBuildEngineDelegate Delegate;
  core::BuildEngine Engine(Delegate);
  Engine.addRule({
      "value-A", simpleAction({}, [&] (const std::vector<int>& Inputs) {
          BuiltKeys.push_back("value-A");
          return 2; }) });
  Engine.addRule({
      "value-B", simpleAction({}, [&] (const std::vector<int>& Inputs) {
          BuiltKeys.push_back("value-B");
          return 3; }) });
  Engine.addRule({
      "result",
      simpleAction({"value-A", "value-B"},
                   [&] (const std::vector<int>& Inputs) {
                     EXPECT_EQ(2U, Inputs.size());
                     EXPECT_EQ(2, Inputs[0]);
                     EXPECT_EQ(3, Inputs[1]);
                     BuiltKeys.push_back("result");
                     return Inputs[0] * Inputs[1] * 5;
                   }) });

  // Build the result.
  EXPECT_EQ(2 * 3 * 5, IntFromValue(Engine.build("result")));
  EXPECT_EQ(3U, BuiltKeys.size());
  EXPECT_EQ("value-A", BuiltKeys[0]);
  EXPECT_EQ("value-B", BuiltKeys[1]);
  EXPECT_EQ("result", BuiltKeys[2]);

  // Check that we can get results for already built nodes, without building
  // anything.
  BuiltKeys.clear();
  EXPECT_EQ(2, IntFromValue(Engine.build("value-A")));
  EXPECT_TRUE(BuiltKeys.empty());
  BuiltKeys.clear();
  EXPECT_EQ(3, IntFromValue(Engine.build("value-B")));
  EXPECT_TRUE(BuiltKeys.empty());
}

TEST(BuildEngineTest, BasicWithSharedInput) {
  // Check a build graph with a input key shared by multiple rules.
  //
  // Dependencies:
  //   value-C: (value-A, value-B)
  //   value-R: (value-A, value-C)
  std::vector<std::string> BuiltKeys;
  SimpleBuildEngineDelegate Delegate;
  core::BuildEngine Engine(Delegate);
  Engine.addRule({
      "value-A", simpleAction({}, [&] (const std::vector<int>& Inputs) {
          BuiltKeys.push_back("value-A");
          return 2; }) });
  Engine.addRule({
      "value-B", simpleAction({}, [&] (const std::vector<int>& Inputs) {
          BuiltKeys.push_back("value-B");
          return 3; }) });
  Engine.addRule({
      "value-C",
      simpleAction({"value-A", "value-B"},
                   [&] (const std::vector<int>& Inputs) {
                     EXPECT_EQ(2U, Inputs.size());
                     EXPECT_EQ(2, Inputs[0]);
                     EXPECT_EQ(3, Inputs[1]);
                     BuiltKeys.push_back("value-C");
                     return Inputs[0] * Inputs[1] * 5;
                   }) });
  Engine.addRule({
      "value-R",
      simpleAction({"value-A", "value-C"},
                   [&] (const std::vector<int>& Inputs) {
                     EXPECT_EQ(2U, Inputs.size());
                     EXPECT_EQ(2, Inputs[0]);
                     EXPECT_EQ(2 * 3 * 5, Inputs[1]);
                     BuiltKeys.push_back("value-R");
                     return Inputs[0] * Inputs[1] * 7;
                   }) });

  // Build the result.
  EXPECT_EQ(2 * 2 * 3 * 5 * 7, IntFromValue(Engine.build("value-R")));
  EXPECT_EQ(4U, BuiltKeys.size());
  EXPECT_EQ("value-A", BuiltKeys[0]);
  EXPECT_EQ("value-B", BuiltKeys[1]);
  EXPECT_EQ("value-C", BuiltKeys[2]);
  EXPECT_EQ("value-R", BuiltKeys[3]);
}

TEST(BuildEngineTest, VeryBasicIncremental) {
  // Check a trivial build graph responds to incremental changes appropriately.
  //
  // Dependencies:
  //   value-R: (value-A, value-B)
  std::vector<std::string> BuiltKeys;
  SimpleBuildEngineDelegate Delegate;
  core::BuildEngine Engine(Delegate);
  int ValueA = 2;
  int ValueB = 3;
  Engine.addRule({
      "value-A", simpleAction({}, [&] (const std::vector<int>& Inputs) {
          BuiltKeys.push_back("value-A");
          return ValueA; }),
      [&](const Rule& rule, const ValueType Value) {
        return ValueA == IntFromValue(Value);
      } });
  Engine.addRule({
      "value-B", simpleAction({}, [&] (const std::vector<int>& Inputs) {
          BuiltKeys.push_back("value-B");
          return ValueB; }),
      [&](const Rule& rule, const ValueType& Value) {
        return ValueB == IntFromValue(Value);
      } });
  Engine.addRule({
      "value-R",
      simpleAction({"value-A", "value-B"},
                   [&] (const std::vector<int>& Inputs) {
                     EXPECT_EQ(2U, Inputs.size());
                     EXPECT_EQ(ValueA, Inputs[0]);
                     EXPECT_EQ(ValueB, Inputs[1]);
                     BuiltKeys.push_back("value-R");
                     return Inputs[0] * Inputs[1] * 5;
                   }) });

  // Build the first result.
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueB * 5, IntFromValue(Engine.build("value-R")));
  EXPECT_EQ(3U, BuiltKeys.size());
  EXPECT_EQ("value-A", BuiltKeys[0]);
  EXPECT_EQ("value-B", BuiltKeys[1]);
  EXPECT_EQ("value-R", BuiltKeys[2]);

  // Mark value-A as having changed, then rebuild and sanity check.
  ValueA = 7;
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueB * 5, IntFromValue(Engine.build("value-R")));
  EXPECT_EQ(2U, BuiltKeys.size());
  EXPECT_EQ("value-A", BuiltKeys[0]);
  EXPECT_EQ("value-R", BuiltKeys[2]);

  // Check that a subsequent build is null.
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueB * 5, IntFromValue(Engine.build("value-R")));
  EXPECT_EQ(0U, BuiltKeys.size());
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
  SimpleBuildEngineDelegate Delegate;
  core::BuildEngine Engine(Delegate);
  int ValueA = 2;
  int ValueB = 3;
  Engine.addRule({
      "value-A", simpleAction({}, [&] (const std::vector<int>& Inputs) {
          BuiltKeys.push_back("value-A");
          return ValueA; }),
      [&](const Rule& rule, const ValueType& Value) {
        return ValueA == IntFromValue(Value);
      } });
  Engine.addRule({
      "value-B", simpleAction({}, [&] (const std::vector<int>& Inputs) {
          BuiltKeys.push_back("value-B");
          return ValueB; }),
      [&](const Rule& rule, const ValueType& Value) {
        return ValueB == IntFromValue(Value);
      } });
  Engine.addRule({
      "value-C",
      simpleAction({"value-A", "value-B"},
                   [&] (const std::vector<int>& Inputs) {
                     EXPECT_EQ(2U, Inputs.size());
                     EXPECT_EQ(ValueA, Inputs[0]);
                     EXPECT_EQ(ValueB, Inputs[1]);
                     BuiltKeys.push_back("value-C");
                     return Inputs[0] * Inputs[1] * 5;
                   }) });
  Engine.addRule({
      "value-R",
      simpleAction({"value-A", "value-C"},
                   [&] (const std::vector<int>& Inputs) {
                     EXPECT_EQ(2U, Inputs.size());
                     EXPECT_EQ(ValueA, Inputs[0]);
                     EXPECT_EQ(ValueA * ValueB * 5, Inputs[1]);
                     BuiltKeys.push_back("value-R");
                     return Inputs[0] * Inputs[1] * 7;
                   }) });
  Engine.addRule({
      "value-D",
      simpleAction({"value-R"},
                   [&] (const std::vector<int>& Inputs) {
                     EXPECT_EQ(1U, Inputs.size());
                     EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7,
                               Inputs[0]);
                     BuiltKeys.push_back("value-D");
                     return Inputs[0] * 11;
                   }) });
  Engine.addRule({
      "value-R2",
      simpleAction({"value-D"},
                   [&] (const std::vector<int>& Inputs) {
                     EXPECT_EQ(1U, Inputs.size());
                     EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7 * 11,
                               Inputs[0]);
                     BuiltKeys.push_back("value-R2");
                     return Inputs[0] * 13;
                   }) });

  // Build the first result.
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7, IntFromValue(Engine.build("value-R")));
  EXPECT_EQ(4U, BuiltKeys.size());
  EXPECT_EQ("value-A", BuiltKeys[0]);
  EXPECT_EQ("value-B", BuiltKeys[1]);
  EXPECT_EQ("value-C", BuiltKeys[2]);
  EXPECT_EQ("value-R", BuiltKeys[3]);

  // Mark value-A as having changed, then rebuild and sanity check.
  ValueA = 17;
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7, IntFromValue(Engine.build("value-R")));
  EXPECT_EQ(3U, BuiltKeys.size());
  EXPECT_EQ("value-A", BuiltKeys[0]);
  EXPECT_EQ("value-C", BuiltKeys[1]);
  EXPECT_EQ("value-R", BuiltKeys[2]);

  // Mark value-B as having changed, then rebuild and sanity check.
  ValueB = 19;
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7, IntFromValue(Engine.build("value-R")));
  EXPECT_EQ(3U, BuiltKeys.size());
  EXPECT_EQ("value-B", BuiltKeys[0]);
  EXPECT_EQ("value-C", BuiltKeys[1]);
  EXPECT_EQ("value-R", BuiltKeys[2]);

  // Build value-R2 for the first time.
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7 * 11 * 13,
            IntFromValue(Engine.build("value-R2")));
  EXPECT_EQ(2U, BuiltKeys.size());
  EXPECT_EQ("value-D", BuiltKeys[0]);
  EXPECT_EQ("value-R2", BuiltKeys[1]);

  // Now mark value-B as having changed, then rebuild value-R, then build
  // value-R2 and sanity check.
  ValueB = 23;
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7, IntFromValue(Engine.build("value-R")));
  EXPECT_EQ(3U, BuiltKeys.size());
  EXPECT_EQ("value-B", BuiltKeys[0]);
  EXPECT_EQ("value-C", BuiltKeys[1]);
  EXPECT_EQ("value-R", BuiltKeys[2]);
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7 * 11 * 13,
            IntFromValue(Engine.build("value-R2")));
  EXPECT_EQ(2U, BuiltKeys.size());
  EXPECT_EQ("value-D", BuiltKeys[0]);
  EXPECT_EQ("value-R2", BuiltKeys[1]);

  // Final sanity check.
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7, IntFromValue(Engine.build("value-R")));
  EXPECT_EQ(ValueA * ValueA * ValueB * 5 * 7 * 11 * 13,
            IntFromValue(Engine.build("value-R2")));
  EXPECT_EQ(0U, BuiltKeys.size());
}

TEST(BuildEngineTest, IncrementalDependency) {
  // Check that the engine properly clears the individual result dependencies
  // when a rule is rerun.
  //
  // Dependencies:
  //   value-R: (value-A)
  //
  // FIXME: This test is rather cumbersome for all it is trying to check, maybe
  // we need a logging BuildDB implementation or something that would be easier
  // to test against (or just update the trace to track it).

  SimpleBuildEngineDelegate Delegate;
  core::BuildEngine Engine(Delegate);

  // Attach a custom database, used to get the results.
  class CustomDB : public BuildDB {
  public:
    std::unordered_map<core::KeyType, Result> RuleResults;

    virtual uint64_t getCurrentIteration() override {
      return 0;
    }
    virtual void setCurrentIteration(uint64_t Value) override {}
    virtual bool lookupRuleResult(const Rule& Rule,
                                  Result* Result_Out) override {
      return false;
    }
    virtual void setRuleResult(const Rule& Rule,
                               const Result& Result) override {
      RuleResults[Rule.Key] = Result;
    }
    virtual void buildStarted() override {}
    virtual void buildComplete() override {}
  };
  CustomDB *DB = new CustomDB();
  Engine.attachDB(std::unique_ptr<CustomDB>(DB));

  int ValueA = 2;
  Engine.addRule({
      "value-A", simpleAction({}, [&] (const std::vector<int>& Inputs) {
          return ValueA; }),
      [&](const Rule& rule, const ValueType& Value) {
        return ValueA == IntFromValue(Value);
      } });
  Engine.addRule({
      "value-R",
      simpleAction({"value-A"},
                   [&] (const std::vector<int>& Inputs) {
                     EXPECT_EQ(1U, Inputs.size());
                     EXPECT_EQ(ValueA, Inputs[0]);
                     return Inputs[0] * 3;
                   }) });

  // Build the first result.
  EXPECT_EQ(ValueA * 3, IntFromValue(Engine.build("value-R")));

  // Mark value-A as having changed, then rebuild.
  ValueA = 5;
  EXPECT_EQ(ValueA * 3, IntFromValue(Engine.build("value-R")));

  // Check the rule results.
  const Result& ValueRResult = DB->RuleResults["value-R"];
  EXPECT_EQ(ValueA * 3, IntFromValue(ValueRResult.Value));
  EXPECT_EQ(1U, ValueRResult.Dependencies.size());
}

TEST(BuildEngineTest, DeepDependencyScanningStack) {
  // Check that the engine can handle dependency scanning of a very deep stack,
  // which would probably crash blowing the stack if the engine used naive
  // recursion.
  //
  // FIXME: It would be nice to run this on a thread with a small stack to
  // guarantee this, and to be able to make the depth small so the test runs
  // faster.
  int Depth = 10000;

  SimpleBuildEngineDelegate Delegate;
  core::BuildEngine Engine(Delegate);
  int LastInputValue = 0;
  for (int i = 0; i != Depth; ++i) {
    char Name[32];
    sprintf(Name, "input-%d", i);
    if (i != Depth-1) {
      char InputName[32];
      sprintf(InputName, "input-%d", i+1);
      Engine.addRule({
          Name, simpleAction({ InputName },
                             [] (const std::vector<int>& Inputs) {
                               return Inputs[0]; }) });
    } else {
      Engine.addRule({
          Name,
          simpleAction({},
                       [&] (const std::vector<int>& Inputs) {
                         return LastInputValue; }),
          [&](const Rule& rule, const ValueType& Value) {
            // FIXME: Once we have custom ValueType objects, we would like to
            // have timestamps on the value and just compare to a timestamp
            // (similar to what we would do for a file).
            return LastInputValue == IntFromValue(Value);
          } });
    }
  }

  // Build the first result.
  LastInputValue = 42;
  EXPECT_EQ(LastInputValue, IntFromValue(Engine.build("input-0")));

  // Perform a null build on the result.
  EXPECT_EQ(LastInputValue, IntFromValue(Engine.build("input-0")));

  // Perform a full rebuild on the result.
  LastInputValue = 52;
  EXPECT_EQ(LastInputValue, IntFromValue(Engine.build("input-0")));
}

TEST(BuildEngineTest, DiscoveredDependencies) {
  // Check basic support for tasks to report discovered dependencies.

  // This models a task which has some out-of-band way to read the input.
  class TaskWithDiscoveredDependency : public Task {
    int& ValueB;
    int ComputedInputValue = -1;

  public:
    TaskWithDiscoveredDependency(int& ValueB)
      : Task("TaskWithDiscoveredDependency"), ValueB(ValueB) { }

    virtual void start(BuildEngine& Engine) override {
      // Request the known input.
      Engine.taskNeedsInput(this, "value-A", 0);
    }

    virtual void provideValue(BuildEngine&, uintptr_t InputID,
                              const ValueType& Value) override {
      assert(InputID == 0);
      ComputedInputValue = IntFromValue(Value);
    }

    virtual void inputsAvailable(core::BuildEngine& Engine) override {
      // Report the discovered dependency.
      Engine.taskDiscoveredDependency(this, "value-B");
      Engine.taskIsComplete(this, IntToValue(ComputedInputValue * ValueB * 5));
    }
  };

  std::vector<std::string> BuiltKeys;
  SimpleBuildEngineDelegate Delegate;
  core::BuildEngine Engine(Delegate);
  int ValueA = 2;
  int ValueB = 3;
  Engine.addRule({
      "value-A",
      simpleAction({ },
                   [&] (const std::vector<int>& Inputs) {
                     BuiltKeys.push_back("value-A");
                     return ValueA;
                   }),
      [&](const Rule& rule, const ValueType& Value) {
        return ValueA == IntFromValue(Value);
      } });
  Engine.addRule({
      "value-B",
      simpleAction({ },
                   [&] (const std::vector<int>& Inputs) {
                     BuiltKeys.push_back("value-B");
                     return ValueB;
                   }),
      [&](const Rule& rule, const ValueType& Value) {
        return ValueB == IntFromValue(Value);
      } });
  Engine.addRule({
      "output",
      [&ValueB, &BuiltKeys] (BuildEngine& Engine) {
        BuiltKeys.push_back("output");
        return Engine.registerTask(new TaskWithDiscoveredDependency(ValueB));
      } });

  // Build the first result.
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueB * 5, IntFromValue(Engine.build("output")));
  EXPECT_EQ(std::vector<std::string>({ "output", "value-A", "value-B" }),
            BuiltKeys);

  // Verify that the next build is a null build.
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueB * 5, IntFromValue(Engine.build("output")));
  EXPECT_EQ(std::vector<std::string>(), BuiltKeys);

  // Verify that the build depends on ValueB.
  ValueB = 7;
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueB * 5, IntFromValue(Engine.build("output")));
  EXPECT_EQ(std::vector<std::string>({ "output", "value-B" }),
            BuiltKeys);


  // Verify again that the next build is a null build.
  BuiltKeys.clear();
  EXPECT_EQ(ValueA * ValueB * 5, IntFromValue(Engine.build("output")));
  EXPECT_EQ(std::vector<std::string>(), BuiltKeys);
}

}
