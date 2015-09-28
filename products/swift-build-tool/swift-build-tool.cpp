//===-- swift-build-tool.cpp - Swift Build Tool ---------------------------===//
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

#include "llvm/Support/Signals.h"

using namespace llbuild;
using namespace llbuild::commands;

int main(int argc, const char **argv) {
  // Print stacks on error.
  llvm::sys::PrintStackTraceOnErrorSignal();
  
  setProgramName("swift-build-tool");
  
  // FIXME: For now, just delegate to the existing 'buildsystem build' command
  // with a hard coded task file name.
  
  std::vector<std::string> args;
  args.push_back("build");
  args.push_back("build.swift-build");
  for (int i = 1; i != argc; ++i) {
    args.push_back(argv[i]);
  }
  return executeBuildSystemCommand(args);
}
