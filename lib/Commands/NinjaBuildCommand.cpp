//===-- NinjaBuildCommand.cpp ---------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "NinjaBuildCommand.h"

#include "llbuild/Basic/Compiler.h"
#include "llbuild/Basic/CrossPlatformCompatibility.h"
#include "llbuild/Basic/ExecutionQueue.h"
#include "llbuild/Basic/FileInfo.h"
#include "llbuild/Basic/Hashing.h"
#include "llbuild/Basic/PlatformUtility.h"
#include "llbuild/Basic/SerialQueue.h"
#include "llbuild/Basic/Version.h"

#include "llbuild/Commands/Commands.h"

#include "llbuild/Core/BuildDB.h"
#include "llbuild/Core/BuildEngine.h"
#include "llbuild/Core/MakefileDepsParser.h"

#include "llbuild/Ninja/ManifestLoader.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include "CommandLineStatusOutput.h"
#include "CommandUtil.h"

#include <atomic>
#include <future>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>

#include <fcntl.h>
#include <signal.h>
#if defined(_WIN32)
#include <process.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <sys/stat.h>

using namespace llbuild;
using namespace llbuild::basic;
using namespace llbuild::commands;

#if !defined(_WIN32)
extern "C" {
  extern char **environ;
}
#endif

static uint64_t getTimeInMicroseconds() {
  auto now = std::chrono::high_resolution_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
}

static std::string getFormattedString(const char* fmt, va_list ap1) {
  va_list ap2;
  va_copy(ap2, ap1);
  int count = vsnprintf(NULL, 0, fmt, ap1);
  if (count <= 0) {
    return "unable to format message";
  }

  std::string result = std::string(count, '\0');
  if (vsnprintf(const_cast<char *>(result.c_str()), count + 1, fmt, ap2) < 0) {
    return "unable to format message";
  }

  return result;
}

static std::string getFormattedString(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::string result = getFormattedString(fmt, ap);
  va_end(ap);
  return result;
}

static void usage(int exitCode=1) {
  int optionWidth = 20;
  fprintf(stderr, "Usage: %s ninja build [options] [<targets>...]\n",
          getProgramName());
  fprintf(stderr, "\nOptions:\n");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--help",
          "show this help message and exit");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--version",
          "print the Ninja compatible version number");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--simulate",
          "simulate the build, assuming commands succeed");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "-C, --chdir <PATH>",
          "change directory to PATH before anything else");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--no-db",
          "do not persist build results");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--db <PATH>",
          "persist build results at PATH [default='build.db']");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "-f <PATH>",
          "load the manifest at PATH [default='build.ninja']");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "-k <N>",
          "keep building until N commands fail [default=1]");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "-t, --tool <TOOL>",
          "run a ninja tool. use 'list' to list available tools.");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "-j, --jobs <JOBS>",
          "number of jobs to build in parallel [default=cpu dependent]");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--scheduler <SCHEDULER>",
          "set scheduler algorithm");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--no-regenerate",
          "disable manifest auto-regeneration");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--dump-graph <PATH>",
          "dump build graph to PATH in Graphviz DOT format");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--profile <PATH>",
          "write a build profile trace event file to PATH");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--strict",
          "use strict mode (no bug compatibility)");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--trace <PATH>",
          "trace build engine operation to PATH");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "--quiet",
          "don't show information on executed commands");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "-v, --verbose",
          "show full invocation for executed commands");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "-l <N>",
          "start jobs only when load average is below N [not implemented]");
  fprintf(stderr, "  %-*s %s\n", optionWidth, "-d <TOOL>",
          "enable debugging tool TOOL. 'list' for available [not implemented]");
  ::exit(exitCode);
}

namespace {

/// Result value that is computed by the rules for input and command files.
class BuildValue {
private:
  // Copying and move assignment are disabled.
  BuildValue(const BuildValue&) LLBUILD_DELETED_FUNCTION;
  void operator=(const BuildValue&) LLBUILD_DELETED_FUNCTION;
  BuildValue &operator=(BuildValue&& rhs) LLBUILD_DELETED_FUNCTION;

public:
  static const int currentSchemaVersion = 3;

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
  BuildValueKind kind;

  /// The number of attached output infos.
  const uint32_t numOutputInfos = 0;

  union {
    /// The file info for the rule output, for existing inputs and successful
    /// commands with a single output.
    FileInfo asOutputInfo;

    /// The file info for successful commands with multiple outputs.
    FileInfo* asOutputInfos;
  } valueData;

  /// The command hash, for successful commands.
  CommandSignature commandHash;

private:
  BuildValue() {}
  BuildValue(BuildValueKind kind)
    : kind(kind), numOutputInfos(0), commandHash(0)
  {
  }
  BuildValue(BuildValueKind kind, const FileInfo& outputInfo,
             CommandSignature commandHash = CommandSignature())
    : kind(kind), numOutputInfos(1), commandHash(commandHash)
  {
    valueData.asOutputInfo = outputInfo;
  }
  BuildValue(BuildValueKind kind, const FileInfo* outputInfos,
             uint32_t numOutputInfos, CommandSignature commandHash = CommandSignature())
    : kind(kind), numOutputInfos(numOutputInfos), commandHash(commandHash)
  {
    valueData.asOutputInfos = new FileInfo[numOutputInfos];
    for (uint32_t i = 0; i != numOutputInfos; ++i) {
      valueData.asOutputInfos[i] = outputInfos[i];
    }
  }

public:
  // Build values can only be moved via construction, not copied.
  BuildValue(BuildValue&& rhs) {
    memcpy(this, &rhs, sizeof(rhs));
    memset(&rhs, 0, sizeof(rhs));
  }
  ~BuildValue() {
    if (hasMultipleOutputs()) {
      delete[] valueData.asOutputInfos;
    }
  }

  static BuildValue makeExistingInput(const FileInfo& outputInfo) {
    return BuildValue(BuildValueKind::ExistingInput, outputInfo);
  }
  static BuildValue makeMissingInput() {
    return BuildValue(BuildValueKind::MissingInput);
  }
  static BuildValue makeSuccessfulCommand(const FileInfo& outputInfo,
                                          CommandSignature commandHash) {
    return BuildValue(BuildValueKind::SuccessfulCommand, outputInfo,
                      commandHash);
  }
  static BuildValue makeSuccessfulCommand(const FileInfo* outputInfos,
                                          uint32_t numOutputInfos,
                                          CommandSignature commandHash) {
    // This ctor function should only be used for multiple outputs.
    assert(numOutputInfos > 1);
    return BuildValue(BuildValueKind::SuccessfulCommand, outputInfos,
                      numOutputInfos, commandHash);
  }
  static BuildValue makeFailedCommand() {
    return BuildValue(BuildValueKind::FailedCommand);
  }
  static BuildValue makeSkippedCommand() {
    return BuildValue(BuildValueKind::SkippedCommand);
  }

  bool isExistingInput() const { return kind == BuildValueKind::ExistingInput; }
  bool isMissingInput() const { return kind == BuildValueKind::MissingInput; }
  bool isSuccessfulCommand() const {
    return kind == BuildValueKind::SuccessfulCommand;
  }
  bool isFailedCommand() const { return kind == BuildValueKind::FailedCommand; }
  bool isSkippedCommand() const {
    return kind == BuildValueKind::SkippedCommand;
  }

  bool hasMultipleOutputs() const {
    return numOutputInfos > 1;
  }

  unsigned getNumOutputs() const {
    assert((isExistingInput() || isSuccessfulCommand()) &&
           "invalid call for value kind");
    return numOutputInfos;
  }

  const FileInfo& getOutputInfo() const {
    assert((isExistingInput() || isSuccessfulCommand()) &&
           "invalid call for value kind");
    assert(!hasMultipleOutputs() &&
           "invalid call on result with multiple outputs");
    return valueData.asOutputInfo;
  }

  const FileInfo& getNthOutputInfo(unsigned n) const {
    assert((isExistingInput() || isSuccessfulCommand()) &&
           "invalid call for value kind");
    assert(n < getNumOutputs());
    if (hasMultipleOutputs()) {
      return valueData.asOutputInfos[n];
    } else {
      assert(n == 0);
      return valueData.asOutputInfo;
    }
  }

  CommandSignature getCommandHash() const {
    assert(isSuccessfulCommand() && "invalid call for value kind");
    return commandHash;
  }

  static BuildValue fromValue(const core::ValueType& value) {
    BuildValue result;
    assert(value.size() >= sizeof(result));
    memcpy(&result, value.data(), sizeof(result));

    // If this result has multiple output values, deserialize them properly.
    if (result.numOutputInfos > 1) {
      assert(value.size() == (sizeof(result) +
                              result.numOutputInfos * sizeof(FileInfo)));
      result.valueData.asOutputInfos = new FileInfo[result.numOutputInfos];
      memcpy(result.valueData.asOutputInfos,
             value.data() + sizeof(result),
             result.numOutputInfos * sizeof(FileInfo));
    } else {
      assert(value.size() == sizeof(result));
    }

    return result;
  }

  core::ValueType toValue() {
    if (numOutputInfos > 1) {
      // FIXME: This could be packed one entry tighter.
      std::vector<uint8_t> result(sizeof(*this) +
                                  numOutputInfos * sizeof(FileInfo));
      memcpy(result.data(), this, sizeof(*this));
      memcpy(result.data() + sizeof(*this), valueData.asOutputInfos,
             numOutputInfos * sizeof(FileInfo));
      return result;
    } else {
      std::vector<uint8_t> result(sizeof(*this));
      memcpy(result.data(), this, sizeof(*this));
      return result;
    }
  }
};

struct NinjaBuildEngineDelegate : public core::BuildEngineDelegate {
  std::string workingDirectory;
  class BuildContext* context = nullptr;

  virtual std::unique_ptr<core::Rule> lookupRule(const core::KeyType& key) override;

  virtual void cycleDetected(const std::vector<core::Rule*>& items) override;

  virtual void error(const Twine& message) override;

  std::unique_ptr<basic::ExecutionQueue> createExecutionQueue() override;

  NinjaBuildEngineDelegate(StringRef workingDirectory)
    : workingDirectory(workingDirectory) { }
};

/// Wrapper for information used during a single build.
class BuildContext : public ExecutionQueueDelegate {
public:
  const std::string workingDirectory;

  /// The build engine delegate.
  NinjaBuildEngineDelegate delegate;

  /// The engine in use.
  core::BuildEngine engine;

  /// The Ninja manifest we are operating on.
  std::unique_ptr<ninja::Manifest> manifest;

  /// User-defined prefix for the status line.
  std::string statusLinePrefixFormat = "[%f/%t] ";

  /// Build start time.
  const std::chrono::steady_clock::time_point buildStartTime = std::chrono::steady_clock::now();

  /// Whether commands should print status information.
  bool quiet = false;
  /// Whether the build is being "simulated", in which case commands won't be
  /// run and inputs will be assumed to exist.
  bool simulate = false;
  /// Whether to use strict mode.
  bool strict = false;
  /// Whether output should use verbose mode.
  bool verbose = false;
  /// The number of failed commands to tolerate, or 0 if unlimited
  unsigned numFailedCommandsToTolerate = 1;

  int numJobsInParallel{0};
  basic::SchedulerAlgorithm schedulerAlgorithm{basic::SchedulerAlgorithm::NamePriority};

  /// The build profile output file.
  FILE *profileFP = nullptr;

  /// Whether the build has been cancelled or not.
  std::atomic<bool> isCancelled{false};

  /// Whether the build was cancelled by SIGINT.
  std::atomic<bool> wasCancelledBySigint{false};

  /// The number of generated errors.
  std::atomic<unsigned> numErrors{0};
  /// The number of commands executed during the build
  unsigned numBuiltCommands{0};
  /// The number of output commands written, for numbering purposes.
  unsigned numOutputDescriptions{0};
  /// The number of failed commands.
  std::atomic<unsigned> numFailedCommands{0};

  /// @name Status Reporting Command Counts
  /// @{

  /// The number of commands being scanned.
  std::atomic<unsigned> numCommandsScanning{0};
  /// The number of commands that were up-to-date.
  std::atomic<unsigned> numCommandsUpToDate{0};
  /// The number of commands that have been completed.
  std::atomic<unsigned> numCommandsCompleted{0};
  /// The number of commands that were updated (started, but didn't actually run
  /// the command).
  std::atomic<unsigned> numCommandsUpdated{0};

  /// @}

  /// The status output object.
  CommandLineStatusOutput statusOutput;

  /// The serial queue we used to order output consistently.
  SerialQueue consoleQueue;

  /// Pending process output
  std::unordered_map<uint64_t, SmallString<1024>> outputBuffers;
  std::mutex outputBufferMutex;

  std::unique_ptr<std::thread> signalHandlerThread;

  /// The previous SIGINT handler.
#if defined(_WIN32)
  void (*previousSigintHandler)(int);
#else
  struct sigaction previousSigintHandler;
#endif

  /// Low-level flag for when a SIGINT has been received.
  static std::atomic<bool> wasInterrupted;

  /// Pipe used to allow detection of signals.
  static int signalWatchingPipe[2];

  static void sigintHandler(int) {
    // Set the atomic interrupt flag.
    BuildContext::wasInterrupted = true;

    // Write to wake up the signal monitoring thread.
    char byte{};
    sys::write(signalWatchingPipe[1], &byte, 1);
  }

  /// Cancel the build in response to an interrupt event.
  void cancelBuildOnInterrupt() {

    emitNote("cancelling build.");
    isCancelled = true;
    wasCancelledBySigint = true;

    // Ask the engine to cancel.
    engine.cancelBuild();

    // FIXME: In our model, we still wait for everything to terminate, which
    // means a process that refuses to respond to SIGINT will cause us to just
    // hang here. We should probably detect and report that and be willing to do
    // a hard kill at some point (for example, on the second interrupt).
  }

  /// Check if an interrupt has occurred.
  void checkForInterrupt() {
    // Save and clear the interrupt flag, atomically.
    bool wasInterrupted = BuildContext::wasInterrupted.exchange(false);

    // Process the interrupt flag, if present.
    if (wasInterrupted) {
      // Otherwise, process the interrupt.
      cancelBuildOnInterrupt();
    }
  }

  /// Thread function to wait for indications that signals have arrived and to
  /// process them.
  void signalWaitThread() {
    // Wait for signal arrival indications.
    while (true) {
      char byte;
      int res = sys::read(signalWatchingPipe[0], &byte, 1);

      // If nothing was read, the pipe has been closed and we should shut down.
      if (res == 0)
        break;

      // Otherwise, check if we were awoke because of an interrupt.
      checkForInterrupt();
    }

    // Shut down the pipe.
    sys::close(signalWatchingPipe[0]);
    signalWatchingPipe[0] = -1;
  }

public:
  BuildContext(StringRef workingDirectory)
    : workingDirectory(workingDirectory),
      delegate({workingDirectory}),
      engine(delegate),
      isCancelled(false)
  {
    // Open the status output.
    std::string error;
    if (!statusOutput.open(&error)) {
      fprintf(stderr, "%s: error: unable to open output: %s\n",
              getProgramName(), error.c_str());
      exit(1);
    }

    if (const char *statusFormat = getenv("NINJA_STATUS")) {
      statusLinePrefixFormat = std::string(statusFormat);
    }

    // Register the context with the delegate.
    delegate.context = this;

    // Register an interrupt handler.
#if defined(_WIN32)
    previousSigintHandler = signal(SIGINT, BuildContext::sigintHandler);
#else
    struct sigaction action {};
    action.sa_handler = &BuildContext::sigintHandler;
    sigaction(SIGINT, &action, &previousSigintHandler);
#endif

    // Create a pipe and thread to watch for signals.
    assert(BuildContext::signalWatchingPipe[0] == -1 &&
           BuildContext::signalWatchingPipe[1] == -1);
    if (basic::sys::pipe(BuildContext::signalWatchingPipe) < 0) {
      perror("pipe");
    }
    signalHandlerThread.reset(new std::thread(&BuildContext::signalWaitThread, this));
  }

  ~BuildContext() {
    // Ensure the console queue tasks have been run to completion.
    consoleQueue.sync([] {});

    // Restore any previous SIGINT handler.
#if defined(_WIN32)
    signal(SIGINT, previousSigintHandler);
#else
    sigaction(SIGINT, &previousSigintHandler, NULL);
#endif

    // Close the status output.
    std::string error;
    statusOutput.close(&error);

    // Close the signal watching pipe.
    sys::close(BuildContext::signalWatchingPipe[1]);
    signalWatchingPipe[1] = -1;

    // Wait for our signal handler thread to terminate and reset signal pipes,
    // otherwise we may race with subsequent iterations. rdar://problem/55036265
    signalHandlerThread->join();
  }

  /// @name Diagnostics Output
  /// @{

  /// Emit a status line, which can be updated.
  ///
  /// This method should only be called from the console queue.
  void emitStatus(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::string message = getFormattedString(fmt, ap);
    va_end(ap);

    if (verbose) {
      statusOutput.writeText(message + "\n");
    } else {
      statusOutput.setOrWriteLine(message);
    }
  }

  /// Emit a diagnostic to the error stream.
  void emitDiagnostic(std::string kind, std::string message) {
    consoleQueue.async([this, kind=std::move(kind), message=std::move(message)] {
        statusOutput.finishLine();
        fprintf(stderr, "%s: %s: %s\n", getProgramName(), kind.c_str(),
                message.c_str());
      });
  }

  /// Emit a diagnostic followed by a block of text, ensuring the text
  /// immediately follows the diagnostic.
  void emitDiagnosticAndText(std::string kind, std::string&& message,
                             std::string&& text) {
    statusOutput.stripColorCodes(text);
    consoleQueue.async([this, kind=std::move(kind), message=std::move(message),
                       text=std::move(text)] {
        statusOutput.finishLine();
        fprintf(stderr, "%s: %s: %s\n", getProgramName(), kind.c_str(),
                message.c_str());
        fflush(stderr);
        fwrite(text.data(), text.size(), 1, stdout);
        fflush(stdout);
      });
  }

  /// Emit a block of text to the output.
  void emitText(std::string&& text) {
    statusOutput.stripColorCodes(text);
    consoleQueue.async([this, text=std::move(text)] {
        statusOutput.finishLine();
        fwrite(text.data(), text.size(), 1, stdout);
        fflush(stdout);
      });
  }

  void emitText(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emitText(getFormattedString(fmt, ap));
    va_end(ap);
  }

  void emitError(std::string&& message) {
    emitDiagnostic("error", std::move(message));
    ++numErrors;
  }

  void emitErrorAndText(std::string&& message, std::string&& text) {
    emitDiagnosticAndText("error", std::move(message), std::move(text));
    ++numErrors;
  }

  void emitError(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emitError(getFormattedString(fmt, ap));
    va_end(ap);
  }

  void emitNote(std::string&& message) {
    emitDiagnostic("note", std::move(message));
  }

  void emitNote(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emitNote(getFormattedString(fmt, ap));
    va_end(ap);
  }

  /// @}

  /// @name Execution Queue Delegate
  /// @{

  void queueJobStarted(JobDescriptor*) override {}
  void queueJobFinished(JobDescriptor*) override {}
  void processStarted(ProcessContext* ctx, ProcessHandle handle, llbuild_pid_t pid) override {
    std::lock_guard<std::mutex> lock(outputBufferMutex);
    outputBuffers.emplace(handle.id, SmallString<1024>());
  }
  void processHadError(ProcessContext*, ProcessHandle,
                       const Twine& message) override {
    emitError(message.str());
  }
  void processHadOutput(ProcessContext* ctx, ProcessHandle handle,
                        StringRef data) override {
    std::lock_guard<std::mutex> lock(outputBufferMutex);
    auto& outputData = outputBuffers[handle.id];
    outputData.insert(outputData.end(), data.bytes_begin(), data.bytes_end());
  }
  void processFinished(ProcessContext* ctx, ProcessHandle handle,
                       const ProcessResult& result) override {
    ninja::Command* job = reinterpret_cast<ninja::Command*>(ctx);
    std::unique_lock<std::mutex> lock(outputBufferMutex);
    auto& outputData = outputBuffers[handle.id];
    lock.unlock();
    if (result.status == ProcessStatus::Succeeded) {
      if (!outputData.empty()) {
        emitText(std::string(outputData.data(), outputData.size()));
      }
    } else {
      // If the process was cancelled, assume it is because we were
      // interrupted.
      if (result.status == ProcessStatus::Cancelled) {
        lock.lock();
        outputBuffers.erase(handle.id);
        return;
      }

      // Otherwise, report the failure.
      emitErrorAndText(
          getFormattedString(
              "process failed: %s", job->getCommandString().c_str()),
          std::string(outputData.data(), outputData.size()));

      // Update the count of failed commands.
      incrementFailedCommands();
    }

    lock.lock();
    outputBuffers.erase(handle.id);
  }

  /// @}

  void reportMissingInput(const ninja::Node* node) {
    // We simply report the missing input here, the build will be cancelled when
    // a rule sees it missing.
    emitError("missing input '%s' and no rule to build it",
              node->getScreenPath().c_str());
  }

  void incrementFailedCommands() {
    // Update our count of the number of failed commands.
    unsigned numFailedCommands = ++this->numFailedCommands;

    // Cancel the build, if the number of command failures exceeds the
    // number to continue past.
    if (numFailedCommandsToTolerate != 0 &&
        numFailedCommands == numFailedCommandsToTolerate) {
      emitError("stopping build due to command failures");
      isCancelled = true;
    }
  }

  unsigned getNumPossibleMaxCommands() const {
    // Compute the "possible" number of maximum commands that will be
    // run. This is only the "possible" max because we can start running
    // commands before dependency scanning is complete -- we include the
    // number of commands that are being scanned so that this number will
    // always be greater than the number of commands that have been executed
    // until the very last command is run.
    int totalPossibleMaxCommands = numCommandsCompleted + numCommandsScanning;

    // Compute the number of max commands to show, subtracting out all the
    // commands that we avoided running.
    int possibleMaxCommands = totalPossibleMaxCommands -
      (numCommandsUpToDate + numCommandsUpdated);

    return possibleMaxCommands;
  }

  /// Return a formatted status line prefix according to
  /// user preferences (NINJA_STATUS env).
  std::string statusLinePrefix(const std::string& format) const {
    auto begin = format.begin();
    auto end = format.end();

    std::string s;
    s.reserve(format.size() * 4);

    for (auto it = begin; it != end; it++) {
      char c = *it;
      if (c == '%') {
        if (++it == end) {
          s += c;
          break;
        }
        c = *it;
        switch (c) {
        case 'e':
          // Elapsed time.
          {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - buildStartTime;
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            char buf[64];
            snprintf(buf, sizeof(buf), "%.3f", (double)elapsedMs / 1000.0);
            s.append(buf);
          }
          break;
        case 'f':
          // Number completed.
          s.append(std::to_string(numOutputDescriptions));;
          break;
        case 'o': case 'c':
          // Undifferentiated task completion rate.
          {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - buildStartTime;
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            double rate = 1000.0 * (double)(numOutputDescriptions) / elapsedMs;
            char buf[64];
            snprintf(buf, sizeof(buf), "%.1f", rate);
            s.append(buf);
          }
          break;
        case 'p':
          // Percentage completed.
          {
            long percentage_started = 100 * numOutputDescriptions / getNumPossibleMaxCommands();
            char buf[64];
            snprintf(buf, sizeof(buf), "%3ld%%", percentage_started);
            s.append(buf);
          }
          break;
        case 'r':
          // Number of edges running.
          {
            auto running = numCommandsScanning + 0;
            s.append(std::to_string(running));
          }
          break;
        case 's':
          // Number of edges started.
          s.append(std::to_string(numCommandsScanning));
          break;
        case 't':
          // Estimated number of edges.
          s.append(std::to_string(getNumPossibleMaxCommands()));
          break;
        case 'u':
          // Remaining number of edges.
          {
            auto remaining = getNumPossibleMaxCommands() - numOutputDescriptions;
            s.append(std::to_string(remaining));
          }
          break;
        case '%':
          s += '%';
          break;
        default:
          s += '%';
          s += c;
          break;
        }
      } else if (c == '\\') {
        if (++it == end) {
          s += c;
          break;
        }
        c = *it;
        switch (c) {
        case 'a': s += '\a'; break;
        case 'b': s += '\b'; break;
        case 'e': s += '\e'; break;
        case 'f': s += '\f'; break;
        case 'n': s += '\n'; break;
        case 'r': s += '\r'; break;
        case 't': s += '\t'; break;
        case 'v': s += '\v'; break;
        case '\\': s += '\\'; break;
        case '0':
          {
            uint8_t result = 0;
            // The '\0033' and '\033' sequences should yield the same result.
            // The first one is canonical, the second one is a fallback.
            // For example, try:
            //  > echo -en '\0033[32mgreen\033[31mred\x1b[0m'
            if (end - it >= 4 && StringRef(&(*(it + 1)), 3).getAsInteger(8, result) == false) {
              it += 3;
              s += result;
            } else if (end - it >= 3 && StringRef(&(*it), 3).getAsInteger(8, result) == false) {
              it += 2;
              s += result;
            } else {
              s += '\\';
              s += c;
            }
          }
          break;
        case 'x':
          {
            uint8_t result = 0;
            if (end - it >= 3 && StringRef(&(*(it + 1)), 2).getAsInteger(16, result) == false) {
              s += result;
              it += 2;
            } else {
              s += '\\';
              s += c;
            }
          }
          break;
        default:
          s += '\\';
          s += c;
          break;
        }
      } else {
        // Not '%' or '\\'
        s += c;
        continue;
      }
    }
    return s;
  }

};

std::atomic<bool> BuildContext::wasInterrupted{false};
int BuildContext::signalWatchingPipe[2]{-1, -1};

class BuildManifestActions : public ninja::ManifestLoaderActions {
  BuildContext& context;
  ninja::ManifestLoader* loader = 0;
  unsigned numErrors = 0;
  unsigned maxErrors = 20;

private:
  virtual void initialize(ninja::ManifestLoader* loader) override {
    this->loader = loader;
  }

  virtual void error(StringRef filename, StringRef message,
                     const ninja::Token& at) override {
    if (numErrors++ >= maxErrors)
      return;

    util::emitError(filename, message, at, loader->getCurrentParser());
  }

  virtual std::unique_ptr<llvm::MemoryBuffer> readFile(
      StringRef path, StringRef forFilename,
      const ninja::Token* forToken) override {
    auto bufferOrError = util::readFileContents(path);
    if (bufferOrError)
      return std::move(*bufferOrError);

    // TODO: util::emitError doesn't use the console queue?
    std::string error = llvm::toString(bufferOrError.takeError());
    ++numErrors;
    if (forToken) {
      util::emitError(forFilename, error, *forToken,
                      loader->getCurrentParser());
    } else {
      context.emitError(std::move(error));
    }
    return nullptr;
  }

public:
  BuildManifestActions(BuildContext& context) : context(context) {}

  unsigned getNumErrors() const { return numErrors; }
};

static core::Task*
buildCommand(BuildContext& context, ninja::Command* command) {
  struct NinjaCommandTask : core::Task {
    BuildContext& context;
    ninja::Command* command;

    /// If true, the command should be skipped (because of an error in an
    /// input).
    bool shouldSkip = false;

    /// If true, the command had a missing input (this implies ShouldSkip is
    /// true).
    bool hasMissingInput = false;

    /// If true, the command can be updated if the output is newer than all of
    /// the inputs.
    bool canUpdateIfNewer = true;

    /// Information on the prior command result, if present.
    bool hasPriorResult = false;
    CommandSignature priorCommandHash;

    /// The timestamp of the most recently rebuilt input.
    FileTimestamp newestModTime{ 0, 0 };

    NinjaCommandTask(BuildContext& context, ninja::Command* command)
        : context(context), command(command) {
      // If this command uses discovered dependencies, we can never skip it (we
      // don't yet have a way to account for the discovered dependencies, or
      // preserve them if skipped).
      //
      // FIXME: We should support update-if-newer for commands with deps.
      if (command->getDepsStyle() != ninja::Command::DepsStyleKind::None)
        canUpdateIfNewer = false;
    }

    virtual void provideValue(core::TaskInterface, uintptr_t inputID,
                              const core::ValueType& valueData) override {
      // Process the input value to see if we should skip this command.
      BuildValue value = BuildValue::fromValue(valueData);

      // All direct inputs to NinjaCommandTask objects should be singleton
      // values.
      assert(!value.hasMultipleOutputs());

      // If the value is not an existing input or a successful command, then we
      // shouldn't run this command.
      if (!value.isExistingInput() && !value.isSuccessfulCommand()) {
        shouldSkip = true;
        if (value.isMissingInput()) {
          hasMissingInput = true;

          context.reportMissingInput(command->getInputs()[inputID]);
        }
      } else {
        // Otherwise, track the information used to determine if we can just
        // update the command instead of running it.
        const FileInfo& outputInfo = value.getOutputInfo();

        // If there is a missing input file (from a successful command), we
        // always need to run the command.
        if (outputInfo.isMissing()) {
          canUpdateIfNewer = false;
        } else {
          // Otherwise, keep track of the newest input.
          if (outputInfo.modTime > newestModTime) {
            newestModTime = outputInfo.modTime;
          }
        }
      }
    }

    bool isImmediatelyCyclicInput(const ninja::Node* node) const {
      for (const auto* output: command->getOutputs())
        if (node == output)
          return true;
      return false;
    }

    virtual void start(core::TaskInterface ti) override {
      // If this is a phony rule, ignore any immediately cyclic dependencies in
      // non-strict mode, which are generated frequently by CMake, but can be
      // ignored by Ninja. See https://github.com/martine/ninja/issues/935.
      //
      // FIXME: Find a way to harden this more, or see if we can just get CMake
      // to fix it.
      bool isPhony = command->getRule() == context.manifest->getPhonyRule();

      // Request all of the explicit and implicit inputs (the only difference
      // between them is that implicit inputs do not appear in ${in} during
      // variable expansion, but that has already been performed).
      unsigned id = 0;
      for (auto it = command->explicitInputs_begin(),
             ie = command->explicitInputs_end(); it != ie; ++it, ++id) {
        if (!context.strict && isPhony && isImmediatelyCyclicInput(*it))
          continue;

        ti.request((*it)->getCanonicalPath(), id);
      }
      for (auto it = command->implicitInputs_begin(),
             ie = command->implicitInputs_end(); it != ie; ++it, ++id) {
        if (!context.strict && isPhony && isImmediatelyCyclicInput(*it))
          continue;

        ti.request((*it)->getCanonicalPath(), id);
      }

      // Request all of the order-only inputs.
      for (auto it = command->orderOnlyInputs_begin(),
             ie = command->orderOnlyInputs_end(); it != ie; ++it) {
        if (!context.strict && isPhony && isImmediatelyCyclicInput(*it))
          continue;

        ti.mustFollow((*it)->getCanonicalPath());
      }
    }

    virtual void providePriorValue(core::TaskInterface,
                                   const core::ValueType& valueData) override {
      BuildValue value = BuildValue::fromValue(valueData);

      if (value.isSuccessfulCommand()) {
        hasPriorResult = true;
        priorCommandHash = value.getCommandHash();
      }
    }

    /// Compute the output result for the command.
    BuildValue computeCommandResult(CommandSignature commandHash) const {
      unsigned numOutputs = command->getOutputs().size();
      if (numOutputs == 1) {
        return BuildValue::makeSuccessfulCommand(
            FileInfo::getInfoForPath(
                command->getOutputs()[0]->getCanonicalPath()),
            commandHash);
      } else {
        std::vector<FileInfo> outputInfos(numOutputs);
        for (unsigned i = 0; i != numOutputs; ++i) {
          outputInfos[i] = FileInfo::getInfoForPath(
              command->getOutputs()[i]->getCanonicalPath());
        }
        return BuildValue::makeSuccessfulCommand(outputInfos.data(), numOutputs,
                                                 commandHash);
      }
    }

    /// Check if it is legal to only update the result (versus rerunning)
    /// because the outputs are newer than all of the inputs.
    bool canUpdateIfNewerWithResult(const BuildValue& result) {
      assert(result.isSuccessfulCommand());

      // Check each output.
      for (unsigned i = 0, e = result.getNumOutputs(); i != e; ++i) {
        const FileInfo& outputInfo = result.getNthOutputInfo(i);

        // If the output is missing, we need to rebuild.
        if (outputInfo.isMissing())
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
        if (context.strict) {
          if (outputInfo.modTime <= newestModTime)
            return false;
        } else {
          if (outputInfo.modTime < newestModTime)
            return false;
        }
      }

      return true;
    }

    virtual void inputsAvailable(core::TaskInterface ti) override {
      // If the build is cancelled, skip everything.
      if (context.isCancelled) {
        return ti.complete(BuildValue::makeSkippedCommand().toValue());
      }

      // Ignore phony commands.
      //
      // FIXME: Is it right to bring this up-to-date when one of the inputs
      // indicated a failure? It probably doesn't matter.
      auto commandHash = CommandSignature(command->getCommandString());
      if (command->getRule() == context.manifest->getPhonyRule()) {
        // Get the result.
        BuildValue result = computeCommandResult(commandHash);

        // If any output is missing, then we always want to force the change to
        // propagate.
        bool forceChange = false;
        for (unsigned i = 0, e = result.getNumOutputs(); i != e; ++i) {
            if (result.getNthOutputInfo(i).isMissing()) {
                forceChange = true;
                break;
            }
        }

        return ti.complete(result.toValue(), forceChange);
      }

      // If it is legal to simply update the command, then if the command output
      // exists and is newer than all of the inputs, don't actually run the
      // command (just bring it up-to-date).
      if (canUpdateIfNewer) {
        // If this isn't a generator command and its command hash differs, we
        // can't update it.
        if (!command->hasGeneratorFlag() &&
            (!hasPriorResult || priorCommandHash != commandHash))
          canUpdateIfNewer = false;

        if (canUpdateIfNewer) {
          BuildValue result = computeCommandResult(commandHash);

          if (canUpdateIfNewerWithResult(result)) {
            // Update the count of the number of commands which have been
            // updated without being rerun.
            ++context.numCommandsUpdated;

            return ti.complete(result.toValue());
          }
        }
      }

      // Otherwise, actually run the command.

      ++context.numBuiltCommands;

      // If we are simulating the build, just print the description and
      // complete.
      if (context.simulate) {
        if (!context.quiet)
          writeDescription(context, command);
        return ti.complete(BuildValue::makeSkippedCommand().toValue());
      }

      // If not simulating, but this command should be skipped, then do nothing.
      if (shouldSkip) {
        // If this command had a failed input, treat it as having failed.
        if (hasMissingInput) {
          context.emitError("cannot build '%s' due to missing input",
                            command->getOutputs()[0]->getScreenPath().c_str());

          // Update the count of failed commands.
          context.incrementFailedCommands();
        }

        return ti.complete(BuildValue::makeSkippedCommand().toValue());
      }
      assert(!hasMissingInput);

      auto addExecuteJob = [this, ti](std::function<void(void)>&& jobFullyExecuted) mutable {
        // Otherwise, enqueue the job to run later.
        ti.spawn({command, [this, ti, done=std::move(jobFullyExecuted)] (QueueJobContext* qctx) mutable {
          // Suppress static analyzer false positive on generalized lambda capture
          // (rdar://problem/22165130).
#ifndef __clang_analyzer__
          // Take care to not rely on the ``this`` object, which may disappear
          // before the queue executes this block.
          BuildContext& localContext(context);
          ninja::Command* localCommand(command);
          auto bucket = qctx->laneID();

          if (localContext.profileFP) {
            localContext.consoleQueue.async(
              [&localContext=localContext, localCommand=localCommand, bucket] {
                uint64_t startTime = getTimeInMicroseconds();
                fprintf(localContext.profileFP,
                        ("{ \"name\": \"%s\", \"ph\": \"B\", \"pid\": 0, "
                         "\"tid\": %d, \"ts\": %llu},\n"),
                        localCommand->getEffectiveDescription().c_str(), bucket,
                        static_cast<unsigned long long>(startTime));
              });
          }

          executeCommand(ti, qctx);

          if (localContext.profileFP) {
            localContext.consoleQueue.async(
              [&localContext=localContext, localCommand=localCommand, bucket] {
                uint64_t endTime = getTimeInMicroseconds();
                fprintf(localContext.profileFP,
                        ("{ \"name\": \"%s\", \"ph\": \"E\", \"pid\": 0, "
                         "\"tid\": %d, \"ts\": %llu},\n"),
                        localCommand->getEffectiveDescription().c_str(), bucket,
                        static_cast<unsigned long long>(endTime));
              });
          }
          done();
#endif
        }});
      };

      bool isConsolePool = command->getExecutionPool() == context.manifest->getConsolePool();
      if (isConsolePool) {
        context.consoleQueue.async([addExecuteJob=std::move(addExecuteJob)] () mutable {
          std::promise<void> p;
          auto result = p.get_future();
          addExecuteJob([&p]{ p.set_value(); });
          result.get();
        });
      } else {
        addExecuteJob([]{});
      }
    }

    static void writeDescription(BuildContext& context,
                                 ninja::Command* command) {
      const std::string& description =
        context.verbose ? command->getCommandString() :
        command->getEffectiveDescription();
      ++context.numOutputDescriptions;
      context.emitStatus("%s%s",
        context.statusLinePrefix(context.statusLinePrefixFormat).c_str(),
        description.c_str());

      // Whenever we write a description for a console job, make sure to finish
      // the output under the expectation that the console job might write to
      // the output. We don't make any attempt to lock this in case the console
      // job can run concurrently with anything else.
      if (command->getExecutionPool() == context.manifest->getConsolePool()) {
        context.statusOutput.finishLine();
      }
    }

    void executeCommand(core::TaskInterface ti, QueueJobContext* qctx) {
      // If the build is cancelled, skip the job.
      if (context.isCancelled) {
        return ti.complete(BuildValue::makeSkippedCommand().toValue());
      }

      // The console pool is a bit special in the way it flushes its output.
      bool isConsolePool = command->getExecutionPool() == context.manifest->getConsolePool();

      // Write the description on the output queue, taking care to not rely on
      // the ``this`` object, which may disappear before the queue executes this
      // block.
      if (!context.quiet) {
          // Suppress static analyzer false positive on generalized lambda capture
          // (rdar://problem/22165130).
#ifndef __clang_analyzer__
        // If this is a console job, do the write synchronously to ensure it
        // appears before the task might start.
        if (isConsolePool) {
          // Jobs in a console pool are guaranteed to run on a pool's console queue,
          // so the output won't get intermixed.
          writeDescription(context, command);
        } else {
          context.consoleQueue.async([&context=context, command=command] {
            writeDescription(context, command);
          });
        }
#endif
      }

      // If response file is used by the command, create the file and
      // fill it with content before command execution.
      // The file should be deleted after successful command execution.
      const auto rspFile = command->getRspFile();
      if (!rspFile.empty()) {
        std::error_code ec;
        llvm::raw_fd_ostream os(rspFile, ec, llvm::sys::fs::OF_Text);
        if (ec) {
          // Treat the command as having a failed input.
          context.emitError("unable to create @response file '%s': %s\n",
                            rspFile.c_str(), ec.message().c_str());

          // Update the count of failed commands.
          context.incrementFailedCommands();
          return ti.complete(BuildValue::makeSkippedCommand().toValue());
        }
        os << command->getRspFileContent();
        os.close();
      }

      StringRef args[] = {
#if defined(_WIN32)
        "C:\\windows\\system32\\cmd.exe",
        "/C",
#else
        DefaultShellPath,
        "-c",
#endif
        command->getCommandString().c_str()
      };

      ti.spawn(qctx, args, {}, {true, isConsolePool}, {
        [this, ti](ProcessResult result) mutable {
          // Actually run the command.
          if (result.status != ProcessStatus::Succeeded) {
            // If the command failed, complete the task with the failed result and
            // always propagate.
            return ti.complete(BuildValue::makeFailedCommand().toValue(),
                               /*ForceChange=*/true);
          }

          // Otherwise, the command succeeded so process the dependencies.
          if (!processDiscoveredDependencies(ti)) {
            context.incrementFailedCommands();
            return ti.complete(BuildValue::makeFailedCommand().toValue(),
                               /*ForceChange=*/true);
          }

          // Complete the task with a successful value.
          //
          // We always restat the output, but we honor Ninja's restat flag by
          // forcing downstream propagation if it isn't set.
          auto commandHash = CommandSignature(command->getCommandString());
          BuildValue resultValue = computeCommandResult(commandHash);

          // Remove response file.
          const auto rspFile = command->getRspFile();
          if (!rspFile.empty())
            llvm::sys::fs::remove(rspFile);

          return ti.complete(resultValue.toValue(),
                             /*ForceChange=*/!command->hasRestatFlag());
        }
      });
    }


    bool processDiscoveredDependencies(core::TaskInterface ti) {
      // Process the discovered dependencies, if used.
      switch (command->getDepsStyle()) {
      case ninja::Command::DepsStyleKind::None:
        return true;
      case ninja::Command::DepsStyleKind::MSVC: {
        context.emitError("MSVC style dependencies are unsupported");
        return false;
      }
      case ninja::Command::DepsStyleKind::GCC: {
        // Read the dependencies file.
        auto bufferOrError = util::readFileContents(command->getDepsFile());
        if (!bufferOrError) {
          // If the file is missing, just ignore it for consistency with Ninja
          // (when using stored deps) in non-strict mode.
          if (!context.strict)
            return true;

          // FIXME: Error handling.
          std::string error = llvm::toString(bufferOrError.takeError());
          context.emitError("unable to read dependency file: %s",
                            error.c_str());
          return false;
        }

        // Parse the output.
        //
        // We just ignore the rule, and add any dependency that we encounter in
        // the file.
        struct DepsActions : public core::MakefileDepsParser::ParseActions {
          BuildContext& context;
          core::TaskInterface ti;
          const StringRef workingDirectory;
          const StringRef path;
          unsigned numErrors{0};

          DepsActions(BuildContext& context, core::TaskInterface ti,
                      const StringRef workingDirectory,
                      const StringRef path)
            : context(context), ti(ti), workingDirectory(workingDirectory), path(path) { }

          virtual void error(StringRef message, uint64_t position) override {
            context.emitError(
                "error reading dependency file: %s (%s) at offset %u",
                path.str().c_str(), message.str().c_str(), unsigned(position));
            ++numErrors;
          }

          virtual void actOnRuleDependency(StringRef dependency,
                                           StringRef unescapedWord) override {

            SmallString<256> absPathTmp = unescapedWord;
            if (!llbuild::ninja::Manifest::normalize_path(workingDirectory, absPathTmp)) {
              return;
            }

            StringRef path = absPathTmp;
            ti.discoveredDependency(path);
          }

          virtual void actOnRuleStart(StringRef name,
                                      StringRef unescapedWord) override {}
          virtual void actOnRuleEnd() override {}
        };

        DepsActions actions(context, ti, context.workingDirectory, command->getDepsFile());
        core::MakefileDepsParser(bufferOrError.get()->getBuffer(),
                                 actions).parse();
        return actions.numErrors == 0;
      }
      }

      assert(0 && "unexpected case");
      return false;
    }
  };

  return new NinjaCommandTask(context, command);
}

static core::Task* buildInput(BuildContext& context, ninja::Node* input) {
  struct NinjaInputTask : core::Task {
    BuildContext& context;
    ninja::Node* node;

    NinjaInputTask(BuildContext& context, ninja::Node* node)
        : context(context), node(node) { }

    virtual void provideValue(core::TaskInterface, uintptr_t inputID,
                              const core::ValueType& value) override { }

    virtual void start(core::TaskInterface) override { }

    virtual void inputsAvailable(core::TaskInterface ti) override {
      if (context.simulate) {
        ti.complete(BuildValue::makeExistingInput({}).toValue());
        return;
      }

      auto outputInfo = FileInfo::getInfoForPath(node->getCanonicalPath());
      if (outputInfo.isMissing()) {
        ti.complete(BuildValue::makeMissingInput().toValue());
        return;
      }

      ti.complete(BuildValue::makeExistingInput(outputInfo).toValue());
    }
  };

  return new NinjaInputTask(context, input);
}

static core::Task*
buildTargets(BuildContext& context,
             const std::vector<std::string>& targetsToBuild) {
  struct TargetsTask : core::Task {
    BuildContext& context;
    std::vector<std::string> targetsToBuild;

    TargetsTask(BuildContext& context,
                const std::vector<std::string>& targetsToBuild)
        : context(context), targetsToBuild(targetsToBuild) { }

    virtual void provideValue(core::TaskInterface, uintptr_t inputID,
                              const core::ValueType& valueData) override {
      BuildValue value = BuildValue::fromValue(valueData);

      if (value.isMissingInput()) {
        context.emitError("unknown target '%s'",
                          targetsToBuild[inputID].c_str());
      }
    }

    virtual void start(core::TaskInterface ti) override {
      // Request all of the targets.
      unsigned id = 0;
      for (const auto& target: targetsToBuild) {
        ti.request(target, id++);
      }
    }

    virtual void inputsAvailable(core::TaskInterface ti) override {
      // Complete the job.
      ti.complete(
        BuildValue::makeSuccessfulCommand({}, CommandSignature()).toValue());
      return;
    }
  };

  return new TargetsTask(context, targetsToBuild);
}

static core::Task*
selectCompositeBuildResult(BuildContext& context, ninja::Command* command,
                           unsigned inputIndex,
                           const core::KeyType& compositeRuleName) {
  struct SelectResultTask : core::Task {
    const BuildContext& context;
    const ninja::Command* command;
    const unsigned inputIndex;
    const core::KeyType compositeRuleName;
    const core::ValueType *compositeValueData = nullptr;

    SelectResultTask(BuildContext& context, ninja::Command* command,
                     unsigned inputIndex,
                     const core::KeyType& compositeRuleName)
        : context(context), command(command),
          inputIndex(inputIndex), compositeRuleName(compositeRuleName) { }

    virtual void start(core::TaskInterface ti) override {
      // Request the composite input.
      ti.request(compositeRuleName, 0);
    }

    virtual void provideValue(core::TaskInterface, uintptr_t inputID,
                              const core::ValueType& valueData) override {
      compositeValueData = &valueData;
    }

    virtual void inputsAvailable(core::TaskInterface ti) override {
      // Construct the appropriate build value from the result.
      assert(compositeValueData);
      BuildValue value(BuildValue::fromValue(*compositeValueData));

      // If the input was a failed or skipped command, propagate that result.
      if (value.isFailedCommand() || value.isSkippedCommand()) {
        ti.complete(value.toValue(), /*ForceChange=*/true);
      } else {
        // FIXME: We don't try and set this in response to the restat flag on
        // the incoming command, because it doesn't generally work -- the output
        // will just honor update-if-newer and still not run. We need to move to
        // a different model for handling restat = 0 to get this to work
        // properly.
        bool forceChange = false;

        // Otherwise, the value should be a successful command with file info
        // for each output.
        assert(value.isSuccessfulCommand() && value.hasMultipleOutputs() &&
               inputIndex < value.getNumOutputs());

        // The result is the InputIndex-th element, and the command hash is
        // propagated.
        ti.complete(
          BuildValue::makeSuccessfulCommand(
            value.getNthOutputInfo(inputIndex),
            value.getCommandHash()).toValue(),
          forceChange);
      }
    }
  };

  return new SelectResultTask(context, command, inputIndex, compositeRuleName);
}

static bool buildInputIsResultValid(ninja::Node* node,
                                    const core::ValueType& valueData) {
  BuildValue value = BuildValue::fromValue(valueData);

  // If the prior value wasn't for an existing input, recompute.
  if (!value.isExistingInput())
    return false;

  // Otherwise, the result is valid if the path exists and the hash has not
  // changed.
  //
  // FIXME: This is inefficient, we will end up doing the stat twice, once when
  // we check the value for up to dateness, and once when we "build" the output.
  //
  // We can solve this by caching ourselves but I wonder if it is something the
  // engine should support more naturally.
  auto info = FileInfo::getInfoForPath(node->getCanonicalPath());
  if (info.isMissing())
    return false;

  return value.getOutputInfo() == info;
}

static bool buildCommandIsResultValid(ninja::Command* command,
                                      const core::ValueType& valueData) {
  BuildValue value = BuildValue::fromValue(valueData);

  // If the prior value wasn't for a successful command, recompute.
  if (!value.isSuccessfulCommand())
    return false;

  // For non-generator commands, if the command hash has changed, recompute.
  if (!command->hasGeneratorFlag()) {
    if (value.getCommandHash() != CommandSignature(
          command->getCommandString()))
      return false;
  }

  // Check the timestamps on each of the outputs.
  for (unsigned i = 0, e = command->getOutputs().size(); i != e; ++i) {
    // Always rebuild if the output is missing.
    auto info = FileInfo::getInfoForPath(command->getOutputs()[i]->getCanonicalPath());
    if (info.isMissing())
      return false;

    // Otherwise, the result is valid if file information has not changed.
    //
    // Note that we may still decide not to actually run the command based on
    // the update-if-newer handling, but it does require running the task.
    if (value.getNthOutputInfo(i) != info)
      return false;
  }

  return true;
}

static bool selectCompositeIsResultValid(ninja::Command* command,
                                         const core::ValueType& valueData) {
  BuildValue value = BuildValue::fromValue(valueData);

  // If the prior value wasn't for a successful command, recompute.
  if (!value.isSuccessfulCommand())
    return false;

  // If the command's signature has changed since it was built, rebuild. This is
  // important for ensuring that we properly reevaluate the select rule when
  // it's incoming composite rule no longer exists.
  if (value.getCommandHash() != CommandSignature(command->getCommandString()))
    return false;

  // Otherwise, this result is always valid.
  return true;
}

static void updateCommandStatus(BuildContext& context,
                                ninja::Command* command,
                                core::Rule::StatusKind status) {
  // Ignore phony rules.
  if (command->getRule() == context.manifest->getPhonyRule())
    return;

  // Track the number of commands which are currently being scanned along with
  // the total number of completed commands.
  if (status == core::Rule::StatusKind::IsScanning) {
    ++context.numCommandsScanning;
  } else if (status == core::Rule::StatusKind::IsUpToDate) {
    --context.numCommandsScanning;
    ++context.numCommandsUpToDate;
    ++context.numCommandsCompleted;
  } else {
    assert(status == core::Rule::StatusKind::IsComplete);
    --context.numCommandsScanning;
    ++context.numCommandsCompleted;
  }
}

std::unique_ptr<core::Rule> NinjaBuildEngineDelegate::lookupRule(const core::KeyType& key) {
  // We created rules for all of the commands up front, so if we are asked for a
  // rule here it is because we are looking for an input.

  // Get the node for this input.
  //
  // FIXME: This is frequently a redundant lookup, given that the caller might
  // well have had the Node* available. This is something that would be nice
  // to avoid when we support generic key types.
  ninja::Node* node = context->manifest->findOrCreateNode(workingDirectory, key.str());

  class NinjaInputRule: public core::Rule {
    BuildContext* context;
    ninja::Node* node;

  public:
    NinjaInputRule(const core::KeyType& key, BuildContext* context, ninja::Node* node)
      : core::Rule(key), context(context), node(node) { }

    core::Task* createTask(core::BuildEngine&) override {
      return buildInput(*context, node);
    }

    bool isResultValid(core::BuildEngine&, const core::ValueType& value) override {
      // If simulating, assume cached results are valid.
      if (context->simulate) return true;

      return buildInputIsResultValid(node, value);
    }
  };

  return std::unique_ptr<core::Rule>(new NinjaInputRule(node->getScreenPath(), context, node));
}

void NinjaBuildEngineDelegate::cycleDetected(
    const std::vector<core::Rule*>& cycle) {
  // Report the cycle.
  std::string message;
  llvm::raw_string_ostream messageStream(message);
  messageStream << "cycle detected among targets:";
  bool first = true;
  for (const auto* rule: cycle) {
    if (!first)
      messageStream << " ->";
    messageStream << " \"" << rule->key.str() << '"';
    first = false;
  }

  messageStream.flush();
  context->emitError(message.c_str());

  // Cancel the build.
  context->isCancelled = true;
}

void NinjaBuildEngineDelegate::error(const Twine& message) {
  // Report the error.
  context->emitError("error: " + message.str());

  // Cancel the build.
  context->isCancelled = true;
}
} // namespace


int commands::executeNinjaBuildCommand(std::vector<std::string> args) {
  std::string chdirPath = "";
  std::string customTool = "";
  std::string dbFilename = "build.db";
  std::string dumpGraphPath, profileFilename, traceFilename;
  std::string manifestFilename = "build.ninja";

  // Create a context for the build.
  bool autoRegenerateManifest = true;
  bool quiet = false;
  bool simulate = false;
  bool strict = false;
  bool verbose = false;
  unsigned numJobsInParallel = 0;
  SchedulerAlgorithm schedulerAlgorithm = SchedulerAlgorithm::NamePriority;
  unsigned numFailedCommandsToTolerate = 1;
  double maximumLoadAverage = 0.0;
  std::vector<std::string> debugTools;

  if (basic::sys::raiseOpenFileLimit() != 0) {
    fprintf(stderr, "%s: error: unable to raise open file limit\n\n",
            getProgramName());
    return -1;
  }

  while (!args.empty() && args[0][0] == '-') {
    const std::string option = args[0];
    args.erase(args.begin());

    if (option == "--")
      break;

    if (option == "--version") {
      // Report a fake version for tools (like CMake) that detect compatibility
      // based on the 'Ninja' version.
      printf("1.7.0 Ninja Compatible (%s)\n", getLLBuildFullVersion().c_str());
      return 0;
    } else if (option == "--help") {
      usage(/*exitCode=*/0);
    } else if (option == "--simulate") {
      simulate = true;
    } else if (option == "--quiet") {
      quiet = true;
    } else if (option == "-C" || option == "--chdir") {
      if (args.empty()) {
        fprintf(stderr, "%s: error: missing argument to '%s'\n\n",
                getProgramName(), option.c_str());
        usage();
      }
      chdirPath = args[0];
      args.erase(args.begin());
    } else if (option == "--no-db") {
      dbFilename = "";
    } else if (option == "--db") {
      if (args.empty()) {
        fprintf(stderr, "%s: error: missing argument to '%s'\n\n",
                getProgramName(), option.c_str());
        usage();
      }
      dbFilename = args[0];
      args.erase(args.begin());
    } else if (option == "--dump-graph") {
      if (args.empty()) {
        fprintf(stderr, "%s: error: missing argument to '%s'\n\n",
                getProgramName(), option.c_str());
        usage();
      }
      dumpGraphPath = args[0];
      args.erase(args.begin());
    } else if (option == "-f") {
      if (args.empty()) {
        fprintf(stderr, "%s: error: missing argument to '%s'\n\n",
                getProgramName(), option.c_str());
        usage();
      }
      manifestFilename = args[0];
      args.erase(args.begin());
    } else if (option == "-k") {
      if (args.empty()) {
        fprintf(stderr, "%s: error: missing argument to '%s'\n\n",
                getProgramName(), option.c_str());
        usage();
      }
      char *end;
      numFailedCommandsToTolerate = ::strtol(args[0].c_str(), &end, 10);
      if (*end != '\0') {
          fprintf(stderr, "%s: error: invalid argument '%s' to '%s'\n\n",
                  getProgramName(), args[0].c_str(), option.c_str());
          usage();
      }
      args.erase(args.begin());
    } else if (option == "-l") {
      if (args.empty()) {
        fprintf(stderr, "%s: error: missing argument to '%s'\n\n",
                getProgramName(), option.c_str());
        usage();
      }
      char *end;
      maximumLoadAverage = ::strtod(args[0].c_str(), &end);
      if (*end != '\0') {
          fprintf(stderr, "%s: error: invalid argument '%s' to '%s'\n\n",
                  getProgramName(), args[0].c_str(), option.c_str());
          usage();
      }
      args.erase(args.begin());
    } else if (option == "-j" || option == "--jobs") {
      if (args.empty()) {
        fprintf(stderr, "%s: error: missing argument to '%s'\n\n",
                getProgramName(), option.c_str());
        usage();
      }
      char *end;
      numJobsInParallel = ::strtol(args[0].c_str(), &end, 10);
      if (*end != '\0') {
          fprintf(stderr, "%s: error: invalid argument '%s' to '%s'\n\n",
                  getProgramName(), args[0].c_str(), option.c_str());
          usage();
      }
      args.erase(args.begin());
    } else if (option == "--scheduler") {
      if (args.empty()) {
        fprintf(stderr, "%s: error: missing argument to '%s'\n\n",
                getProgramName(), option.c_str());
        break;
      }
      auto algorithm = args[0];
      if (algorithm == "commandNamePriority" || algorithm == "default") {
        schedulerAlgorithm = SchedulerAlgorithm::NamePriority;
      } else if (algorithm == "fifo") {
        schedulerAlgorithm = SchedulerAlgorithm::FIFO;
      } else {
        fprintf(stderr, "%s: error: unknown scheduler algorithm '%s'\n\n",
                getProgramName(), args[0].c_str());
        break;
      }
      args.erase(args.begin());
    } else if (StringRef(option).startswith("-j")) {
      char *end;
      numJobsInParallel = ::strtol(&option[2], &end, 10);
      if (*end != '\0') {
          fprintf(stderr, "%s: error: invalid argument '%s' to '-j'\n\n",
                  getProgramName(), &option[2]);
          usage();
      }
    } else if (option == "--no-regenerate") {
      autoRegenerateManifest = false;
    } else if (option == "--profile") {
      if (args.empty()) {
        fprintf(stderr, "%s: error: missing argument to '%s'\n\n",
                getProgramName(), option.c_str());
        usage();
      }
      profileFilename = args[0];
      args.erase(args.begin());
    } else if (option == "--strict") {
      strict = true;
    } else if (option == "-t" || option == "--tool") {
      if (args.empty()) {
        fprintf(stderr, "%s: error: missing argument to '%s'\n\n",
                getProgramName(), option.c_str());
        usage();
      }
      customTool = args[0];
      args.erase(args.begin());
    } else if (option == "-d") {
      if (args.empty()) {
        fprintf(stderr, "%s: error: missing argument to '%s'\n\n",
                getProgramName(), option.c_str());
        usage();
      }
      debugTools.push_back(args[0]);
      args.erase(args.begin());
    } else if (option == "--trace") {
      if (args.empty()) {
        fprintf(stderr, "%s: error: missing argument to '%s'\n\n",
                getProgramName(), option.c_str());
        usage();
      }
      traceFilename = args[0];
      args.erase(args.begin());
    } else if (option == "-v" || option == "--verbose") {
      verbose = true;
    } else {
      fprintf(stderr, "%s: error: invalid option: '%s'\n\n",
              getProgramName(), option.c_str());
      usage();
    }
  }

  if (maximumLoadAverage > 0.0) {
    fprintf(stderr, "%s: warning: maximum load average %.8g not implemented\n",
            getProgramName(), maximumLoadAverage);
  }

  if (!debugTools.empty()) {
    if (std::find(debugTools.begin(), debugTools.end(), "list")
        != debugTools.end()) {
      printf("debugging modes:\n");
      printf("no debugging modes supported\n");
    } else {
      fprintf(stderr, "%s: error: unknown debug mode '%s'\n",
              getProgramName(), debugTools.front().c_str());
    }
    return 1;
  }

  // Honor the --chdir option, if used.
  if (!chdirPath.empty()) {
    if (!sys::chdir(chdirPath.c_str())) {
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

  if (!customTool.empty()) {
    std::vector<std::string> availableTools = {
      "clean",
      "targets",
      "list",
    };

    if (std::find(availableTools.begin(), availableTools.end(), customTool) ==
        availableTools.end()) {
        fprintf(stderr, "error: unknown tool '%s'\n", customTool.c_str());
        return 1;
    } else if (customTool == "list") {
      if (!args.empty()) {
        fprintf(stderr, "error: unsupported arguments to tool '%s'\n",
                customTool.c_str());
        return 1;
      }

      fprintf(stdout, "available ninja tools:\n");
      for (const auto& tool: availableTools) {
        fprintf(stdout, "  %s\n", tool.c_str());
      }

      return 0;
    }
  }

  SmallString<128> current_dir;
  if (std::error_code ec = llvm::sys::fs::current_path(current_dir)) {
    fprintf(stderr, "%s: error: cannot determine current directory\n",
            getProgramName());
    return 1;
  }

  if (numJobsInParallel == 0) {
    unsigned numCPUs = std::thread::hardware_concurrency();
    if (numCPUs == 0) {
      fprintf(stderr, "%s: error: unable to detect number of CPUs (%s)",
              getProgramName(), strerror(errno));
      return 1;
    }

    numJobsInParallel = numCPUs + 2;
  }

  const std::string workingDirectory = current_dir.str().str();

  // Run up to two iterations, the first one loads the manifest and rebuilds it
  // if necessary, the second only runs if the manifest needs to be reloaded.
  //
  // This is somewhat inefficient in the case where the manifest needs to be
  // reloaded (we reopen the database, for example), but we don't expect that to
  // be a common case spot in practice.
  for (int iteration = 0; iteration != 2; ++iteration) {
    BuildContext context{workingDirectory};

    context.numFailedCommandsToTolerate = numFailedCommandsToTolerate;
    context.quiet = quiet;
    context.simulate = simulate;
    context.strict = strict;
    context.verbose = verbose;
    context.numJobsInParallel = numJobsInParallel;
    context.schedulerAlgorithm = schedulerAlgorithm;

    // Load the manifest.
    BuildManifestActions actions(context);
    ninja::ManifestLoader loader(workingDirectory, manifestFilename, actions);
    context.manifest = loader.load();

    // If there were errors loading, we are done.
    if (unsigned numErrors = actions.getNumErrors()) {
      context.emitNote("%d errors generated.", numErrors);
      return 1;
    }

    // Run the targets tool, if specified.
    if (!customTool.empty() && customTool == "targets") {
      if (args.size() != 1 || args[0] != "all") {
        if (args.empty()) {
          context.emitError("unsupported arguments to tool '%s'",
                            customTool.c_str());
        } else {
          context.emitError("unsupported argument to tool '%s': '%s'",
                            customTool.c_str(), args[0].c_str());
        }
        return 1;
      }

      for (const auto command: context.manifest->getCommands()) {
        for (const auto& output: command->getOutputs()) {
          fprintf(stdout, "%s: %s\n", output->getScreenPath().c_str(),
                  command->getRule()->getName().c_str());
        }
      }

      return 0;
    }

    // Emulate `-t clean` by removing the database.
    if (!customTool.empty() && customTool == "clean") {
      if(dbFilename.empty()) {
        context.emitError("unable to clean without a database. Command ignored.");
        return 1;
      } else {
        (void)basic::sys::unlink(dbFilename.c_str());
        context.emitNote("cleaned the build database, artifacts preserved.");
        return 0;
      }
    }

    // Otherwise, run the build.

    // Attach the database, if requested.
    if (!dbFilename.empty()) {
      std::string error;
      std::unique_ptr<core::BuildDB> db(
        core::createSQLiteBuildDB(dbFilename,
                                  BuildValue::currentSchemaVersion,
                                  /* recreateUnmatchedVersion = */ true,
                                  &error));
      if (!db || !context.engine.attachDB(std::move(db), &error)) {
        context.emitError("unable to open build database: %s", error.c_str());
        return 1;
      }
    }

    // Enable tracing, if requested.
    if (!traceFilename.empty()) {
      std::string error;
      if (!context.engine.enableTracing(traceFilename, &error)) {
        context.emitError("unable to enable tracing: %s", error.c_str());
        return 1;
      }
    }

    class NinjaBuildCommandRule: public core::Rule {
      BuildContext& context;
      ninja::Command* command;
    public:
      NinjaBuildCommandRule(const core::KeyType& key, BuildContext& context,
                            ninja::Command* command)
        : core::Rule(key), context(context), command(command) {}

      core::Task* createTask(core::BuildEngine&) override {
        return buildCommand(context, command);
      }

      bool isResultValid(core::BuildEngine&, const core::ValueType& value) override {
        // If simulating, assume cached results are valid.
        if (context.simulate)
          return true;

        return buildCommandIsResultValid(command, value);
      }

      void updateStatus(core::BuildEngine&, core::Rule::StatusKind status) override {
        updateCommandStatus(context, command, status);
      }
    };

    class NinjaCompositeResultRule: public core::Rule {
      BuildContext& context;
      ninja::Command* command;
      int index;
      const core::KeyType compositeRuleName;
    public:
      NinjaCompositeResultRule(const core::KeyType& key, BuildContext& context,
                            ninja::Command* command, int index,
                               const core::KeyType& compositeRuleName)
        : core::Rule(key), context(context), command(command), index(index)
        , compositeRuleName(compositeRuleName) {}

      core::Task* createTask(core::BuildEngine&) override {
        return selectCompositeBuildResult(context, command, index, compositeRuleName);
      }

      bool isResultValid(core::BuildEngine&, const core::ValueType& value) override {
        // If simulating, assume cached results are valid.
        if (context.simulate)
          return true;

        return selectCompositeIsResultValid(command, value);
      }
    };

    class NinjaBuildTargetsRule: public core::Rule {
      BuildContext& context;
      std::vector<std::string> targets;
    public:
      NinjaBuildTargetsRule(const core::KeyType& key, BuildContext& context,
                            const std::vector<std::string>& targets)
        : core::Rule(key), context(context), targets(targets) {}

      core::Task* createTask(core::BuildEngine&) override {
        return buildTargets(context, targets);
      }

      bool isResultValid(core::BuildEngine&, const core::ValueType& value) override {
        return false;
      }
    };


    // Create rules for all of the build commands up front.
    //
    // FIXME: We should probably also move this to be dynamic.
    for (const auto command: context.manifest->getCommands()) {
      // If this command has a single output, create the trivial rule.
      if (command->getOutputs().size() == 1) {
        context.engine.addRule(std::unique_ptr<core::Rule>(new NinjaBuildCommandRule(command->getOutputs()[0]->getCanonicalPath(), context, command)));
        continue;
      }

      // Otherwise, create a composite rule group for the multiple outputs.

      // Create a signature for the composite rule.
      //
      // FIXME: Make efficient.
      std::string compositeRuleName = "";
      for (auto& output: command->getOutputs()) {
        if (!compositeRuleName.empty())
          compositeRuleName += "&&";
        compositeRuleName += output->getCanonicalPath();
      }

      // Add the composite rule, which will run the command and build all
      // outputs.
      context.engine.addRule(std::unique_ptr<core::Rule>(new NinjaBuildCommandRule(compositeRuleName, context, command)));

      // Create the per-output selection rules that select the individual output
      // result from the composite result.
      for (unsigned i = 0, e = command->getOutputs().size(); i != e; ++i) {
        context.engine.addRule(std::unique_ptr<core::Rule>(new NinjaCompositeResultRule(command->getOutputs()[i]->getCanonicalPath(), context, command, i, compositeRuleName)));
      }
    }

    // If this is the first iteration, build the manifest, unless disabled.
    if (autoRegenerateManifest && iteration == 0) {
      SmallString<256> absManifestPath = StringRef(manifestFilename);
      llbuild::ninja::Manifest::normalize_path(workingDirectory, absManifestPath);
      context.engine.build(StringRef(absManifestPath));

      // If the manifest was rebuilt, then reload it and build again.
      if (context.numBuiltCommands) {
        continue;
      }

      // Otherwise, perform the main build.
      //
      // FIXME: This is somewhat inefficient, as we will end up repeating any
      // dependency scanning that was required for checking the manifest. We can
      // fix this by building the manifest inline with the targets...
    }

    // If using a build profile, open it.
    if (!profileFilename.empty()) {
      context.profileFP = ::fopen(profileFilename.c_str(), "w");
      if (!context.profileFP) {
        context.emitError("unable to open build profile '%s' (%s)\n",
                          profileFilename.c_str(), strerror(errno));
        return 1;
      }

      fprintf(context.profileFP, "[\n");
    }

    // Parse the positional arguments.
    std::vector<std::string> targetsToBuild;

    for (const std::string& arg: args) {
      if (auto *node = context.manifest->findNode(context.workingDirectory, arg)) {
          targetsToBuild.push_back(node->getCanonicalPath());
      } else {
        fprintf(stderr, "%s: error: unknown target: '%s'\n",
            getProgramName(), arg.c_str());
        exit(1);
      }
    }

    // If no explicit targets were named, build the default targets.
    if (targetsToBuild.empty()) {
      for (auto& target: context.manifest->getDefaultTargets())
        targetsToBuild.push_back(target->getCanonicalPath());

      // If there are no default targets, then build all of the root targets.
      if (targetsToBuild.empty()) {
        std::unordered_set<const ninja::Node*> inputNodes;

        // Collect all of the input nodes.
        for (const auto& command: context.manifest->getCommands()) {
          for (const auto* input: command->getInputs()) {
            inputNodes.emplace(input);
          }
        }

        // Build all of the targets that are not an input.
        for (const auto& command: context.manifest->getCommands()) {
          for (const auto& output: command->getOutputs()) {
            if (!inputNodes.count(output)) {
              targetsToBuild.push_back(output->getCanonicalPath());
            }
          }
        }
      }
    }

    // Generate an error if there is nothing to build.
    if (targetsToBuild.empty()) {
      context.emitError("no targets to build");
      return 1;
    }

    // If building multiple targets, do so via a dummy rule to allow them to
    // build concurrently (and without duplicates).
    //
    // FIXME: We should sort out eventually whether the engine itself should
    // support this. It seems like an obvious feature, but it is also trivial
    // for the client to implement on top of the existing API.
    if (targetsToBuild.size() > 1) {
      // Create a dummy rule to build all targets.
      context.engine.addRule(std::unique_ptr<core::Rule>(new NinjaBuildTargetsRule("<<build>>", context,
                                                       targetsToBuild)));
      context.engine.build("<<build>>");
    } else {
      context.engine.build(targetsToBuild[0]);
    }

    if (!dumpGraphPath.empty()) {
      context.engine.dumpGraphToFile(dumpGraphPath);
    }

    // Close the build profile, if used.
    if (context.profileFP) {
      ::fclose(context.profileFP);

      context.emitNote(
          "wrote build profile to '%s', use Chrome's about:tracing to view.",
          profileFilename.c_str());
    }

    // If the build was cancelled by SIGINT, cause ourself to also die by SIGINT
    // to support proper shell behavior.
    if (context.wasCancelledBySigint) {
      // Ensure SIGINT action is default.
#if defined(_WIN32)
      signal(SIGINT, SIG_DFL);
#else
      struct sigaction action {};
      action.sa_handler = SIG_DFL;
      sigaction(SIGINT, &action, 0);
#endif

#if defined(_WIN32)
      raise(SIGINT);
#else
      kill(getpid(), SIGINT);
#endif
      std::this_thread::sleep_for(std::chrono::microseconds(1000));
      return 2;
    }

    // If there were command failures, report the count.
    if (context.numFailedCommands) {
      context.emitError("build had %d command failures",
                        context.numFailedCommands.load());
    }

    // If the build was stopped because of an error, return an error status.
    if (context.numErrors) {
      return 1;
    }

    // Otherwise, if nothing was done, print a single message to let the user
    // know we completed successfully.
    if (!context.quiet && !context.numBuiltCommands) {
      context.emitNote("no work to do.");
    }

    // If we reached here on the first iteration, then we don't need a second
    // and are done.
    if (iteration == 0)
      break;
  }

  // Return an appropriate exit status.
  return 0;
}

std::unique_ptr<basic::ExecutionQueue> NinjaBuildEngineDelegate::createExecutionQueue() {
  return std::unique_ptr<basic::ExecutionQueue>(
    createLaneBasedExecutionQueue(*context, context->numJobsInParallel, context->schedulerAlgorithm, getDefaultQualityOfService(), nullptr)
  );
}
