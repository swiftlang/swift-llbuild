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

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Errno.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
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
  fprintf(stderr, "  analyze     -- Run the analyze subtool\n");
  fprintf(stderr, "\n");
  exit(0);
}

std::string getExecutablePath(std::string programName) {
  void *P = (void *)(intptr_t)getExecutablePath;
  return llvm::sys::fs::getMainExecutable(programName.c_str(), P);
}

int executeExternalCommand(std::string command, std::vector<std::string> args) {
  // We are running as a subcommand, try to find the subcommand adjacent to
  // the executable we are running as.
  SmallString<256> SubcommandPath(
                                  llvm::sys::path::parent_path(getExecutablePath(command)));
  llvm::sys::path::append(SubcommandPath, command);
  
  // If we didn't find the tool there, let the OS search for it.
  if (!llvm::sys::fs::exists(SubcommandPath)) {
    // Search for the program and use the path if found. If there was an
    // error, ignore it and just let the exec fail.
    auto result = llvm::sys::findProgramByName(command);
    if (!result.getError())
      SubcommandPath = *result;
  }
  
  std::vector<StringRef> argRefs(args.size());
  std::transform(args.begin(), args.end(), argRefs.begin(), [](std::string element) { return StringRef(strdup(element.c_str())); });
  llvm::sys::ExecuteAndWait(StringRef(SubcommandPath), ArrayRef<StringRef>(argRefs));
  
  return 0;
}

int main(int argc, const char **argv) {
  // Print stacks on error.
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);
  
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
  } else if (command == "analyze") {
    // Next to the llbuild binary we build a llbuild-analyze binary which we treat as a subtool to llbuild
    return executeExternalCommand("llbuild-analyze", args);
  } else {
    fprintf(stderr, "error: %s: unknown command '%s'\n", getProgramName(),
            command.c_str());
    return 1;
  }
}
