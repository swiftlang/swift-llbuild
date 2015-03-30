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

#include "llbuild/Basic/Compiler.h"
#include "llbuild/Basic/Hashing.h"
#include "llbuild/Core/BuildDB.h"
#include "llbuild/Core/BuildEngine.h"
#include "llbuild/Core/MakefileDepsParser.h"
#include "llbuild/Ninja/ManifestLoader.h"

#include "CommandUtil.h"

#include <cerrno>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <thread>
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
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "-k <N>",
          "keep building until N commands fail [default=1]");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--no-parallel",
          "build commands serially");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--parallel",
          "build commands in parallel [default]");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--no-regenerate",
          "disable manifest auto-regeneration");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--dump-graph <PATH>",
          "dump build graph to PATH in Graphviz DOT format");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--profile <PATH>",
          "write a build profile trace event file to PATH");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--strict",
          "use strict mode (no bug compatibility)");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--trace <PATH>",
          "trace build engine operation to PATH");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "--quiet",
          "don't show information on executed commands");
  fprintf(stderr, "  %-*s %s\n", OptionWidth, "-v, --verbose",
          "show full invocation for executed commands");
  ::exit(ExitCode);
}

namespace {

/// The build execution queue manages the actual parallel execution of jobs
/// which have been enqueued as a result of command processing.
///
/// This queue guarantees serial execution when only configured with a single
/// lane.
struct BuildExecutionQueue {
  /// The number of lanes the queue was configured with.
  unsigned NumLanes;

  /// A thread for each lane.
  std::vector<std::unique_ptr<std::thread>> Lanes;

  /// The ready queue of jobs to execute.
  std::deque<std::function<void(unsigned)>> ReadyJobs;
  std::mutex ReadyJobsMutex;
  std::condition_variable ReadyJobsCondition;

  /// Use LIFO execution.
  bool UseLIFO;

public:
  BuildExecutionQueue(unsigned NumLanes, bool UseLIFO)
      : NumLanes(NumLanes), UseLIFO(UseLIFO)
  {
    for (unsigned i = 0; i != NumLanes; ++i) {
      Lanes.push_back(std::unique_ptr<std::thread>(
                          new std::thread(
                              &BuildExecutionQueue::executeLane, this, i)));
    }
  }
  ~BuildExecutionQueue() {
    // Shut down the lanes.
    for (unsigned i = 0; i != NumLanes; ++i) {
      addJob({});
    }
    for (unsigned i = 0; i != NumLanes; ++i) {
      Lanes[i]->join();
    }
  }

  void executeLane(unsigned LaneNumber) {
    // Just execute items from the queue until shutdown.
    while (true) {
      // Take a job from the ready queue.
      std::function<void(unsigned)> Job;
      {
        std::unique_lock<std::mutex> Lock(ReadyJobsMutex);

        // While the queue is empty, wait for an item.
        while (ReadyJobs.empty()) {
          ReadyJobsCondition.wait(Lock);
        }

        // Take an item according to the chosen policy.
        if (UseLIFO) {
          Job = ReadyJobs.back();
          ReadyJobs.pop_back();
        } else {
          Job = ReadyJobs.front();
          ReadyJobs.pop_front();
        }
      }

      // If we got an empty job, the queue is shutting down.
      if (!Job)
        break;

      // Process the job.
      Job(LaneNumber);
    }
  }

  void addJob(std::function<void(unsigned)> Job) {
    std::lock_guard<std::mutex> Guard(ReadyJobsMutex);
    ReadyJobs.push_back(Job);
    ReadyJobsCondition.notify_one();
  }
};

struct FileTimestamp {
  uint64_t Seconds;
  uint64_t Nanoseconds;

  bool operator==(const FileTimestamp& RHS) const {
    return Seconds == RHS.Seconds && Nanoseconds == RHS.Nanoseconds;
  }
  bool operator!=(const FileTimestamp& RHS) const {
    return !(*this == RHS);
  }
  bool operator<(const FileTimestamp& RHS) const {
    return (Seconds < RHS.Seconds ||
            (Seconds == RHS.Seconds && Nanoseconds < RHS.Nanoseconds));
  }
  bool operator<=(const FileTimestamp& RHS) const {
    return (Seconds < RHS.Seconds ||
            (Seconds == RHS.Seconds && Nanoseconds <= RHS.Nanoseconds));
  }
  bool operator>(const FileTimestamp& RHS) const {
    return RHS < *this;
  }
  bool operator>=(const FileTimestamp& RHS) const {
    return RHS <= *this;
  }
};

/// Information on an external file stored as part of a build value.
///
/// This structure is intentionally sized to have no packing holes.
struct FileInfo {
  uint64_t Device;
  uint64_t INode;
  uint64_t Size;
  FileTimestamp ModTime;

  /// Check if this is a FileInfo representing a missing file.
  bool isMissing() const {
    // We use an all-zero FileInfo as a sentinel, under the assumption this can
    // never exist in normal circumstances.
    return (Device == 0 && INode == 0 && Size == 0 &&
            ModTime.Seconds == 0 && ModTime.Nanoseconds == 0);
  }

  bool operator==(const FileInfo& RHS) const {
    return (Device == RHS.Device &&
            INode == RHS.INode &&
            Size == RHS.Size &&
            ModTime == RHS.ModTime);
  }
  bool operator!=(const FileInfo& RHS) const {
    return !(*this == RHS);
  }
};

/// Result value that is computed by the rules for input and command files.
class BuildValue {
private:
  // Copying and move assignment are disabled.
  BuildValue(const BuildValue&) LLBUILD_DELETED_FUNCTION;
  void operator=(const BuildValue&) LLBUILD_DELETED_FUNCTION;
  BuildValue &operator=(BuildValue&& RHS) LLBUILD_DELETED_FUNCTION;

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

  /// The number of attached output infos.
  const uint32_t NumOutputInfos = 0;

  union {
    /// The file info for the rule output, for existing inputs and successful
    /// commands with a single output.
    FileInfo AsOutputInfo;

    /// The file info for successful commands with multiple outputs.
    FileInfo* AsOutputInfos;
  } ValueData;

  /// The command hash, for successful commands.
  uint64_t CommandHash;

private:
  BuildValue() {}
  BuildValue(BuildValueKind Kind)
    : Kind(Kind), NumOutputInfos(0), CommandHash(0)
  {
  }
  BuildValue(BuildValueKind Kind, const FileInfo& OutputInfo,
             uint64_t CommandHash = 0)
    : Kind(Kind), NumOutputInfos(1), CommandHash(CommandHash)
  {
    ValueData.AsOutputInfo = OutputInfo;
  }
  BuildValue(BuildValueKind Kind, const FileInfo* OutputInfos,
             uint32_t NumOutputInfos, uint64_t CommandHash = 0)
    : Kind(Kind), NumOutputInfos(NumOutputInfos), CommandHash(CommandHash)
  {
    ValueData.AsOutputInfos = new FileInfo[NumOutputInfos];
    for (uint32_t i = 0; i != NumOutputInfos; ++i) {
      ValueData.AsOutputInfos[i] = OutputInfos[i];
    }
  }

public:
  // Build values can only be moved via construction, not copied.
  BuildValue(BuildValue&& RHS) {
    memcpy(this, &RHS, sizeof(RHS));
    memset(&RHS, 0, sizeof(RHS));
  }
  ~BuildValue() {
    if (hasMultipleOutputs()) {
      delete[] ValueData.AsOutputInfos;
    }
  }

  static BuildValue makeExistingInput(const FileInfo& OutputInfo) {
    return BuildValue(BuildValueKind::ExistingInput, OutputInfo);
  }
  static BuildValue makeMissingInput() {
    return BuildValue(BuildValueKind::MissingInput);
  }
  static BuildValue makeSuccessfulCommand(const FileInfo& OutputInfo,
                                          uint64_t CommandHash) {
    return BuildValue(BuildValueKind::SuccessfulCommand, OutputInfo,
                      CommandHash);
  }
  static BuildValue makeSuccessfulCommand(const FileInfo* OutputInfos,
                                          uint32_t NumOutputInfos,
                                          uint64_t CommandHash) {
    // This ctor function should only be used for multiple outputs.
    assert(NumOutputInfos > 1);
    return BuildValue(BuildValueKind::SuccessfulCommand, OutputInfos,
                      NumOutputInfos, CommandHash);
  }
  static BuildValue makeFailedCommand() {
    return BuildValue(BuildValueKind::FailedCommand);
  }
  static BuildValue makeSkippedCommand() {
    return BuildValue(BuildValueKind::SkippedCommand);
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

  bool hasMultipleOutputs() const {
    return NumOutputInfos > 1;
  }

  unsigned getNumOutputs() const {
    assert((isExistingInput() || isSuccessfulCommand()) &&
           "invalid call for value kind");
    return NumOutputInfos;
  }

  const FileInfo& getOutputInfo() const {
    assert((isExistingInput() || isSuccessfulCommand()) &&
           "invalid call for value kind");
    assert(!hasMultipleOutputs() &&
           "invalid call on result with multiple outputs");
    return ValueData.AsOutputInfo;
  }

  const FileInfo& getNthOutputInfo(unsigned N) const {
    assert((isExistingInput() || isSuccessfulCommand()) &&
           "invalid call for value kind");
    assert(N < getNumOutputs());
    if (hasMultipleOutputs()) {
      return ValueData.AsOutputInfos[N];
    } else {
      assert(N == 0);
      return ValueData.AsOutputInfo;
    }
  }

  uint64_t getCommandHash() const {
    assert(isSuccessfulCommand() && "invalid call for value kind");
    return CommandHash;
  }

  static BuildValue fromValue(const core::ValueType& Value) {
    BuildValue Result;
    assert(Value.size() >= sizeof(Result));
    memcpy(&Result, Value.data(), sizeof(Result));

    // If this result has multiple output values, deserialize them properly.
    if (Result.NumOutputInfos > 1) {
      assert(Value.size() == (sizeof(Result) +
                              Result.NumOutputInfos * sizeof(FileInfo)));
      Result.ValueData.AsOutputInfos = new FileInfo[Result.NumOutputInfos];
      memcpy(Result.ValueData.AsOutputInfos,
             Value.data() + sizeof(Result),
             Result.NumOutputInfos * sizeof(FileInfo));
    } else {
      assert(Value.size() == sizeof(Result));
    }

    return Result;
  }

  core::ValueType toValue() {
    if (NumOutputInfos > 1) {
      // FIXME: This could be packed one entry tighter.
      std::vector<uint8_t> Result(sizeof(*this) +
                                  NumOutputInfos * sizeof(FileInfo));
      memcpy(Result.data(), this, sizeof(*this));
      memcpy(Result.data() + sizeof(*this), ValueData.AsOutputInfos,
             NumOutputInfos * sizeof(FileInfo));
      return Result;
    } else {
      std::vector<uint8_t> Result(sizeof(*this));
      memcpy(Result.data(), this, sizeof(*this));
      return Result;
    }
  }
};

struct NinjaBuildEngineDelegate : public core::BuildEngineDelegate {
  class BuildContext* Context = nullptr;

  virtual core::Rule lookupRule(const core::KeyType& Key) override;

  virtual void cycleDetected(const std::vector<core::Rule*>& Items) override;
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

  /// Whether commands should print status information.
  bool Quiet = false;
  /// Whether the build is being "simulated", in which case commands won't be
  /// run and inputs will be assumed to exist.
  bool Simulate = false;
  /// Whether to use strict mode.
  bool Strict = false;
  /// Whether output should use verbose mode.
  bool Verbose = false;
  /// The number of failed commands to tolerate, or 0 if unlimited
  unsigned NumFailedCommandsToTolerate = 1;

  /// The build profile output file.
  FILE *ProfileFP = nullptr;

  /// Whether the build has been cancelled or not.
  std::atomic<bool> IsCancelled{false};

  /// Whether the build was cancelled by SIGINT.
  std::atomic<bool> WasCancelledBySigint{false};
  std::atomic<bool> WasCancelledByCycle{false};

  /// The number of inputs used during the build.
  unsigned NumBuiltInputs{0};
  /// The number of commands executed during the build
  unsigned NumBuiltCommands{0};
  /// The number of output commands written, for numbering purposes.
  unsigned NumOutputDescriptions{0};
  /// The number of failed commands.
  std::atomic<unsigned> NumFailedCommands{0};

  /// @name Status Reporting Command Counts
  /// @{

  /// The number of commands being scanned.
  std::atomic<unsigned> NumCommandsScanning{0};
  /// The number of commands that have ever been started.
  std::atomic<unsigned> NumCommandsStarted{0};
  /// The number of commands that have been completed.
  std::atomic<unsigned> NumCommandsCompleted{0};
  /// The number of commands being executed.
  std::atomic<unsigned> NumCommandsExecuting{0};
  /// The number of commands that were updated (started, but didn't actually run
  /// the command).
  std::atomic<unsigned> NumCommandsUpdated{0};

  /// @}

  /// The serial queue we used to order output consistently.
  dispatch_queue_t OutputQueue;

  /// The limited queue we use to execute parallel jobs.
  std::unique_ptr<BuildExecutionQueue> JobQueue;

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

  void reportMissingInput(const ninja::Node* Node) {
    // We simply report the missing input here, the build will be cancelled when
    // a rule sees it missing.
    dispatch_async(OutputQueue, ^() {
        fprintf(stderr,
                "error: %s: missing input '%s' and no rule to build it\n",
                getprogname(), Node->getPath().c_str());
      });
  }

  void incrementFailedCommands() {
    // Update our count of the number of failed commands.
    unsigned NumFailedCommands = ++this->NumFailedCommands;

    // Cancel the build, if the number of command failures exceeds the
    // number to continue past.
    if (NumFailedCommandsToTolerate != 0 &&
        NumFailedCommands == NumFailedCommandsToTolerate) {
      dispatch_async(OutputQueue, ^() {
          fprintf(stderr, "error: %s: stopping build due to command failures\n",
                  getprogname());
        });
      IsCancelled = true;
    }
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
    assert(Info_Out->isMissing());
    return false;
  }

  Info_Out->Device = Buf.st_dev;
  Info_Out->INode = Buf.st_ino;
  Info_Out->Size = Buf.st_size;
  Info_Out->ModTime.Seconds = Buf.st_mtimespec.tv_sec;
  Info_Out->ModTime.Nanoseconds = Buf.st_mtimespec.tv_nsec;

  // Enforce we never accidentally create our sentinel missing file value.
  if (Info_Out->isMissing()) {
    Info_Out->ModTime.Nanoseconds = 1;
  }

  // Verify we didn't truncate any values.
  assert(Info_Out->Device == (unsigned)Buf.st_dev &&
         Info_Out->INode == (unsigned)Buf.st_ino &&
         Info_Out->Size == (unsigned)Buf.st_size &&
         Info_Out->ModTime.Seconds == (unsigned)Buf.st_mtimespec.tv_sec &&
         Info_Out->ModTime.Nanoseconds == (unsigned)Buf.st_mtimespec.tv_nsec);

  return true;
}

core::Task* BuildCommand(BuildContext& Context, ninja::Command* Command) {
  struct NinjaCommandTask : core::Task {
    BuildContext& Context;
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
    FileTimestamp NewestModTime{ 0, 0 };

    NinjaCommandTask(BuildContext& Context, ninja::Command* Command)
      : Task("ninja-command"), Context(Context), Command(Command) {
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

      // All direct inputs to NinjaCommandTask objects should be singleton
      // values.
      assert(!Value.hasMultipleOutputs());

      // If the value is not an existing input or a successful command, then we
      // shouldn't run this command.
      if (!Value.isExistingInput() && !Value.isSuccessfulCommand()) {
        ShouldSkip = true;
        if (Value.isMissingInput()) {
          HasMissingInput = true;

          Context.reportMissingInput(Command->getInputs()[InputID]);
        }
      } else {
        // Otherwise, track the information used to determine if we can just
        // update the command instead of running it.
        const FileInfo& OutputInfo = Value.getOutputInfo();

        // If there is a missing input file (from a successful command), we
        // always need to run the command.
        if (OutputInfo.isMissing()) {
          CanUpdateIfNewer = false;
        } else {
          // Otherwise, keep track of the newest input.
          if (OutputInfo.ModTime > NewestModTime) {
            NewestModTime = OutputInfo.ModTime;
          }
        }
      }
    }

    bool isImmediatelyCyclicInput(const ninja::Node *Node) const {
      for (auto* Output: Command->getOutputs())
        if (Node == Output)
          return true;
      return false;
    }

    void completeTask(BuildValue&& Result, bool ForceChange=false) {
      // Update our count of actual commands executing.
      if (Command->getRule() != Context.Manifest->getPhonyRule())
        --Context.NumCommandsExecuting;

      Context.Engine.taskIsComplete(this, Result.toValue(), ForceChange);
    }

    virtual void start(core::BuildEngine& engine) override {
      // Update our count of actual commands started and executing.
      if (Command->getRule() != Context.Manifest->getPhonyRule()) {
        ++Context.NumCommandsStarted;
        ++Context.NumCommandsExecuting;
      }

      // If this is a phony rule, ignore any immediately cyclic dependencies in
      // non-strict mode, which are generated frequently by CMake, but can be
      // ignored by Ninja. See https://github.com/martine/ninja/issues/935.
      //
      // FIXME: Find a way to harden this more, or see if we can just get CMake
      // to fix it.
      bool isPhony = Command->getRule() == Context.Manifest->getPhonyRule();

      // Request all of the explicit and implicit inputs (the only difference
      // between them is that implicit inputs do not appear in ${in} during
      // variable expansion, but that has already been performed).
      unsigned ID = 0;
      for (auto it = Command->explicitInputs_begin(),
             ie = Command->explicitInputs_end(); it != ie; ++it) {
        if (!Context.Strict && isPhony && isImmediatelyCyclicInput(*it))
          continue;

        engine.taskNeedsInput(this, (*it)->getPath(), ID++);
      }
      for (auto it = Command->implicitInputs_begin(),
             ie = Command->implicitInputs_end(); it != ie; ++it) {
        if (!Context.Strict && isPhony && isImmediatelyCyclicInput(*it))
          continue;

        engine.taskNeedsInput(this, (*it)->getPath(), ID++);
      }

      // Request all of the order-only inputs.
      for (auto it = Command->orderOnlyInputs_begin(),
             ie = Command->orderOnlyInputs_end(); it != ie; ++it) {
        if (!Context.Strict && isPhony && isImmediatelyCyclicInput(*it))
          continue;

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

    /// Compute the output result for the command.
    BuildValue computeCommandResult(uint64_t CommandHash) const {
      unsigned NumOutputs = Command->getOutputs().size();
      if (NumOutputs == 1) {
        FileInfo OutputInfo;
        GetStatInfoForNode(Command->getOutputs()[0], &OutputInfo);
        return BuildValue::makeSuccessfulCommand(OutputInfo, CommandHash);
      } else {
        std::vector<FileInfo> OutputInfos(NumOutputs);
        for (unsigned i = 0; i != NumOutputs; ++i) {
          GetStatInfoForNode(Command->getOutputs()[i], &OutputInfos[i]);
        }
        return BuildValue::makeSuccessfulCommand(OutputInfos.data(), NumOutputs,
                                                 CommandHash);
      }
    }

    /// Check if it is legal to only update the result (versus rerunning)
    /// because the outputs are newer than all of the inputs.
    bool canUpdateIfNewerWithResult(const BuildValue& Result) {
      assert(Result.isSuccessfulCommand());

      // Check each output.
      for (unsigned i = 0, e = Result.getNumOutputs(); i != e; ++i) {
        const FileInfo& OutputInfo = Result.getNthOutputInfo(i);

        // If the output is missing, we need to rebuild.
        if (OutputInfo.isMissing())
          return false;

        // Check if the output is actually newer than the most recent input.
        //
        // In strict mode, we use a strict "newer-than" check here, to guarantee
        // correctness in the face of equivalent timestamps. This is
        // particularly important on OS X, which has a low resolution mtime.
        //
        // However, in non-strict mode, we need to be compatible with Ninja
        // here, because there are some very important uses cases where this
        // behavior is relied on. One major example is CMake's initial
        // configuration checks using Ninja -- if this is not in place, those
        // rules will try and rerun the generator of the "TRY_COMPILE" steps,
        // and will enter an infinite reconfiguration loop. See also:
        //
        // See: http://www.cmake.org/Bug/view.php?id=15456
        if (Context.Strict) {
          if (OutputInfo.ModTime <= NewestModTime)
            return false;
        } else {
          if (OutputInfo.ModTime < NewestModTime)
            return false;
        }
      }

      return true;
    }

    virtual void inputsAvailable(core::BuildEngine& engine) override {
      // If the build is cancelled, skip everything.
      if (Context.IsCancelled) {
        return completeTask(BuildValue::makeSkippedCommand());
      }

      // Ignore phony commands.
      //
      // FIXME: Is it right to bring this up-to-date when one of the inputs
      // indicated a failure? It probably doesn't matter.
      if (Command->getRule() == Context.Manifest->getPhonyRule()) {
        // Get the result.
        BuildValue Result = computeCommandResult(/*CommandHash=*/0);

        // If any output is missing, then we always want to force the change to
        // propagate.
        bool ForceChange = false;
        for (unsigned i = 0, e = Result.getNumOutputs(); i != e; ++i) {
            if (Result.getNthOutputInfo(i).isMissing()) {
                ForceChange = true;
                break;
            }
        }

        return completeTask(std::move(Result), ForceChange);
      }

      // If it is legal to simply update the command, then if the command output
      // exists and is newer than all of the inputs, don't actually run the
      // command (just bring it up-to-date).
      if (CanUpdateIfNewer) {
        // If this isn't a generator command and its command hash differs, we
        // can't update it.
        uint64_t CommandHash = basic::HashString(Command->getCommandString());
        if (!Command->hasGeneratorFlag() &&
            (!HasPriorResult || PriorCommandHash != CommandHash))
          CanUpdateIfNewer = false;

        if (CanUpdateIfNewer) {
          BuildValue Result = computeCommandResult(CommandHash);

          if (canUpdateIfNewerWithResult(Result)) {
            // Update the count of the number of commands which have been
            // updated without being rerun.
            ++Context.NumCommandsUpdated;

            return completeTask(std::move(Result));
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
        return completeTask(BuildValue::makeSkippedCommand());
      }

      // If not simulating, but this command should be skipped, then do nothing.
      if (ShouldSkip) {
        // If this command had a failed input, treat it as having failed.
        if (HasMissingInput) {
          // Take care to not rely on the ``this`` object, which may disappear
          // before the queue executes this block.
          ninja::Command* LocalCommand(Command);

          dispatch_async(Context.OutputQueue, ^() {
              fprintf(stderr,
                      "error: %s: cannot build '%s' due to missing input\n",
                      getprogname(),
                      LocalCommand->getOutputs()[0]->getPath().c_str());
            });

          // Update the count of failed commands.
          Context.incrementFailedCommands();
        }

        return completeTask(BuildValue::makeSkippedCommand());
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

    static unsigned getNumPossibleMaxCommands(BuildContext& Context) {
      // Compute the "possible" number of maximum commands that will be
      // run. This is only the "possible" max because we can start running
      // commands before dependency scanning is complete -- we include the
      // number of commands that are being scanned so that this number will
      // always be greater than the number of commands that have been executed
      // until the very last command is run.
      int TotalPossibleMaxCommands =
        Context.NumCommandsCompleted + Context.NumCommandsScanning;

      // Compute the number of max commands to show, subtracting out all the
      // commands that we avoided running.
      //
      // We need to do some algebra in order to compute this number because we
      // need to combine the statistics from the BuildEngine status mechanism
      // with our own knowledge of what commands have been run so far and what
      // have been skipped or updated.
      
      // Compute the number of completed commands that were never even executed,
      // by subtracting the number of completed commands that *were* executed.
      int NumCompletedCommandsNeverExecuted = Context.NumCommandsCompleted -
        (Context.NumCommandsStarted - Context.NumCommandsExecuting);

      // Then the number of max commands to show is the total max possible
      // commands, minus the commands that were never executed and the commands
      // that were updated.
      int PossibleMaxCommands = TotalPossibleMaxCommands -
        (NumCompletedCommandsNeverExecuted + Context.NumCommandsUpdated);

      return PossibleMaxCommands;
    }

    static void writeDescription(BuildContext& Context,
                                 ninja::Command* Command) {
      const std::string& Description =
        Context.Verbose ? Command->getCommandString() :
        Command->getEffectiveDescription();
      fprintf(stderr, "[%d/%d] %s\n", ++Context.NumOutputDescriptions,
              getNumPossibleMaxCommands(Context), Description.c_str());
    }

    void executeCommand() {
      // If the build is cancelled, skip the job.
      if (Context.IsCancelled) {
        return completeTask(BuildValue::makeSkippedCommand());
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

      // Actually run the command.
      if (!spawnAndWaitForCommand()) {
        // If the command failed, complete the task with the failed result and
        // always propagate.
        return completeTask(BuildValue::makeFailedCommand(),
                            /*ForceChange=*/true);
      }

      // Otherwise, the command succeeded so process the dependencies.
      processDiscoveredDependencies();

      // Complete the task with a successful value.
      //
      // We always restat the output, but we honor Ninja's restat flag by
      // forcing downstream propagation if it isn't set.
      uint64_t CommandHash = basic::HashString(Command->getCommandString());
      BuildValue Result = computeCommandResult(CommandHash);
      return completeTask(std::move(Result),
                          /*ForceChange=*/!Command->hasRestatFlag());
    }

    /// Execute the command process and wait for it to complete.
    ///
    /// \returns True if the command succeeded.
    bool spawnAndWaitForCommand() const {
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
        // FIXME: Error handling.
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

          if (Signal == SIGINT)
            return false;

          dispatch_async(Context.OutputQueue, ^() {
              std::cerr << "  ... process exited with signal: "
                        << Signal << "\n";
            });
        } else {
          // Report the exit status.
          int ExitStatus = WEXITSTATUS(Status);
          dispatch_async(Context.OutputQueue, ^() {
              std::cerr << "  ... process returned error status: "
                        << ExitStatus << "\n";
            });

          // Update the count of failed commands.
          Context.incrementFailedCommands();
        }

        return false;
      }

      return true;
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
          // If the file is missing, just ignore it for consistency with Ninja
          // (when using stored deps) in non-strict mode.
          if (!Context.Strict)
              return;

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

  return Context.Engine.registerTask(new NinjaCommandTask(Context, Command));
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

    virtual void inputsAvailable(core::BuildEngine& engine) override {
      ++Context.NumBuiltInputs;

      if (Context.Simulate) {
        engine.taskIsComplete(
          this, BuildValue::makeExistingInput({}).toValue());
        return;
      }

      FileInfo OutputInfo;
      if (!GetStatInfoForNode(Node, &OutputInfo)) {
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

core::Task* SelectCompositeBuildResult(BuildContext& Context,
                                       ninja::Command* Command,
                                       unsigned InputIndex,
                                       const core::KeyType& CompositeRuleName) {
  struct SelectResultTask : core::Task {
    const BuildContext& Context;
    const ninja::Command* Command;
    const unsigned InputIndex;
    const core::KeyType CompositeRuleName;
    const core::ValueType *CompositeValueData = nullptr;

    SelectResultTask(BuildContext& Context, ninja::Command* Command,
                     unsigned InputIndex,
                     const core::KeyType& CompositeRuleName)
      : Task("ninja-select-result"), Context(Context), Command(Command),
        InputIndex(InputIndex), CompositeRuleName(CompositeRuleName) { }

    virtual void start(core::BuildEngine& engine) override {
      // Request the composite input.
      engine.taskNeedsInput(this, CompositeRuleName, 0);
    }

    virtual void provideValue(core::BuildEngine& engine, uintptr_t InputID,
                              const core::ValueType& ValueData) override {
      CompositeValueData = &ValueData;
    }

    virtual void inputsAvailable(core::BuildEngine& engine) override {
      // Construct the appropriate build value from the result.
      assert(CompositeValueData);
      BuildValue Value(BuildValue::fromValue(*CompositeValueData));

      // If the input was a failed or skipped command, propagate that result.
      if (Value.isFailedCommand() || Value.isSkippedCommand()) {
        engine.taskIsComplete(this, Value.toValue(), /*ForceChange=*/true);
      } else {
        // FIXME: We don't try and set this in response to the restat flag on
        // the incoming command, because it doesn't generally work -- the output
        // will just honor update-if-newer and still not run. We need to move to
        // a different model for handling restat = 0 to get this to work
        // properly.
        bool ForceChange = false;

        // Otherwise, the value should be a successful command with file info
        // for each output.
        assert(Value.isSuccessfulCommand() && Value.hasMultipleOutputs() &&
               InputIndex < Value.getNumOutputs());

        // The result is the InputIndex-th element, and the command hash is
        // propagated.
        engine.taskIsComplete(
          this, BuildValue::makeSuccessfulCommand(
            Value.getNthOutputInfo(InputIndex),
            Value.getCommandHash()).toValue(),
          ForceChange);
      }
    }
  };

  return Context.Engine.registerTask(
    new SelectResultTask(Context, Command, InputIndex, CompositeRuleName));
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

  // Check the timestamps on each of the outputs.
  for (unsigned i = 0, e = Command->getOutputs().size(); i != e; ++i) {
    // Always rebuild if the output is missing.
    FileInfo Info;
    if (!GetStatInfoForNode(Command->getOutputs()[i], &Info))
      return false;

    // Otherwise, the result is valid if file information has not changed.
    //
    // Note that we may still decide not to actually run the command based on
    // the update-if-newer handling, but it does require running the task.
    if (Value.getNthOutputInfo(i) != Info)
      return false;
  }

  return true;
}

static bool SelectCompositeIsResultValid(ninja::Command* Command,
                                         const core::ValueType& ValueData) {
  BuildValue Value = BuildValue::fromValue(ValueData);

  // If the prior value wasn't for a successful command, recompute.
  if (!Value.isSuccessfulCommand())
    return false;

  // If the command's signature has changed since it was built, rebuild. This is
  // important for ensuring that we properly reevaluate the select rule when
  // it's incoming composite rule no longer exists.
  if (Value.getCommandHash() != basic::HashString(Command->getCommandString()))
    return false;

  // Otherwise, this result is always valid.
  return true;
}

static void UpdateCommandStatus(BuildContext& Context,
                                ninja::Command* Command,
                                core::Rule::StatusKind Status) {
  // Ignore phony rules.
  if (Command->getRule() == Context.Manifest->getPhonyRule())
    return;

  // Track the number of commands which are currently being scanned along with
  // the total number of completed commands.
  if (Status == core::Rule::StatusKind::IsScanning) {
    ++Context.NumCommandsScanning;
  } else {
    assert(Status == core::Rule::StatusKind::IsComplete);
    --Context.NumCommandsScanning;
    ++Context.NumCommandsCompleted;
  }
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

void NinjaBuildEngineDelegate::cycleDetected(
    const std::vector<core::Rule*>& Cycle) {
  // Report the cycle.
  dispatch_sync(Context->OutputQueue, ^() {
      fprintf(stderr, "error: %s: cycle detected among targets:",
              getprogname());
      bool First = true;
      for (const auto& Rule: Cycle) {
        fprintf(stderr, "%s \"%s\"", First ? "" : " ->",
                Rule->Key.c_str());
        First = false;
      }
      fprintf(stderr, "\n");
    });

  // Cancel the build.
  Context->IsCancelled = true;
  Context->WasCancelledByCycle = true;
}

}

int commands::ExecuteNinjaBuildCommand(std::vector<std::string> Args) {
  std::string ChdirPath = "";
  std::string DBFilename = "build.db";
  std::string ManifestFilename = "build.ninja";
  std::string DumpGraphPath, ProfileFilename, TraceFilename;

  // Create a context for the build.
  bool AutoRegenerateManifest = true;
  bool Quiet = false;
  bool Simulate = false;
  bool Strict = false;
  bool UseLIFOExecutionQueue = false;
  bool UseParallelBuild = true;
  bool Verbose = false;
  unsigned NumFailedCommandsToTolerate = 1;

  while (!Args.empty() && Args[0][0] == '-') {
    const std::string Option = Args[0];
    Args.erase(Args.begin());

    if (Option == "--")
      break;

    if (Option == "--help") {
      usage(/*ExitCode=*/0);
    } else if (Option == "--simulate") {
      Simulate = true;
    } else if (Option == "--lifo") {
      UseLIFOExecutionQueue = true;
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
    } else if (Option == "-k") {
      if (Args.empty()) {
        fprintf(stderr, "error: %s: missing argument to '%s'\n\n",
                ::getprogname(), Option.c_str());
        usage();
      }
      char *End;
      NumFailedCommandsToTolerate = ::strtol(Args[0].c_str(), &End, 10);
      if (*End != '\0') {
          fprintf(stderr, "error: %s: invalid argument '%s' to '%s'\n\n",
                  getprogname(), Args[0].c_str(), Option.c_str());
          usage();
      }
      Args.erase(Args.begin());
    } else if (Option == "--no-parallel") {
      UseParallelBuild = false;
    } else if (Option == "--parallel") {
      UseParallelBuild = true;
    } else if (Option == "--no-regenerate") {
      AutoRegenerateManifest = false;
    } else if (Option == "--profile") {
      if (Args.empty()) {
        fprintf(stderr, "error: %s: missing argument to '%s'\n\n",
                ::getprogname(), Option.c_str());
        usage();
      }
      ProfileFilename = Args[0];
      Args.erase(Args.begin());
    } else if (Option == "--strict") {
      Strict = true;
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

    // Print a message about the changed directory. The exact format here is
    // important, it is recognized by other tools (like Emacs).
    fprintf(stdout, "%s: Entering directory `%s'\n", getprogname(),
            ChdirPath.c_str());
    fflush(stdout);
  }

  // Run up to two iterations, the first one loads the manifest and rebuilds it
  // if necessary, the second only runs if the manifest needs to be reloaded.
  //
  // This is somewhat inefficient in the case where the manifest needs to be
  // reloaded (we reopen the database, for example), but we don't expect that to
  // be a common case spot in practice.
  for (int Iteration = 0; Iteration != 2; ++Iteration) {
    BuildContext Context;

    Context.NumFailedCommandsToTolerate = NumFailedCommandsToTolerate;
    Context.Quiet = Quiet;
    Context.Simulate = Simulate;
    Context.Strict = Strict;
    Context.Verbose = Verbose;

    // Create the job queue to use.
    unsigned NumJobs;
    if (!UseParallelBuild) {
      NumJobs = 1;
    } else {
      long NumCPUs = sysconf(_SC_NPROCESSORS_ONLN);
      if (NumCPUs < 0) {
        fprintf(stderr, "error: %s: unable to detect number of CPUs: %s\n",
                getprogname(), strerror(errno));
        return 1;
      }

      NumJobs = NumCPUs + 2;
    }
    Context.JobQueue.reset(new BuildExecutionQueue(
                               NumJobs, UseLIFOExecutionQueue));

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
    for (auto& CommandOwner: Context.Manifest->getCommands()) {
      auto* Command = CommandOwner.get();

      // If this command has a single output, create the trivial rule.
      if (Command->getOutputs().size() == 1) {
        Context.Engine.addRule({
            Command->getOutputs()[0]->getPath(),
            [=, &Context] (core::BuildEngine& Engine) {
              return BuildCommand(Context, Command);
            },
              [=, &Context] (const core::Rule& Rule,
                                       const core::ValueType Value) {
              // If simulating, assume cached results are valid.
              if (Context.Simulate)
                return true;

              return BuildCommandIsResultValid(Command, Value);
            },
            [=, &Context](core::Rule::StatusKind Status) {
              UpdateCommandStatus(Context, Command, Status);
            } });
        continue;
      }

      // Otherwise, create a composite rule group for the multiple outputs.

      // Create a signature for the composite rule.
      //
      // FIXME: Make efficient.
      std::string CompositeRuleName = "";
      for (auto& Output: Command->getOutputs()) {
        if (!CompositeRuleName.empty())
          CompositeRuleName += "&&";
        CompositeRuleName += Output->getPath();
      }

      // Add the composite rule, which will run the command and build all
      // outputs.
      Context.Engine.addRule({
          CompositeRuleName,
          [=, &Context] (core::BuildEngine& Engine) {
            return BuildCommand(Context, Command);
          },
          [=, &Context] (const core::Rule& Rule, const core::ValueType Value) {
            // If simulating, assume cached results are valid.
            if (Context.Simulate)
              return true;

            return BuildCommandIsResultValid(Command, Value);
          },
          [=, &Context](core::Rule::StatusKind Status) {
            UpdateCommandStatus(Context, Command, Status);
          } });

      // Create the per-output selection rules that select the individual output
      // result from the composite result.
      for (unsigned i = 0, e = Command->getOutputs().size(); i != e; ++i) {
        Context.Engine.addRule({
            Command->getOutputs()[i]->getPath(),
            [=, &Context] (core::BuildEngine& Engine) {
              return SelectCompositeBuildResult(Context, Command, i,
                                                CompositeRuleName);
            },
            [=, &Context] (const core::Rule& Rule, const core::ValueType Value) {
              // If simulating, assume cached results are valid.
              if (Context.Simulate)
                return true;

              return SelectCompositeIsResultValid(Command, Value);
            } });
      }
    }

    // If this is the first iteration, build the manifest, unless disabled.
    if (AutoRegenerateManifest && Iteration == 0) {
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

    // If the build was stopped because of a cycle, return an error status.
    if (Context.WasCancelledByCycle) {
      return 1;
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
