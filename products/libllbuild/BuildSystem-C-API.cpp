//===-- BuildSystem-C-API.cpp ---------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2015 - 2016 Apple Inc. and the Swift project authors
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
#include "llbuild/BuildSystem/BuildSystemFrontend.h"
#include "llbuild/Core/BuildEngine.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/SourceMgr.h"

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
    result.size = file_info.size;
    result.modTime.seconds = file_info.mod_time.seconds;;
    result.modTime.nanoseconds = file_info.mod_time.nanoseconds;
    return result;
  }
};
  
class CAPIBuildSystemFrontendDelegate : public BuildSystemFrontendDelegate {
  llb_buildsystem_delegate_t cAPIDelegate;
  CAPIFileSystem fileSystem;
  
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

  virtual void commandStarted(Command* command) override {
    cAPIDelegate.command_started(
        cAPIDelegate.context,
        (llb_buildsystem_command_t*) command);
  }

  virtual void commandFinished(Command* command) override {
    cAPIDelegate.command_finished(
        cAPIDelegate.context,
        (llb_buildsystem_command_t*) command);
  }

  virtual void commandProcessStarted(Command* command,
                                     ProcessHandle handle) override {
    cAPIDelegate.command_process_started(
        cAPIDelegate.context,
        (llb_buildsystem_command_t*) command,
        (llb_buildsystem_process_t*) handle.id);
  }

  virtual void commandProcessHadError(Command* command, ProcessHandle handle,
                                      const Twine& message) override {
    SmallString<256> data;
    message.toVector(data);
    llb_data_t cData{ data.size(), (const uint8_t*) data.data() };
    cAPIDelegate.command_process_had_error(
        cAPIDelegate.context,
        (llb_buildsystem_command_t*) command,
        (llb_buildsystem_process_t*) handle.id,
        &cData);
  }

  virtual void commandProcessHadOutput(Command* command, ProcessHandle handle,
                                       StringRef data) override {
    llb_data_t cData{ data.size(), (const uint8_t*) data.data() };
    cAPIDelegate.command_process_had_output(
        cAPIDelegate.context,
        (llb_buildsystem_command_t*) command,
        (llb_buildsystem_process_t*) handle.id,
        &cData);
  }
  
  virtual void commandProcessFinished(Command* command, ProcessHandle handle,
                                      int exitStatus) override {
    cAPIDelegate.command_process_finished(
        cAPIDelegate.context,
        (llb_buildsystem_command_t*) command,
        (llb_buildsystem_process_t*) handle.id,
        exitStatus);
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

bool llb_buildsystem_build(llb_buildsystem_t* system_p, const llb_data_t* key) {
  CAPIBuildSystem* system = (CAPIBuildSystem*) system_p;

  return system->getFrontend().build(
      core::KeyType((const char*)key->data, key->length));
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
