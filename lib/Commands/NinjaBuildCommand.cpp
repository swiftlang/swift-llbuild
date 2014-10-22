//===-- NinjaBuildCommand.cpp ---------------------------------------------===//
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

#include "NinjaBuildCommand.h"

#include "llbuild/Core/BuildEngine.h"
#include "llbuild/Ninja/ManifestLoader.h"

#include "CommandUtil.h"

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <unordered_set>

#include <spawn.h>
#include <unistd.h>

using namespace llbuild;
using namespace llbuild::commands;

extern "C" {
  char **environ;
}

static void usage() {
  fprintf(stderr, "Usage: %s ninja build [--help] <manifest> [<targets>]\n",
          ::getprogname());
  ::exit(1);
}

namespace {

static bool NoExecute = false;

class BuildManifestActions : public ninja::ManifestLoaderActions {
  ninja::ManifestLoader *Loader = 0;
  unsigned NumErrors = 0;
  unsigned MaxErrors = 20;

private:
  virtual void initialize(ninja::ManifestLoader *Loader) {
    this->Loader = Loader;
  }

  virtual void error(std::string Filename, std::string Message,
                     const ninja::Token &At) override {
    if (NumErrors++ >= MaxErrors)
      return;

    util::EmitError(Filename, Message, At, Loader->getCurrentParser());
  }

  virtual bool readFileContents(const std::string& FromFilename,
                                const std::string& Filename,
                                const ninja::Token* ForToken,
                                std::unique_ptr<char[]> *Data_Out,
                                uint64_t *Length_Out) override {
    // Load the file contents and return if successful.
    std::string Error;
    if (util::ReadFileContents(Filename, Data_Out, Length_Out, &Error))
      return true;

    // Otherwise, emit the error.
    if (ForToken) {
      util::EmitError(FromFilename, Error, *ForToken,
                      Loader->getCurrentParser());
    } else {
      // We were unable to open the main file.
      fprintf(stderr, "error: %s: %s\n", getprogname(), Error.c_str());
      exit(1);
    }

    return false;
  };

public:
  unsigned getNumErrors() const { return NumErrors; }
};

unsigned NumBuiltInputs = 0;
unsigned NumBuiltCommands = 0;

core::Task* BuildCommand(core::BuildEngine& Engine, ninja::Node* Output,
                         ninja::Command* Command, ninja::Manifest* Manifest) {
  struct NinjaCommandTask : core::Task {
    ninja::Node* Output;
    ninja::Command* Command;

    NinjaCommandTask(ninja::Node* Output, ninja::Command* Command)
      : Task("ninja-command"), Output(Output), Command(Command) { }

    virtual void provideValue(core::BuildEngine& engine, uintptr_t InputID,
                              core::ValueType Value) override {
    }

    virtual void start(core::BuildEngine& engine) override {
      // Request all of the input values.
      for (auto& Input: Command->getInputs()) {
        engine.taskNeedsInput(this, Input->getPath(), 0);
      }
    }

    virtual core::ValueType finish() override {
      ++NumBuiltCommands;
      std::cerr << "[" << NumBuiltCommands << "] "
                << Command->getDescription() << "\n";

      if (NoExecute)
          return 0;

      // Spawn the command.
      const char* Args[4];
      Args[0] = "/bin/sh";
      Args[1] = "-c";
      Args[2] = Command->getCommandString().c_str();
      Args[3] = nullptr;
      int PID;
      if (posix_spawn(&PID, Args[0], /*file_actions=*/0, /*attrp=*/0,
                      const_cast<char**>(Args), ::environ) != 0) {
        fprintf(stderr, "error: %s: unable to spawn process (%s)\n",
                getprogname(), strerror(errno));
        exit(1);
      }

      // Wait for the command to complete.
      int Status, Result = waitpid(PID, &Status, 0);
      if (Result == -1) {
        fprintf(stderr, "error: %s: unable to wait for process (%s)\n",
                getprogname(), strerror(errno));
        exit(1);
      }
      if (Status != 0) {
        std::cerr << "  ... process returned error status: " << Status << "\n";
      }

      return 0;
    }
  };

  return Engine.registerTask(new NinjaCommandTask(Output, Command));
}

core::Task* BuildInput(core::BuildEngine& Engine, ninja::Node* Input) {
  struct NinjaInputTask : core::Task {
    ninja::Node* Node;

    NinjaInputTask(ninja::Node* Node) : Task("ninja-input"), Node(Node) { }

    virtual void provideValue(core::BuildEngine& engine, uintptr_t InputID,
                              core::ValueType Value) override { }

    virtual void start(core::BuildEngine& engine) override { }

    virtual core::ValueType finish() override {
      ++NumBuiltInputs;
      return 0;
    }
  };

  return Engine.registerTask(new NinjaInputTask(Input));
}

}

int commands::ExecuteNinjaBuildCommand(std::vector<std::string> Args) {
  if (Args.empty() || Args[0] == "--help")
    usage();

  while (!Args.empty() && Args[0][0] == '-') {
    const std::string Option = Args[0];
    Args.erase(Args.begin());

    if (Option == "--")
      break;

    if (Option == "--no-execute") {
      NoExecute = true;
    } else {
      fprintf(stderr, "\error: %s: invalid option: '%s'\n\n",
              ::getprogname(), Option.c_str());
      usage();
    }
  }

  if (Args.size() < 1) {
    fprintf(stderr, "\error: %s: invalid number of arguments\n\n",
            ::getprogname());
    usage();
  }

  // Parse the arguments.
  std::string Filename = Args[0];
  std::vector<std::string> TargetsToBuild;
  for (unsigned i = 1, ie = Args.size(); i < ie; ++i) {
      TargetsToBuild.push_back(Args[i]);
  }

  // Change to the directory containing the input file, so include references
  // can be relative.
  //
  // FIXME: Need llvm::sys::fs.
  size_t Pos = Filename.find_last_of('/');
  if (Pos != std::string::npos) {
    if (::chdir(std::string(Filename.substr(0, Pos)).c_str()) < 0) {
      fprintf(stderr, "error: %s: unable to chdir(): %s\n",
              getprogname(), strerror(errno));
      return 1;
    }
    Filename = Filename.substr(Pos+1);
  }

  // Load the manifest.
  BuildManifestActions Actions;
  ninja::ManifestLoader Loader(Filename, Actions);
  std::unique_ptr<ninja::Manifest> Manifest = Loader.load();

  // If there were errors loading, we are done.
  if (unsigned NumErrors = Actions.getNumErrors()) {
    fprintf(stderr, "%d errors generated.\n", NumErrors);
    return 1;
  }

  // Otherwise, run the build.

  // Create the build engine.
  core::BuildEngine Engine;

  // Create rules for all of the build commands.
  //
  // FIXME: This is already a place where we could do lazy rule construction,
  // which starts to beg the question of why does the engine need to have a Rule
  // at all?
  std::unordered_set<ninja::Node*> VisitedNodes;
  for (auto& Command: Manifest->getCommands()) {
    for (auto& Output: Command->getOutputs()) {
      Engine.addRule({ Output->getPath(), [&] (core::BuildEngine& Engine) {
            return BuildCommand(Engine, Output, Command.get(), Manifest.get());
          } });
      VisitedNodes.insert(Output);
    }
  }

  // Add dummy rules for all of the nodes that are not outputs (source files).
  for (auto& Entry: Manifest->getNodes()) {
    ninja::Node* Node = Entry.second.get();
    if (!VisitedNodes.count(Node)) {
      Engine.addRule({ Node->getPath(), [Node] (core::BuildEngine& Engine) {
            return BuildInput(Engine, Node);
          } });
    }
  }

  // If no explicit targets were named, build the default targets.
  if (TargetsToBuild.empty()) {
    for (auto& Target: Manifest->getDefaultTargets())
      TargetsToBuild.push_back(Target->getPath());
  }

  // Build the requested targets.
  for (auto& Name: TargetsToBuild) {
    std::cerr << "building target \"" << util::EscapedString(Name) << "\"...\n";
    Engine.build(Name);
  }
  std::cerr << "... built using " << NumBuiltInputs << " inputs\n";
  std::cerr << "... built using " << NumBuiltCommands << " commands\n";

  return 0;
}
