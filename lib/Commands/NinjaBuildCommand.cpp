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

#include "llbuild/Core/BuildDB.h"
#include "llbuild/Core/BuildEngine.h"
#include "llbuild/Core/MakefileDepsParser.h"
#include "llbuild/Ninja/ManifestLoader.h"

#include "CommandUtil.h"

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <unordered_set>

#include <signal.h>
#include <spawn.h>
#include <unistd.h>
#include <sys/stat.h>

#include <dispatch/dispatch.h>

using namespace llbuild;
using namespace llbuild::commands;

extern "C" {
  char **environ;
}

static void usage() {
  int OptionWidth = 20;
  fprintf(stderr, "Usage: %s ninja build [options] <manifest> [<targets>]\n",
          ::getprogname());
  fprintf(stderr, "\nOptions:\n");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--help",
          "show this help message and exit");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--simulate",
          "simulate the build, assuming commands succeed");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--db <PATH>",
          "persist build results at PATH");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--dump-graph <PATH>",
          "dump build graph to PATH in Graphviz DOT format");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--jobs <NUM>",
          "maximum number of parallel jobs to use [default=1]");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--trace <PATH>",
          "trace build engine operation to PATH");
  ::exit(1);
}

namespace {

/// A simple queue for concurrent task work.
struct ConcurrentLimitedQueue {
  /// Semaphore used to implement our task limit.
  dispatch_semaphore_t LimitSemaphore;

  /// The queue we should execute on.
  dispatch_queue_t Queue;

public:
  ConcurrentLimitedQueue(unsigned JobLimit, dispatch_queue_t Queue)
    : LimitSemaphore(dispatch_semaphore_create(JobLimit)), Queue(Queue) { }
  ~ConcurrentLimitedQueue() {
    dispatch_release(LimitSemaphore);
    dispatch_release(Queue);
  }

  void addJob(std::function<void(void)> Job) {
    dispatch_async(Queue, ^() {
        // Acquire the semaphore.
        dispatch_semaphore_wait(LimitSemaphore, DISPATCH_TIME_FOREVER);

        // Execute the job.
        Job();

        // Release the semaphore.
        dispatch_semaphore_signal(LimitSemaphore);
      });
  }
};

struct NinjaBuildEngineDelegate : public core::BuildEngineDelegate {
  class BuildContext* Context = nullptr;

  virtual core::Rule lookupRule(const core::KeyType& Key) override;
};

/// Wrapper for information used during a single build.
class BuildContext {
public:
  /// The build engine delegate.
  NinjaBuildEngineDelegate Delegate;

  /// The engine in use.
  core::BuildEngine Engine;

  /// The Ninja manifest we are operating on.
  std::unique_ptr<ninja::Manifest> Manifest;

  /// Whether the build is being "simulated", in which case commands won't be
  /// run and inputs will be assumed to exist.
  bool Simulate = false;
  /// Whether commands should print status information.
  bool Quiet = false;

  /// The number of inputs used during the build.
  unsigned NumBuiltInputs = 0;
  /// The number of commands executed during the build
  unsigned NumBuiltCommands = 0;
  /// The number of output commands written, for numbering purposes.
  unsigned NumOutputDescriptions = 0;
  /// The number of failed commands.
  unsigned NumFailedCommands = 0;

  /// The serial queue we used to order output consistently.
  dispatch_queue_t OutputQueue;

  /// The limited queue we use to execute parallel jobs.
  std::unique_ptr<ConcurrentLimitedQueue> JobQueue;

public:
  BuildContext()
    : Engine(Delegate),
      OutputQueue(dispatch_queue_create("output-queue",
                                        /*attr=*/DISPATCH_QUEUE_SERIAL))
  {
    // Register the context with the delegate.
    Delegate.Context = this;
  }

  ~BuildContext() {
    dispatch_release(OutputQueue);
  }
};

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

static core::ValueType GetStatHashForNode(const ninja::Node* Node) {
  struct ::stat Buf;
  if (::stat(Node->getPath().c_str(), &Buf) != 0) {
    // FIXME: What now.
    return 0;
  }

  // Hash the stat information.
  auto Hash = std::hash<uint64_t>();
  auto Result = Hash(Buf.st_dev) ^ Hash(Buf.st_ino) ^
    Hash(Buf.st_mtimespec.tv_sec) ^ Hash(Buf.st_mtimespec.tv_nsec) ^
    Hash(Buf.st_size);

  // Ensure there is never a collision between a valid stat result and an error.
  if (Result == 0)
      Result = 1;

  return Result;
}

core::Task* BuildCommand(BuildContext& Context, ninja::Node* Output,
                         ninja::Command* Command) {
  struct NinjaCommandTask : core::Task {
    BuildContext& Context;
    ninja::Node* Output;
    ninja::Command* Command;

    NinjaCommandTask(BuildContext& Context, ninja::Node* Output,
                     ninja::Command* Command)
      : Task("ninja-command"), Context(Context), Output(Output),
        Command(Command) { }

    virtual void provideValue(core::BuildEngine& engine, uintptr_t InputID,
                              core::ValueType Value) override {
    }

    virtual void start(core::BuildEngine& engine) override {
      // Request all of the explicit and implicit inputs (the only difference
      // between them is that implicit inputs do not appear in ${in} during
      // variable expansion, but that has already been performed).
      for (auto it = Command->explicitInputs_begin(),
             ie = Command->explicitInputs_end(); it != ie; ++it) {
        engine.taskNeedsInput(this, (*it)->getPath(), 0);
      }
      for (auto it = Command->implicitInputs_begin(),
             ie = Command->implicitInputs_end(); it != ie; ++it) {
        engine.taskNeedsInput(this, (*it)->getPath(), 0);
      }

      // Request all of the order-only inputs.
      for (auto it = Command->orderOnlyInputs_begin(),
             ie = Command->orderOnlyInputs_end(); it != ie; ++it) {
        engine.taskMustFollow(this, (*it)->getPath());
      }
    }

    virtual void inputsAvailable(core::BuildEngine& engine) override {
      // Ignore phony commands.
      //
      // FIXME: Make efficient.
      if (Command->getRule()->getName() == "phony") {
        engine.taskIsComplete(this, 0);
        return;
      }

      ++Context.NumBuiltCommands;

      // If we are simulating the build, just print the description and
      // complete.
      if (Context.Simulate) {
        if (!Context.Quiet)
          writeDescription(Context, Command);
        Context.Engine.taskIsComplete(this, 0);
        return;
      }

      // Otherwise, enqueue the job to run later.
      Context.JobQueue->addJob([&] () { executeCommand(); });
    }

    static void writeDescription(BuildContext& Context,
                                 ninja::Command* Command) {
      std::cerr << "[" << ++Context.NumOutputDescriptions << "] ";
      if (Command->getDescription().empty()) {
        std::cerr << Command->getCommandString() << "\n";
      } else {
        std::cerr << Command->getDescription() << "\n";
      }
    }

    void executeCommand() {
      // Write the description on the output queue, taking care to not rely on
      // the ``this`` object, which may disappear before the queue executes this
      // block.
      if (!Context.Quiet) {
        BuildContext& LocalContext(Context);
        ninja::Command* LocalCommand(Command);
        dispatch_async(Context.OutputQueue, ^() {
            writeDescription(LocalContext, LocalCommand);
          });
      }

      // Initialize the spawn attributes.
      //
      // FIXME: We need to audit this to be robust about resetting everything
      // that is important, in particular we aren't handling file descriptors
      // yet.
      posix_spawnattr_t Attributes;
      posix_spawnattr_init(&Attributes);

      // Unmask all signals
      sigset_t NoSignals;
      sigemptyset(&NoSignals);
      posix_spawnattr_setsigmask(&Attributes, &NoSignals);

      // Reset all signals to default behavior.
      sigset_t AllSignals;
      sigfillset(&AllSignals);
      posix_spawnattr_setsigdefault(&Attributes, &AllSignals);

      // Set the attribute flags.
      posix_spawnattr_setflags(&Attributes, (POSIX_SPAWN_SETSIGMASK |
                                             POSIX_SPAWN_SETSIGDEF));

      // Spawn the command.
      //
      // FIXME: We would like to buffer the command output, in the same manner
      // as Ninja.
      const char* Args[4];
      Args[0] = "/bin/sh";
      Args[1] = "-c";
      Args[2] = Command->getCommandString().c_str();
      Args[3] = nullptr;
      int PID;
      if (posix_spawn(&PID, Args[0], /*file_actions=*/0, /*attrp=*/&Attributes,
                      const_cast<char**>(Args), ::environ) != 0) {
        fprintf(stderr, "error: %s: unable to spawn process (%s)\n",
                getprogname(), strerror(errno));
        exit(1);
      }

      posix_spawnattr_destroy(&Attributes);

      // Wait for the command to complete.
      int Status, Result = waitpid(PID, &Status, 0);
      while (Result == -1 && errno == EINTR)
          Result = waitpid(PID, &Status, 0);
      if (Result == -1) {
        // FIXME: Error handling.
        fprintf(stderr, "error: %s: unable to wait for process (%s)\n",
                getprogname(), strerror(errno));
        exit(1);
      }
      if (Status != 0) {
        std::cerr << "  ... process returned error status: " << Status << "\n";
        dispatch_async(Context.OutputQueue, ^() {
            ++Context.NumFailedCommands;
          });
      }

      // If the command succeeded, process the dependencies.
      if (Status == 0) {
        processDiscoveredDependencies();
      }

      Context.Engine.taskIsComplete(this, 0);
    }

    void processDiscoveredDependencies() {
      // Process the discovered dependencies, if used.
      switch (Command->getDepsStyle()) {
      case ninja::Command::DepsStyleKind::None:
        break;
      case ninja::Command::DepsStyleKind::MSVC: {
        fprintf(stderr, "error: %s: MSVC style dependencies are unsupported\n",
                getprogname());
        exit(1);
        break;
      }
      case ninja::Command::DepsStyleKind::GCC: {
        // Read the dependencies file.
        std::string Error;
        std::unique_ptr<char[]> Data;
        uint64_t Length;
        if (!util::ReadFileContents(Command->getDepsFile(), &Data, &Length,
                                    &Error)) {
          // FIXME: Error handling.
          fprintf(stderr,
                  "error: %s: unable to read dependency file: %s (%s)\n",
                  getprogname(), Command->getDepsFile().c_str(), Error.c_str());
          exit(1);
        }

        // Parse the output.
        //
        // We just ignore the rule, and add any dependency that we encounter in
        // the file.
        struct DepsActions : public core::MakefileDepsParser::ParseActions {
          BuildContext& Context;
          NinjaCommandTask* Task;
          const std::string& Path;
          
          DepsActions(BuildContext& Context, NinjaCommandTask* Task,
                      const std::string& Path)
            : Context(Context), Task(Task), Path(Path) {}

          virtual void error(const char* Message, uint64_t Length) override {
            // FIXME: Error handling.
            fprintf(stderr, ("error: %s: error reading dependency file: "
                             "%s (%s) at offset %u\n"),
                    getprogname(), Path.c_str(), Message, unsigned(Length));
          }
          
          virtual void actOnRuleDependency(const char* Dependency,
                                           uint64_t Length) override {
            Context.Engine.taskDiscoveredDependency(
              Task, std::string(Dependency, Dependency+Length));
          }
          
          virtual void actOnRuleStart(const char* Name,
                                      uint64_t Length) override {}
          virtual void actOnRuleEnd() override {}
        };
        DepsActions Actions(Context, this, Command->getDepsFile());
        core::MakefileDepsParser(Data.get(), Length, Actions).parse();
        break;
      }
      }
    }
  };

  return Context.Engine.registerTask(new NinjaCommandTask(Context, Output,
                                                          Command));
}

core::Task* BuildInput(BuildContext& Context, ninja::Node* Input) {
  struct NinjaInputTask : core::Task {
    BuildContext& Context;
    ninja::Node* Node;

    NinjaInputTask(BuildContext& Context, ninja::Node* Node)
      : Task("ninja-input"), Context(Context), Node(Node) { }

    virtual void provideValue(core::BuildEngine& engine, uintptr_t InputID,
                              core::ValueType Value) override { }

    virtual void start(core::BuildEngine& engine) override { }

    virtual void inputsAvailable(core::BuildEngine& engine) override {
      ++Context.NumBuiltInputs;

      if (Context.Simulate) {
        engine.taskIsComplete(this, 0);
        return;
      }

      engine.taskIsComplete(this, GetStatHashForNode(Node));
    }
  };

  return Context.Engine.registerTask(new NinjaInputTask(Context, Input));
}

static bool BuildInputIsResultValid(ninja::Node *Node,
                                    const core::ValueType Value) {
  // FIXME: This is inefficient, we will end up doing the stat twice, once when
  // we check the value for up to dateness, and once when we "build" the output.
  //
  // We can solve this by caching ourselves but I wonder if it is something the
  // engine should support more naturally.
  return GetStatHashForNode(Node) == Value;
}

core::Rule NinjaBuildEngineDelegate::lookupRule(const core::KeyType& Key) {
  // We created rules for all of the commands up front, so if we are asked for a
  // rule here it is because we are looking for an input.

  // Get the node for this input.
  //
  // FIXME: This is frequently a redundant lookup, given that the caller might
  // well have had the Node* available. This is something that would be nice
  // to avoid when we support generic key types.
  ninja::Node* Node = Context->Manifest->getOrCreateNode(Key);

  return core::Rule{
    Node->getPath(),
      [&, Node] (core::BuildEngine& Engine) {
      return BuildInput(*Context, Node);
    },
    [&, Node] (const core::Rule& Rule, const core::ValueType Value) {
      // If simulating, assume cached results are valid.
      if (Context->Simulate)
        return true;

      return BuildInputIsResultValid(Node, Value);
    } };
}

}

int commands::ExecuteNinjaBuildCommand(std::vector<std::string> Args) {
  std::string DBFilename, DumpGraphPath, TraceFilename;

  if (Args.empty() || Args[0] == "--help")
    usage();

  // Create a context for the build.
  BuildContext Context;
  unsigned NumJobs = 1;

  while (!Args.empty() && Args[0][0] == '-') {
    const std::string Option = Args[0];
    Args.erase(Args.begin());

    if (Option == "--")
      break;

    if (Option == "--simulate") {
      Context.Simulate = true;
    } else if (Option == "--quiet") {
      Context.Quiet = true;
    } else if (Option == "--db") {
      if (Args.empty()) {
        fprintf(stderr, "\error: %s: missing argument to '%s'\n\n",
                ::getprogname(), Option.c_str());
        usage();
      }
      DBFilename = Args[0];
      Args.erase(Args.begin());
    } else if (Option == "--dump-graph") {
      if (Args.empty()) {
        fprintf(stderr, "\error: %s: missing argument to '%s'\n\n",
                ::getprogname(), Option.c_str());
        usage();
      }
      DumpGraphPath = Args[0];
      Args.erase(Args.begin());
    } else if (Option == "--jobs") {
      if (Args.empty()) {
        fprintf(stderr, "\error: %s: missing argument to '%s'\n\n",
                ::getprogname(), Option.c_str());
        usage();
      }
      char *End;
      NumJobs = ::strtol(Args[0].c_str(), &End, 10);
      if (*End != '\0') {
        fprintf(stderr, "\error: %s: invalid argument to '%s'\n\n",
                ::getprogname(), Option.c_str());
        usage();
      }
      Args.erase(Args.begin());
    } else if (Option == "--trace") {
      if (Args.empty()) {
        fprintf(stderr, "\error: %s: missing argument to '%s'\n\n",
                ::getprogname(), Option.c_str());
        usage();
      }
      TraceFilename = Args[0];
      Args.erase(Args.begin());
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
  Context.Manifest = Loader.load();

  // If there were errors loading, we are done.
  if (unsigned NumErrors = Actions.getNumErrors()) {
    fprintf(stderr, "%d errors generated.\n", NumErrors);
    return 1;
  }

  // Otherwise, run the build.

  // Create the job queue to use.
  //
  // If we are only executing with a single job, we take care to use a serial
  // queue to ensure deterministic execution.
  dispatch_queue_t TaskQueue;
  if (NumJobs == 1) {
    TaskQueue = dispatch_queue_create("task-queue", DISPATCH_QUEUE_SERIAL);
  } else {
    TaskQueue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT,
                                          /*flags=*/0);
    dispatch_retain(TaskQueue);
  }
  Context.JobQueue.reset(new ConcurrentLimitedQueue(NumJobs, TaskQueue));

  // Attach the database, if requested.
  if (!DBFilename.empty()) {
    std::string Error;
    std::unique_ptr<core::BuildDB> DB(
      core::CreateSQLiteBuildDB(DBFilename, &Error));
    if (!DB) {
      fprintf(stderr, "error: %s: unable to open build database: %s\n",
              getprogname(), Error.c_str());
      return 1;
    }
    Context.Engine.attachDB(std::move(DB));
  }

  // Enable tracing, if requested.
  if (!TraceFilename.empty()) {
    std::string Error;
    if (!Context.Engine.enableTracing(TraceFilename, &Error)) {
      fprintf(stderr, "error: %s: unable to enable tracing: %s\n",
              getprogname(), Error.c_str());
      return 1;
    }
  }

  // Create rules for all of the build commands up front.
  //
  // FIXME: We should probably also move this to be dynamic.
  for (auto& Command: Context.Manifest->getCommands()) {
    for (auto& Output: Command->getOutputs()) {
      Context.Engine.addRule({
          Output->getPath(),
          [&] (core::BuildEngine& Engine) {
            return BuildCommand(Context, Output, Command.get());
          },
          [&] (const core::Rule& Rule, const core::ValueType Value) {
            // If simulating, assume cached results are valid.
            if (Context.Simulate)
              return true;

            // Always rebuild if the output is missing.
            if (GetStatHashForNode(Output) == 0)
              return false;

            return true;
          } });
    }
  }

  // If no explicit targets were named, build the default targets.
  if (TargetsToBuild.empty()) {
    for (auto& Target: Context.Manifest->getDefaultTargets())
      TargetsToBuild.push_back(Target->getPath());
  }

  // Build the requested targets.
  for (auto& Name: TargetsToBuild) {
    std::cerr << "building target \"" << util::EscapedString(Name) << "\"...\n";
    Context.Engine.build(Name);
  }
  // Ensure the output queue is finished.
  dispatch_sync(Context.OutputQueue, ^() {});

  std::cerr << "... built using " << Context.NumBuiltInputs << " inputs\n";
  std::cerr << "... built using " << Context.NumBuiltCommands << " commands\n";

  if (!DumpGraphPath.empty()) {
    Context.Engine.dumpGraphToFile(DumpGraphPath);
  }

  // If there were command failures, return an error status.
  if (Context.NumFailedCommands) {
    fprintf(stderr, "error: %s: build had %d command failures\n",
            getprogname(), Context.NumFailedCommands);
    return 1;
  }

  // Return an appropriate exit status.
  return 0;
}
