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
using namespace llbuild::core;

namespace {

class SimpleBuildEngineDelegate : public core::BuildEngineDelegate, public basic::ExecutionQueueDelegate {
  /// The cycle, if one was detected during building.
public:
  std::vector<std::string> cycle;
  bool resolveCycle = false;

  std::vector<std::string> errors;
  bool expectedError = false;

private:
  virtual std::unique_ptr<core::Rule> lookupRule(const core::KeyType& key) override {
    // We never expect dynamic rule lookup.
    fprintf(stderr, "error: unexpected rule lookup for \"%s\"\n",
            key.c_str());
    abort();
    return nullptr;
  }

  virtual void cycleDetected(const std::vector<core::Rule*>& items) override {
    cycle.clear();
    std::transform(items.begin(), items.end(), std::back_inserter(cycle),
                   [](auto rule) { return rule->key.str(); });
  }

  virtual bool shouldResolveCycle(const std::vector<Rule*>& items,
                                  Rule* candidateRule,
                                  Rule::CycleAction action) override {
    return resolveCycle;
  }

  virtual void error(const Twine& message) override {
    errors.push_back(message.str());
    if (!expectedError) {
      fprintf(stderr, "error: %s\n", message.str().c_str());
      abort();
    }
  }

  void processStarted(basic::ProcessContext*, basic::ProcessHandle, llbuild_pid_t) override { }
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

  virtual void start(TaskInterface ti) override {
    // Compute the list of inputs.
    auto inputs = listInputs();

    // Request all of the inputs.
    inputValues.resize(inputs.size());
    for (int i = 0, e = inputs.size(); i != e; ++i) {
      ti.request(inputs[i], i);
    }
  }

  virtual void provideValue(TaskInterface, uintptr_t inputID,
                            const KeyType& key, const ValueType& value) override {
    // Update the input values.
    assert(inputID < inputValues.size());
    inputValues[inputID] = intFromValue(value);
  }

  virtual void inputsAvailable(TaskInterface ti) override {
    ti.complete(intToValue(compute(inputValues)));
  }
};

// Helper function for creating a simple action.
typedef std::function<Task*(BuildEngine&)> ActionFn;

class SimpleRule: public Rule {
public:
  typedef std::function<bool(const ValueType& value)> ValidFnType;
  typedef std::function<void(core::Rule::StatusKind status)> StatusFnType;

private:
  SimpleTask::ComputeFnType compute;
  std::vector<KeyType> inputs;
  ValidFnType valid;
  StatusFnType update;
public:
  SimpleRule(const KeyType& key, const std::vector<KeyType>& inputs,
             SimpleTask::ComputeFnType compute, ValidFnType valid = nullptr,
             StatusFnType update = nullptr, const basic::CommandSignature& sig = {})
    : Rule(key, sig), compute(compute), inputs(inputs), valid(valid), update(update) { }

  Task* createTask(BuildEngine&) override { return new SimpleTask([this]{ return inputs; }, compute); }

  bool isResultValid(BuildEngine&, const ValueType& value) override {
    if (!valid) return true;
    return valid(value);
  }

  void updateStatus(BuildEngine&, core::Rule::StatusKind status) override {
    if (update) update(status);
  }

};



TEST(BuildEngineTest, basic) {
  // Check a trivial build graph.
  std::vector<std::string> builtKeys;
  SimpleBuildEngineDelegate delegate;
  core::BuildEngine engine(delegate);
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
    "value-A", {}, [&] (const std::vector<int>& inputs) {
          builtKeys.push_back("value-A");
          return 2; })));
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-B", {}, [&] (const std::vector<int>& inputs) {
          builtKeys.push_back("value-B");
          return 3; })));
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "result", {"value-A", "value-B"},
                   [&] (const std::vector<int>& inputs) {
                     EXPECT_EQ(2U, inputs.size());
                     EXPECT_EQ(2, inputs[0]);
                     EXPECT_EQ(3, inputs[1]);
                     builtKeys.push_back("result");
                     return inputs[0] * inputs[1] * 5;
                   })));

  // Build the result.
  EXPECT_EQ(2 * 3 * 5, intFromValue(engine.build("result")));
  EXPECT_EQ(3U, builtKeys.size());
  EXPECT_EQ("value-A", builtKeys[0]);
  EXPECT_EQ("value-B", builtKeys[1]);
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


TEST(BuildEngineTest, duplicateRule) {
  SimpleBuildEngineDelegate delegate;
  delegate.expectedError = true;

  core::BuildEngine engine(delegate);
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
    "value-A", {}, [&] (const std::vector<int>& inputs) { return 1; })));
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
    "value-A", {}, [&] (const std::vector<int>& inputs) { return 2; })));

  EXPECT_EQ(1U, delegate.errors.size());
  EXPECT_TRUE(delegate.errors[0].find("duplicate") != std::string::npos);
}


TEST(BuildEngineTest, basicWithSharedInput) {
  // Check a build graph with a input key shared by multiple rules.
  //
  // Dependencies:
  //   value-C: (value-A, value-B)
  //   value-R: (value-A, value-C)
  std::vector<std::string> builtKeys;
  SimpleBuildEngineDelegate delegate;
  core::BuildEngine engine(delegate);
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-A", {}, [&] (const std::vector<int>& inputs) {
          builtKeys.push_back("value-A");
          return 2; })));
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-B", {}, [&] (const std::vector<int>& inputs) {
          builtKeys.push_back("value-B");
          return 3; })));
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-C", {"value-A", "value-B"},
                   [&] (const std::vector<int>& inputs) {
                     EXPECT_EQ(2U, inputs.size());
                     EXPECT_EQ(2, inputs[0]);
                     EXPECT_EQ(3, inputs[1]);
                     builtKeys.push_back("value-C");
                     return inputs[0] * inputs[1] * 5;
                   })));
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-R", {"value-A", "value-C"},
                   [&] (const std::vector<int>& inputs) {
                     EXPECT_EQ(2U, inputs.size());
                     EXPECT_EQ(2, inputs[0]);
                     EXPECT_EQ(2 * 3 * 5, inputs[1]);
                     builtKeys.push_back("value-R");
                     return inputs[0] * inputs[1] * 7;
                   })));

  // Build the result.
  EXPECT_EQ(2 * 2 * 3 * 5 * 7, intFromValue(engine.build("value-R")));
  EXPECT_EQ(4U, builtKeys.size());
  EXPECT_EQ("value-A", builtKeys[0]);
  EXPECT_EQ("value-B", builtKeys[1]);
  EXPECT_EQ("value-C", builtKeys[2]);
  EXPECT_EQ("value-R", builtKeys[3]);
}

TEST(BuildEngineTest, veryBasicIncremental) {
  // Check a trivial build graph responds to incremental changes appropriately.
  //
  // Dependencies:
  //   value-R: (value-A, value-B)
  std::vector<std::string> builtKeys;
  SimpleBuildEngineDelegate delegate;
  core::BuildEngine engine(delegate);
  int valueA = 2;
  int valueB = 3;
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-A", {}, [&] (const std::vector<int>& inputs) {
          builtKeys.push_back("value-A");
          return valueA; },
      [&](const ValueType& value) {
        return valueA == intFromValue(value);
      } )));
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-B", {}, [&] (const std::vector<int>& inputs) {
          builtKeys.push_back("value-B");
          return valueB; },
      [&](const ValueType& value) {
        return valueB == intFromValue(value);
      } )));
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-R", {"value-A", "value-B"},
                   [&] (const std::vector<int>& inputs) {
                     EXPECT_EQ(2U, inputs.size());
                     EXPECT_EQ(valueA, inputs[0]);
                     EXPECT_EQ(valueB, inputs[1]);
                     builtKeys.push_back("value-R");
                     return inputs[0] * inputs[1] * 5;
                   })));

  // Build the first result.
  builtKeys.clear();
  EXPECT_EQ(valueA * valueB * 5, intFromValue(engine.build("value-R")));
  EXPECT_EQ(3U, builtKeys.size());
  EXPECT_EQ("value-A", builtKeys[0]);
  EXPECT_EQ("value-B", builtKeys[1]);
  EXPECT_EQ("value-R", builtKeys[2]);

  // Mark value-A as having changed, then rebuild and sanity check.
  valueA = 7;
  builtKeys.clear();
  EXPECT_EQ(valueA * valueB * 5, intFromValue(engine.build("value-R")));
  EXPECT_EQ(2U, builtKeys.size());
  EXPECT_EQ("value-A", builtKeys[0]);
  EXPECT_EQ("value-R", builtKeys[1]);

  // Check that a subsequent build is null.
  builtKeys.clear();
  EXPECT_EQ(valueA * valueB * 5, intFromValue(engine.build("value-R")));
  EXPECT_EQ(0U, builtKeys.size());
}

TEST(BuildEngineTest, basicIncremental) {
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
  std::vector<std::string> builtKeys;
  SimpleBuildEngineDelegate delegate;
  core::BuildEngine engine(delegate);
  int valueA = 2;
  int valueB = 3;
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-A", {}, [&] (const std::vector<int>& inputs) {
          builtKeys.push_back("value-A");
          return valueA; },
      [&](const ValueType& value) {
        return valueA == intFromValue(value);
      })));
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-B", {}, [&] (const std::vector<int>& inputs) {
          builtKeys.push_back("value-B");
          return valueB; },
      [&](const ValueType& value) {
        return valueB == intFromValue(value);
      })));
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-C", {"value-A", "value-B"},
                   [&] (const std::vector<int>& inputs) {
                     EXPECT_EQ(2U, inputs.size());
                     EXPECT_EQ(valueA, inputs[0]);
                     EXPECT_EQ(valueB, inputs[1]);
                     builtKeys.push_back("value-C");
                     return inputs[0] * inputs[1] * 5;
                   })));
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-R", {"value-A", "value-C"},
                   [&] (const std::vector<int>& inputs) {
                     EXPECT_EQ(2U, inputs.size());
                     EXPECT_EQ(valueA, inputs[0]);
                     EXPECT_EQ(valueA * valueB * 5, inputs[1]);
                     builtKeys.push_back("value-R");
                     return inputs[0] * inputs[1] * 7;
                   })));
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-D", {"value-R"},
                   [&] (const std::vector<int>& inputs) {
                     EXPECT_EQ(1U, inputs.size());
                     EXPECT_EQ(valueA * valueA * valueB * 5 * 7,
                               inputs[0]);
                     builtKeys.push_back("value-D");
                     return inputs[0] * 11;
                   })));
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-R2", {"value-D"},
                   [&] (const std::vector<int>& inputs) {
                     EXPECT_EQ(1U, inputs.size());
                     EXPECT_EQ(valueA * valueA * valueB * 5 * 7 * 11,
                               inputs[0]);
                     builtKeys.push_back("value-R2");
                     return inputs[0] * 13;
                   })));

  // Build the first result.
  builtKeys.clear();
  EXPECT_EQ(valueA * valueA * valueB * 5 * 7,
            intFromValue(engine.build("value-R")));
  EXPECT_EQ(4U, builtKeys.size());
  EXPECT_EQ("value-A", builtKeys[0]);
  EXPECT_EQ("value-B", builtKeys[1]);
  EXPECT_EQ("value-C", builtKeys[2]);
  EXPECT_EQ("value-R", builtKeys[3]);

  // Mark value-A as having changed, then rebuild and sanity check.
  valueA = 17;
  builtKeys.clear();
  EXPECT_EQ(valueA * valueA * valueB * 5 * 7,
            intFromValue(engine.build("value-R")));
  EXPECT_EQ(3U, builtKeys.size());
  EXPECT_EQ("value-A", builtKeys[0]);
  EXPECT_EQ("value-C", builtKeys[1]);
  EXPECT_EQ("value-R", builtKeys[2]);

  // Mark value-B as having changed, then rebuild and sanity check.
  valueB = 19;
  builtKeys.clear();
  EXPECT_EQ(valueA * valueA * valueB * 5 * 7,
            intFromValue(engine.build("value-R")));
  EXPECT_EQ(3U, builtKeys.size());
  EXPECT_EQ("value-B", builtKeys[0]);
  EXPECT_EQ("value-C", builtKeys[1]);
  EXPECT_EQ("value-R", builtKeys[2]);

  // Build value-R2 for the first time.
  builtKeys.clear();
  EXPECT_EQ(valueA * valueA * valueB * 5 * 7 * 11 * 13,
            intFromValue(engine.build("value-R2")));
  EXPECT_EQ(2U, builtKeys.size());
  EXPECT_EQ("value-D", builtKeys[0]);
  EXPECT_EQ("value-R2", builtKeys[1]);

  // Now mark value-B as having changed, then rebuild value-R, then build
  // value-R2 and sanity check.
  valueB = 23;
  builtKeys.clear();
  EXPECT_EQ(valueA * valueA * valueB * 5 * 7,
            intFromValue(engine.build("value-R")));
  EXPECT_EQ(3U, builtKeys.size());
  EXPECT_EQ("value-B", builtKeys[0]);
  EXPECT_EQ("value-C", builtKeys[1]);
  EXPECT_EQ("value-R", builtKeys[2]);
  builtKeys.clear();
  EXPECT_EQ(valueA * valueA * valueB * 5 * 7 * 11 * 13,
            intFromValue(engine.build("value-R2")));
  EXPECT_EQ(2U, builtKeys.size());
  EXPECT_EQ("value-D", builtKeys[0]);
  EXPECT_EQ("value-R2", builtKeys[1]);

  // Final sanity check.
  builtKeys.clear();
  EXPECT_EQ(valueA * valueA * valueB * 5 * 7,
            intFromValue(engine.build("value-R")));
  EXPECT_EQ(valueA * valueA * valueB * 5 * 7 * 11 * 13,
            intFromValue(engine.build("value-R2")));
  EXPECT_EQ(0U, builtKeys.size());
}

TEST(BuildEngineTest, incrementalDependency) {
  // Check that the engine properly clears the individual result dependencies
  // when a rule is rerun.
  //
  // Dependencies:
  //   value-R: (value-A)
  //
  // FIXME: This test is rather cumbersome for all it is trying to check, maybe
  // we need a logging BuildDB implementation or something that would be easier
  // to test against (or just update the trace to track it).

  SimpleBuildEngineDelegate delegate;
  core::BuildEngine engine(delegate);

  // Attach a custom database, used to get the results.
  class CustomDB : public BuildDB {
  public:
    llvm::StringMap<bool> keyTable;
    
    std::unordered_map<KeyType, Result> ruleResults;

    virtual void attachDelegate(BuildDBDelegate* delegate) override { ; }

    virtual uint64_t getCurrentEpoch(bool* success_out, std::string* error_out) override {
      return 0;
    }
    virtual bool setCurrentIteration(uint64_t value, std::string* error_out) override { return true; }
    virtual bool lookupRuleResult(KeyID keyID,
                                  const KeyType& key,
                                  Result* result_out,
                                  std::string* error_out) override {
      return false;
    }
    virtual bool setRuleResult(KeyID key,
                               const Rule& rule,
                               const Result& result,
                               std::string* error_out) override {
      ruleResults[rule.key] = result;
      return true;
    }
    virtual bool buildStarted(std::string* error_out) override { return true; }
    virtual void buildComplete() override {}
    virtual bool getKeys(std::vector<KeyType>& keys_out, std::string* error_out) override { return false; }
    virtual bool getKeysWithResult(std::vector<KeyType> &keys_out, std::vector<Result> &results_out, std::string* error_out) override { return false; };
  };
  CustomDB *db = new CustomDB();
  std::string error;
  engine.attachDB(std::unique_ptr<CustomDB>(db), &error);

  int valueA = 2;
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-A", {}, [&] (const std::vector<int>& inputs) {
          return valueA; },
      [&](const ValueType& value) {
        return valueA == intFromValue(value);
      })));
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-R", {"value-A"},
                   [&] (const std::vector<int>& inputs) {
                     EXPECT_EQ(1U, inputs.size());
                     EXPECT_EQ(valueA, inputs[0]);
                     return inputs[0] * 3;
                   })));

  // Build the first result.
  EXPECT_EQ(valueA * 3, intFromValue(engine.build("value-R")));

  // Mark value-A as having changed, then rebuild.
  valueA = 5;
  EXPECT_EQ(valueA * 3, intFromValue(engine.build("value-R")));

  // Check the rule results.
  const Result& valueRResult = db->ruleResults["value-R"];
  EXPECT_EQ(valueA * 3, intFromValue(valueRResult.value));
  EXPECT_EQ(1U, valueRResult.dependencies.size());
}

TEST(BuildEngineTest, deepDependencyScanningStack) {
  // Check that the engine can handle dependency scanning of a very deep stack,
  // which would probably crash blowing the stack if the engine used naive
  // recursion.
  //
  // FIXME: It would be nice to run this on a thread with a small stack to
  // guarantee this, and to be able to make the depth small so the test runs
  // faster.
  int depth = 10000;

  SimpleBuildEngineDelegate delegate;
  core::BuildEngine engine(delegate);
  int lastInputValue = 0;
  for (int i = 0; i != depth; ++i) {
    char name[32];
    snprintf(name, sizeof(name), "input-%d", i);
    if (i != depth-1) {
      char inputName[32];
      snprintf(inputName, sizeof(inputName), "input-%d", i+1);
      engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
          name, { inputName },
                             [] (const std::vector<int>& inputs) {
                               return inputs[0]; })));
    } else {
      engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
          name, {},
                       [&] (const std::vector<int>& inputs) {
                         return lastInputValue; },
          [&](const ValueType& value) {
            // FIXME: Once we have custom ValueType objects, we would like to
            // have timestamps on the value and just compare to a timestamp
            // (similar to what we would do for a file).
            return lastInputValue == intFromValue(value);
          })));
    }
  }

  // Build the first result.
  lastInputValue = 42;
  EXPECT_EQ(lastInputValue, intFromValue(engine.build("input-0")));

  // Perform a null build on the result.
  EXPECT_EQ(lastInputValue, intFromValue(engine.build("input-0")));

  // Perform a full rebuild on the result.
  lastInputValue = 52;
  EXPECT_EQ(lastInputValue, intFromValue(engine.build("input-0")));
}

TEST(BuildEngineTest, discoveredDependencies) {
  // Check basic support for tasks to report discovered dependencies.

  // This models a task which has some out-of-band way to read the input.
  class TaskWithDiscoveredDependency : public Task {
    int& valueB;
    int computedInputValue = -1;

  public:
    TaskWithDiscoveredDependency(int& valueB) : valueB(valueB) { }

    virtual void start(TaskInterface ti) override {
      // Request the known input.
      ti.request("value-A", 0);
    }

    virtual void provideValue(TaskInterface, uintptr_t inputID,
                              const KeyType& key, const ValueType& value) override {
      assert(inputID == 0);
      computedInputValue = intFromValue(value);
    }

    virtual void inputsAvailable(TaskInterface ti) override {
      // Report the discovered dependency.
      ti.discoveredDependency("value-B");
      ti.complete(intToValue(computedInputValue * valueB * 5));
    }
  };

  std::vector<std::string> builtKeys;
  SimpleBuildEngineDelegate delegate;
  core::BuildEngine engine(delegate);
  int valueA = 2;
  int valueB = 3;
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-A", { },
                   [&] (const std::vector<int>& inputs) {
                     builtKeys.push_back("value-A");
                     return valueA;
                   },
      [&](const ValueType& value) {
        return valueA == intFromValue(value);
      })));
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-B", { },
                   [&] (const std::vector<int>& inputs) {
                     builtKeys.push_back("value-B");
                     return valueB;
                   },
      [&](const ValueType& value) {
        return valueB == intFromValue(value);
      })));

  class RuleWithDiscoveredDependency: public Rule {
    std::vector<std::string>& builtKeys;
    int& dep;
  public:
    RuleWithDiscoveredDependency(const KeyType& key,
                                 std::vector<std::string>& builtKeys, int& dep)
      : Rule(key), builtKeys(builtKeys), dep(dep) { }
    Task* createTask(BuildEngine&) override {
      builtKeys.push_back(key.str());
      return new TaskWithDiscoveredDependency(dep);
    }
    bool isResultValid(BuildEngine&, const ValueType&) override { return true; }
  };
  engine.addRule(std::unique_ptr<core::Rule>(new RuleWithDiscoveredDependency("output", builtKeys, valueB)));

  // Build the first result.
  builtKeys.clear();
  EXPECT_EQ(valueA * valueB * 5, intFromValue(engine.build("output")));
  EXPECT_EQ(std::vector<std::string>({ "output", "value-A", "value-B" }),
            builtKeys);

  // Verify that the next build is a null build.
  builtKeys.clear();
  EXPECT_EQ(valueA * valueB * 5, intFromValue(engine.build("output")));
  EXPECT_EQ(std::vector<std::string>(), builtKeys);

  // Verify that the build depends on valueB.
  valueB = 7;
  builtKeys.clear();
  EXPECT_EQ(valueA * valueB * 5, intFromValue(engine.build("output")));
  EXPECT_EQ(std::vector<std::string>({ "value-B", "output" }),
            builtKeys);


  // Verify again that the next build is a null build.
  builtKeys.clear();
  EXPECT_EQ(valueA * valueB * 5, intFromValue(engine.build("output")));
  EXPECT_EQ(std::vector<std::string>(), builtKeys);
}

TEST(BuildEngineTest, unchangedOutputs) {
  // Check building with unchanged outputs.
  std::vector<std::string> builtKeys;
  SimpleBuildEngineDelegate delegate;
  core::BuildEngine engine(delegate);
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value", {}, [&] (const std::vector<int>& inputs) {
        builtKeys.push_back("value");
        return 2; },
      [&](const ValueType&) {
        // Always rebuild
        return false;
      })));
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "result", {"value"},
                   [&] (const std::vector<int>& inputs) {
                     EXPECT_EQ(1U, inputs.size());
                     EXPECT_EQ(2, inputs[0]);
                     builtKeys.push_back("result");
                     return inputs[0] * 3;
                   })));

  // Build the result.
  EXPECT_EQ(2 * 3, intFromValue(engine.build("result")));
  EXPECT_EQ(2U, builtKeys.size());
  EXPECT_EQ("value", builtKeys[0]);
  EXPECT_EQ("result", builtKeys[1]);

  // Rebuild the result.
  //
  // Only "value" should rebuild, as it explicitly declares itself invalid each
  // time, but "result" should not need to rerun.
  builtKeys.clear();
  EXPECT_EQ(2 * 3, intFromValue(engine.build("result")));
  EXPECT_EQ(1U, builtKeys.size());
  EXPECT_EQ("value", builtKeys[0]);
}

TEST(BuildEngineTest, StatusCallbacks) {
  unsigned numScanned = 0;
  unsigned numComplete = 0;
  SimpleBuildEngineDelegate delegate;
  core::BuildEngine engine(delegate);
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "input", {}, [&] (const std::vector<int>& inputs) {
          return 2; },
      nullptr,
      [&] (core::Rule::StatusKind status) {
        if (status == core::Rule::StatusKind::IsScanning) {
          ++numScanned;
        } else {
          assert(status == core::Rule::StatusKind::IsComplete);
          ++numComplete;
        }
      } )));
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "output", {"input"},
                   [&] (const std::vector<int>& inputs) {
                     return inputs[0] * 3;
                   },
      nullptr,
      [&] (core::Rule::StatusKind status) {
        if (status == core::Rule::StatusKind::IsScanning) {
          ++numScanned;
        } else {
          assert(status == core::Rule::StatusKind::IsComplete);
          ++numComplete;
        }
      } )));

  // Build the result.
  EXPECT_EQ(2 * 3, intFromValue(engine.build("output")));
  EXPECT_EQ(2U, numScanned);
  EXPECT_EQ(2U, numComplete);
}

/// Check basic cycle detection.
TEST(BuildEngineTest, SimpleCycle) {
  SimpleBuildEngineDelegate delegate;
  core::BuildEngine engine(delegate);
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "A", {"B"}, [&](const std::vector<int>& inputs) {
          return 2; })));
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "B", {"A"}, [&](const std::vector<int>& inputs) {
          return 2; })));

  // Build the result.
  auto result = engine.build("A");
  EXPECT_EQ(ValueType{}, result);
  EXPECT_EQ(std::vector<std::string>({ "A", "B", "A" }), delegate.cycle);
}

/// Check detection of a cycle discovered during scanning from input.
TEST(BuildEngineTest, CycleDuringScanningFromTop) {
  SimpleBuildEngineDelegate delegate;
  core::BuildEngine engine(delegate);
  unsigned iteration = 0;
  class CycleRule: public Rule {
  public:
    typedef std::function<std::vector<KeyType>()> InputFnType;
    typedef std::function<int(const std::vector<int>&)> ComputeFnType;
  private:
    InputFnType input;
    ComputeFnType compute;
    bool valid;
  public:
    CycleRule(const KeyType& key, InputFnType input, ComputeFnType compute, bool valid = true)
      : Rule(key), input(input), compute(compute), valid(valid) { }
    Task* createTask(BuildEngine&) override {
      return new SimpleTask(input, compute);
    }
    bool isResultValid(BuildEngine&, const ValueType&) override {
      return valid;
    }
  };
  engine.addRule(std::unique_ptr<core::Rule>(new CycleRule(
    "A",
    [&iteration]() -> std::vector<KeyType> {
      switch (iteration) {
      case 0:
        return { "B", "C" };
      case 1:
        return { "B" };
      default:
        llvm::report_fatal_error("unexpected iterator");
      }
    },
    [](const std::vector<int>& inputs) { return 2; }
  )));
  engine.addRule(std::unique_ptr<core::Rule>(new CycleRule(
    "B",
    [&iteration]() -> std::vector<KeyType> {
      switch (iteration) {
      case 0:
        return { "C" };
      case 1:
        return { "C" };
      default:
        llvm::report_fatal_error("unexpected iterator");
      }
    },
    [](const std::vector<int>& inputs) { return 2; }
  )));
  engine.addRule(std::unique_ptr<core::Rule>(new CycleRule(
    "C",
    [&iteration]() -> std::vector<KeyType> {
      switch (iteration) {
      case 0:
        return { };
      case 1:
        return { "B" };
      default:
        llvm::report_fatal_error("unexpected iterator");
      }
    },
    [](const std::vector<int>& inputs) { return 2; },
    false // always rebuild
  )));

  // Build the result.
  {
    EXPECT_EQ(2, intFromValue(engine.build("A")));
    EXPECT_EQ(std::vector<std::string>({}), delegate.cycle);
  }

  // Introduce a cycle, and rebuild.
  {
    iteration = 1;
    auto result = engine.build("A");
    EXPECT_EQ(ValueType{}, result);
    EXPECT_EQ(std::vector<std::string>({ "A", "B", "C", "B" }), delegate.cycle);
  }

  // Rebuild, allowing the engine to resolve the cycle
  {
    iteration = 1;
    delegate.cycle.clear();
    delegate.resolveCycle = true;
    auto result = engine.build("A");
    EXPECT_EQ(2, intFromValue(result));
    EXPECT_EQ(std::vector<std::string>({}), delegate.cycle);
  }
}

TEST(BuildEngineTest, basicIncrementalSignatureChange) {
  // Check a trivial build graph responds to incremental changes in rule
  // signatures appropriately.
  //
  // Dependencies:
  //   value-R: (value-A, value-B)

  // Create a temporary file.
  llvm::SmallString<256> dbPath;
  auto ec = llvm::sys::fs::createTemporaryFile("build", "db", dbPath);
  EXPECT_EQ(bool(ec), false);
  fprintf(stderr, "using db: %s\n", dbPath.c_str());


  std::vector<std::string> builtKeys;
  SimpleBuildEngineDelegate delegate;
  int valueA = 2;
  int valueB = 3;
  int sigA = 1;
  int sigB = 2;

  auto setupEngine = [&](core::BuildEngine& engine) {
    // Attach the database.
    {
      std::string error;
      auto db = createSQLiteBuildDB(dbPath, 1, /* recreateUnmatchedVersion = */ true, &error);
      EXPECT_EQ(bool(db), true);
      if (!db) {
        fprintf(stderr, "unable to open database: %s\n", error.c_str());
        return;
      }
      engine.attachDB(std::move(db), &error);
    }

    engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-A", {}, [&] (const std::vector<int>& inputs) {
        builtKeys.push_back("value-A");
        return valueA; },
      [&](const ValueType& value) {
        return valueA == intFromValue(value);
      }, nullptr, basic::CommandSignature(sigA))));
    engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-B", {}, [&] (const std::vector<int>& inputs) {
        builtKeys.push_back("value-B");
        return valueB; },
      [&](const ValueType& value) {
        return valueB == intFromValue(value);
      }, nullptr, basic::CommandSignature(sigB))));
    engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
      "value-R", {"value-A", "value-B"},
                   [&] (const std::vector<int>& inputs) {
                     EXPECT_EQ(2U, inputs.size());
                     EXPECT_EQ(valueA, inputs[0]);
                     EXPECT_EQ(valueB, inputs[1]);
                     builtKeys.push_back("value-R");
                     return inputs[0] * inputs[1] * 5;
                   })));
  };

  std::unique_ptr<core::BuildEngine> engine;

  // Build the first result.
  builtKeys.clear();
  engine = llvm::make_unique<core::BuildEngine>(delegate);
  setupEngine(*engine);
  EXPECT_EQ(valueA * valueB * 5, intFromValue(engine->build("value-R")));
  EXPECT_EQ(3U, builtKeys.size());
  EXPECT_EQ("value-A", builtKeys[0]);
  EXPECT_EQ("value-B", builtKeys[1]);
  EXPECT_EQ("value-R", builtKeys[2]);


  // Mark value-A as having a different signature, then rebuild and sanity check.
  builtKeys.clear();
  engine = llvm::make_unique<core::BuildEngine>(delegate);
  sigA = 2;
  setupEngine(*engine);
  EXPECT_EQ(valueA * valueB * 5, intFromValue(engine->build("value-R")));
  EXPECT_EQ(1U, builtKeys.size());
  EXPECT_EQ("value-A", builtKeys[0]);

  // Check that a subsequent build is null.
  builtKeys.clear();
  engine = llvm::make_unique<core::BuildEngine>(delegate);
  setupEngine(*engine);
  EXPECT_EQ(valueA * valueB * 5, intFromValue(engine->build("value-R")));
  EXPECT_EQ(0U, builtKeys.size());
}

TEST(BuildEngineTest, concurrentProtection) {
  // Cross thread coordination
  std::mutex mutex;
  bool started = false;
  std::condition_variable started_cv;
  bool done = false;
  std::condition_variable done_cv;

  // Basic 1-rule build graph
  SimpleBuildEngineDelegate delegate;
  core::BuildEngine engine(delegate);
  engine.addRule(std::unique_ptr<core::Rule>(new SimpleRule(
    "result", {}, [&] (const std::vector<int>& inputs) {
      auto lock = std::unique_lock<std::mutex>(mutex);
      started = true;
      started_cv.notify_all();
      while (!done) {
        done_cv.wait(lock);
      }
      return 1;
    })
  ));

  // Start a build on another thread
  auto build1 = std::async( std::launch::async, [&](){
    // We expect that this build should eventually succeed with the build value
    // of '1' from above.
    EXPECT_EQ(1, intFromValue(engine.build("result")));
  });

  // Wait for the build to actually start
  auto lock = std::unique_lock<std::mutex>(mutex);
  while (!started) {
    started_cv.wait(lock);
  }
  lock.unlock();

  // Expect that we'll get an error trying to start this build
  delegate.expectedError = true;
  engine.build("result");
  EXPECT_EQ(1U, delegate.errors.size());
  EXPECT_TRUE(delegate.errors[0].find("busy") != std::string::npos);

  // Wrap up the first build
  lock.lock();
  done = true;
  lock.unlock();
  done_cv.notify_all();
  build1.wait();

  // Ensure that we can build again
  delegate.errors.clear();
  delegate.expectedError = false;
  EXPECT_EQ(1, intFromValue(engine.build("result")));
  EXPECT_EQ(0U, delegate.errors.size());
}

}
