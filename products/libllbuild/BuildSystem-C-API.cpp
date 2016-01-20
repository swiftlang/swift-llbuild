//===-- BuildSystem-C-API.cpp ---------------------------------------------===//
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

// Include the public API.
#include <llbuild/llbuild.h>

#include "llbuild/BuildSystem/BuildFile.h"
#include "llbuild/BuildSystem/BuildSystemFrontend.h"
#include "llbuild/Core/BuildEngine.h"

#include "llvm/Support/SourceMgr.h"

#include <cassert>
#include <memory>

using namespace llbuild;
using namespace llbuild::buildsystem;

/* Build Engine API */

namespace {

class CAPIBuildSystemFrontendDelegate : public BuildSystemFrontendDelegate {
  llb_buildsystem_delegate_t cAPIDelegate;
  
public:
  CAPIBuildSystemFrontendDelegate(llvm::SourceMgr& sourceMgr,
                                  BuildSystemInvocation& invocation,
                                  llb_buildsystem_delegate_t delegate)
      : BuildSystemFrontendDelegate(sourceMgr, invocation, "basic", 0),
        cAPIDelegate(delegate) { }
  
  virtual std::unique_ptr<Tool> lookupTool(StringRef name) override {
    // No support for custom tools yet.
    return {};
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

    // Allocate the frontend delegate.
    frontendDelegate.reset(
        // FIXME: Need to get the client name and schema version from
        // parameters.
        new CAPIBuildSystemFrontendDelegate(sourceMgr, invocation, delegate));

    // Allocate the actual frontend.
    frontend.reset(new BuildSystemFrontend(*frontendDelegate, invocation));

    // Suppress unused warning, for now.
    (void)cAPIDelegate;
  }

  BuildSystemFrontend& getFrontend() {
    return *frontend;
  }
};

};

llb_buildsystem_t* llb_buildsystem_create(
    llb_buildsystem_delegate_t delegate,
    llb_buildsystem_invocation_t invocation) {
  // Check that all required methods are provided.
  assert(delegate.command_started);
  assert(delegate.command_finished);
  assert(delegate.command_process_started);
  assert(delegate.command_process_had_output);
  assert(delegate.command_process_finished);
         
  return (llb_buildsystem_t*) new CAPIBuildSystem(delegate, invocation);
}

void llb_buildsystem_destroy(llb_buildsystem_t* system) {
  delete (CAPIBuildSystem*)system;
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
