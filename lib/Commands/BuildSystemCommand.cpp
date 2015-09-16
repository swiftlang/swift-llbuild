//===-- BuildFileCommand.cpp ----------------------------------------------===//
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

#include "llbuild/BuildSystem/BuildExecutionQueue.h"
#include "llbuild/BuildSystem/BuildFile.h"
#include "llbuild/BuildSystem/BuildSystem.h"
#include "llbuild/BuildSystem/BuildValue.h"

#include "llvm/Support/Path.h"

#include "CommandUtil.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <memory>
#include <thread>

#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

using namespace llbuild;
using namespace llbuild::commands;
using namespace llbuild::core;
using namespace llbuild::buildsystem;

extern "C" {
  extern char **environ;
}

namespace {

/*  Parse Command */

class ParseBuildFileDelegate : public BuildFileDelegate {
  bool showOutput;
  llvm::StringRef bufferBeingParsed;
  
public:
  ParseBuildFileDelegate(bool showOutput) : showOutput(showOutput) {}
  ~ParseBuildFileDelegate() {}

  virtual bool shouldShowOutput() { return showOutput; }
  
  virtual void setFileContentsBeingParsed(llvm::StringRef buffer) override;
  
  virtual void error(const std::string& filename,
                     const Token& at,
                     const std::string& message) override;

  virtual bool configureClient(const std::string& name,
                               uint32_t version,
                               const property_list_type& properties) override;

  virtual std::unique_ptr<Tool> lookupTool(const std::string& name) override;

  virtual void loadedTarget(const std::string& name,
                            const Target& target) override;

  virtual std::unique_ptr<Node> lookupNode(const std::string& name,
                                           bool isImplicit) override;

  virtual void loadedCommand(const std::string& name,
                             const Command& command) override;
};

class ParseDummyNode : public Node {
  ParseBuildFileDelegate& delegate;
  
public:
  ParseDummyNode(ParseBuildFileDelegate& delegate, const std::string& name)
      : Node(name), delegate(delegate) {}
  
  virtual bool configureAttribute(const std::string& name,
                                  const std::string& value) override {
    if (delegate.shouldShowOutput()) {
      printf("  -- '%s': '%s'\n", name.c_str(), value.c_str());
    }
    return true;
  }
};

class ParseDummyCommand : public Command {
  ParseBuildFileDelegate& delegate;
  
public:
  ParseDummyCommand(ParseBuildFileDelegate& delegate, const std::string& name)
      : Command(name), delegate(delegate) {}

  virtual void configureInputs(const std::vector<Node*>& inputs) override {
    if (delegate.shouldShowOutput()) {
      bool first = true;
      printf("  -- 'inputs': [");
      for (const auto& node: inputs) {
        printf("%s'%s'", first ? "" : ", ", node->getName().c_str());
        first = false;
      }
      printf("]\n");
    }
  }

  virtual void configureOutputs(const std::vector<Node*>& outputs) override {
    if (delegate.shouldShowOutput()) {
      bool first = true;
      printf("  -- 'outputs': [");
      for (const auto& node: outputs) {
        printf("%s'%s'", first ? "" : ", ", node->getName().c_str());
        first = false;
      }
      printf("]\n");
    }
  }

  virtual bool configureAttribute(const std::string& name,
                                  const std::string& value) override {
    if (delegate.shouldShowOutput()) {
      printf("  -- '%s': '%s'\n", name.c_str(), value.c_str());
    }
    return true;
  }

  virtual BuildValue getResultForOutput(Node* node,
                                        const BuildValue& value) override {
    return BuildValue::makeMissingInput();
  }
  virtual bool isResultValid(const BuildValue&) override { return false; }
  virtual void start(BuildSystemCommandInterface&, Task*) override {}
  virtual void providePriorValue(BuildSystemCommandInterface&, Task*,
                                 const BuildValue&) override {}
  virtual void provideValue(BuildSystemCommandInterface&, Task*,
                                 uintptr_t inputID,
                                 const BuildValue&) override {}
  virtual void inputsAvailable(BuildSystemCommandInterface&, Task*) override {}
};

class ParseDummyTool : public Tool {
  ParseBuildFileDelegate& delegate;
  
public:
  ParseDummyTool(ParseBuildFileDelegate& delegate, const std::string& name)
      : Tool(name), delegate(delegate) {}
  
  virtual bool configureAttribute(const std::string& name,
                                  const std::string& value) override {
    if (delegate.shouldShowOutput()) {
      printf("  -- '%s': '%s'\n", name.c_str(), value.c_str());
    }
    return true;
  }

  virtual std::unique_ptr<Command> createCommand(
      const std::string& name) override {
    if (delegate.shouldShowOutput()) {
      printf("command('%s')\n", name.c_str());
      printf("  -- 'tool': '%s')\n", getName().c_str());
    }

    return std::make_unique<ParseDummyCommand>(delegate, name);
  }
};

void ParseBuildFileDelegate::setFileContentsBeingParsed(
    llvm::StringRef buffer) {
  bufferBeingParsed = buffer;
}

void ParseBuildFileDelegate::error(const std::string& filename,
                                   const Token& at,
                                   const std::string& message) {
  if (at.start) {
    util::emitError(filename, message, at.start, at.length, bufferBeingParsed);
  } else {
    fprintf(stderr, "%s: error: %s\n", filename.c_str(), message.c_str());
  }
}

bool
ParseBuildFileDelegate::configureClient(const std::string& name,
                                        uint32_t version,
                                        const property_list_type& properties) {
  if (showOutput) {
    // Dump the client information.
    printf("client ('%s', version: %u)\n", name.c_str(), version);
    for (const auto& property: properties) {
      printf("  -- '%s': '%s'\n", property.first.c_str(),
             property.second.c_str());
    }
  }

  return true;
}

std::unique_ptr<Tool>
ParseBuildFileDelegate::lookupTool(const std::string& name) {
  if (showOutput) {
    printf("tool('%s')\n", name.c_str());
  }

  return std::make_unique<ParseDummyTool>(*this, name);
}

void ParseBuildFileDelegate::loadedTarget(const std::string& name,
                                          const Target& target) {
  if (showOutput) {
    printf("target('%s')\n", target.getName().c_str());

    // Print the nodes in the target.
    bool first = true;
    printf(" -- nodes: [");
    for (const auto& nodeName: target.getNodeNames()) {
      printf("%s'%s'", first ? "" : ", ", nodeName.c_str());
      first = false;
    }
    printf("]\n");
  }
}

std::unique_ptr<Node>
ParseBuildFileDelegate::lookupNode(const std::string& name,
                                   bool isImplicit) {
  if (!isImplicit) {
    if (showOutput) {
      printf("node('%s')\n", name.c_str());
    }
  }

  return std::make_unique<ParseDummyNode>(*this, name);
}

void ParseBuildFileDelegate::loadedCommand(const std::string& name,
                                        const Command& command) {
  if (showOutput) {
    printf("  -- -- loaded command('%s')\n", command.getName().c_str());
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
      fprintf(stderr, "\error: %s: invalid option: '%s'\n\n",
              getProgramName(), option.c_str());
      parseUsage(1);
    }
  }

  if (args.size() != 1) {
    fprintf(stderr, "error: %s: invalid number of arguments\n", getProgramName());
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

/// Build execution queue.
//
// FIXME: Consider trying to share this with the Ninja implementation.
class ExecutionQueue : public BuildExecutionQueue {
  /// The number of lanes the queue was configured with.
  unsigned numLanes;

  /// A thread for each lane.
  std::vector<std::unique_ptr<std::thread>> lanes;

  /// The ready queue of jobs to execute.
  std::deque<QueueJob> readyJobs;
  std::mutex readyJobsMutex;
  std::condition_variable readyJobsCondition;
  
  void executeLane(unsigned laneNumber) {
    // Execute items from the queue until shutdown.
    while (true) {
      // Take a job from the ready queue.
      QueueJob job{};
      {
        std::unique_lock<std::mutex> lock(readyJobsMutex);

        // While the queue is empty, wait for an item.
        while (readyJobs.empty()) {
          readyJobsCondition.wait(lock);
        }

        // Take an item according to the chosen policy.
        job = readyJobs.front();
        readyJobs.pop_front();
      }

      // If we got an empty job, the queue is shutting down.
      if (!job.getForCommand())
        break;

      // Process the job.
      QueueJobContext* context = nullptr;
      job.execute(context);
    }
  }

public:
  ExecutionQueue(unsigned numLanes) : numLanes(numLanes) {
    for (unsigned i = 0; i != numLanes; ++i) {
      lanes.push_back(std::unique_ptr<std::thread>(
                          new std::thread(
                              &ExecutionQueue::executeLane, this, i)));
    }
  }
  
  virtual ~ExecutionQueue() {
    // Shut down the lanes.
    for (unsigned i = 0; i != numLanes; ++i) {
      addJob({});
    }
    for (unsigned i = 0; i != numLanes; ++i) {
      lanes[i]->join();
    }
  }

  virtual void addJob(QueueJob job) override {
    std::lock_guard<std::mutex> guard(readyJobsMutex);
    readyJobs.push_back(job);
    readyJobsCondition.notify_one();
  }

  virtual bool executeShellCommand(QueueJobContext*,
                                   const std::string& command) override {
    // Initialize the spawn attributes.
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);

    // Unmask all signals.
    sigset_t noSignals;
    sigemptyset(&noSignals);
    posix_spawnattr_setsigmask(&attributes, &noSignals);

    // Reset all signals to default behavior.
      //
      // On Linux, this can only be used to reset signals that are legal to
      // modify, so we have to take care about the set we use.
#if defined(__linux__)
      sigset_t mostSignals;
      sigemptyset(&mostSignals);
      for (int i = 1; i < SIGUNUSED; ++i) {
        if (i == SIGKILL || i == SIGSTOP) continue;
        sigaddset(&mostSignals, i);
      }
      posix_spawnattr_setsigdefault(&attributes, &mostSignals);
#else
      sigset_t allSignals;
      sigfillset(&allSignals);
      posix_spawnattr_setsigdefault(&attributes, &allSignals);
#endif      

    // Establish a separate process group.
    posix_spawnattr_setpgroup(&attributes, 0);

    // Set the attribute flags.
    unsigned flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
    flags |= POSIX_SPAWN_SETPGROUP;

    // Close all other files by default.
    //
    // FIXME: Note that this is an Apple-specific extension, and we will have to
    // do something else on other platforms (and unfortunately, there isn't
    // really an easy answer other than using a stub executable).
#ifdef __APPLE__
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif

    posix_spawnattr_setflags(&attributes, flags);

    // Setup the file actions.
    posix_spawn_file_actions_t fileActions;
    posix_spawn_file_actions_init(&fileActions);

    // Open /dev/null as stdin.
    posix_spawn_file_actions_addopen(
        &fileActions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fileActions, 1, 1);
    posix_spawn_file_actions_adddup2(&fileActions, 2, 2);

    // Spawn the command.
    const char* args[4];
    args[0] = "/bin/sh";
    args[1] = "-c";
    args[2] = command.c_str();
    args[3] = nullptr;

    // FIXME: Need to track spawned processes for the purposes of cancellation.
    
    pid_t pid;
    if (posix_spawn(&pid, args[0], /*file_actions=*/&fileActions,
                    /*attrp=*/&attributes, const_cast<char**>(args),
                    ::environ) != 0) {
      // FIXME: Error handling.
      fprintf(stderr, "error: unable to spawn process (%s)", strerror(errno));
      return false;
    }

    posix_spawn_file_actions_destroy(&fileActions);
    posix_spawnattr_destroy(&attributes);

    // Wait for the command to complete.
    int status, result = waitpid(pid, &status, 0);
    while (result == -1 && errno == EINTR)
      result = waitpid(pid, &status, 0);
    if (result == -1) {
      // FIXME: Error handling.
      fprintf(stderr, "error: unable to wait for process (%s)",
              strerror(errno));
      return false;
    }

    // If the child failed, show the full command and the output.
    return (status == 0);
  }
};

class BuildCommandDelegate : public BuildSystemDelegate {
  llvm::StringRef bufferBeingParsed;
  
public:
  BuildCommandDelegate() : BuildSystemDelegate("basic", /*version=*/0) {}

  void setFileContentsBeingParsed(llvm::StringRef buffer) override {
    bufferBeingParsed = buffer;
  }

  virtual void error(const std::string& filename,
                     const Token& at,
                     const std::string& message) override {
    if (at.start) {
      util::emitError(filename, message, at.start, at.length,
                      bufferBeingParsed);
    } else {
      fprintf(stderr, "%s: error: %s\n", filename.c_str(), message.c_str());
    }
  }
  
  virtual std::unique_ptr<Tool> lookupTool(const std::string& name) override {
    // We do not support any non-built-in tools.
    return nullptr;
  }

  virtual std::unique_ptr<BuildExecutionQueue> createExecutionQueue() override {
    // Get the number of CPUs to use.
    long numCPUs = sysconf(_SC_NPROCESSORS_ONLN);
    unsigned numLanes;
    if (numCPUs < 0) {
      error("<unknown>", {}, "unable to detect number of CPUs");
      numLanes = 1;
    } else {
      numLanes = numCPUs + 2;
    }
    
    return std::make_unique<ExecutionQueue>(numLanes);
  }
};

static void buildUsage(int exitCode) {
  int optionWidth = 20;
  fprintf(stderr, "Usage: %s buildsystem build [options] <path>\n",
          getProgramName());
  fprintf(stderr, "\nOptions:\n");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--help",
          "show this help message and exit");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--no-db",
          "disable use of a build database");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--db <PATH>",
          "enable building against the database at PATH [default='build.db']");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "-C <PATH>, --chdir <PATH>",
          "change directory to PATH before building");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--trace <PATH>",
          "trace build engine operation to PATH");
  ::exit(exitCode);
}

static int executeBuildCommand(std::vector<std::string> args) {
  std::string dbFilename = "build.db";
  std::string chdirPath;
  std::string traceFilename;
  
  while (!args.empty() && args[0][0] == '-') {
    const std::string option = args[0];
    args.erase(args.begin());

    if (option == "--")
      break;

    if (option == "--help") {
      buildUsage(0);
    } else if (option == "--no-db") {
      dbFilename = "";
    } else if (option == "--db") {
      if (args.empty()) {
        fprintf(stderr, "%s: error: missing argument to '%s'\n\n",
                getProgramName(), option.c_str());
        buildUsage(1);
      }
      dbFilename = args[0];
      args.erase(args.begin());
    } else if (option == "-C" || option == "--chdir") {
      if (args.empty()) {
        fprintf(stderr, "%s: error: missing argument to '%s'\n\n",
                getProgramName(), option.c_str());
        buildUsage(1);
      }
      chdirPath = args[0];
      args.erase(args.begin());
    } else if (option == "--trace") {
      if (args.empty()) {
        fprintf(stderr, "%s: error: missing argument to '%s'\n\n",
                getProgramName(), option.c_str());
        buildUsage(1);
      }
      traceFilename = args[0];
      args.erase(args.begin());
    } else {
      fprintf(stderr, "\error: %s: invalid option: '%s'\n\n",
              getProgramName(), option.c_str());
      buildUsage(1);
    }
  }

  if (args.size() != 1) {
    fprintf(stderr, "error: %s: invalid number of arguments\n", getProgramName());
    buildUsage(1);
  }

  // Honor the --chdir option, if used.
  if (!chdirPath.empty()) {
    if (::chdir(chdirPath.c_str()) < 0) {
      fprintf(stderr, "%s: error: unable to honor --chdir: %s\n",
              getProgramName(), strerror(errno));
      return 1;
    }

    // Print a message about the changed directory. The exact format here is
    // important, it is recognized by other tools (like Emacs).
    fprintf(stdout, "%s: Entering directory `%s'\n", getProgramName(),
            chdirPath.c_str());
    fflush(stdout);
  }

  std::string filename = args[0].c_str();

  BuildCommandDelegate delegate{};
  BuildSystem system(delegate, filename);

  // Enable tracing, if requested.
  if (!traceFilename.empty()) {
    std::string error;
    if (!system.enableTracing(traceFilename, &error)) {
      fprintf(stderr, "error: %s: unable to enable tracing: %s",
              getProgramName(), error.c_str());
      return 1;
    }
  }

  // Attach the database.
  if (!dbFilename.empty()) {
    // If the database path is relative, always make it relative to the input
    // file.
    if (llvm::sys::path::has_relative_path(dbFilename)) {
      llvm::SmallString<256> tmp;
      llvm::sys::path::append(tmp, llvm::sys::path::parent_path(filename),
                              dbFilename);
      dbFilename = tmp.str();
    }
    
    std::string error;
    if (!system.attachDB(dbFilename, &error)) {
      fprintf(stderr, "error: %s: unable to attach DB: %s\n", getProgramName(),
              error.c_str());
      return 1;
    }
  }
  
  // Build the default target.
  system.build("");
  
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
