//===-- llbuild.cpp - llbuild Frontend Utillity ---------------------------===//
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

#include "llbuild/Basic/Version.h"

#include "llbuild/Commands/Commands.h"

#include "llvm/Support/Path.h"
#include "llvm/Support/Signals.h"

#include <cstdio>
#include <cstdlib>

using namespace llbuild;
using namespace llbuild::commands;

static void usage() {
  fprintf(stderr, "Usage: %s [--version] [--help] <command> [<args>]\n",
          getProgramName());
  fprintf(stderr, "\n");
  fprintf(stderr, "Available commands:\n");
  fprintf(stderr, "  ninja       -- Run the Ninja subtool\n");
  fprintf(stderr, "  buildengine -- Run the build engine subtool\n");
  fprintf(stderr, "  buildsystem -- Run the build system subtool\n");
  fprintf(stderr, "\n");
  exit(0);
}

int main(int argc, const char **argv) {
  // Print stacks on error.
  llvm::sys::PrintStackTraceOnErrorSignal();
  
  // Support use of llbuild as a replacement for ninja by indirecting to the
  // `ninja build` subtool when invoked under the name `ninja.
  if (llvm::sys::path::filename(argv[0]) == "ninja") {
    // We still want to represent ourselves as llbuild in output messages.
    setProgramName("llbuild");

    std::vector<std::string> args;
    args.push_back("build");
    for (int i = 1; i != argc; ++i) {
      args.push_back(argv[i]);
    }
    return executeNinjaCommand(args);
  } else {
    setProgramName(llvm::sys::path::filename(argv[0]));
  }

  // Expect the first argument to be the name of a subtool to delegate to.
  if (argc == 1 || std::string(argv[1]) == "--help")
    usage();

  if (std::string(argv[1]) == "--version") {
    // Print the version and exit.
    printf("%s\n", getLLBuildFullVersion().c_str());
    return 0;
  }

  // Otherwise, expect a command name.
  std::string command(argv[1]);
  std::vector<std::string> args;
  for (int i = 2; i != argc; ++i) {
    args.push_back(argv[i]);
  }

  if (command == "ninja") {
    return executeNinjaCommand(args);
  } else if (command == "buildengine") {
    return executeBuildEngineCommand(args);
  } else if (command == "buildsystem") {
    return executeBuildSystemCommand(args);
  } else {
    fprintf(stderr, "error: %s: unknown command '%s'\n", getProgramName(),
            command.c_str());
    return 1;
  }
}
