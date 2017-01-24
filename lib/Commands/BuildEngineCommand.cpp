//===-- BuildEngineCommand.cpp --------------------------------------------===//
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

#include "llbuild/Commands/Commands.h"

#include "llbuild/Basic/LLVM.h"
#include "llbuild/Core/BuildEngine.h"

#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>

using namespace llbuild;
using namespace llbuild::commands;

#pragma mark - Ackermann Build Command

namespace {

#ifndef NDEBUG
static uint64_t ack(int m, int n) {
  // Memoize using an array of growable vectors.
  std::vector<std::vector<int>> memoTable(m+1);

  std::function<int(int,int)> ack_internal;
  ack_internal = [&] (int m, int n) {
    assert(m >= 0 && n >= 0);
    auto& memoRow = memoTable[m];
    if (size_t(n) >= memoRow.size())
        memoRow.resize(n + 1);
    if (memoRow[n] != 0)
      return memoRow[n];

    int result;
    if (m == 0) {
      result = n + 1;
    } else if (n == 0) {
      result = ack_internal(m - 1, 1);
    } else {
      result = ack_internal(m - 1, ack_internal(m, n - 1));
    };

    memoRow[n] = result;
    return result;
  };

  return ack_internal(m, n);
}
#endif

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

/// Key representation used in Ackermann build.
struct AckermannKey {
  /// The Ackermann number this key represents.
  int m, n;

  /// Create a key representing the given Ackermann number.
  AckermannKey(int m, int n) : m(m), n(n) {}

  /// Create an Ackermann key from the encoded representation.
  AckermannKey(const core::KeyType& key) {
      auto keyString = StringRef(key);
      assert(keyString.startswith("ack(") && keyString.endswith(")"));
      auto arguments = keyString.split("(").second.split(")").first.split(",");
      m = 0;
      n = 0;
      (void)arguments.first.getAsInteger(10, m);
      (void)arguments.second.getAsInteger(10, n);
      assert(m >= 0 && m < 4);
      assert(n >= 0);
  }

  /// Convert an Ackermann key to its encoded representation.
  operator core::KeyType() const {
    char inputKey[32];
    sprintf(inputKey, "ack(%d,%d)", m, n);
    return inputKey;
  }
};

/// Value representation used in Ackermann build.
struct AckermannValue {
  int value;

  /// Create a value for 0.
  AckermannValue() : value(0) { }

  /// Create a value from an integer.
  AckermannValue(int value) : value(value) { }

  /// Create a value from the encoded representation.
  AckermannValue(const core::ValueType& value) : value(intFromValue(value)) { }

  /// Convert a value to its encoded representation.
  operator core::ValueType() const {
    return intToValue(value);
  }

  /// Convert a wrapped value to its actual value.
  operator int() const {
    return value;
  }
};
    
struct AckermannTask : core::Task {
  int m, n;
  AckermannValue recursiveResultA = {};
  AckermannValue recursiveResultB = {};

  AckermannTask(core::BuildEngine& engine, int m, int n) : m(m), n(n) {
    engine.registerTask(this);
  }

  /// Called when the task is started.
  virtual void start(core::BuildEngine& engine) override {
    // Request the first recursive result, if necessary.
    if (m == 0) {
      ;
    } else if (n == 0) {
      engine.taskNeedsInput(this, AckermannKey(m-1, 1), 0);
    } else {
      engine.taskNeedsInput(this, AckermannKey(m, n-1), 0);
    }
  }

  /// Called when a task’s requested input is available.
  virtual void provideValue(core::BuildEngine& engine, uintptr_t inputID,
                            const core::ValueType& value) override {
    if (inputID == 0) {
      recursiveResultA = value;

      // Request the second recursive result, if needed.
      if (m != 0 && n != 0) {
        engine.taskNeedsInput(this, AckermannKey(m-1, recursiveResultA), 1);
      }
    } else {
      assert(inputID == 1 && "invalid input ID");
      recursiveResultB = value;
    }
  }

  /// Called when all inputs are available.
  virtual void inputsAvailable(core::BuildEngine& engine) override {
    if (m == 0) {
      engine.taskIsComplete(this, AckermannValue(n + 1));
      return;
    }

    assert(recursiveResultA != 0);
    if (n == 0) {
      engine.taskIsComplete(this, recursiveResultA);
      return;
    }

    assert(recursiveResultB != 0);
    engine.taskIsComplete(this, recursiveResultB);
  }
};

static int runAckermannBuild(int m, int n, int recomputeCount,
                             const std::string& traceFilename,
                             const std::string& dumpGraphPath) {
  // Compute the value of ackermann(M, N) using the build system.
  assert(m >= 0 && m < 4);
  assert(n >= 0);

  // Define the delegate which will dynamically construct rules of the form
  // "ack(M,N)".
  class AckermannDelegate : public core::BuildEngineDelegate {
  public:
    int numRules = 0;

    /// Get the rule to use for the given Key.
    virtual core::Rule lookupRule(const core::KeyType& keyData) override {
      ++numRules;
      auto key = AckermannKey(keyData);
      return core::Rule{key, [key] (core::BuildEngine& engine) {
          return new AckermannTask(engine, key.m, key.n); } };
    }

    /// Called when a cycle is detected by the build engine and it cannot make
    /// forward progress.
    virtual void cycleDetected(const std::vector<core::Rule*>& items) override {
      assert(0 && "unexpected cycle!");
    }

    /// Called when a fatal error is encountered by the build engine.
    virtual void error(const Twine &message) override {
      assert(0 && ("error:" + message.str()).c_str());
    }
  };
  AckermannDelegate delegate;
  core::BuildEngine engine(delegate);

  // Enable tracing, if requested.
  if (!traceFilename.empty()) {
    std::string error;
    if (!engine.enableTracing(traceFilename, &error)) {
      fprintf(stderr, "error: %s: unable to enable tracing: %s\n",
              getProgramName(), error.c_str());
      return 1;
    }
  }

  auto key = AckermannKey(m, n);
  auto result = AckermannValue(engine.build(key));
  llvm::outs() << "ack(" << m << ", " << n << ") = " << result << "\n";
  if (n < 10) {
#ifndef NDEBUG
    int expected = ack(m, n);
    assert(result == expected);
#endif
  }
  llvm::outs() << "... computed using " << delegate.numRules << " rules\n";

  if (!dumpGraphPath.empty()) {
    engine.dumpGraphToFile(dumpGraphPath);
  }

  // Recompute the result as many times as requested.
  for (int i = 0; i != recomputeCount; ++i) {
    auto recomputedResult = AckermannValue(engine.build(key));
    if (recomputedResult != result)
      abort();
  }

  return 0;
}

static void ackermannUsage() {
  int optionWidth = 20;
  fprintf(stderr, "Usage: %s buildengine ack [options] <M> <N>\n",
          getProgramName());
  fprintf(stderr, "\nOptions:\n");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--help",
          "show this help message and exit");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--dump-graph <PATH>",
          "dump build graph to PATH in Graphviz DOT format");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--recompute <N>",
          "recompute the result N times, to stress dependency checking");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--trace <PATH>",
          "trace build engine operation to PATH");
  ::exit(1);
}

static int executeAckermannCommand(std::vector<std::string> args) {
  int recomputeCount = 0;
  std::string dumpGraphPath, traceFilename;
  while (!args.empty() && args[0][0] == '-') {
    const std::string option = args[0];
    args.erase(args.begin());

    if (option == "--")
      break;

    if (option == "--help") {
      ackermannUsage();
    } else if (option == "--recompute") {
      if (args.empty()) {
        fprintf(stderr, "error: %s: missing argument to '%s'\n\n",
                getProgramName(), option.c_str());
        ackermannUsage();
      }
      char *end;
      recomputeCount = ::strtol(args[0].c_str(), &end, 10);
      if (*end != '\0') {
        fprintf(stderr, "error: %s: invalid argument to '%s'\n\n",
                getProgramName(), option.c_str());
        ackermannUsage();
      }
      args.erase(args.begin());
    } else if (option == "--dump-graph") {
      if (args.empty()) {
        fprintf(stderr, "error: %s: missing argument to '%s'\n\n",
                getProgramName(), option.c_str());
        ackermannUsage();
      }
      dumpGraphPath = args[0];
      args.erase(args.begin());
    } else if (option == "--trace") {
      if (args.empty()) {
        fprintf(stderr, "error: %s: missing argument to '%s'\n\n",
                getProgramName(), option.c_str());
        ackermannUsage();
      }
      traceFilename = args[0];
      args.erase(args.begin());
    } else {
      fprintf(stderr, "error: %s: invalid option: '%s'\n\n",
              getProgramName(), option.c_str());
      ackermannUsage();
    }
  }

  if (args.size() != 2) {
    fprintf(stderr, "error: %s: invalid number of arguments\n", getProgramName());
    ackermannUsage();
  }

  const char *str = args[0].c_str();
  int m = ::strtol(str, (char**)&str, 10);
  if (*str != '\0') {
    fprintf(stderr, "error: %s: invalid argument '%s' (expected integer)\n",
            getProgramName(), args[0].c_str());
    return 1;
  }
  str = args[1].c_str();
  int n = ::strtol(str, (char**)&str, 10);
  if (*str != '\0') {
    fprintf(stderr, "error: %s: invalid argument '%s' (expected integer)\n",
            getProgramName(), args[1].c_str());
    return 1;
  }

  if (m >= 4) {
    fprintf(stderr, "error: %s: invalid argument M = '%d' (too large)\n",
            getProgramName(), m);
    return 1;
  }

  if (n >= 1024) {
    fprintf(stderr, "error: %s: invalid argument N = '%d' (too large)\n",
            getProgramName(), n);
    return 1;
  }

  return runAckermannBuild(m, n, recomputeCount, traceFilename, dumpGraphPath);
}

}

#pragma mark - Build Engine Top-Level Command

static void usage() {
  fprintf(stderr, "Usage: %s buildengine [--help] <command> [<args>]\n",
          getProgramName());
  fprintf(stderr, "\n");
  fprintf(stderr, "Available commands:\n");
  fprintf(stderr, "  ack           -- Compute Ackermann\n");
  fprintf(stderr, "\n");
  exit(1);
}

int commands::executeBuildEngineCommand(const std::vector<std::string> &args) {
  // Expect the first argument to be the name of another subtool to delegate to.
  if (args.empty() || args[0] == "--help")
    usage();

  if (args[0] == "ack") {
    return executeAckermannCommand({args.begin()+1, args.end()});
  } else {
    fprintf(stderr, "error: %s: unknown command '%s'\n", getProgramName(),
            args[0].c_str());
    return 1;
  }
}
