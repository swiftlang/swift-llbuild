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
  std::vector<std::vector<int>> MemoTable(m+1);

  std::function<int(int,int)> ack_internal;
  ack_internal = [&] (int m, int n) {
    assert(m >= 0 && n >= 0);
    auto& MemoRow = MemoTable[m];
    if (size_t(n) >= MemoRow.size())
        MemoRow.resize(n + 1);
    if (MemoRow[n] != 0)
      return MemoRow[n];

    int Result;
    if (m == 0) {
      Result = n + 1;
    } else if (n == 0) {
      Result = ack_internal(m - 1, 1);
    } else {
      Result = ack_internal(m - 1, ack_internal(m, n - 1));
    };

    MemoRow[n] = Result;
    return Result;
  };

  return ack_internal(m, n);
}
#endif

static core::Task* BuildAck(core::BuildEngine& engine, int M, int N) {
  struct AckermannTask : core::Task {
    int M, N;
    int RecursiveResultA = 0;
    int RecursiveResultB = 0;

    AckermannTask(int M, int N) : Task("ack()"), M(M), N(N) { }

    virtual void provideValue(core::BuildEngine& engine, uintptr_t InputID,
                              core::ValueType Value) override {
      if (InputID == 0) {
        RecursiveResultA = Value;

        // Request the second recursive result, if needed.
        if (M != 0 && N != 0) {
          char InputKey[32];
          sprintf(InputKey, "ack(%d,%d)", M - 1, RecursiveResultA);
          engine.taskNeedsInput(this, InputKey, 1);
        }
      } else {
        assert(InputID == 1 && "invalid input ID");
        RecursiveResultB = Value;
      }
    }

    virtual void start(core::BuildEngine& engine) override {
      // Request the first recursive result, if necessary.
      if (M == 0) {
        ;
      } else if (N == 0) {
        char InputKey[32];
        sprintf(InputKey, "ack(%d,%d)", M-1, 1);
        engine.taskNeedsInput(this, InputKey, 0);
      } else {
        char InputKey[32];
        sprintf(InputKey, "ack(%d,%d)", M, N-1);
        engine.taskNeedsInput(this, InputKey, 0);
      }
    }

    virtual void inputsAvailable(core::BuildEngine& engine) override {
      if (M == 0) {
        engine.taskIsComplete(this, N + 1);
        return;
      }

      assert(RecursiveResultA != 0);
      if (N == 0) {
        engine.taskIsComplete(this, RecursiveResultA);
        return;
      }

      assert(RecursiveResultB != 0);
      engine.taskIsComplete(this, RecursiveResultB);
    }
  };

  // Create the task.
  return engine.registerTask(new AckermannTask(M, N));
}

static int RunAckermannBuild(int M, int N, int RecomputeCount,
                             const std::string& TraceFilename) {
  // Compute the value of ackermann(M, N) using the build system.
  assert(M >= 0 && M < 4);
  assert(N >= 0);

  // First, create rules for each of the necessary results.
  core::BuildEngine engine;

  // Enable tracing, if requested.
  if (!TraceFilename.empty()) {
    std::string Error;
    if (!engine.enableTracing(TraceFilename, &Error)) {
      fprintf(stderr, "error: %s: unable to enable tracing: %s\n",
              getprogname(), Error.c_str());
      return 1;
    }
  }

  int NumRules = 0;
  for (int i = 0; i <= M; ++i) {
    int UpperBound;
    if (i == 0 || i == 1) {
      UpperBound = int(pow(2, N+3) - 3) + 1;
    } else if (i == 2) {
      UpperBound = int(pow(2, N+3 - 1) - 3) + 1;
    } else {
      assert(i == M);
      UpperBound = N + 1;
    }
    for (int j = 0; j <= UpperBound; ++j) {
      char Name[32];
      sprintf(Name, "ack(%d,%d)", i, j);
      engine.addRule({ Name, [i, j] (core::BuildEngine& engine) {
            return BuildAck(engine, i, j); } });
      ++NumRules;
    }
  }

  char Key[32];
  sprintf(Key, "ack(%d,%d)", M, N);
  core::ValueType Result = engine.build(Key);
  std::cout << "ack(" << M << ", " << N << ") = " << Result << "\n";
  if (N < 10) {
#ifndef NDEBUG
    core::ValueType Expected = ack(M, N);
    assert(Result == Expected);
#endif
  }
  std::cout << "... computed using " << NumRules << " rules\n";

  // Recompute the result as many times as requested.
  for (int i = 0; i != RecomputeCount; ++i) {
    core::ValueType RecomputedResult = engine.build(Key);
    if (RecomputedResult != Result)
      abort();
  }

  return 0;
}

static void AckermannUsage() {
  int OptionWidth = 20;
  fprintf(stderr, "Usage: %s buildengine ack [options] <M> <N>\n",
          ::getprogname());
  fprintf(stderr, "\nOptions:\n");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--help",
          "show this help message and exit");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--recompute <N>",
          "recompute the result N times, to stress dependency checking");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--trace <PATH>",
          "trace build engine operation to PATH");
  ::exit(1);
}

static int ExecuteAckermannCommand(std::vector<std::string> Args) {
  int RecomputeCount = 0;
  std::string TraceFilename;
  while (!Args.empty() && Args[0][0] == '-') {
    const std::string Option = Args[0];
    Args.erase(Args.begin());

    if (Option == "--")
      break;

    if (Option == "--recompute") {
      if (Args.empty()) {
        fprintf(stderr, "\error: %s: missing argument to '%s'\n\n",
                ::getprogname(), Option.c_str());
        AckermannUsage();
      }
      char *End;
      RecomputeCount = ::strtol(Args[0].c_str(), &End, 10);
      if (*End != '\0') {
        fprintf(stderr, "\error: %s: invalid argument to '%s'\n\n",
                ::getprogname(), Option.c_str());
        AckermannUsage();
      }
      Args.erase(Args.begin());
    } else if (Option == "--trace") {
      if (Args.empty()) {
        fprintf(stderr, "\error: %s: missing argument to '%s'\n\n",
                ::getprogname(), Option.c_str());
        AckermannUsage();
      }
      TraceFilename = Args[0];
      Args.erase(Args.begin());
    } else {
      fprintf(stderr, "\error: %s: invalid option: '%s'\n\n",
              ::getprogname(), Option.c_str());
      AckermannUsage();
    }
  }

  if (Args.size() != 2) {
    fprintf(stderr, "error: %s: invalid number of arguments\n", getprogname());
    AckermannUsage();
  }

  const char *Str = Args[0].c_str();
  int M = ::strtol(Str, (char**)&Str, 10);
  if (*Str != '\0') {
    fprintf(stderr, "error: %s: invalid argument '%s' (expected integer)\n",
            getprogname(), Args[0].c_str());
    return 1;
  }
  Str = Args[1].c_str();
  int N = ::strtol(Str, (char**)&Str, 10);
  if (*Str != '\0') {
    fprintf(stderr, "error: %s: invalid argument '%s' (expected integer)\n",
            getprogname(), Args[1].c_str());
    return 1;
  }

  if (M >= 4) {
    fprintf(stderr, "error: %s: invalid argument M = '%d' (too large)\n",
            getprogname(), M);
    return 1;
  }

  if (N >= 1024) {
    fprintf(stderr, "error: %s: invalid argument N = '%d' (too large)\n",
            getprogname(), N);
    return 1;
  }

  return RunAckermannBuild(M, N, RecomputeCount, TraceFilename);
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

int commands::ExecuteBuildEngineCommand(const std::vector<std::string> &Args) {
  // Expect the first argument to be the name of another subtool to delegate to.
  if (Args.empty() || Args[0] == "--help")
    usage();

  if (Args[0] == "ack") {
    return ExecuteAckermannCommand({Args.begin()+1, Args.end()});
  } else {
    fprintf(stderr, "error: %s: unknown command '%s'\n", getprogname(),
            Args[0].c_str());
    return 1;
  }
}
