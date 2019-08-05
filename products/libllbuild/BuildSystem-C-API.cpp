//===-- BuildSystem-C-API.cpp ---------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2015 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

// Include the public API.
#include <llbuild/llbuild.h>

#include "llbuild/Basic/FileSystem.h"
#include "llbuild/BuildSystem/BuildFile.h"
#include "llbuild/BuildSystem/BuildKey.h"
#include "llbuild/BuildSystem/BuildSystemCommandInterface.h"
#include "llbuild/BuildSystem/BuildSystemFrontend.h"
#include "llbuild/BuildSystem/ExternalCommand.h"
#include "llbuild/Core/BuildEngine.h"
#include "llbuild/Core/DependencyInfoParser.h"

#include "BuildKey-C-API-Private.h"

#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include <atomic>
#include <cassert>
#include <memory>

using namespace llbuild;
using namespace llbuild::basic;
using namespace llbuild::buildsystem;

/* Build Engine API */

namespace {
class CAPIFileSystem : public basic::FileSystem {
  llb_buildsystem_delegate_t cAPIDelegate;
  std::unique_ptr<basic::FileSystem> localFileSystem;
  
public:
  CAPIFileSystem(llb_buildsystem_delegate_t delegate)
      : cAPIDelegate(delegate),
        localFileSystem(basic::createLocalFileSystem()) { }

  virtual bool
  createDirectory(const std::string& path) override {
    if (!cAPIDelegate.fs_create_directory) {
      return localFileSystem->createDirectory(path);
    }

    return cAPIDelegate.fs_create_directory(cAPIDelegate.context, path.c_str());
  }
  
  virtual std::unique_ptr<llvm::MemoryBuffer>
  getFileContents(const std::string& path) override {
    if (!cAPIDelegate.fs_get_file_contents) {
      return localFileSystem->getFileContents(path);
    }
    
    llb_data_t data;
    if (!cAPIDelegate.fs_get_file_contents(cAPIDelegate.context, path.c_str(),
                                           &data)) {
      return nullptr;
    }

    // Create a new memory buffer to copy the data to.
    //
    // FIXME: This is an unfortunate amount of copying.
    llvm::StringRef fileContents = llvm::StringRef((const char*)data.data, data.length);
    auto result = llvm::MemoryBuffer::getMemBufferCopy(fileContents, path);

    // Release the client memory.
    //
    // FIXME: This is gross, come up with a general purpose solution.
    free((char*)data.data);

    return result;
  }

  virtual bool remove(const std::string& path) override {
    if (!cAPIDelegate.fs_remove) {
      return localFileSystem->remove(path);
    }

    return cAPIDelegate.fs_remove(cAPIDelegate.context, path.c_str());
  }

  virtual basic::FileInfo getFileInfo(const std::string& path) override {
    if (!cAPIDelegate.fs_get_file_info) {
      return localFileSystem->getFileInfo(path);
    }

    llb_fs_file_info_t file_info;
    cAPIDelegate.fs_get_file_info(cAPIDelegate.context, path.c_str(),
                                  &file_info);

    basic::FileInfo result{};
    result.device = file_info.device;
    result.inode = file_info.inode;
    result.mode = file_info.mode;
    result.size = file_info.size;
    result.modTime.seconds = file_info.mod_time.seconds;;
    result.modTime.nanoseconds = file_info.mod_time.nanoseconds;
    return result;
  }

  virtual basic::FileInfo getLinkInfo(const std::string& path) override {
    if (!cAPIDelegate.fs_get_link_info && !cAPIDelegate.fs_get_file_info) {
      return localFileSystem->getLinkInfo(path);
    }

    llb_fs_file_info_t file_info;
    if (cAPIDelegate.fs_get_link_info) {
      cAPIDelegate.fs_get_link_info(cAPIDelegate.context, path.c_str(),
                                    &file_info);
    } else {
      cAPIDelegate.fs_get_file_info(cAPIDelegate.context, path.c_str(),
                                    &file_info);
    }

    basic::FileInfo result{};
    result.device = file_info.device;
    result.inode = file_info.inode;
    result.mode = file_info.mode;
    result.size = file_info.size;
    result.modTime.seconds = file_info.mod_time.seconds;;
    result.modTime.nanoseconds = file_info.mod_time.nanoseconds;
    return result;
  }
};
  
class CAPIBuildSystemFrontendDelegate : public BuildSystemFrontendDelegate {
  llb_buildsystem_delegate_t cAPIDelegate;

  llb_buildsystem_command_result_t get_command_result(ProcessStatus commandResult) {
    switch (commandResult) {
      case ProcessStatus::Succeeded:
        return llb_buildsystem_command_result_succeeded;
      case ProcessStatus::Cancelled:
        return llb_buildsystem_command_result_cancelled;
      case ProcessStatus::Failed:
        return llb_buildsystem_command_result_failed;
      case ProcessStatus::Skipped:
        return llb_buildsystem_command_result_skipped;
      default:
        assert(0 && "unknown command result");
        break;
    }
    return llb_buildsystem_command_result_failed;
  }

public:
  CAPIBuildSystemFrontendDelegate(llvm::SourceMgr& sourceMgr,
                                  BuildSystemInvocation& invocation,
                                  llb_buildsystem_delegate_t delegate)
      : BuildSystemFrontendDelegate(sourceMgr, invocation, "basic", 0),
        cAPIDelegate(delegate) { }

  virtual std::unique_ptr<Tool> lookupTool(StringRef name) override {
    if (!cAPIDelegate.lookup_tool) {
      return nullptr;
    }
    
    llb_data_t cName{ name.size(), (const uint8_t*) name.data() };
    auto tool = cAPIDelegate.lookup_tool(cAPIDelegate.context, &cName);
    if (!tool) {
      return nullptr;
    }

    return std::unique_ptr<Tool>((Tool*)tool);
  }

  virtual void hadCommandFailure() override {
    // Call the base implementation.
    BuildSystemFrontendDelegate::hadCommandFailure();

    // Report the command failure, if the client provided an appropriate
    // callback.
    if (cAPIDelegate.had_command_failure) {
      cAPIDelegate.had_command_failure(cAPIDelegate.context);
    } else {
      // Otherwise, the default behavior is to immediately cancel.
      cancel();
    }
  }

  static llb_buildsystem_command_status_kind_t
  convertStatusKind(BuildSystemDelegate::CommandStatusKind kind) {
    switch (kind) {
    case BuildSystemDelegate::CommandStatusKind::IsScanning:
      return llb_buildsystem_command_status_kind_is_scanning;
    case BuildSystemDelegate::CommandStatusKind::IsUpToDate:
      return llb_buildsystem_command_status_kind_is_up_to_date;
    case BuildSystemDelegate::CommandStatusKind::IsComplete:
      return llb_buildsystem_command_status_kind_is_complete;
    }
    assert(0 && "unknown status kind");
    return llb_buildsystem_command_status_kind_is_scanning;
  }
  
  virtual void
  commandStatusChanged(Command* command,
                       BuildSystemDelegate::CommandStatusKind kind) override {
    if (cAPIDelegate.command_status_changed) {
      cAPIDelegate.command_status_changed(
          cAPIDelegate.context, (llb_buildsystem_command_t*) command,
          convertStatusKind(kind));
    }
  }

  virtual void commandPreparing(Command* command) override {
    if (cAPIDelegate.command_preparing) {
      cAPIDelegate.command_preparing(
          cAPIDelegate.context,
          (llb_buildsystem_command_t*) command);
    }
  }

  virtual bool shouldCommandStart(Command * command) override {
    if (cAPIDelegate.should_command_start) {
      return cAPIDelegate.should_command_start(
          cAPIDelegate.context, (llb_buildsystem_command_t*) command);
    } else {
      return true;
    }
  }

  virtual void commandStarted(Command* command) override {
    if (cAPIDelegate.command_started) {
      cAPIDelegate.command_started(
          cAPIDelegate.context,
          (llb_buildsystem_command_t*) command);
    }
  }

  virtual void commandFinished(Command* command, ProcessStatus commandResult) override {
    if (cAPIDelegate.command_finished) {
      cAPIDelegate.command_finished(
          cAPIDelegate.context,
          (llb_buildsystem_command_t*) command,
          get_command_result(commandResult));
    }
  }

  virtual void commandHadError(Command* command, StringRef message) override {
    if (cAPIDelegate.command_had_error) {
      llb_data_t cMessage { message.size(), (const uint8_t*) message.data() };
      cAPIDelegate.command_had_error(
          cAPIDelegate.context,
          (llb_buildsystem_command_t*) command,
          &cMessage);
    }
  }

  virtual void commandHadNote(Command* command, StringRef message) override {
    if (cAPIDelegate.command_had_note) {
      llb_data_t cMessage { message.size(), (const uint8_t*) message.data() };
      cAPIDelegate.command_had_note(
          cAPIDelegate.context,
          (llb_buildsystem_command_t*) command,
          &cMessage);
    }
  }

  virtual void commandHadWarning(Command* command, StringRef message) override {
    if (cAPIDelegate.command_had_warning) {
      llb_data_t cMessage { message.size(), (const uint8_t*) message.data() };
      cAPIDelegate.command_had_warning(
         cAPIDelegate.context,
         (llb_buildsystem_command_t*) command,
         &cMessage);
    }
  }

  virtual void commandCannotBuildOutputDueToMissingInputs(Command* command,
               Node* outputNode, SmallPtrSet<Node*, 1> inputNodes) override {
    if (cAPIDelegate.command_cannot_build_output_due_to_missing_inputs) {
      auto str = outputNode->getName().str();
      auto output = new CAPIBuildKey(BuildKey::makeNode(str));

      CAPINodesVector inputs(inputNodes);

      cAPIDelegate.command_cannot_build_output_due_to_missing_inputs(
        cAPIDelegate.context,
        (llb_buildsystem_command_t*) command,
        (llb_build_key_t **)&output,
        inputs.data(),
        inputs.count()
      );

      llb_build_key_destroy((llb_build_key_t *)output);
    }
  }

  class CAPINodesVector {
  private:
    std::vector<CAPIBuildKey *> keys;

  public:
    CAPINodesVector(const SmallPtrSet<Node*, 1> inputNodes) : keys(inputNodes.size()) {
      int idx = 0;
      for (auto inputNode : inputNodes) {
        keys[idx++] = new CAPIBuildKey(BuildKey::makeNode(inputNode->getName()));
      }
    }

    ~CAPINodesVector() {
      for (auto key : keys) {
        llb_build_key_destroy((llb_build_key_t *)key);
      }
    }

    llb_build_key_t** data() { return (llb_build_key_t **)&keys[0]; }
    uint64_t count() { return keys.size(); }
  };

  virtual void cannotBuildNodeDueToMultipleProducers(Node* outputNode,
               std::vector<Command*> commands) override {
    if (cAPIDelegate.cannot_build_node_due_to_multiple_producers) {
      auto str = outputNode->getName().str();
      auto output = (llb_build_key_t *)new CAPIBuildKey(BuildKey::makeNode(str));

      cAPIDelegate.cannot_build_node_due_to_multiple_producers(
        cAPIDelegate.context,
        &output,
        (llb_buildsystem_command_t**)commands.data(),
        commands.size()
      );
      
      llb_build_key_destroy(output);
    }
  }

  virtual void commandProcessStarted(Command* command,
                                     ProcessHandle handle) override {
    if (cAPIDelegate.command_process_started) {
      cAPIDelegate.command_process_started(
          cAPIDelegate.context,
          (llb_buildsystem_command_t*) command,
          (llb_buildsystem_process_t*) handle.id);
    }
  }

  virtual void commandProcessHadError(Command* command, ProcessHandle handle,
                                      const Twine& message) override {
    if (cAPIDelegate.command_process_had_error) {
      SmallString<256> data;
      message.toVector(data);
      llb_data_t cData{ data.size(), (const uint8_t*) data.data() };
      cAPIDelegate.command_process_had_error(
          cAPIDelegate.context,
          (llb_buildsystem_command_t*) command,
          (llb_buildsystem_process_t*) handle.id,
          &cData);
    }
  }

  virtual void commandProcessHadOutput(Command* command, ProcessHandle handle,
                                       StringRef data) override {
    if (cAPIDelegate.command_process_had_output) {
      llb_data_t cData{ data.size(), (const uint8_t*) data.data() };
      cAPIDelegate.command_process_had_output(
          cAPIDelegate.context,
          (llb_buildsystem_command_t*) command,
          (llb_buildsystem_process_t*) handle.id,
          &cData);
    }
  }
  
  virtual void commandProcessFinished(Command* command, ProcessHandle handle,
                                      const ProcessResult& commandResult) override {
    if (cAPIDelegate.command_process_finished) {
      llb_buildsystem_command_extended_result_t result;
      result.result = get_command_result(commandResult.status);
      result.exit_status = commandResult.exitCode;
      result.pid = commandResult.pid;
      result.utime = commandResult.utime;
      result.stime = commandResult.stime;
      result.maxrss = commandResult.maxrss;

      cAPIDelegate.command_process_finished(
          cAPIDelegate.context,
          (llb_buildsystem_command_t*) command,
          (llb_buildsystem_process_t*) handle.id,
          &result);
    }
  }

  /// Request cancellation of any current build.
  void cancel() override {
    BuildSystemFrontendDelegate::cancel();
  }

  class CAPIRulesVector {
  private:
    std::vector<CAPIBuildKey *> rules;

  public:
    CAPIRulesVector(const std::vector<core::Rule*>& items) : rules(items.size()) {
      int idx = 0;

      for (std::vector<core::Rule*>::const_iterator it = items.begin(); it != items.end(); ++it) {
        core::Rule* rule = *it;
        auto key = BuildKey::fromData(rule->key);
        rules[idx++] = new CAPIBuildKey(key);
      }
    }

    ~CAPIRulesVector() {
      for (auto rule : rules) {
        llb_build_key_destroy((llb_build_key_t *)rule);
      }
    }

    llb_build_key_t** data() { return (llb_build_key_t** )&rules[0]; }
    uint64_t count() { return rules.size(); }
  };


  virtual void cycleDetected(const std::vector<core::Rule*>& items) override {
    CAPIRulesVector rules(items);
    cAPIDelegate.cycle_detected(cAPIDelegate.context, rules.data(), rules.count());
  }

  static llb_cycle_action_t
  convertCycleAction(core::Rule::CycleAction action) {
    switch (action) {
      case core::Rule::CycleAction::ForceBuild:
        return llb_cycle_action_force_build;
      case core::Rule::CycleAction::SupplyPriorValue:
        return llb_cycle_action_supply_prior_value;
    }
    assert(0 && "unknown cycle action");
    return llb_cycle_action_force_build;
  }

  virtual bool shouldResolveCycle(const std::vector<core::Rule*>& items,
                                  core::Rule* candidateRule,
                                  core::Rule::CycleAction action) override {
    if (!cAPIDelegate.should_resolve_cycle)
      return false;

    CAPIRulesVector rules(items);
    auto key = BuildKey::fromData(candidateRule->key);
    auto candidate = (llb_build_key_t *)new CAPIBuildKey(key);

    uint8_t result = cAPIDelegate.should_resolve_cycle(cAPIDelegate.context,
                                                       rules.data(),
                                                       rules.count(),
                                                       candidate,
                                                       convertCycleAction(action));

    llb_build_key_destroy(candidate);

    return (result);
  }

  virtual void error(StringRef filename, const Token& at, const Twine& message) override {
    if (cAPIDelegate.handle_diagnostic) {
      cAPIDelegate.handle_diagnostic(cAPIDelegate.context,
                                     llb_buildsystem_diagnostic_kind_error,
                                     filename.str().c_str(),
                                     -1,
                                     -1,
                                     message.str().c_str());
    } else {
        BuildSystemFrontendDelegate::error(filename, at, message);
    }
  }
};

class CAPIBuildSystem {
  llb_buildsystem_delegate_t cAPIDelegate;
  
  BuildSystemInvocation invocation;
  
  llvm::SourceMgr sourceMgr;

  std::unique_ptr<CAPIBuildSystemFrontendDelegate> frontendDelegate;
  std::unique_ptr<BuildSystemFrontend> frontend;

  void handleDiagnostic(const llvm::SMDiagnostic& diagnostic) {
    llb_buildsystem_diagnostic_kind_t kind = llb_buildsystem_diagnostic_kind_note;
    switch (diagnostic.getKind()) {
    case llvm::SourceMgr::DK_Error:
      kind = llb_buildsystem_diagnostic_kind_error;
      break;
    case llvm::SourceMgr::DK_Warning:
      kind = llb_buildsystem_diagnostic_kind_warning;
      break;
    case llvm::SourceMgr::DK_Note:
    case llvm::SourceMgr::DK_Remark:
      kind = llb_buildsystem_diagnostic_kind_note;
      break;
    }

    // FIXME: We don't currently expose the caret diagnostic information, or
    // fixits. llbuild does currently make use of the caret diagnostics for
    // reporting problems in build manifest files...
    cAPIDelegate.handle_diagnostic(
        cAPIDelegate.context, kind,
        diagnostic.getFilename().str().c_str(),
        diagnostic.getLineNo(), diagnostic.getColumnNo(),
        diagnostic.getMessage().str().c_str());
  }
  
public:
  CAPIBuildSystem(llb_buildsystem_delegate_t delegate,
                  llb_buildsystem_invocation_t cAPIInvocation)
    : cAPIDelegate(delegate)
  {
    // Convert the invocation.
    invocation.buildFilePath =
      cAPIInvocation.buildFilePath ? cAPIInvocation.buildFilePath : "";
    invocation.dbPath = cAPIInvocation.dbPath ? cAPIInvocation.dbPath : "";
    invocation.traceFilePath = (
        cAPIInvocation.traceFilePath ? cAPIInvocation.traceFilePath : "");
    invocation.environment = cAPIInvocation.environment;
    invocation.useSerialBuild = cAPIInvocation.useSerialBuild;
    invocation.showVerboseStatus = cAPIInvocation.showVerboseStatus;
    invocation.schedulerLanes = cAPIInvocation.schedulerLanes;

    // Register a custom diagnostic handler with the source manager.
    sourceMgr.setDiagHandler([](const llvm::SMDiagnostic& diagnostic,
                                void* context) {
        auto system = (CAPIBuildSystem*) context;
        system->handleDiagnostic(diagnostic);
      }, this);
    
    // Allocate the frontend delegate.
    frontendDelegate.reset(
        // FIXME: Need to get the client name and schema version from
        // parameters.
        new CAPIBuildSystemFrontendDelegate(sourceMgr, invocation, delegate));

    // Allocate the file system
    std::unique_ptr<basic::FileSystem> fileSystem(new CAPIFileSystem(delegate));

    // Allocate the actual frontend.
    frontend.reset(new BuildSystemFrontend(*frontendDelegate, invocation,
                                           std::move(fileSystem)));
  }

  BuildSystemFrontend& getFrontend() {
    return *frontend;
  }

  bool initialize() {
    return getFrontend().initialize();
  }

  bool build(const core::KeyType& key) {
    // Reset mutable build state.
    frontendDelegate->resetForBuild();

    // FIXME: We probably should return a context to represent the running
    // build, instead of keeping state (like cancellation) in the delegate.
    return getFrontend().build(key);
  }

  bool buildNode(const core::KeyType& key) {
    frontendDelegate->resetForBuild();
    return getFrontend().buildNode(key);
  }

  void cancel() {
    frontendDelegate->cancel();
  }
};

class CAPITool : public Tool {
  llb_buildsystem_tool_delegate_t cAPIDelegate;
  
public:
  CAPITool(StringRef name, llb_buildsystem_tool_delegate_t delegate)
      : Tool(name), cAPIDelegate(delegate) {}

  virtual bool configureAttribute(const ConfigureContext& context,
                                  StringRef name,
                                  StringRef value) override {
    // FIXME: Support custom attributes in client tools.
    return false;
  }
  
  virtual bool configureAttribute(const ConfigureContext& context,
                                  StringRef name,
                                  ArrayRef<StringRef> values) override {
    // FIXME: Support custom attributes in client tools.
    return false;
  }

  virtual bool configureAttribute(
      const ConfigureContext& ctx, StringRef name,
      ArrayRef<std::pair<StringRef, StringRef>> values) override {
    // FIXME: Support custom attributes in client tools.
    return false;
  }

  virtual std::unique_ptr<Command> createCommand(StringRef name) override {
    llb_data_t cName{ name.size(), (const uint8_t*) name.data() };
    return std::unique_ptr<Command>(
        (Command*) cAPIDelegate.create_command(cAPIDelegate.context, &cName));
  }

  virtual std::unique_ptr<Command>
  createCustomCommand(const BuildKey& key) override {
    // FIXME: Support dynamic commands in client tools.
    
    return Tool::createCustomCommand(key);
  }
};

class CAPIExternalCommand : public ExternalCommand {
  // FIXME: This is incredibly wasteful to copy everywhere. Rephrase things so
  // that the delegates are const and we just carry the context pointer around.
  llb_buildsystem_external_command_delegate_t cAPIDelegate;
  
  /// The path to the dependency output file, if used.
  std::string depsPath;

  bool processDiscoveredDependencies(BuildSystemCommandInterface& bsci,
                                     core::Task* task,
                                     QueueJobContext* context) {
    // Read the dependencies file.
    auto input = bsci.getFileSystem().getFileContents(depsPath);
    if (!input) {
      bsci.getDelegate().commandHadError(this, "unable to open dependencies file (" + depsPath + ")");
      return false;
    }
    
    // Parse the output.
    //
    // We just ignore the rule, and add any dependency that we encounter in the
    // file.
    struct DepsActions : public core::DependencyInfoParser::ParseActions {
      BuildSystemCommandInterface& bsci;
      core::Task* task;
      CAPIExternalCommand* command;
      StringRef depsPath;
      unsigned numErrors{0};
      
      DepsActions(BuildSystemCommandInterface& bsci, core::Task* task,
                  CAPIExternalCommand* command, StringRef depsPath)
      : bsci(bsci), task(task), command(command), depsPath(depsPath) {}
      
      virtual void error(const char* message, uint64_t position) override {
        bsci.getDelegate().commandHadError(command, "error reading dependency file '" + depsPath.str() + "': " + std::string(message));
        ++numErrors;
      }
      
      // Ignore everything but actual inputs.
      virtual void actOnVersion(StringRef) override { }
      virtual void actOnMissing(StringRef) override { }
      virtual void actOnOutput(StringRef) override { }
      virtual void actOnInput(StringRef name) override {
        bsci.taskDiscoveredDependency(task, BuildKey::makeNode(name));
      }
    };
    
    DepsActions actions(bsci, task, this, depsPath);
    core::DependencyInfoParser(input->getBuffer(), actions).parse();
    return actions.numErrors == 0;
  }

  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  StringRef value) override {
    if (name == "deps") {
      depsPath = value;
    } else {
      return ExternalCommand::configureAttribute(ctx, name, value);
    }
    return true;
  }

  virtual void executeExternalCommand(BuildSystemCommandInterface& bsci,
                                               core::Task* task,
                                               QueueJobContext* job_context,
                                               llvm::Optional<ProcessCompletionFn> completionFn) override {
    auto result = cAPIDelegate.execute_command(
        cAPIDelegate.context, (llb_buildsystem_command_t*)this,
        (llb_buildsystem_command_interface_t*)&bsci,
        (llb_task_t*)task, (llb_buildsystem_queue_job_context_t*)job_context)
          ? ProcessStatus::Succeeded : ProcessStatus::Failed;

    if (result != ProcessStatus::Succeeded) {
      // If the command failed, there is no need to gather dependencies.
      if (completionFn.hasValue())
        completionFn.getValue()(result);
      return;
    }
    
    // Otherwise, collect the discovered dependencies, if used.
    if (!depsPath.empty()) {
      if (!processDiscoveredDependencies(bsci, task, job_context)) {
        // If we were unable to process the dependencies output, report a
        // failure.
        if (completionFn.hasValue())
          completionFn.getValue()(ProcessStatus::Failed);
        return;
      }
    }

    if (completionFn.hasValue())
      completionFn.getValue()(result);
  }
  
public:
  CAPIExternalCommand(StringRef name,
                      llb_buildsystem_external_command_delegate_t delegate)
      : ExternalCommand(name), cAPIDelegate(delegate) {}


  virtual void getShortDescription(SmallVectorImpl<char> &result) const override {
    // FIXME: Provide client control.
    if (!getDescription().empty()) {
      llvm::raw_svector_ostream(result) << getDescription();
    } else {
      llvm::raw_svector_ostream(result) << getName();
    }
  }

  virtual void getVerboseDescription(SmallVectorImpl<char> &result) const override {
    // FIXME: Provide client control.
    if (!getDescription().empty()) {
      llvm::raw_svector_ostream(result) << getDescription();
    } else {
      llvm::raw_svector_ostream(result) << getName();
    }
  }

  virtual llbuild::basic::CommandSignature getSignature() const override {
    auto sig = ExternalCommand::getSignature();
    if (cAPIDelegate.get_signature) {
      llb_data_t data;
      cAPIDelegate.get_signature(cAPIDelegate.context, (llb_buildsystem_command_t*)this,
                                 &data);
      sig = sig.combine(StringRef((const char*)data.data, data.length));

      // Release the client memory.
      //
      // FIXME: This is gross, come up with a general purpose solution.
      free((char*)data.data);
    }
    return sig;
  }
};

}

const char* llb_buildsystem_diagnostic_kind_get_name(
    llb_buildsystem_diagnostic_kind_t kind) {
  switch (kind) {
  case llb_buildsystem_diagnostic_kind_note:
    return "note";
  case llb_buildsystem_diagnostic_kind_warning:
    return "warning";
  case llb_buildsystem_diagnostic_kind_error:
    return "error";
  default:
    return "<unknown>";
  }
}

llb_buildsystem_t* llb_buildsystem_create(
    llb_buildsystem_delegate_t delegate,
    llb_buildsystem_invocation_t invocation) {
  // Check that all required methods are provided.
  assert(delegate.handle_diagnostic);
  assert(delegate.command_started);
  assert(delegate.command_finished);
  assert(delegate.command_process_started);
  assert(delegate.command_process_had_error);
  assert(delegate.command_process_had_output);
  assert(delegate.command_process_finished);
         
  return (llb_buildsystem_t*) new CAPIBuildSystem(delegate, invocation);
}

void llb_buildsystem_destroy(llb_buildsystem_t* system) {
  delete (CAPIBuildSystem*)system;
}

llb_buildsystem_tool_t*
llb_buildsystem_tool_create(const llb_data_t* name,
                            llb_buildsystem_tool_delegate_t delegate) {
  // Check that all required methods are provided.
  assert(delegate.create_command);
  return (llb_buildsystem_tool_t*) new CAPITool(
      StringRef((const char*)name->data, name->length), delegate);
}

bool llb_buildsystem_initialize(llb_buildsystem_t* system_p) {
  CAPIBuildSystem* system = (CAPIBuildSystem*) system_p;
  return system->initialize();
}

bool llb_buildsystem_build(llb_buildsystem_t* system_p, const llb_data_t* key) {
  CAPIBuildSystem* system = (CAPIBuildSystem*) system_p;
  return system->build(core::KeyType((const char*)key->data, key->length));
}

bool llb_buildsystem_build_node(llb_buildsystem_t* system_p, const llb_data_t* key) {
  CAPIBuildSystem* system = (CAPIBuildSystem*) system_p;
  return system->buildNode(core::KeyType((const char*)key->data, key->length));
}

void llb_buildsystem_cancel(llb_buildsystem_t* system_p) {
  CAPIBuildSystem* system = (CAPIBuildSystem*) system_p;
  system->cancel();
}

llb_buildsystem_command_t*
llb_buildsystem_external_command_create(
    const llb_data_t* name,
    llb_buildsystem_external_command_delegate_t delegate) {
  // Check that all required methods are provided.
  assert(delegate.execute_command);
  
  return (llb_buildsystem_command_t*) new CAPIExternalCommand(
      StringRef((const char*)name->data, name->length), delegate);
}

void llb_buildsystem_command_get_name(llb_buildsystem_command_t* command_p,
                                      llb_data_t* key_out) {
  auto command = (Command*) command_p;
  
  auto name = command->getName();
  key_out->length = name.size();
  key_out->data = (const uint8_t*) name.data();
}

bool llb_buildsystem_command_should_show_status(
    llb_buildsystem_command_t* command_p) {
  auto command = (Command*) command_p;
  return command->shouldShowStatus();
}

char* llb_buildsystem_command_get_description(
    llb_buildsystem_command_t* command_p) {
  auto command = (Command*) command_p;

  SmallString<256> result;
  command->getShortDescription(result);
  return strdup(result.c_str());
}

char* llb_buildsystem_command_get_verbose_description(
    llb_buildsystem_command_t* command_p) {
  auto command = (Command*) command_p;

  SmallString<256> result;
  command->getVerboseDescription(result);
  return strdup(result.c_str());
}

llb_quality_of_service_t llb_get_quality_of_service() {
  switch (getDefaultQualityOfService()) {
  case QualityOfService::Normal:
    return llb_quality_of_service_default;
  case QualityOfService::UserInitiated:
    return llb_quality_of_service_user_initiated;
  case QualityOfService::Utility:
    return llb_quality_of_service_utility;
  case QualityOfService::Background:
    return llb_quality_of_service_background;
  default:
    assert(0 && "unknown quality service level");
    return llb_quality_of_service_default;
  }
}

void llb_set_quality_of_service(llb_quality_of_service_t level) {
  switch (level) {
  case llb_quality_of_service_default:
    setDefaultQualityOfService(QualityOfService::Normal);
    break;
  case llb_quality_of_service_user_initiated:
    setDefaultQualityOfService(QualityOfService::UserInitiated);
    break;
  case llb_quality_of_service_utility:
    setDefaultQualityOfService(QualityOfService::Utility);
    break;
  case llb_quality_of_service_background:
    setDefaultQualityOfService(QualityOfService::Background);
    break;
  default:
    assert(0 && "unknown quality service level");
    break;
  }
}

void* llb_alloc(size_t size) { return malloc(size); }
void llb_free(void* ptr) { free(ptr); }
