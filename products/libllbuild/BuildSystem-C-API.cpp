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
#include "llbuild/BuildSystem/CommandResult.h"
#include "llbuild/BuildSystem/ExternalCommand.h"
#include "llbuild/Core/BuildEngine.h"

#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include <atomic>
#include <cassert>
#include <memory>

using namespace llbuild;
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
    auto result = llvm::MemoryBuffer::getNewUninitMemBuffer(data.length, path);
    memcpy((char*)result->getBufferStart(), data.data, data.length);

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
  CAPIFileSystem fileSystem;

  llb_buildsystem_command_result_t get_command_result(CommandResult commandResult) {
    switch (commandResult) {
      case CommandResult::Succeeded:
        return llb_buildsystem_command_result_succeeded;
      case CommandResult::Cancelled:
        return llb_buildsystem_command_result_cancelled;
      case CommandResult::Failed:
        return llb_buildsystem_command_result_failed;
      case CommandResult::Skipped:
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
        cAPIDelegate(delegate), fileSystem(delegate) { }

  virtual basic::FileSystem& getFileSystem() override { return fileSystem; }
  
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

  virtual void commandFinished(Command* command, CommandResult commandResult) override {
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
                                      CommandResult commandResult,
                                      int exitStatus) override {
    if (cAPIDelegate.command_process_finished) {
      cAPIDelegate.command_process_finished(
          cAPIDelegate.context,
          (llb_buildsystem_command_t*) command,
          (llb_buildsystem_process_t*) handle.id,
          get_command_result(commandResult),
          exitStatus);
    }
  }

  /// Request cancellation of any current build.
  void cancel() override {
    BuildSystemFrontendDelegate::cancel();
  }

  virtual void cycleDetected(const std::vector<core::Rule*>& items) override {
    std::vector<llb_build_key_t> rules(items.size());
    int idx = 0;

    for (std::vector<core::Rule*>::const_iterator it = items.begin(); it != items.end(); ++it) {
      core::Rule* rule = *it;
      auto key = BuildKey::fromData(rule->key);
      auto& buildKey = rules[idx++];

      switch (key.getKind()) {
        case BuildKey::Kind::Command:
          buildKey.kind = llb_build_key_kind_command;
          buildKey.key = strdup(key.getCommandName().str().c_str());
          break;
        case BuildKey::Kind::CustomTask:
          buildKey.kind = llb_build_key_kind_custom_task;
          buildKey.key = strdup(key.getCustomTaskName().str().c_str());
          break;
        case BuildKey::Kind::DirectoryContents:
          buildKey.kind = llb_build_key_kind_directory_contents;
          buildKey.key = strdup(key.getDirectoryContentsPath().str().c_str());
          break;
        case BuildKey::Kind::DirectoryTreeSignature:
          buildKey.kind = llb_build_key_kind_directory_tree_signature;
          buildKey.key = strdup(
              key.getDirectoryTreeSignaturePath().str().c_str());
          break;
        case BuildKey::Kind::DirectoryTreeStructureSignature:
          buildKey.kind = llb_build_key_kind_directory_tree_structure_signature;
          buildKey.key = strdup(
              key.getDirectoryTreeStructureSignaturePath().str().c_str());
          break;
        case BuildKey::Kind::Node:
          buildKey.kind = llb_build_key_kind_node;
          buildKey.key = strdup(key.getNodeName().str().c_str());
          break;
        case BuildKey::Kind::Target:
          buildKey.kind = llb_build_key_kind_target;
          buildKey.key = strdup(key.getTargetName().str().c_str());
          break;
        case BuildKey::Kind::Unknown:
          buildKey.kind = llb_build_key_kind_unknown;
          buildKey.key = strdup("((unknown))");
          break;
      }
    }

    cAPIDelegate.cycle_detected(cAPIDelegate.context, &rules[0], rules.size());

    for (unsigned long i=0;i<rules.size();i++) {
      free((char *)rules[i].key);
    }
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
    llb_buildsystem_diagnostic_kind_t kind;
    switch (diagnostic.getKind()) {
    case llvm::SourceMgr::DK_Error:
      kind = llb_buildsystem_diagnostic_kind_error;
      break;
    case llvm::SourceMgr::DK_Warning:
      kind = llb_buildsystem_diagnostic_kind_warning;
      break;
    case llvm::SourceMgr::DK_Note:
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

    // Allocate the actual frontend.
    frontend.reset(new BuildSystemFrontend(*frontendDelegate, invocation));
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

  virtual CommandResult executeExternalCommand(BuildSystemCommandInterface& bsci,
                                               core::Task* task,
                                               QueueJobContext* job_context) override {
    return cAPIDelegate.execute_command(
        cAPIDelegate.context, (llb_buildsystem_command_t*)this,
        (llb_buildsystem_command_interface_t*)&bsci,
        (llb_task_t*)task, (llb_buildsystem_queue_job_context_t*)job_context) ? CommandResult::Succeeded : CommandResult::Failed;
  }

public:
  CAPIExternalCommand(StringRef name,
                      llb_buildsystem_external_command_delegate_t delegate)
      : ExternalCommand(name), cAPIDelegate(delegate) {}


  virtual void getShortDescription(SmallVectorImpl<char> &result) override {
    // FIXME: Provide client control.
    llvm::raw_svector_ostream(result) << getName();
  }

  virtual void getVerboseDescription(SmallVectorImpl<char> &result) override {
    // FIXME: Provide client control.
    llvm::raw_svector_ostream(result) << getName();
  }

  virtual uint64_t getSignature() override {
    // FIXME: Use a more appropriate hashing infrastructure.
    using llvm::hash_combine;
    llvm::hash_code code = ExternalCommand::getSignature();
    if (cAPIDelegate.get_signature) {
      llb_data_t data;
      cAPIDelegate.get_signature(cAPIDelegate.context, (llb_buildsystem_command_t*)this,
                                 &data);
      code = hash_combine(code, StringRef((const char*)data.data, data.length));

      // Release the client memory.
      //
      // FIXME: This is gross, come up with a general purpose solution.
      free((char*)data.data);
    }
    return size_t(code);
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
