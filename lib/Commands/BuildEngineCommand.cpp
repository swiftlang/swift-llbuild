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

#include "llbuild/Core/BuildEngine.h"

#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>

using namespace llbuild;

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

static core::Task* buildAck(core::BuildEngine& engine, int m, int n) {
  struct AckermannTask : core::Task {
    int m, n;
    int recursiveResultA = 0;
    int recursiveResultB = 0;

    AckermannTask(int m, int n) : m(m), n(n) { }

    virtual void provideValue(core::BuildEngine& engine, uintptr_t inputID,
                              const core::ValueType& value) override {
      if (inputID == 0) {
        recursiveResultA = intFromValue(value);

        // Request the second recursive result, if needed.
        if (m != 0 && n != 0) {
          char inputKey[32];
          sprintf(inputKey, "ack(%d,%d)", m - 1, recursiveResultA);
          engine.taskNeedsInput(this, inputKey, 1);
        }
      } else {
        assert(inputID == 1 && "invalid input ID");
        recursiveResultB = intFromValue(value);
      }
    }

    virtual void start(core::BuildEngine& engine) override {
      // Request the first recursive result, if necessary.
      if (m == 0) {
        ;
      } else if (n == 0) {
        char inputKey[32];
        sprintf(inputKey, "ack(%d,%d)", m-1, 1);
        engine.taskNeedsInput(this, inputKey, 0);
      } else {
        char inputKey[32];
        sprintf(inputKey, "ack(%d,%d)", m, n-1);
        engine.taskNeedsInput(this, inputKey, 0);
      }
    }

    virtual void inputsAvailable(core::BuildEngine& engine) override {
      if (m == 0) {
        engine.taskIsComplete(this, intToValue(n + 1));
        return;
      }

      assert(recursiveResultA != 0);
      if (n == 0) {
        engine.taskIsComplete(this, intToValue(recursiveResultA));
        return;
      }

      assert(recursiveResultB != 0);
      engine.taskIsComplete(this, intToValue(recursiveResultB));
    }
  };

  // Create the task.
  return engine.registerTask(new AckermannTask(m, n));
}

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

    virtual core::Rule lookupRule(const core::KeyType& key) override {
      // Extract the Ackermann parameters.
      //
      // FIXME: Need generalized key type.
      assert(key[0] == 'a' && key[1] == 'c' && key[2] == 'k' &&
             key[3] == '(');
      const char* mStart = &key[4];
      const char* mEnd = strchr(mStart, ',');
      assert(mEnd != nullptr && mEnd[0] == ',');
      const char* nStart = &mEnd[1];
      int m = strtol(mStart, nullptr, 10);
      int n = strtol(nStart, nullptr, 10);
      assert(m >= 0 && m < 4);
      assert(n >= 0);

      ++numRules;
      return core::Rule{key, [m, n] (core::BuildEngine& engine) {
          return buildAck(engine, m, n); } };
    }

    virtual void cycleDetected(const std::vector<core::Rule*>& items) override {
      assert(0 && "unexpected cycle!");
    }
  };
  AckermannDelegate delegate;
  core::BuildEngine engine(delegate);

  // Enable tracing, if requested.
  if (!traceFilename.empty()) {
    std::string error;
    if (!engine.enableTracing(traceFilename, &error)) {
      fprintf(stderr, "error: %s: unable to enable tracing: %s\n",
              getprogname(), error.c_str());
      return 1;
    }
  }

  char key[32];
  sprintf(key, "ack(%d,%d)", m, n);
  int result = intFromValue(engine.build(key));
  std::cout << "ack(" << m << ", " << n << ") = " << result << "\n";
  if (n < 10) {
#ifndef NDEBUG
    int expected = ack(m, n);
    assert(result == expected);
#endif
  }
  std::cout << "... computed using " << delegate.numRules << " rules\n";

  if (!dumpGraphPath.empty()) {
    engine.dumpGraphToFile(dumpGraphPath);
  }

  // Recompute the result as many times as requested.
  for (int i = 0; i != recomputeCount; ++i) {
    int recomputedResult = intFromValue(engine.build(key));
    if (recomputedResult != result)
      abort();
  }

  return 0;
}

static void ackermannUsage() {
  int optionWidth = 20;
  fprintf(stderr, "Usage: %s buildengine ack [options] <M> <N>\n",
          ::getprogname());
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
        fprintf(stderr, "\error: %s: missing argument to '%s'\n\n",
                ::getprogname(), option.c_str());
        ackermannUsage();
      }
      char *end;
      recomputeCount = ::strtol(args[0].c_str(), &end, 10);
      if (*end != '\0') {
        fprintf(stderr, "\error: %s: invalid argument to '%s'\n\n",
                ::getprogname(), option.c_str());
        ackermannUsage();
      }
      args.erase(args.begin());
    } else if (option == "--dump-graph") {
      if (args.empty()) {
        fprintf(stderr, "\error: %s: missing argument to '%s'\n\n",
                ::getprogname(), option.c_str());
        ackermannUsage();
      }
      dumpGraphPath = args[0];
      args.erase(args.begin());
    } else if (option == "--trace") {
      if (args.empty()) {
        fprintf(stderr, "\error: %s: missing argument to '%s'\n\n",
                ::getprogname(), option.c_str());
        ackermannUsage();
      }
      traceFilename = args[0];
      args.erase(args.begin());
    } else {
      fprintf(stderr, "\error: %s: invalid option: '%s'\n\n",
              ::getprogname(), option.c_str());
      ackermannUsage();
    }
  }

  if (args.size() != 2) {
    fprintf(stderr, "error: %s: invalid number of arguments\n", getprogname());
    ackermannUsage();
  }

  const char *str = args[0].c_str();
  int m = ::strtol(str, (char**)&str, 10);
  if (*str != '\0') {
    fprintf(stderr, "error: %s: invalid argument '%s' (expected integer)\n",
            getprogname(), args[0].c_str());
    return 1;
  }
  str = args[1].c_str();
  int n = ::strtol(str, (char**)&str, 10);
  if (*str != '\0') {
    fprintf(stderr, "error: %s: invalid argument '%s' (expected integer)\n",
            getprogname(), args[1].c_str());
    return 1;
  }

  if (m >= 4) {
    fprintf(stderr, "error: %s: invalid argument M = '%d' (too large)\n",
            getprogname(), m);
    return 1;
  }

  if (n >= 1024) {
    fprintf(stderr, "error: %s: invalid argument N = '%d' (too large)\n",
            getprogname(), n);
    return 1;
  }

  return runAckermannBuild(m, n, recomputeCount, traceFilename, dumpGraphPath);
}

}

#pragma mark - Build Engine Top-Level Command

static void usage() {
  fprintf(stderr, "Usage: %s buildengine [--help] <command> [<args>]\n",
          getprogname());
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
    fprintf(stderr, "error: %s: unknown command '%s'\n", getprogname(),
            args[0].c_str());
    return 1;
  }
}
