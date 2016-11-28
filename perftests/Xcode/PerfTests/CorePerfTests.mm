//===-- BuildEngine.mm ---------------------------------------------------===//
//
// Copyright (c) 2014 Apple Inc. All rights reserved.
//
//===----------------------------------------------------------------------===//

#import "llbuild/Commands/Commands.h"

#import "llbuild/Core/BuildEngine.h"

#import <XCTest/XCTest.h>

#import <functional>

using namespace llbuild;
using namespace llbuild::core;

@interface CorePerfTests : XCTestCase

@end

#pragma mark - Support Classes

namespace {

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
//
// FIXME: This is copied from the Core BuildEngine unittest, we should figure
// out if it should be shared in a common build engine support library at some
// point.
class SimpleTask : public Task {
public:
  typedef std::function<int(const std::vector<int>&)> ComputeFnType;

private:
  std::vector<KeyType> Inputs;
  std::vector<int> InputValues;
  ComputeFnType Compute;

public:
  SimpleTask(const std::vector<KeyType>& Inputs, ComputeFnType Compute)
    : Inputs(Inputs), Compute(Compute)
  {
    InputValues.resize(Inputs.size());
  }

  virtual void start(BuildEngine& Engine) override {
    // Request all of the inputs.
    for (size_t i = 0, e = Inputs.size(); i != e; ++i) {
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

}

@implementation CorePerfTests

#pragma mark - "buildengine ack" Performance Tests

- (void)testBuildEngineBasicPerf {
  // Test the timing of 'buildengine ack 3 14'.
  //
  // This test uses ~300k rules, and is a good stress test for the core engine
  // operation.

  [self measureBlock:^{
      llbuild::commands::executeBuildEngineCommand({
          "ack", "3", "14" });
    }];
}

- (void)testBuildEngineDependencyScanningCorePerf {
  // Test the timing of 'buildengine ack 3 11', with a high recompute count.
  //
  // This test uses ~40k rules, but then recomputes the results multiple times,
  // which is a stress test of the dependency scanning performance.

  [self measureBlock:^{
      llbuild::commands::executeBuildEngineCommand({
          "ack", "--recompute", "100", "3", "11" });
    }];
}

#pragma mark - Synthetic Graph Dependency Scanning Tests

- (void)testBuildEngineDependencyScanningOnLinearChain {
  // Test the scanning performance on a deep linear build graph of M nodes::
  //
  //   i1 -> i2 -> ... -> iM
  int M = 1000000; // Use a graph of 1 million nodes.

  // Set up the build rules.
  struct LinearDelegate : public BuildEngineDelegate {
    virtual core::Rule lookupRule(const core::KeyType& Key) override {
      // We never expect dynamic rule lookup.
      fprintf(stderr, "error: unexpected rule lookup for \"%s\"\n",
              Key.c_str());
      abort();
      return core::Rule();
    }
    virtual void cycleDetected(const std::vector<core::Rule*>& Cycle) override {
      // We never expect to find a cycle.
      fprintf(stderr, "error: unexpected cycle\n");
      abort();
    }
  } Delegate;
  __block core::BuildEngine Engine(Delegate);
  int LastInputValue = 0;
  for (int i = 1; i <= M; ++i) {
    char Name[32];
    sprintf(Name, "i%d", i);
    if (i != M) {
      char InputName[32];
      sprintf(InputName, "i%d", i+1);
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
          [&](BuildEngine&, const Rule& rule, const ValueType& Value) {
              return LastInputValue == IntFromValue(Value);
          } });
    }
  }

  // Build the first result.
  LastInputValue = 42;
  auto Result = IntFromValue(Engine.build("i1"));
  (void)Result;
  assert(Result == LastInputValue);

  // Run a single initial null build to try and warm the timings below.
  Engine.build("i1");

  // Measure the null build time.
  [self measureBlock:^{
      auto Result = IntFromValue(Engine.build("i1"));
      (void)Result;
      assert(Result == LastInputValue);
    }];
}

static int64_t i64pow(int64_t Value, int64_t Exponent) {
  int64_t Result = 1;
  for (int64_t i = 0; i != Exponent; ++i)
    Result *= Value;
  return Result;
}

- (void)testBuildEngineDependencyScanningOnNaryTree {
  // Test the scanning performance on an M-height N-ary tree with no sharing::
  //
  //   i1,1 ---> i2,1 ... ---> iM,1
  //         \-> i2,2       ...
  //         \-> i2,N          iM,{N**(M-1)}

  int M = 13, N = 3; // Use a graph of 797,161 nodes.
  size_t NumTotalNodes = (i64pow(N, M) - 1) / (N - 1);
  NSLog(@"running test with %d-ary tree of depth %d (%zu nodes)\n",
         N, M, NumTotalNodes);

  // Set up the build rules.
  struct NaryTreeDelegate : public BuildEngineDelegate {
    virtual core::Rule lookupRule(const core::KeyType& Key) override {
      // We never expect dynamic rule lookup.
      fprintf(stderr, "error: unexpected rule lookup for \"%s\"\n",
              Key.c_str());
      abort();
      return core::Rule();
    }
    virtual void cycleDetected(const std::vector<core::Rule*>& Cycle) override {
      // We never expect to find a cycle.
      fprintf(stderr, "error: unexpected cycle\n");
      abort();
    }
  } Delegate;
  __block core::BuildEngine Engine(Delegate);
  int LastInputValue = 0;
  for (int i = 1; i <= M; ++i) {
    // Compute the total number of groups at this depth.
    int64_t NumNodes = i64pow(N, i - 1);
    for (int j = 1; j <= NumNodes; ++j) {
      char Name[32];
      sprintf(Name, "i%d,%d", i, j);
      if (i != M) {
        std::vector<KeyType> Inputs;
        for (int k = 1; k <= N; ++k) {
          char InputName[32];
          sprintf(InputName, "i%d,%d", i+1, 1 + (j - 1)*N + (k - 1));
          Inputs.push_back(InputName);
        }
        Engine.addRule({
            Name, simpleAction(Inputs, [] (const std::vector<int>& Inputs) {
                return Inputs[0]; }) });
      } else {
        Engine.addRule({
            Name,
            simpleAction({},
                         [&] (const std::vector<int>& Inputs) {
                           return LastInputValue; }),
            [&](BuildEngine&, const Rule& rule, const ValueType& Value) {
              return LastInputValue == IntFromValue(Value);
            } });
      }
    }
  }

  // Build the first result.
  LastInputValue = 42;
  auto Result = IntFromValue(Engine.build("i1,1"));
  (void)Result;
  assert(Result == LastInputValue);

  // Run a single initial null build to try and warm the timings below.
  Engine.build("i1,1");

  // Measure the null build time.
  [self measureBlock:^{
      auto Result = IntFromValue(Engine.build("i1,1"));
      (void)Result;
      assert(Result == LastInputValue);
    }];
}

- (void)testBuildEngineDependencyScanningOn2DMatrix {
  // Test the scanning performance on a 2D {M+1}x{N+1} matrix where each node
  // depends on nodes which are adjacent above or to the right::
  //
  //   i1,N --> i2,N --> ... --> iM,N
  //    ^        ^       ...      ^
  //    |        |                |
  //   i1,2 --> i2,2 --> ... --> iM,2
  //    ^        ^       ...      ^
  //    |        |                |
  //   i1,1 --> i2,1 --> ... --> iM,1
  //
  // This is an easy to construct synthetic graph which is scalable and has
  // sharing.
  int M = 1000, N = 1000; // Use a graph of 1 million nodes.

  // Set up the build rules.
  struct MatrixDelegate : public BuildEngineDelegate {
    virtual core::Rule lookupRule(const core::KeyType& Key) override {
      // We never expect dynamic rule lookup.
      fprintf(stderr, "error: unexpected rule lookup for \"%s\"\n",
              Key.c_str());
      abort();
      return core::Rule();
    }
    virtual void cycleDetected(const std::vector<core::Rule*>& Cycle) override {
      // We never expect to find a cycle.
      fprintf(stderr, "error: unexpected cycle\n");
      abort();
    }
  } Delegate;
  __block core::BuildEngine Engine(Delegate);
  int LastInputValue = 0;
  for (int i = 1; i <= M; ++i) {
    for (int j = 1; j <= N; ++j) {
      char Name[32];
      sprintf(Name, "i%d,%d", i, j);
      if (i != M && j != N) {
        // Nodes not on an edge.
        char InputAName[32];
        sprintf(InputAName, "i%d,%d", i+1, j);
        char InputBName[32];
        sprintf(InputBName, "i%d,%d", i, j+1);
        Engine.addRule({
            Name, simpleAction({ InputAName, InputBName },
                               [] (const std::vector<int>& Inputs) {
                                 return Inputs[0]; }) });
      } else if (i != M) {
        // Top edge.
        assert(j == N);
        char InputName[32];
        sprintf(InputName, "i%d,%d", i+1, j);
        Engine.addRule({
            Name, simpleAction({ InputName },
                               [] (const std::vector<int>& Inputs) {
                                 return Inputs[0]; }) });
      } else if (j != N) {
        // Right edge.
        assert(i == M);
        char InputName[32];
        sprintf(InputName, "i%d,%d", i, j+1);
        Engine.addRule({
            Name, simpleAction({ InputName },
                               [] (const std::vector<int>& Inputs) {
                                 return Inputs[0]; }) });
      } else {
        // Top-right corner node.
        assert(i == M && j == N);
        Engine.addRule({
            Name,
            simpleAction({},
                         [&] (const std::vector<int>& Inputs) {
                           return LastInputValue; }),
            [&](BuildEngine&, const Rule& rule, const ValueType& Value) {
              return LastInputValue == IntFromValue(Value);
            } });
      }
    }
  }

  // Build the first result.
  LastInputValue = 42;
  auto Result = IntFromValue(Engine.build("i1,1"));
  (void)Result;
  assert(Result == LastInputValue);

  // Run a single initial null build to try and warm the timings below.
  Engine.build("i1,1");

  // Measure the null build time.
  [self measureBlock:^{
      auto Result = IntFromValue(Engine.build("i1,1"));
      (void)Result;
      assert(Result == LastInputValue);
    }];
}

@end
