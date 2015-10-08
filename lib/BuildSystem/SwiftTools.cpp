//===-- SwiftTools.cpp ----------------------------------------------------===//
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

#include "llbuild/BuildSystem/SwiftTools.h"

#include "llbuild/Basic/Hashing.h"
#include "llbuild/Basic/LLVM.h"
#include "llbuild/BuildSystem/BuildExecutionQueue.h"
#include "llbuild/BuildSystem/BuildFile.h"
#include "llbuild/BuildSystem/BuildSystemCommandInterface.h"
#include "llbuild/BuildSystem/ExternalCommand.h"

#include "llvm/ADT/Twine.h"

using namespace llbuild;
using namespace llbuild::buildsystem;

namespace {

class SwiftCompilerShellCommand : public ExternalCommand {
  /// The compiler command to invoke.
  std::string executable;
  
  /// The name of the module.
  std::string moduleName;

  /// The list of sources (combined).
  //
  // FIXME: This should be an actual list.
  std::string sourcesList;

  /// Additional arguments, as a string.
  //
  // FIXME: This should be an actual list.
  std::string otherArgs;
  
  virtual uint64_t getSignature() override {
    uint64_t result = ExternalCommand::getSignature();
    result ^= basic::hashString(executable);
    result ^= basic::hashString(moduleName);
    result ^= basic::hashString(sourcesList);
    result ^= basic::hashString(otherArgs);
    return result;
  }

public:
  using ExternalCommand::ExternalCommand;
  
  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  StringRef value) override {
    if (name == "executable") {
      executable = value;
    } else if (name == "module-name") {
      moduleName = value;
    } else if (name == "sources") {
      sourcesList = value;
    } else if (name == "other-args") {
      otherArgs = value;
    } else {
      return ExternalCommand::configureAttribute(ctx, name, value);
    }

    return true;
  }

  virtual bool executeExternalCommand(BuildSystemCommandInterface& bsci,
                                      core::Task* task,
                                      QueueJobContext* context) override {
    // Form the complete command.
    std::string command = executable + " -module-name " + moduleName +
      " -c " + sourcesList + " " + otherArgs;
    
    // Log the command.
    //
    // FIXME: Design the logging and status output APIs.
    fprintf(stdout, "%s\n", command.c_str());
    fflush(stdout);

    // Execute the command.
    if (!bsci.getExecutionQueue().executeShellCommand(context, command)) {
      // If the command failed, there is no need to gather dependencies.
      return false;
    }

    return true;
  }
};

class SwiftCompilerTool : public Tool {
public:
  SwiftCompilerTool(StringRef name) : Tool(name) {}

  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  StringRef value) override {
    // No supported attributes.
    ctx.error("unexpected attribute: '" + name + "'");
    return false;
  }

  virtual std::unique_ptr<Command> createCommand(StringRef name) override {
    return std::make_unique<SwiftCompilerShellCommand>(name);
  }
};
}

std::unique_ptr<Tool> buildsystem::createSwiftCompilerTool(StringRef name) {
  return std::make_unique<SwiftCompilerTool>(name);
}
