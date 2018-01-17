//===-- BuildSystemFrontend.cpp -------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/BuildSystem/BuildSystemFrontend.h"

#include "llbuild/Basic/FileSystem.h"
#include "llbuild/Basic/LLVM.h"
#include "llbuild/Basic/PlatformUtility.h"
#include "llbuild/BuildSystem/BuildDescription.h"
#include "llbuild/BuildSystem/BuildExecutionQueue.h"
#include "llbuild/BuildSystem/BuildFile.h"
#include "llbuild/BuildSystem/BuildKey.h"
#include "llbuild/BuildSystem/BuildValue.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

using namespace llbuild;
using namespace llbuild::basic;
using namespace llbuild::buildsystem;

#pragma mark - BuildSystemInvocation implementation

void BuildSystemInvocation::getUsage(int optionWidth, raw_ostream& os) {
  const struct Options {
    llvm::StringRef option, helpText;
  } options[] = {
    { "--help", "show this help message and exit" },
    { "--version", "show the tool version" },
    { "-C <PATH>, --chdir <PATH>", "change directory to PATH before building" },
    { "--no-db", "disable use of a build database" },
    { "--db <PATH>", "enable building against the database at PATH" },
    { "-f <PATH>", "load the build task file at PATH" },
    { "--serial", "do not build in parallel" },
    { "-v, --verbose", "show verbose status information" },
    { "--trace <PATH>", "trace build engine operation to PATH" },
  };
  
  for (const auto& entry: options) {
    os << "  " << llvm::format("%-*s", optionWidth, entry.option) << " "
       << entry.helpText << "\n";
  }
}

void BuildSystemInvocation::parse(llvm::ArrayRef<std::string> args,
                                  llvm::SourceMgr& sourceMgr) {
  auto error = [&](const Twine &message) {
    sourceMgr.PrintMessage(llvm::SMLoc{}, llvm::SourceMgr::DK_Error, message);
    hadErrors = true;
  };

  while (!args.empty()) {
    const auto& option = args.front();
    args = args.slice(1);

    if (option == "-") {
      for (const auto& arg: args) {
        positionalArgs.push_back(arg);
      }
      break;
    }

    if (!option.empty() && option[0] != '-') {
      positionalArgs.push_back(option);
      continue;
    }
    
    if (option == "--help") {
      showUsage = true;
      break;
    } else if (option == "--version") {
      showVersion = true;
      break;
    } else if (option == "--no-db") {
      dbPath = "";
    } else if (option == "--db") {
      if (args.empty()) {
        error("missing argument to '" + option + "'");
        break;
      }
      dbPath = args[0];
      args = args.slice(1);
    } else if (option == "-C" || option == "--chdir") {
      if (args.empty()) {
        error("missing argument to '" + option + "'");
        break;
      }
      chdirPath = args[0];
      args = args.slice(1);
    } else if (option == "-f") {
      if (args.empty()) {
        error("missing argument to '" + option + "'");
        break;
      }
      buildFilePath = args[0];
      args = args.slice(1);
    } else if (option == "--serial") {
      useSerialBuild = true;
    } else if (option == "-v" || option == "--verbose") {
      showVerboseStatus = true;
    } else if (option == "--trace") {
      if (args.empty()) {
        error("missing argument to '" + option + "'");
        break;
      }
      traceFilePath = args[0];
      args = args.slice(1);
    } else {
      error("invalid option '" + option + "'");
      break;
    }
  }
}

std::string BuildSystemInvocation::formatDetectedCycle(const std::vector<core::Rule*>& cycle) {
  // Compute a description of the cycle path.
  SmallString<256> message;
  llvm::raw_svector_ostream os(message);
  os << "cycle detected while building: ";
  bool first = true;
  for (const auto* rule: cycle) {
    if (!first)
      os << " -> ";

    // Convert to a build key.
    auto key = BuildKey::fromData(rule->key);
    switch (key.getKind()) {
      case BuildKey::Kind::Unknown:
        os << "((unknown))";
        break;
      case BuildKey::Kind::Command:
        os << "command '" << key.getCommandName() << "'";
        break;
      case BuildKey::Kind::CustomTask:
        os << "custom task '" << key.getCustomTaskName() << "'";
        break;
      case BuildKey::Kind::DirectoryContents:
        os << "directory-contents '" << key.getDirectoryContentsPath() << "'";
        break;
      case BuildKey::Kind::DirectoryTreeSignature:
        os << "directory-tree-signature '"
        << key.getDirectoryTreeSignaturePath() << "'";
        break;
      case BuildKey::Kind::DirectoryTreeStructureSignature:
        os << "directory-tree-structure-signature '"
        << key.getDirectoryTreeStructureSignaturePath() << "'";
        break;
      case BuildKey::Kind::Node:
        os << "node '" << key.getNodeName() << "'";
        break;
      case BuildKey::Kind::Target:
        os << "target '" << key.getTargetName() << "'";
        break;
    }
    first = false;
  }

  return os.str().str();
}

#pragma mark - BuildSystemFrontendDelegate implementation

namespace {

struct BuildSystemFrontendDelegateImpl;

class BuildSystemFrontendExecutionQueueDelegate
      : public BuildExecutionQueueDelegate {
  BuildSystemFrontendDelegateImpl &delegateImpl;
  
  bool showVerboseOutput() const;

  BuildSystem& getSystem() const;
    
public:
  BuildSystemFrontendExecutionQueueDelegate(
          BuildSystemFrontendDelegateImpl& delegateImpl)
      : delegateImpl(delegateImpl) { }
  
  virtual void commandJobStarted(Command* command) override {
    static_cast<BuildSystemFrontendDelegate*>(&getSystem().getDelegate())->
      commandJobStarted(command);
  }

  virtual void commandJobFinished(Command* command) override {
    static_cast<BuildSystemFrontendDelegate*>(&getSystem().getDelegate())->
      commandJobFinished(command);
  }

  virtual void commandProcessStarted(Command* command,
                                     ProcessHandle handle) override {
    static_cast<BuildSystemFrontendDelegate*>(&getSystem().getDelegate())->
      commandProcessStarted(
          command, BuildSystemFrontendDelegate::ProcessHandle { handle.id });
  }
  
  virtual void commandProcessHadError(Command* command, ProcessHandle handle,
                                      const Twine& message) override {
    static_cast<BuildSystemFrontendDelegate*>(&getSystem().getDelegate())->
      commandProcessHadError(
          command, BuildSystemFrontendDelegate::ProcessHandle { handle.id },
          message);
  }
  
  virtual void commandProcessHadOutput(Command* command, ProcessHandle handle,
                                       StringRef data) override {
    static_cast<BuildSystemFrontendDelegate*>(&getSystem().getDelegate())->
      commandProcessHadOutput(
          command, BuildSystemFrontendDelegate::ProcessHandle { handle.id },
          data);
  }

  virtual void commandProcessFinished(Command* command, ProcessHandle handle,
                                      CommandResult result,
                                      int exitStatus) override {
    static_cast<BuildSystemFrontendDelegate*>(&getSystem().getDelegate())->
      commandProcessFinished(
          command, BuildSystemFrontendDelegate::ProcessHandle { handle.id },
          result, exitStatus);
  }
};

struct BuildSystemFrontendDelegateImpl {

  /// The status of delegate.
  enum class Status {
    Uninitialized = 0,
    Initialized,
    InitializationFailure,
    Cancelled,
  };

  llvm::SourceMgr& sourceMgr;
  const BuildSystemInvocation& invocation;
  
  StringRef bufferBeingParsed;
  std::atomic<unsigned> numErrors{0};
  std::atomic<unsigned> numFailedCommands{0};

  BuildSystemFrontendExecutionQueueDelegate executionQueueDelegate;

  BuildSystemFrontend* frontend = nullptr;
  BuildSystem* system = nullptr;

  /// The set of active command output buffers, by process handle.
  llvm::DenseMap<uintptr_t, std::vector<uint8_t>> processOutputBuffers;

  /// The lock protecting `processOutputBuffers`.
  std::mutex processOutputBuffersMutex;
  
  BuildSystemFrontendDelegateImpl(llvm::SourceMgr& sourceMgr,
                                  const BuildSystemInvocation& invocation)
      : sourceMgr(sourceMgr), invocation(invocation),
        executionQueueDelegate(*this) {}

private:
  /// The status of the delegate.
  std::atomic<Status> status{Status::Uninitialized};

public:

  /// Set the status of delegate to the given value.
  ///
  /// It is not possible to update the status once status is set to initialization failure.
  void setStatus(Status newStatus) {
    // Disallow changing status if there was an initialization failure.
    if (status == Status::InitializationFailure) {
      return;
    }
    status = newStatus;
  }

  /// Returns the current status.
  Status getStatus() {
    return status;
  }
};

bool BuildSystemFrontendExecutionQueueDelegate::showVerboseOutput() const {
  return delegateImpl.invocation.showVerboseStatus;
}

BuildSystem& BuildSystemFrontendExecutionQueueDelegate::getSystem() const {
  assert(delegateImpl.system);
  return *delegateImpl.system;
}

}

BuildSystemFrontendDelegate::
BuildSystemFrontendDelegate(llvm::SourceMgr& sourceMgr,
                            const BuildSystemInvocation& invocation,
                            StringRef name,
                            uint32_t version)
    : BuildSystemDelegate(name, version),
      impl(new BuildSystemFrontendDelegateImpl(sourceMgr, invocation))
{
  
}

BuildSystemFrontendDelegate::~BuildSystemFrontendDelegate() {
  delete static_cast<BuildSystemFrontendDelegateImpl*>(this->impl);
}

void
BuildSystemFrontendDelegate::setFileContentsBeingParsed(StringRef buffer) {
  auto impl = static_cast<BuildSystemFrontendDelegateImpl*>(this->impl);

  impl->bufferBeingParsed = buffer;
}

BuildSystemFrontend& BuildSystemFrontendDelegate::getFrontend() {
  auto impl = static_cast<BuildSystemFrontendDelegateImpl*>(this->impl);
  
  return *impl->frontend;
}

llvm::SourceMgr& BuildSystemFrontendDelegate::getSourceMgr() {
  auto impl = static_cast<BuildSystemFrontendDelegateImpl*>(this->impl);
  
  return impl->sourceMgr;
}

unsigned BuildSystemFrontendDelegate::getNumErrors() {
  auto impl = static_cast<BuildSystemFrontendDelegateImpl*>(this->impl);
  
  return impl->numErrors;
}

unsigned BuildSystemFrontendDelegate::getNumFailedCommands() {
  auto impl = static_cast<BuildSystemFrontendDelegateImpl*>(this->impl);

  return impl->numFailedCommands;
}

void
BuildSystemFrontendDelegate::error(const Twine& message) {
  error("", {}, message.str());
}

void
BuildSystemFrontendDelegate::error(StringRef filename,
                                   const Token& at,
                                   const Twine& message) {
  auto impl = static_cast<BuildSystemFrontendDelegateImpl*>(this->impl);
  
  ++impl->numErrors;

  // If we have a file and token, resolve the location and range to one
  // accessible by the source manager.
  //
  // FIXME: We shouldn't need to do this, but should switch llbuild to using
  // SourceMgr natively.
  llvm::SMLoc loc{};
  llvm::SMRange range{};
  if (!filename.empty() && at.start) {
    // FIXME: We ignore errors here, for now, this will be resolved when we move
    // to SourceMgr completely.
    auto buffer = getFileSystem().getFileContents(filename);
    if (buffer) {
      unsigned offset = at.start - impl->bufferBeingParsed.data();
      if (offset + at.length < buffer->getBufferSize()) {
        range.Start = loc = llvm::SMLoc::getFromPointer(
            buffer->getBufferStart() + offset);
        range.End = llvm::SMLoc::getFromPointer(
            buffer->getBufferStart() + (offset + at.length));
        getSourceMgr().AddNewSourceBuffer(std::move(buffer), llvm::SMLoc{});
      }
    }
  }

  if (range.Start.isValid()) {
    getSourceMgr().PrintMessage(loc, llvm::SourceMgr::DK_Error, message, range);
  } else {
    getSourceMgr().PrintMessage(loc, llvm::SourceMgr::DK_Error, message);
  }
  fflush(stderr);
}

std::unique_ptr<BuildExecutionQueue>
BuildSystemFrontendDelegate::createExecutionQueue() {
  auto impl = static_cast<BuildSystemFrontendDelegateImpl*>(this->impl);
  
  if (impl->invocation.useSerialBuild) {
    return std::unique_ptr<BuildExecutionQueue>(
        createLaneBasedExecutionQueue(impl->executionQueueDelegate, 1,
                                      impl->invocation.environment));
  }
    
  // Get the number of CPUs to use.
  unsigned numCPUs = std::thread::hardware_concurrency();
  unsigned numLanes;
  if (numCPUs == 0) {
    error("<unknown>", {}, "unable to detect number of CPUs");
    numLanes = 1;
  } else {
    numLanes = numCPUs;
  }
    
  return std::unique_ptr<BuildExecutionQueue>(
      createLaneBasedExecutionQueue(impl->executionQueueDelegate, numLanes,
                                    impl->invocation.environment));
}

void BuildSystemFrontendDelegate::cancel() {
  auto delegateImpl = static_cast<BuildSystemFrontendDelegateImpl*>(impl);
  assert(delegateImpl->getStatus() != BuildSystemFrontendDelegateImpl::Status::Uninitialized);

  // Update the status to cancelled.
  delegateImpl->setStatus(BuildSystemFrontendDelegateImpl::Status::Cancelled);

  auto system = delegateImpl->system;
  if (system) {
    system->cancel();
  }
}

void BuildSystemFrontendDelegate::resetForBuild() {
  auto impl = static_cast<BuildSystemFrontendDelegateImpl*>(this->impl);

  impl->numFailedCommands = 0;
  impl->numErrors = 0;

  // Update status back to initialized on reset.
  if (impl->getStatus() == BuildSystemFrontendDelegateImpl::Status::Cancelled) {
      impl->setStatus(BuildSystemFrontendDelegateImpl::Status::Initialized);
  }

  // Reset the build system.
  auto system = impl->system;
  if (system) {
    system->resetForBuild();
  }
}

void BuildSystemFrontendDelegate::hadCommandFailure() {
  auto impl = static_cast<BuildSystemFrontendDelegateImpl*>(this->impl);
  
  // Increment the failed command count.
  ++impl->numFailedCommands;
}

void BuildSystemFrontendDelegate::commandStatusChanged(Command*,
                                                       CommandStatusKind) {
}

void BuildSystemFrontendDelegate::commandPreparing(Command*) {
}

bool BuildSystemFrontendDelegate::shouldCommandStart(Command*) {
  return true;
}

void BuildSystemFrontendDelegate::commandStarted(Command* command) {
  // Don't report status if opted out by the command.
  if (!command->shouldShowStatus()) {
    return;
  }
  
  // Log the command.
  //
  // FIXME: Design the logging and status output APIs.
  SmallString<64> description;
  if (getFrontend().getInvocation().showVerboseStatus) {
    command->getVerboseDescription(description);
  } else {
    command->getShortDescription(description);

    // If the short description is empty, always show the verbose one.
    if (description.empty()) {
      command->getVerboseDescription(description);
    }
  }
  fprintf(stdout, "%s\n", description.c_str());
  fflush(stdout);
}

void BuildSystemFrontendDelegate::commandHadError(Command* command, StringRef data) {
  fwrite(data.data(), data.size(), 1, stderr);
  fflush(stderr);
}

void BuildSystemFrontendDelegate::commandHadNote(Command* command, StringRef data) {
  fwrite(data.data(), data.size(), 1, stdout);
  fflush(stdout);
}

void BuildSystemFrontendDelegate::commandHadWarning(Command* command, StringRef data) {
  fwrite(data.data(), data.size(), 1, stdout);
  fflush(stdout);
}

void BuildSystemFrontendDelegate::commandFinished(Command*, CommandResult) {
}

void BuildSystemFrontendDelegate::commandJobStarted(Command*) {
}

void BuildSystemFrontendDelegate::commandJobFinished(Command*) {
}

void BuildSystemFrontendDelegate::commandProcessStarted(Command*,
                                                        ProcessHandle) {
}
  
void BuildSystemFrontendDelegate::
commandProcessHadError(Command* command, ProcessHandle handle,
                       const Twine& message) {
  SmallString<256> buffer;
  auto str = message.toStringRef(buffer);
  
  // FIXME: Design the logging and status output APIs.
  fwrite(str.data(), str.size(), 1, stderr);
  fputc('\n', stderr);
  fflush(stderr);
}
  
void BuildSystemFrontendDelegate::
commandProcessHadOutput(Command* command, ProcessHandle handle,
                        StringRef data) {
  auto impl = static_cast<BuildSystemFrontendDelegateImpl*>(this->impl);
  std::unique_lock<std::mutex> lock(impl->processOutputBuffersMutex);

  // Append to the output buffer.
  auto& buffer = impl->processOutputBuffers[handle.id];
  buffer.insert(buffer.end(), data.begin(), data.end());
}

void BuildSystemFrontendDelegate::
commandProcessFinished(Command*, ProcessHandle handle,
                       CommandResult result,
                       int exitStatus) {
  auto impl = static_cast<BuildSystemFrontendDelegateImpl*>(this->impl);
  std::unique_lock<std::mutex> lock(impl->processOutputBuffersMutex);

  // If there was an output buffer, flush it.
  auto it = impl->processOutputBuffers.find(handle.id);
  if (it == impl->processOutputBuffers.end())
    return;

  fwrite(it->second.data(), it->second.size(), 1, stdout);
  fflush(stdout);

  impl->processOutputBuffers.erase(it);  
}

#pragma mark - BuildSystemFrontend implementation

BuildSystemFrontend::
BuildSystemFrontend(BuildSystemFrontendDelegate& delegate,
                    const BuildSystemInvocation& invocation)
    : delegate(delegate), invocation(invocation)
{
  auto delegateImpl =
    static_cast<BuildSystemFrontendDelegateImpl*>(delegate.impl);

  delegateImpl->frontend = this;
}

bool BuildSystemFrontend::initialize() {
  if (!invocation.chdirPath.empty()) {
    if (!sys::chdir(invocation.chdirPath.c_str())) {
      getDelegate().error(Twine("unable to honor --chdir: ") + strerror(errno));
      return false;
    }
  }

  // Create the build system.
  buildSystem.emplace(delegate);

  // Load the build file.
  if (!buildSystem->loadDescription(invocation.buildFilePath))
    return false;

  // Register the system back pointer.
  //
  // FIXME: Eliminate this.
  auto delegateImpl =
    static_cast<BuildSystemFrontendDelegateImpl*>(delegate.impl);
  delegateImpl->system = buildSystem.getPointer();
  
  // Enable tracing, if requested.
  if (!invocation.traceFilePath.empty()) {
    std::string error;
    if (!buildSystem->enableTracing(invocation.traceFilePath, &error)) {
      getDelegate().error(Twine("unable to enable tracing: ") + error);
      return false;
    }
  }

  // Attach the database.
  if (!invocation.dbPath.empty()) {
    // If the database path is relative, always make it relative to the input
    // file.
    SmallString<256> tmp;
    StringRef dbPath = invocation.dbPath;
    if (llvm::sys::path::is_relative(invocation.dbPath) &&
        dbPath.find("://") == StringRef::npos && !dbPath.startswith(":")) {
      llvm::sys::path::append(
          tmp, llvm::sys::path::parent_path(invocation.buildFilePath),
          invocation.dbPath);
      dbPath = tmp.str();
    }
    
    std::string error;
    if (!buildSystem->attachDB(dbPath, &error)) {
      getDelegate().error(Twine("unable to attach DB: ") + error);
      return false;
    }
  }

  return true;
}

bool BuildSystemFrontend::setupBuild() {
  auto delegateImpl =
  static_cast<BuildSystemFrontendDelegateImpl*>(delegate.impl);

  // We expect build to be called in these states only.
  assert(delegateImpl->getStatus() == BuildSystemFrontendDelegateImpl::Status::Uninitialized
         || delegateImpl->getStatus() == BuildSystemFrontendDelegateImpl::Status::Initialized);

  // Set the delegate status to initialized.
  delegateImpl->setStatus(BuildSystemFrontendDelegateImpl::Status::Initialized);

  // Initialize the build system, if necessary
  if (!buildSystem.hasValue()) {
    if (!initialize()) {
      // Set status to initialization failure. It is not possible to recover from this state.
      delegateImpl->setStatus(BuildSystemFrontendDelegateImpl::Status::InitializationFailure);
      return false;
    }
  }

  // If delegate was told to cancel while we were initializing, abort now.
  if (delegateImpl->getStatus() == BuildSystemFrontendDelegateImpl::Status::Cancelled) {
    return false;
  }

  return true;
}

bool BuildSystemFrontend::buildNode(StringRef nodeToBuild) {
  if (!setupBuild()) {
    return false;
  }

  auto buildValue = buildSystem->build(BuildKey::makeNode(nodeToBuild));
  if (!buildValue.hasValue()) {
    return false;
  }

  if (!buildValue.getValue().isExistingInput()) {
    if (buildValue.getValue().isMissingInput()) {
      delegate.error((Twine("missing input '") + nodeToBuild + "' and no rule to build it"));
    }
    return false;
  }

  return delegate.getNumFailedCommands() == 0 && delegate.getNumErrors() == 0;
}

bool BuildSystemFrontend::build(StringRef targetToBuild) {
  if (!setupBuild()) {
    return false;
  }

  // Build the target; if something unspecified failed about the build, return
  // an error.
  if (!buildSystem->build(targetToBuild))
    return false;

  // The build was successful if there were no failed commands or unspecified
  // errors.
  //
  // It is the job of the client to report a summary, if desired.
  return delegate.getNumFailedCommands() == 0 && delegate.getNumErrors() == 0;
}
