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

#include "llbuild/BuildSystem/BuildSystemFrontend.h"

#include "llbuild/Basic/FileSystem.h"
#include "llbuild/Basic/Version.h"
#include "llbuild/BuildSystem/BuildDescription.h"
#include "llbuild/BuildSystem/BuildFile.h"
#include "llbuild/BuildSystem/SwiftTools.h"

#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"

using namespace llbuild;
using namespace llbuild::buildsystem;

class BasicBuildSystemFrontendDelegate : public BuildSystemFrontendDelegate {
  std::unique_ptr<basic::FileSystem> fileSystem;
    
public:
  BasicBuildSystemFrontendDelegate(llvm::SourceMgr& sourceMgr,
                                   const BuildSystemInvocation& invocation)
    : BuildSystemFrontendDelegate(sourceMgr, invocation,
                                  "swift-build", /*version=*/0),
      fileSystem(basic::createLocalFileSystem()) {}

  virtual basic::FileSystem& getFileSystem() override { return *fileSystem; }

  virtual void hadCommandFailure() override {
    // Call the base implementation.
    BuildSystemFrontendDelegate::hadCommandFailure();

    // Cancel the build, by default.
    cancel();
  }
  
  virtual std::unique_ptr<Tool> lookupTool(StringRef name) override {
    if (name == "swift-compiler") {
      return createSwiftCompilerTool(name);
    }

    return nullptr;
  }

  virtual void cycleDetected(const std::vector<core::Rule*>& items) override {
    auto message = BuildSystemInvocation::formatDetectedCycle(items);
    error(message);
  }
};

static void usage(int exitCode) {
  int optionWidth = 25;
  fprintf(stderr, "Usage: swift-build-tool [options] [<target>]\n");
  fprintf(stderr, "\nOptions:\n");
  BuildSystemInvocation::getUsage(optionWidth, llvm::errs());
  ::exit(exitCode);
}

static int execute(ArrayRef<std::string> args) {
  // The source manager to use for diagnostics.
  llvm::SourceMgr sourceMgr;

  // Create the invocation.
  BuildSystemInvocation invocation{};

  // Initialize defaults.
  invocation.dbPath = "build.db";
  invocation.buildFilePath = "build.swift-build";
  invocation.parse(args, sourceMgr);

  // Handle invocation actions.
  if (invocation.showUsage) {
    usage(0);
  } else if (invocation.showVersion) {
    // Print the version and exit.
    printf("%s\n", getLLBuildFullVersion("swift-build-tool").c_str());
    return 0;
  } else if (invocation.hadErrors) {
    usage(1);
  }
  
  if (invocation.positionalArgs.size() > 1) {
    fprintf(stderr, "swift-build-tool: error: invalid number of arguments\n");
    usage(1);
  }

  // Select the target to build.
  std::string targetToBuild =
    invocation.positionalArgs.empty() ? "" : invocation.positionalArgs[0];

  // Create the frontend object.
  BasicBuildSystemFrontendDelegate delegate(sourceMgr, invocation);
  BuildSystemFrontend frontend(delegate, invocation);
  if (!frontend.build(targetToBuild)) {
    return 1;
  }

  return 0;
}

int main(int argc, const char **argv) {
  // Print stacks on error.
  llvm::sys::PrintStackTraceOnErrorSignal();
  
  std::vector<std::string> args;
  for (int i = 1; i != argc; ++i) {
    args.push_back(argv[i]);
  }
  return execute(args);
}
