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

#include "llbuild/Basic/Hashing.h"
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
#include <sys/time.h>

#include <dispatch/dispatch.h>

using namespace llbuild;
using namespace llbuild::commands;

extern "C" {
  extern char **environ;
}

static uint64_t GetTimeInMicroseconds() {
  struct timeval tv;
  ::gettimeofday(&tv, nullptr);
  return tv.tv_sec * 1000000 + tv.tv_usec;
}

static void usage(int ExitCode=1) {
  int OptionWidth = 20;
  fprintf(stderr, "Usage: %s ninja build [options] [<targets>...]\n",
          ::getprogname());
  fprintf(stderr, "\nOptions:\n");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--help",
          "show this help message and exit");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--simulate",
          "simulate the build, assuming commands succeed");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--chdir <PATH>",
          "change directory to PATH before anything else");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--no-db",
          "do not persist build results");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--db <PATH>",
          "persist build results at PATH [default='build.db']");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "-f <PATH>",
          "load the manifest at PATH [default='build.ninja']");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--no-parallel",
          "build commands serially");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--parallel",
          "build commands in parallel [default]");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--dump-graph <PATH>",
          "dump build graph to PATH in Graphviz DOT format");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--profile <PATH>",
          "write a build profile trace event file to PATH");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--trace <PATH>",
          "trace build engine operation to PATH");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--quiet",
          "don't show information on executed commands");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "-v, --verbose",
          "show full invocation for executed commands");
  ::exit(ExitCode);
}

namespace {

/// A simple queue for concurrent task work.
struct ConcurrentLimitedQueue {
  /// Semaphore used to implement our task limit.
  dispatch_semaphore_t LimitSemaphore;

  /// The queue we should execute on.
  dispatch_queue_t Queue;

  /// Queue we use to assign bucket numbers.
  dispatch_queue_t BucketQueue;
  uint64_t BucketMask = 0;

public:
  ConcurrentLimitedQueue(unsigned JobLimit, dispatch_queue_t Queue)
    : LimitSemaphore(dispatch_semaphore_create(JobLimit)), Queue(Queue)
  {
    BucketQueue = dispatch_queue_create("bucket-queue",
                                        /*attr=*/DISPATCH_QUEUE_SERIAL);
  }
  ~ConcurrentLimitedQueue() {
    dispatch_release(BucketQueue);
    dispatch_release(LimitSemaphore);
    dispatch_release(Queue);
  }

  void addJob(std::function<void(unsigned)> Job) {
    dispatch_async(Queue, ^() {
        // Acquire the semaphore.
        dispatch_semaphore_wait(LimitSemaphore, DISPATCH_TIME_FOREVER);

        // Allocate a bucket number.
        __block unsigned BucketNumber;
        dispatch_sync(BucketQueue, ^() {
            for (BucketNumber = 0; BucketNumber != 64; ++BucketNumber) {
              if (!(BucketMask & (1 << BucketNumber))) {
                BucketMask |= (1 << BucketNumber);
                break;
              }
            }
          });

        // Execute the job.
        Job(BucketNumber);

        // Release the bucket number.
        dispatch_sync(BucketQueue, ^() {
            assert(BucketMask & (1 << BucketNumber));
            BucketMask &= ~(1 << BucketNumber);
          });

        // Release the semaphore.
        dispatch_semaphore_signal(LimitSemaphore);
      });
  }
};

/// Information on an external file stored as part of a build value.
///
/// This structure is intentionally sized to have no packing holes.
struct FileInfo {
  uint64_t Device;
  uint64_t INode;
  uint64_t Size;
  struct {
    uint64_t Seconds;
    uint64_t Nanoseconds;
  } ModTime;

  bool operator==(const FileInfo& RHS) const {
    return (Device == RHS.Device &&
            INode == RHS.INode &&
            Size == RHS.Size &&
            ModTime.Seconds == RHS.ModTime.Seconds &&
            ModTime.Nanoseconds == RHS.ModTime.Nanoseconds);
  }
  bool operator!=(const FileInfo& RHS) const {
    return !(*this == RHS);
  }
};

/// Result value that is computed by the rules for input and command files.
class BuildValue {
public:
  static const int CurrentSchemaVersion = 3;

private:
  enum class BuildValueKind : uint32_t {
    /// A value produced by a existing input file.
    ExistingInput = 0,

    /// A value produced by a missing input file.
    MissingInput,

    /// A value produced by a successful command.
    SuccessfulCommand,

    /// A value produced by a failing command.
    FailedCommand,

    /// A value produced by a command that was not run.  
    SkippedCommand,
  };

  /// The kind of value.
  BuildValueKind Kind;

  /// Padding, to ensure all bytes are defined (this is important given how we
  /// serialize).
  const uint32_t Padding = 0;

  /// The file info for the rule output.
  FileInfo OutputInfo;

  /// The command hash, for successful commands.
  uint64_t CommandHash;

private:
  BuildValue() {}
  BuildValue(BuildValueKind Kind, const FileInfo& OutputInfo,
             uint64_t CommandHash = 0)
    : Kind(Kind), OutputInfo(OutputInfo), CommandHash(CommandHash) {}

public:
  static BuildValue makeExistingInput(const FileInfo& OutputInfo) {
    return BuildValue(BuildValueKind::ExistingInput, OutputInfo);
  }
  static BuildValue makeMissingInput() {
    return BuildValue(BuildValueKind::MissingInput, {});
  }
  static BuildValue makeSuccessfulCommand(const FileInfo& OutputInfo,
                                          uint64_t CommandHash) {
    return BuildValue(BuildValueKind::SuccessfulCommand, OutputInfo,
                      CommandHash);
  }
  static BuildValue makeFailedCommand() {
    return BuildValue(BuildValueKind::FailedCommand, {});
  }
  static BuildValue makeSkippedCommand() {
    return BuildValue(BuildValueKind::SkippedCommand, {});
  }

  bool isExistingInput() const { return Kind == BuildValueKind::ExistingInput; }
  bool isMissingInput() const { return Kind == BuildValueKind::MissingInput; }
  bool isSuccessfulCommand() const {
    return Kind == BuildValueKind::SuccessfulCommand;
  }
  bool isFailedCommand() const { return Kind == BuildValueKind::FailedCommand; }
  bool isSkippedCommand() const {
    return Kind == BuildValueKind::SkippedCommand;
  }

  const FileInfo& getOutputInfo() const {
    assert((isExistingInput() || isSuccessfulCommand()) &&
           "invalid call for value kind");
    return OutputInfo;
  }

  uint64_t getCommandHash() const {
    assert(isSuccessfulCommand() && "invalid call for value kind");
    return CommandHash;
  }

  static BuildValue fromValue(const core::ValueType& Value) {
    BuildValue Result;
    assert(Value.size() == sizeof(Result));
    memcpy(&Result, Value.data(), sizeof(Result));
    return Result;
  }
  
  core::ValueType toValue() {
    std::vector<uint8_t> Result(sizeof(*this));
    memcpy(Result.data(), this, sizeof(*this));
    return Result;
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
  /// Whether output should use verbose mode.
  bool Verbose = false;

  /// The build profile output file.
  FILE *ProfileFP = nullptr;

  /// Whether the build has been cancelled or not.
  std::atomic<bool> IsCancelled;

  /// Whether the build was cancelled by SIGINT.
  std::atomic<bool> WasCancelledBySigint;

  /// The number of inputs used during the build.
  unsigned NumBuiltInputs{0};
  /// The number of commands executed during the build
  unsigned NumBuiltCommands{0};
  /// The number of output commands written, for numbering purposes.
  unsigned NumOutputDescriptions{0};
  /// The number of failed commands.
  std::atomic<unsigned> NumFailedCommands{0};

  /// The serial queue we used to order output consistently.
  dispatch_queue_t OutputQueue;

  /// The limited queue we use to execute parallel jobs.
  std::unique_ptr<ConcurrentLimitedQueue> JobQueue;

  /// The SIGINT dispatch source.
  dispatch_source_t SigintSource;

  /// The previous SIGINT handler.
  struct sigaction PreviousSigintHandler;

public:
  BuildContext()
    : Engine(Delegate),
      IsCancelled(false),
      OutputQueue(dispatch_queue_create("output-queue",
                                        /*attr=*/DISPATCH_QUEUE_SERIAL))
  {
    // Register the context with the delegate.
    Delegate.Context = this;

    // Register a dispatch source to handle SIGINT.
    SigintSource = dispatch_source_create(
      DISPATCH_SOURCE_TYPE_SIGNAL, SIGINT, 0, OutputQueue);
    dispatch_source_set_event_handler(SigintSource, ^{
        fprintf(stderr, "... cancelling build.\n");
        IsCancelled = true;
        WasCancelledBySigint = true;
      });
    dispatch_resume(SigintSource);

    // Clear the default SIGINT handling behavior.
    struct sigaction Action{};
    Action.sa_handler = SIG_IGN;
    sigaction(SIGINT, &Action, &PreviousSigintHandler);
  }

  ~BuildContext() {
    // Clean up our dispatch source.
    dispatch_source_cancel(SigintSource);
    dispatch_release(SigintSource);

    // Restore any previous SIGINT handler.
    sigaction(SIGINT, &PreviousSigintHandler, NULL);

    dispatch_release(OutputQueue);
  }
};

class BuildManifestActions : public ninja::ManifestLoaderActions {
  ninja::ManifestLoader *Loader = 0;
  unsigned NumErrors = 0;
  unsigned MaxErrors = 20;

private:
  virtual void initialize(ninja::ManifestLoader *Loader) override {
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

/// Get the information to represent the state of the given node in the file
/// system.
///
/// \param Info_Out [out] On success, the important path information.
/// \returns True if information on the path was found.
static bool GetStatInfoForNode(const ninja::Node* Node, FileInfo *Info_Out) {
  struct ::stat Buf;
  if (::stat(Node->getPath().c_str(), &Buf) != 0) {
    memset(Info_Out, 0, sizeof(*Info_Out));
    return false;
  }

  Info_Out->Device = Buf.st_dev;
  Info_Out->INode = Buf.st_ino;
  Info_Out->Size = Buf.st_size;
  Info_Out->ModTime.Seconds = Buf.st_mtimespec.tv_sec;
  Info_Out->ModTime.Nanoseconds = Buf.st_mtimespec.tv_nsec;

  // Verify we didn't truncate any values.
  assert(Info_Out->Device == (unsigned)Buf.st_dev &&
         Info_Out->INode == (unsigned)Buf.st_ino &&
         Info_Out->Size == (unsigned)Buf.st_size &&
         Info_Out->ModTime.Seconds == (unsigned)Buf.st_mtimespec.tv_sec &&
         Info_Out->ModTime.Nanoseconds == (unsigned)Buf.st_mtimespec.tv_nsec);

  return true;
}

core::Task* BuildCommand(BuildContext& Context, ninja::Node* Output,
                         ninja::Command* Command) {
  struct NinjaCommandTask : core::Task {
    BuildContext& Context;
    ninja::Node* Output;
    ninja::Command* Command;

    /// If true, the command should be skipped (because of an error in an
    /// input).
    bool ShouldSkip = false;

    /// If true, the command had a missing input (this implies ShouldSkip is
    /// true).
    bool HasMissingInput = false;

    /// If true, the command can be updated if the output is newer than all of
    /// the inputs.
    bool CanUpdateIfNewer = true;

    /// Information on the prior command result, if present.
    bool HasPriorResult = false;
    uint64_t PriorCommandHash;

    /// The timestamp of the most recently rebuilt input.
    struct {
      uint64_t Seconds, Nanoseconds;
    } NewestModTime = { 0, 0 };

    NinjaCommandTask(BuildContext& Context, ninja::Node* Output,
                     ninja::Command* Command)
      : Task("ninja-command"), Context(Context), Output(Output),
        Command(Command) {
      // If this command uses discovered dependencies, we can never skip it (we
      // don't yet have a way to account for the discovered dependencies, or
      // preserve them if skipped).
      //
      // FIXME: We should support update-if-newer for commands with deps.
      if (Command->getDepsStyle() != ninja::Command::DepsStyleKind::None)
        CanUpdateIfNewer = false;
    }

    virtual void provideValue(core::BuildEngine& engine, uintptr_t InputID,
                              const core::ValueType& ValueData) override {
      // Process the input value to see if we should skip this command.
      BuildValue Value = BuildValue::fromValue(ValueData);

      // If the value is not an existing input or a successful command, then we
      // shouldn't run this command.
      if (!Value.isExistingInput() && !Value.isSuccessfulCommand()) {
        ShouldSkip = true;
        if (Value.isMissingInput())
          HasMissingInput = true;
      } else {
        // Otherwise, track the information used to determine if we can just
        // update the command instead of running it.
        const FileInfo& OutputInfo = Value.getOutputInfo();

        // If there is a missing input file (from a successful command), we
        // always need to run the command.
        //
        // FIXME: Add an explicit state for missing files?
        if (OutputInfo.ModTime.Seconds == 0 &&
            OutputInfo.ModTime.Nanoseconds == 0) {
          CanUpdateIfNewer = false;
        } else {
          // Otherwise, keep track of the newest input.
          if (OutputInfo.ModTime.Seconds > NewestModTime.Seconds ||
              (OutputInfo.ModTime.Seconds == NewestModTime.Seconds &&
               OutputInfo.ModTime.Nanoseconds > NewestModTime.Nanoseconds)) {
            NewestModTime.Seconds = OutputInfo.ModTime.Seconds;
            NewestModTime.Nanoseconds = OutputInfo.ModTime.Nanoseconds;
          }
        }
      }
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

    virtual void providePriorValue(core::BuildEngine& Engine,
                                   const core::ValueType& ValueData) override {
      BuildValue Value = BuildValue::fromValue(ValueData);

      if (Value.isSuccessfulCommand()) {
        HasPriorResult = true;
        PriorCommandHash = Value.getCommandHash();
      }
    }

    virtual void inputsAvailable(core::BuildEngine& engine) override {
      // If the build is cancelled, skip everything.
      if (Context.IsCancelled) {
        Context.Engine.taskIsComplete(
          this, BuildValue::makeSkippedCommand().toValue());
        return;
      }

      // Ignore phony commands.
      //
      // FIXME: Make efficient.
      //
      // FIXME: Is it right to bring this up-to-date when one of the inputs
      // indicated a failure? It probably doesn't matter.
      if (Command->getRule()->getName() == "phony") {
        // Get the output info.
        //
        // If the output is missing, then we always want to force the change to
        // propagate.
        FileInfo OutputInfo;
        bool OutputExists = GetStatInfoForNode(Output, &OutputInfo);
        engine.taskIsComplete(
          this, BuildValue::makeSuccessfulCommand(OutputInfo,
                                                  /*CommandHash=*/0).toValue(),
          /*ForceChange=*/!OutputExists);
        return;
      }

      // If it is legal to simply update the command, then if the command output
      // exists and is newer than all of the inputs, don't actually run the
      // command (just bring it up-to-date).
      //
      // We use a strict "newer-than" check here, to guarantee correctness in
      // the face of equivalent timestamps. This is particularly important on OS
      // X, which has a low resolution mtime.
      if (CanUpdateIfNewer) {
        // If this isn't a generator command and it's command hash differs, we
        // can't update it.
        uint64_t CommandHash = basic::HashString(Command->getCommandString());
        if (!Command->hasGeneratorFlag() &&
            (!HasPriorResult || PriorCommandHash != CommandHash))
          CanUpdateIfNewer = false;

        if (CanUpdateIfNewer) {
          FileInfo OutputInfo;
          if (GetStatInfoForNode(Output, &OutputInfo) &&
              (OutputInfo.ModTime.Seconds > NewestModTime.Seconds ||
               (OutputInfo.ModTime.Seconds == NewestModTime.Seconds &&
                OutputInfo.ModTime.Nanoseconds > NewestModTime.Nanoseconds)) ) {
            // Complete the task with a successful value.
            Context.Engine.taskIsComplete(
              this, BuildValue::makeSuccessfulCommand(OutputInfo,
                                                      CommandHash).toValue());
            return;
          }
        }
      }

      // Otherwise, actually run the command.

      ++Context.NumBuiltCommands;

      // If we are simulating the build, just print the description and
      // complete.
      if (Context.Simulate) {
        if (!Context.Quiet)
          writeDescription(Context, Command);
        Context.Engine.taskIsComplete(
          this, BuildValue::makeSkippedCommand().toValue());
        return;
      }

      // If not simulating, but this command should be skipped, then do nothing.
      if (ShouldSkip) {
        // If this command had a failed input, treat it as having failed.
        if (HasMissingInput) {
          // Take care to not rely on the ``this`` object, which may disappear
          // before the queue executes this block.
          ninja::Node *LocalOutput = Output;

          ++Context.NumFailedCommands;

          dispatch_async(Context.OutputQueue, ^() {
              fprintf(stderr,
                      "error: %s: cannot build '%s' due to missing input\n",
                      getprogname(), LocalOutput->getPath().c_str());
            });
        }

        Context.Engine.taskIsComplete(
          this, BuildValue::makeSkippedCommand().toValue());
        return;
      }
      assert(!HasMissingInput);

      // Otherwise, enqueue the job to run later.
      Context.JobQueue->addJob([&] (unsigned Bucket) {
          // Take care to not rely on the ``this`` object, which may disappear
          // before the queue executes this block.
          BuildContext& LocalContext(Context);
          ninja::Command* LocalCommand(Command);

          if (LocalContext.ProfileFP) {
            dispatch_sync(LocalContext.OutputQueue, ^() {
                uint64_t StartTime = GetTimeInMicroseconds();
                fprintf(LocalContext.ProfileFP,
                        ("{ \"name\": \"%s\", \"ph\": \"B\", \"pid\": 0, "
                         "\"tid\": %d, \"ts\": %llu},\n"),
                        LocalCommand->getEffectiveDescription().c_str(), Bucket,
                        StartTime);
              });
          }

          executeCommand();

          if (LocalContext.ProfileFP) {
            dispatch_sync(LocalContext.OutputQueue, ^() {
                uint64_t EndTime = GetTimeInMicroseconds();
                fprintf(LocalContext.ProfileFP,
                        ("{ \"name\": \"%s\", \"ph\": \"E\", \"pid\": 0, "
                         "\"tid\": %d, \"ts\": %llu},\n"),
                        LocalCommand->getEffectiveDescription().c_str(), Bucket,
                        EndTime);
              });
          }
        });
    }

    static void writeDescription(BuildContext& Context,
                                 ninja::Command* Command) {
      std::cerr << "[" << ++Context.NumOutputDescriptions << "] ";
      if (Context.Verbose) {
        std::cerr << Command->getCommandString() << "\n";
      } else {
        std::cerr << Command->getEffectiveDescription() << "\n";
      }
    }

    void executeCommand() {
      // If the build is cancelled, skip the job.
      if (Context.IsCancelled) {
        Context.Engine.taskIsComplete(
          this, BuildValue::makeSkippedCommand().toValue());
        return;
      }

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
        // If the child was killed by SIGINT, assume it is because we were
        // interrupted.
        //
        // FIXME: We should probably match Ninja here, if what it does is run
        // the process in its own process group. I haven't checked.
        if (WIFSIGNALED(Status)) {
          int Signal = WTERMSIG(Status);

          if (Signal == SIGINT) {
            Context.Engine.taskIsComplete(
              this, BuildValue::makeFailedCommand().toValue(),
              /*ForceChange=*/true);
            return;
          }

          dispatch_async(Context.OutputQueue, ^() {
              std::cerr << "  ... process exited with signal: "
                        << Signal << "\n";
            });
        } else {
          int ExitStatus = WEXITSTATUS(Status);

          ++Context.NumFailedCommands;
          dispatch_async(Context.OutputQueue, ^() {
              std::cerr << "  ... process returned error status: "
                        << ExitStatus << "\n";
            });
        }

        // Complete the task with a failing value.
        Context.Engine.taskIsComplete(
          this, BuildValue::makeFailedCommand().toValue(),
          /*ForceChange=*/true);
        return;
      }

      // Otherwise, the command succeeded so process the dependencies.
      processDiscoveredDependencies();

      // Get the output hash, ignoring missing outputs.
      FileInfo OutputInfo;
      GetStatInfoForNode(Output, &OutputInfo);

      // Complete the task with a successful value.
      uint64_t CommandHash = basic::HashString(Command->getCommandString());
      Context.Engine.taskIsComplete(
        this, BuildValue::makeSuccessfulCommand(OutputInfo,
                                                CommandHash).toValue(),
        /*ForceChange=*/!Command->hasRestatFlag());
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
                              const core::ValueType& Value) override { }

    virtual void start(core::BuildEngine& engine) override { }

    static void reportMissingInput(const ninja::Node* Node) {
      fprintf(stderr, "error: missing input '%s' and no rule to build it\n",
              Node->getPath().c_str());
    }

    virtual void inputsAvailable(core::BuildEngine& engine) override {
      ++Context.NumBuiltInputs;

      if (Context.Simulate) {
        engine.taskIsComplete(
          this, BuildValue::makeExistingInput({}).toValue());
        return;
      }

      FileInfo OutputInfo;
      if (!GetStatInfoForNode(Node, &OutputInfo)) {
        // Report the missing input on the output queue, taking care to not rely
        // on the ``this`` object, which may disappear before the queue executes
        // this block.
        const ninja::Node* LocalNode(Node);
        dispatch_async(Context.OutputQueue, ^() {
            reportMissingInput(LocalNode);
          });

        engine.taskIsComplete(this, BuildValue::makeMissingInput().toValue());
        return;
      }
        
      engine.taskIsComplete(
        this, BuildValue::makeExistingInput(OutputInfo).toValue());
    }
  };

  return Context.Engine.registerTask(new NinjaInputTask(Context, Input));
}

core::Task* BuildTargets(BuildContext& Context,
                         const std::vector<std::string> &TargetsToBuild) {
  struct TargetsTask : core::Task {
    BuildContext& Context;
    std::vector<std::string> TargetsToBuild;

    TargetsTask(BuildContext& Context,
                const std::vector<std::string> &TargetsToBuild)
      : Task("targets"), Context(Context), TargetsToBuild(TargetsToBuild) { }

    virtual void provideValue(core::BuildEngine& engine, uintptr_t InputID,
                              const core::ValueType& ValueData) override { }

    virtual void start(core::BuildEngine& engine) override {
      // Request all of the targets.
      for (const auto& Target: TargetsToBuild) {
        engine.taskNeedsInput(this, Target, 0);
      }
    }

    virtual void inputsAvailable(core::BuildEngine& engine) override {
      // Complete the job.
      engine.taskIsComplete(
        this, BuildValue::makeSuccessfulCommand({}, 0).toValue());
      return;
    }
  };

  return Context.Engine.registerTask(new TargetsTask(Context, TargetsToBuild));
}

static bool BuildInputIsResultValid(ninja::Node* Node,
                                    const core::ValueType& ValueData) {
  BuildValue Value = BuildValue::fromValue(ValueData);

  // If the prior value wasn't for an existing input, recompute.
  if (!Value.isExistingInput())
    return false;

  // Otherwise, the result is valid if the path exists and the hash has not
  // changed.
  //
  // FIXME: This is inefficient, we will end up doing the stat twice, once when
  // we check the value for up to dateness, and once when we "build" the output.
  //
  // We can solve this by caching ourselves but I wonder if it is something the
  // engine should support more naturally.
  FileInfo Info;
  if (!GetStatInfoForNode(Node, &Info))
    return false;

  return Value.getOutputInfo() == Info;
}

static bool BuildCommandIsResultValid(ninja::Command* Command,
                                      ninja::Node* Node,
                                      const core::ValueType& ValueData) {
  BuildValue Value = BuildValue::fromValue(ValueData);

  // If the prior value wasn't for a successful command, recompute.
  if (!Value.isSuccessfulCommand())
    return false;

  // For non-generator commands, if the command hash has changed, recompute.
  if (!Command->hasGeneratorFlag()) {
    if (Value.getCommandHash() != basic::HashString(
          Command->getCommandString()))
      return false;
  }

  // Always rebuild if the output is missing.
  FileInfo Info;
  if (!GetStatInfoForNode(Node, &Info))
    return false;

  // Otherwise, the result is valid if the output exists and the hash has not
  // changed.
  //
  // FIXME: While this is reasonable in a strictly functional view of the build
  // system, a common behavior is that the *producer* of the output is not
  // considered out of date if the output has been modified outside the build
  // system, but *consumers* are. This is occasionally useful when doing things
  // like manually reissuing compilation commands, for example. We do not yet
  // support that model, and will rebuild outputs if anyone tampers with them.
  return Value.getOutputInfo() == Info;
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
    [&, Node] (const core::Rule& Rule, const core::ValueType& Value) {
      // If simulating, assume cached results are valid.
      if (Context->Simulate)
        return true;

      return BuildInputIsResultValid(Node, Value);
    } };
}

}

int commands::ExecuteNinjaBuildCommand(std::vector<std::string> Args) {
  std::string ChdirPath = "";
  std::string DBFilename = "build.db";
  std::string ManifestFilename = "build.ninja";
  std::string DumpGraphPath, ProfileFilename, TraceFilename;

  // Create a context for the build.
  bool Quiet = false;
  bool Simulate = false;
  bool UseParallelBuild = true;
  bool Verbose = false;

  while (!Args.empty() && Args[0][0] == '-') {
    const std::string Option = Args[0];
    Args.erase(Args.begin());

    if (Option == "--")
      break;

    if (Option == "--help") {
      usage(/*ExitCode=*/0);
    } else if (Option == "--simulate") {
      Simulate = true;
    } else if (Option == "--quiet") {
      Quiet = true;
    } else if (Option == "--chdir") {
      if (Args.empty()) {
        fprintf(stderr, "error: %s: missing argument to '%s'\n\n",
                ::getprogname(), Option.c_str());
        usage();
      }
      ChdirPath = Args[0];
      Args.erase(Args.begin());
    } else if (Option == "--no-db") {
      DBFilename = "";
    } else if (Option == "--db") {
      if (Args.empty()) {
        fprintf(stderr, "error: %s: missing argument to '%s'\n\n",
                ::getprogname(), Option.c_str());
        usage();
      }
      DBFilename = Args[0];
      Args.erase(Args.begin());
    } else if (Option == "--dump-graph") {
      if (Args.empty()) {
        fprintf(stderr, "error: %s: missing argument to '%s'\n\n",
                ::getprogname(), Option.c_str());
        usage();
      }
      DumpGraphPath = Args[0];
      Args.erase(Args.begin());
    } else if (Option == "-f") {
      if (Args.empty()) {
        fprintf(stderr, "error: %s: missing argument to '%s'\n\n",
                ::getprogname(), Option.c_str());
        usage();
      }
      ManifestFilename = Args[0];
      Args.erase(Args.begin());
    } else if (Option == "--no-parallel") {
      UseParallelBuild = false;
    } else if (Option == "--parallel") {
      UseParallelBuild = true;
    } else if (Option == "--profile") {
      if (Args.empty()) {
        fprintf(stderr, "error: %s: missing argument to '%s'\n\n",
                ::getprogname(), Option.c_str());
        usage();
      }
      ProfileFilename = Args[0];
      Args.erase(Args.begin());
    } else if (Option == "--trace") {
      if (Args.empty()) {
        fprintf(stderr, "error: %s: missing argument to '%s'\n\n",
                ::getprogname(), Option.c_str());
        usage();
      }
      TraceFilename = Args[0];
      Args.erase(Args.begin());
    } else if (Option == "-v" || Option == "--verbose") {
      Verbose = true;
    } else {
      fprintf(stderr, "error: %s: invalid option: '%s'\n\n",
              ::getprogname(), Option.c_str());
      usage();
    }
  }

  // Parse the positional arguments.
  std::vector<std::string> TargetsToBuild(Args);

  // Honor the --chdir option, if used.
  if (!ChdirPath.empty()) {
    if (::chdir(ChdirPath.c_str()) < 0) {
      fprintf(stderr, "error: %s: unable to honor --chdir: %s\n",
              getprogname(), strerror(errno));
      return 1;
    }
  }

  // Run up to two iterations, the first one loads the manifest and rebuilds it
  // if necessary, the second only runs if the manifest needs to be reloaded.
  //
  // This is somewhat inefficient in the case where the manifest needs to be
  // reloaded (we reopen the database, for example), but we don't expect that to
  // be a common case spot in practice.
  for (int Iteration = 0; Iteration != 2; ++Iteration) {
    BuildContext Context;

    Context.Simulate = Simulate;
    Context.Quiet = Quiet;
    Context.Verbose = Verbose;

    // Create the job queue to use.
    //
    // If we are only executing with a single job, we take care to use a serial
    // queue to ensure deterministic execution.
    dispatch_queue_t TaskQueue;
    unsigned NumJobs;
    if (!UseParallelBuild) {
      TaskQueue = dispatch_queue_create("task-queue", DISPATCH_QUEUE_SERIAL);
      NumJobs = 1;
    } else {
      long NumCPUs = sysconf(_SC_NPROCESSORS_ONLN);
      if (NumCPUs < 0) {
        fprintf(stderr, "error: %s: unable to detect number of CPUs: %s\n",
                getprogname(), strerror(errno));
        return 1;
      }
  
      NumJobs = NumCPUs + 2;
      TaskQueue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT,
                                            /*flags=*/0);
      dispatch_retain(TaskQueue);
    }
    Context.JobQueue.reset(new ConcurrentLimitedQueue(NumJobs, TaskQueue));

    // Load the manifest.
    BuildManifestActions Actions;
    ninja::ManifestLoader Loader(ManifestFilename, Actions);
    Context.Manifest = Loader.load();

    // If there were errors loading, we are done.
    if (unsigned NumErrors = Actions.getNumErrors()) {
        fprintf(stderr, "%d errors generated.\n", NumErrors);
        return 1;
    }

    // Otherwise, run the build.
    
    // Attach the database, if requested.
    if (!DBFilename.empty()) {
      std::string Error;
      std::unique_ptr<core::BuildDB> DB(
        core::CreateSQLiteBuildDB(DBFilename,
                                  BuildValue::CurrentSchemaVersion,
                                  &Error));
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
  
              return BuildCommandIsResultValid(Command.get(), Output, Value);
            } });
      }
    }

    // If this is the first iteration, build the manifest.
    if (Iteration == 0) {
      Context.Engine.build(ManifestFilename);
        
      // If the manifest was rebuilt, then reload it and build again.
      if (Context.NumBuiltCommands) {
        continue;
      }

      // Otherwise, perform the main build.
      //
      // FIXME: This is somewhat inefficient, as we will end up repeating any
      // dependency scanning that was required for checking the manifest. We can
      // fix this by building the manifest inline with the targets...
    }

    // If using a build profile, open it.
    if (!ProfileFilename.empty()) {
      Context.ProfileFP = ::fopen(ProfileFilename.c_str(), "w");
      if (!Context.ProfileFP) {
        fprintf(stderr, "error: %s: unable to open build profile '%s': %s\n",
                getprogname(), ProfileFilename.c_str(), strerror(errno));
        return 1;
      }

      fprintf(Context.ProfileFP, "[\n");
    }
  
    // If no explicit targets were named, build the default targets.
    if (TargetsToBuild.empty()) {
      for (auto& Target: Context.Manifest->getDefaultTargets())
        TargetsToBuild.push_back(Target->getPath());
  
      // If there are no default targets, then build all of the root targets.
      if (TargetsToBuild.empty()) {
        std::unordered_set<ninja::Node*> InputNodes;
  
        // Collect all of the input nodes.
        for (const auto& Command: Context.Manifest->getCommands()) {
          for (const auto& Input: Command->getInputs()) {
            InputNodes.emplace(Input);
          }
        }
  
        // Build all of the targets that are not an input.
        for (const auto& Command: Context.Manifest->getCommands()) {
          for (const auto& Output: Command->getOutputs()) {
            if (!InputNodes.count(Output)) {
              TargetsToBuild.push_back(Output->getPath());
            }
          }
        }
      }
    }
  
    // Generate an error if there is nothing to build.
    if (TargetsToBuild.empty()) {
      fprintf(stderr, "error: %s: no targets to build\n", getprogname());
      return 1;
    }
  
    // If building multiple targets, do so via a dummy rule to allow them to
    // build concurrently (and without duplicates).
    //
    // FIXME: We should sort out eventually whether the engine itself should
    // support this. It seems like an obvious feature, but it is also trivial
    // for the client to implement on top of the existing API.
    if (TargetsToBuild.size() > 1) {
      // Create a dummy rule to build all targets.
      Context.Engine.addRule({
          "<<build>>",
          [&] (core::BuildEngine& Engine) {
            return BuildTargets(Context, TargetsToBuild);
          },
          [&] (const core::Rule& Rule, const core::ValueType Value) {
            // Always rebuild the dummy rule.
            return false;
          } });
  
      Context.Engine.build("<<build>>");
    } else {
      Context.Engine.build(TargetsToBuild[0]);
    }

    // Ensure the output queue is finished.
    dispatch_sync(Context.OutputQueue, ^() {});

    if (!DumpGraphPath.empty()) {
      Context.Engine.dumpGraphToFile(DumpGraphPath);
    }

    // Close the build profile, if used.
    if (Context.ProfileFP) {
      ::fclose(Context.ProfileFP);

      fprintf(stderr, ("... wrote build profile to '%s', use Chrome's "
                       "about:tracing to view.\n"),
              ProfileFilename.c_str());
    }

    // If the build was cancelled by SIGINT, cause ourself to also die by SIGINT
    // to support proper shell behavior.
    if (Context.WasCancelledBySigint) {
      // Ensure SIGINT action is default.
      struct sigaction Action{};
      Action.sa_handler = SIG_DFL;
      sigaction(SIGINT, &Action, 0);

      kill(getpid(), SIGINT);
      usleep(1000);
      return 2;
    }

    // If nothing was done, print a single message to let the user know we
    // completed successfully.
    if (!Context.Quiet && !Context.NumBuiltCommands) {
      printf("%s: no work to do.\n", getprogname());
    }

    // If there were command failures, return an error status.
    if (Context.NumFailedCommands) {
      fprintf(stderr, "error: %s: build had %d command failures\n",
              getprogname(), Context.NumFailedCommands.load());
      return 1;
    }

    // If we reached here on the first iteration, then we don't need a second
    // and are done.
    if (Iteration == 0)
        break;
  }

  // Return an appropriate exit status.
  return 0;
}
