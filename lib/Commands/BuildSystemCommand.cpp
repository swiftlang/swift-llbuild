//===-- BuildSystemCommand.cpp --------------------------------------------===//
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

#include "llbuild/Basic/FileSystem.h"
#include "llbuild/Basic/LLVM.h"
#include "llbuild/Basic/PlatformUtility.h"
#include "llbuild/BuildSystem/BuildDescription.h"
#include "llbuild/BuildSystem/BuildFile.h"
#include "llbuild/BuildSystem/BuildSystem.h"
#include "llbuild/BuildSystem/BuildSystemFrontend.h"
#include "llbuild/BuildSystem/BuildValue.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include "CommandUtil.h"

#include <cerrno>
#include <thread>

#include <signal.h>
#include <unistd.h>

using namespace llbuild;
using namespace llbuild::commands;
using namespace llbuild::core;
using namespace llbuild::buildsystem;

namespace {

/*  Parse Command */

class ParseBuildFileDelegate : public BuildFileDelegate {
  bool showOutput;
  StringRef bufferBeingParsed;
  std::unique_ptr<basic::FileSystem> fileSystem;
  llvm::StringMap<bool> internedStrings;
  
public:
  ParseBuildFileDelegate(bool showOutput)
      : showOutput(showOutput),
        fileSystem(basic::createLocalFileSystem()) {}
  ~ParseBuildFileDelegate() {}

  virtual bool shouldShowOutput() { return showOutput; }

  virtual StringRef getInternedString(StringRef value) override {
    auto entry = internedStrings.insert(std::make_pair(value, true));
    return entry.first->getKey();
  }
  
  virtual basic::FileSystem& getFileSystem() override { return *fileSystem; }
  
  virtual void setFileContentsBeingParsed(StringRef buffer) override;
  
  virtual void error(StringRef filename,
                     const BuildFileToken& at,
                     const Twine& message) override;

  virtual bool configureClient(const ConfigureContext&, StringRef name,
                               uint32_t version,
                               const property_list_type& properties) override;

  virtual std::unique_ptr<Tool> lookupTool(StringRef name) override;

  virtual void loadedTarget(StringRef name,
                            const Target& target) override;

  virtual void loadedDefaultTarget(StringRef target) override;
  
  virtual std::unique_ptr<Node> lookupNode(StringRef name,
                                           bool isImplicit) override;

  virtual void loadedCommand(StringRef name,
                             const Command& command) override;
};

class ParseDummyNode : public Node {
  ParseBuildFileDelegate& delegate;
  
public:
  ParseDummyNode(ParseBuildFileDelegate& delegate, StringRef name)
      : Node(name), delegate(delegate) {}
  
  virtual bool configureAttribute(const ConfigureContext&, StringRef name,
                                  StringRef value) override {
    if (delegate.shouldShowOutput()) {
      printf("  -- '%s': '%s'\n", name.str().c_str(), value.str().c_str());
    }
    return true;
  }
  virtual bool configureAttribute(const ConfigureContext&, StringRef name,
                                  ArrayRef<StringRef> values) override {
    if (delegate.shouldShowOutput()) {
      printf("  -- '%s': [", name.str().c_str());
      bool first = true;
      for (const auto& value: values) {
        printf("%s'%s'", first ? "" : ", ", value.str().c_str());
        first = false;
      }
      printf("]\n");
    }
    return true;
  }
  virtual bool configureAttribute(
      const ConfigureContext&, StringRef name,
      ArrayRef<std::pair<StringRef, StringRef>> values) override {
    if (delegate.shouldShowOutput()) {
      printf("  -- '%s': {\n", name.str().c_str());
      for (const auto& value: values) {
        printf("  --   '%s': '%s'\n", value.first.str().c_str(),
               value.second.str().c_str());
      }
      printf("  -- }\n");
    }
    return true;
  }
};

class ParseDummyCommand : public Command {
  ParseBuildFileDelegate& delegate;
  
public:
  ParseDummyCommand(ParseBuildFileDelegate& delegate, StringRef name)
      : Command(name), delegate(delegate) {}

  virtual void getShortDescription(SmallVectorImpl<char> &result) override {
    llvm::raw_svector_ostream(result) << "<dummy command>";
  }

  virtual void getVerboseDescription(SmallVectorImpl<char> &result) override {
    llvm::raw_svector_ostream(result) << "<dummy command>";
  }

  virtual void configureDescription(const ConfigureContext&,
                                    StringRef description) override {
    if (delegate.shouldShowOutput()) {
      printf("  -- 'description': '%s'", description.str().c_str());
    }
  }
  
  virtual void configureInputs(const ConfigureContext&,
                               const std::vector<Node*>& inputs) override {
    if (delegate.shouldShowOutput()) {
      bool first = true;
      printf("  -- 'inputs': [");
      for (const auto& node: inputs) {
        printf("%s'%s'", first ? "" : ", ", node->getName().str().c_str());
        first = false;
      }
      printf("]\n");
    }
  }

  virtual void configureOutputs(const ConfigureContext&,
                                const std::vector<Node*>& outputs) override {
    if (delegate.shouldShowOutput()) {
      bool first = true;
      printf("  -- 'outputs': [");
      for (const auto& node: outputs) {
        printf("%s'%s'", first ? "" : ", ", node->getName().str().c_str());
        first = false;
      }
      printf("]\n");
    }
  }

  virtual bool configureAttribute(const ConfigureContext&, StringRef name,
                                  StringRef value) override {
    if (delegate.shouldShowOutput()) {
      printf("  -- '%s': '%s'\n", name.str().c_str(), value.str().c_str());
    }
    return true;
  }
  virtual bool configureAttribute(const ConfigureContext&, StringRef name,
                                  ArrayRef<StringRef> values) override {
    if (delegate.shouldShowOutput()) {
      printf("  -- '%s': [", name.str().c_str());
      bool first = true;
      for (const auto& value: values) {
        printf("%s'%s'", first ? "" : ", ", value.str().c_str());
        first = false;
      }
      printf("]\n");
    }
    return true;
  }
  virtual bool configureAttribute(
      const ConfigureContext&, StringRef name,
      ArrayRef<std::pair<StringRef, StringRef>> values) override {
    if (delegate.shouldShowOutput()) {
      printf("  -- '%s': {\n", name.str().c_str());
      for (const auto& value: values) {
        printf("  --   '%s': '%s'\n", value.first.str().c_str(),
               value.second.str().c_str());
      }
      printf("  -- }\n");
    }
    return true;
  }

  virtual BuildValue getResultForOutput(Node* node,
                                        const BuildValue& value) override {
    return BuildValue::makeMissingInput();
  }
  virtual bool isResultValid(BuildSystem&, const BuildValue&) override {
    return false;
  }
  virtual void start(BuildSystemCommandInterface&, Task*) override {}
  virtual void providePriorValue(BuildSystemCommandInterface&, Task*,
                                 const BuildValue&) override {}
  virtual void provideValue(BuildSystemCommandInterface&, Task*,
                                 uintptr_t inputID,
                                 const BuildValue&) override {}
  virtual BuildValue execute(BuildSystemCommandInterface&, Task*,
                             QueueJobContext*) override {
    return BuildValue::makeFailedCommand();
  }
};

class ParseDummyTool : public Tool {
  ParseBuildFileDelegate& delegate;
  
public:
  ParseDummyTool(ParseBuildFileDelegate& delegate, StringRef name)
      : Tool(name), delegate(delegate) {}
  
  virtual bool configureAttribute(const ConfigureContext&, StringRef name,
                                  StringRef value) override {
    if (delegate.shouldShowOutput()) {
      printf("  -- '%s': '%s'\n", name.str().c_str(), value.str().c_str());
    }
    return true;
  }
  virtual bool configureAttribute(const ConfigureContext&, StringRef name,
                                  ArrayRef<StringRef> values) override {
    if (delegate.shouldShowOutput()) {
      printf("  -- '%s': [", name.str().c_str());
      bool first = true;
      for (const auto& value: values) {
        printf("%s'%s'", first ? "" : ", ", value.str().c_str());
        first = false;
      }
      printf("]\n");
    }
    return true;
  }
  virtual bool configureAttribute(
      const ConfigureContext&, StringRef name,
      ArrayRef<std::pair<StringRef, StringRef>> values) override {
    if (delegate.shouldShowOutput()) {
      printf("  -- '%s': {\n", name.str().c_str());
      for (const auto& value: values) {
        printf("  --   '%s': '%s'", value.first.str().c_str(),
               value.second.str().c_str());
      }
      printf("  -- }\n");
    }
    return true;
  }

  virtual std::unique_ptr<Command> createCommand(
      StringRef name) override {
    if (delegate.shouldShowOutput()) {
      printf("command('%s')\n", name.str().c_str());
      printf("  -- 'tool': '%s')\n", getName().str().c_str());
    }

    return llvm::make_unique<ParseDummyCommand>(delegate, name);
  }
};

void ParseBuildFileDelegate::setFileContentsBeingParsed(StringRef buffer) {
  bufferBeingParsed = buffer;
}

void ParseBuildFileDelegate::error(StringRef filename,
                                   const BuildFileToken& at,
                                   const Twine& message) {
  if (at.start) {
    util::emitError(filename, message.str(), at.start, at.length,
                    bufferBeingParsed);
  } else {
    fprintf(stderr, "%s: error: %s\n", filename.str().c_str(),
            message.str().c_str());
  }
}

bool
ParseBuildFileDelegate::configureClient(const ConfigureContext&,
                                        StringRef name,
                                        uint32_t version,
                                        const property_list_type& properties) {
  if (showOutput) {
    // Dump the client information.
    printf("client ('%s', version: %u)\n", name.str().c_str(), version);
    for (const auto& property: properties) {
      printf("  -- '%s': '%s'\n", property.first.c_str(),
             property.second.c_str());
    }
  }

  return true;
}

std::unique_ptr<Tool>
ParseBuildFileDelegate::lookupTool(StringRef name) {
  if (showOutput) {
    printf("tool('%s')\n", name.str().c_str());
  }

  return llvm::make_unique<ParseDummyTool>(*this, name);
}

void ParseBuildFileDelegate::loadedTarget(StringRef name,
                                          const Target& target) {
  if (showOutput) {
    printf("target('%s')\n", target.getName().str().c_str());

    // Print the nodes in the target.
    bool first = true;
    printf(" -- nodes: [");
    for (const auto& node: target.getNodes()) {
      printf("%s'%s'", first ? "" : ", ", node->getName().str().c_str());
      first = false;
    }
    printf("]\n");
  }
}
  
void ParseBuildFileDelegate::loadedDefaultTarget(StringRef target) {
  if (showOutput) {
    printf("default_target('%s')\n", target.str().c_str());
  }
}

std::unique_ptr<Node>
ParseBuildFileDelegate::lookupNode(StringRef name,
                                   bool isImplicit) {
  if (!isImplicit) {
    if (showOutput) {
      printf("node('%s')\n", name.str().c_str());
    }
  }

  return llvm::make_unique<ParseDummyNode>(*this, name);
}

void ParseBuildFileDelegate::loadedCommand(StringRef name,
                                        const Command& command) {
  if (showOutput) {
    printf("  -- -- loaded command('%s')\n", command.getName().str().c_str());
  }
}

static void parseUsage(int exitCode) {
  int optionWidth = 20;
  fprintf(stderr, "Usage: %s buildsystem parse [options] <path>\n",
          getProgramName());
  fprintf(stderr, "\nOptions:\n");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--help",
          "show this help message and exit");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--no-output",
          "don't display parser output");
  ::exit(exitCode);
}

static int executeParseCommand(std::vector<std::string> args) {
  bool showOutput = true;
  
  while (!args.empty() && args[0][0] == '-') {
    const std::string option = args[0];
    args.erase(args.begin());

    if (option == "--")
      break;

    if (option == "--help") {
      parseUsage(0);
    } else if (option == "--no-output") {
      showOutput = false;
    } else {
      fprintf(stderr, "error: %s: invalid option: '%s'\n\n",
              getProgramName(), option.c_str());
      parseUsage(1);
    }
  }

  if (args.size() != 1) {
    fprintf(stderr, "error: %s: invalid number of arguments\n",
            getProgramName());
    parseUsage(1);
  }

  std::string filename = args[0].c_str();

  // Load the BuildFile.
  fprintf(stderr, "note: parsing '%s'\n", filename.c_str());
  ParseBuildFileDelegate delegate(showOutput);
  BuildFile buildFile(filename, delegate);
  buildFile.load();

  return 0;
}


/* Build Command */

class BasicBuildSystemFrontendDelegate : public BuildSystemFrontendDelegate {
  std::unique_ptr<basic::FileSystem> fileSystem;

  /// The previous SIGINT handler.
  struct sigaction previousSigintHandler;

  /// Low-level flag for when a SIGINT has been received.
  static std::atomic<bool> wasInterrupted;

  /// Pipe used to allow detection of signals.
  static int signalWatchingPipe[2];

  static void sigintHandler(int) {
    // Set the atomic interrupt flag.
    BasicBuildSystemFrontendDelegate::wasInterrupted = true;

    // Write to wake up the signal monitoring thread.
    char byte{};
    basic::sys::write(signalWatchingPipe[1], &byte, 1);
  }

  /// Check if an interrupt has occurred.
  void checkForInterrupt() {
    // Save and clear the interrupt flag, atomically.
    bool wasInterrupted = BasicBuildSystemFrontendDelegate::wasInterrupted.exchange(false);

    // Process the interrupt flag, if present.
    if (wasInterrupted) {
      // Otherwise, cancel the build.
      printf("cancelling build.\n");
      cancel();
    }
  }

  /// Thread function to wait for indications that signals have arrived and to
  /// process them.
  void signalWaitThread() {
    // Wait for signal arrival indications.
    while (true) {
      char byte;
      int res = basic::sys::read(signalWatchingPipe[0], &byte, 1);

      // If nothing was read, the pipe has been closed and we should shut down.
      if (res == 0)
        break;

      // Otherwise, check if we were awoke because of an interrupt.
      checkForInterrupt();
    }

    // Shut down the pipe.
    basic::sys::close(signalWatchingPipe[0]);
    signalWatchingPipe[0] = -1;
  }
  
public:
  BasicBuildSystemFrontendDelegate(llvm::SourceMgr& sourceMgr,
                                   const BuildSystemInvocation& invocation)
      : BuildSystemFrontendDelegate(sourceMgr, invocation,
                                    "basic", /*version=*/0),
        fileSystem(basic::createLocalFileSystem()) {
    // Register an interrupt handler.
    struct sigaction action{};
    action.sa_handler = &BasicBuildSystemFrontendDelegate::sigintHandler;
    sigaction(SIGINT, &action, &previousSigintHandler);

    // Create a pipe and thread to watch for signals.
    assert(BasicBuildSystemFrontendDelegate::signalWatchingPipe[0] == -1 &&
           BasicBuildSystemFrontendDelegate::signalWatchingPipe[1] == -1);
    if (basic::sys::pipe(BasicBuildSystemFrontendDelegate::signalWatchingPipe) < 0) {
      perror("pipe");
    }
    new std::thread(&BasicBuildSystemFrontendDelegate::signalWaitThread, this);
  }

  ~BasicBuildSystemFrontendDelegate() {
    // Restore any previous SIGINT handler.
    sigaction(SIGINT, &previousSigintHandler, NULL);

    // Close the signal watching pipe.
    basic::sys::close(BasicBuildSystemFrontendDelegate::signalWatchingPipe[1]);
    signalWatchingPipe[1] = -1;
  }

  virtual basic::FileSystem& getFileSystem() override { return *fileSystem; }

  virtual void hadCommandFailure() override {
    // Call the base implementation.
    BuildSystemFrontendDelegate::hadCommandFailure();

    // Cancel the build, by default.
    cancel();
  }

  virtual std::unique_ptr<Tool> lookupTool(StringRef name) override {
    // We do not support any non-built-in tools.
    return nullptr;
  }

  virtual void cycleDetected(const std::vector<Rule*>& cycle) override {
    auto message = BuildSystemInvocation::formatDetectedCycle(cycle);
    error(message);
  }
};

std::atomic<bool> BasicBuildSystemFrontendDelegate::wasInterrupted{false};
int BasicBuildSystemFrontendDelegate::signalWatchingPipe[2]{-1, -1};

static void buildUsage(int exitCode) {
  int optionWidth = 25;
  fprintf(stderr, "Usage: %s buildsystem build [options] [<target>]\n",
          getProgramName());
  fprintf(stderr, "\nOptions:\n");
  BuildSystemInvocation::getUsage(optionWidth, llvm::errs());
  ::exit(exitCode);
}

static int executeBuildCommand(std::vector<std::string> args) {
  // The source manager to use for diagnostics.
  llvm::SourceMgr sourceMgr;

  // Create the invocation.
  BuildSystemInvocation invocation{};

  // Initialize defaults.
  invocation.dbPath = "build.db";
  invocation.buildFilePath = "build.llbuild";
  invocation.parse(args, sourceMgr);

  // Handle invocation actions.
  if (invocation.showUsage) {
    buildUsage(0);
  } else if (invocation.hadErrors) {
    buildUsage(1);
  }
  
  if (invocation.positionalArgs.size() > 1) {
    fprintf(stderr, "error: %s: invalid number of arguments\n",
            getProgramName());
    buildUsage(1);
  }

  // Select the target to build.
  std::string targetToBuild =
    invocation.positionalArgs.empty() ? "" : invocation.positionalArgs[0];

  // Create the frontend object.
  BasicBuildSystemFrontendDelegate delegate(sourceMgr, invocation);
  BuildSystemFrontend frontend(delegate, invocation);
  if (!frontend.build(targetToBuild)) {
    // If there were failed commands, report the count and return an error.
    if (delegate.getNumFailedCommands()) {
      delegate.error("build had " + Twine(delegate.getNumFailedCommands()) +
                     " command failures");
    }

    return 1;
  }

  return 0;
}

}

#pragma mark - Build System Top-Level Command

static void usage(int exitCode) {
  fprintf(stderr, "Usage: %s buildsystem [--help] <command> [<args>]\n",
          getProgramName());
  fprintf(stderr, "\n");
  fprintf(stderr, "Available commands:\n");
  fprintf(stderr, "  parse         -- Parse a build file\n");
  fprintf(stderr, "  build         -- Build using a build file\n");
  fprintf(stderr, "\n");
  exit(exitCode);
}

int commands::executeBuildSystemCommand(const std::vector<std::string> &args) {
  // Expect the first argument to be the name of another subtool to delegate to.
  if (args.empty() || args[0] == "--help")
    usage(0);

  if (args[0] == "parse") {
    return executeParseCommand({args.begin()+1, args.end()});
  } else if (args[0] == "build") {
    return executeBuildCommand({args.begin()+1, args.end()});
  } else {
    fprintf(stderr, "error: %s: unknown command '%s'\n", getProgramName(),
            args[0].c_str());
    usage(1);
    return 1;
  }
}
