//===- BuildSystem.h --------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BUILDSYSTEM_BUILDSYSTEM_H
#define LLBUILD_BUILDSYSTEM_BUILDSYSTEM_H

#include "llbuild/Basic/Compiler.h"
#include "llbuild/Basic/LLVM.h"
#include "llbuild/BuildSystem/CommandResult.h"

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <memory>
#include <string>

namespace llbuild {
namespace basic {

class FileSystem;

}

namespace buildsystem {

class BuildDescription;
class BuildExecutionQueue;
class BuildKey;
class BuildValue;
class Command;
class Tool;

bool pathIsPrefixedByPath(std::string path, std::string prefixPath);
  
class BuildSystemDelegate {
  // DO NOT COPY
  BuildSystemDelegate(const BuildSystemDelegate&)
    LLBUILD_DELETED_FUNCTION;
  void operator=(const BuildSystemDelegate&)
    LLBUILD_DELETED_FUNCTION;
  BuildSystemDelegate &operator=(BuildSystemDelegate&& rhs)
    LLBUILD_DELETED_FUNCTION;
  
public:
  /// Command status change event kinds.
  ///
  /// This must be kept in sync with core::Rule::StatusKind.
  enum class CommandStatusKind {
    /// Indicates the command is being scanned.
    IsScanning = 0,

    /// Indicates the command is up-to-date, and doesn't need to run.
    IsUpToDate = 1,

    /// Indicates the command was run, and is now complete.
    IsComplete = 2
  };

  /// Minimal token object representing the range where a diagnostic occurred.
  struct Token {
    const char* start;
    unsigned length;
  };

private:
  std::string name;
  uint32_t version;
  
public:
  /// Configure the client properties.
  ///
  /// \param name An identifier for the client system.
  ///
  /// \param version A version number to identify the schema the client is
  /// using, and changes to the schema version number will result in
  /// invalidation of all cached build results. NOTE: Currently, this is limited
  /// to a 16-bit number as an implementation detail.
  BuildSystemDelegate(StringRef name, uint32_t version)
      : name(name), version(version) {}
  virtual ~BuildSystemDelegate();

  /// Called by the build system to get the client name.
  StringRef getName() const { return name; }

  /// Called by the build system to get the current client version.
  uint32_t getVersion() const { return version; }

  /// Get the file system to use for access.
  virtual basic::FileSystem& getFileSystem() = 0;

  /// Called by the build file loader to register the current file contents.
  //
  // FIXME: This is a total hack, and should be cleaned up.
  virtual void setFileContentsBeingParsed(StringRef buffer) = 0;

  /// Called by the build file loader to report an error.
  ///
  /// \param filename The file the error occurred in.
  ///
  /// \param at The token at which the error occurred. The token will be null if
  /// no location is associated.
  ///
  /// \param message The diagnostic message.
  virtual void error(StringRef filename,
                     const Token& at,
                     const Twine& message) = 0;

  /// Called by the build system to get a tool definition.
  ///
  /// This method is called to look for all tools, even ones which are built-in
  /// to the BuildSystem, in order to give the client an opportunity to override
  /// built-in tools.
  ///
  /// \param name The name of the tool to lookup.
  /// \returns The tool to use on success, or otherwise nil.
  virtual std::unique_ptr<Tool> lookupTool(StringRef name) = 0;

  /// Called by the build system to get create the object used to dispatch work.
  virtual std::unique_ptr<BuildExecutionQueue> createExecutionQueue() = 0;
  
  /// Called by the build system to report a command failure.
  virtual void hadCommandFailure() = 0;

  /// Called by the build system to report that a declared command's state is
  /// changing.
  //
  // FIXME: This API is now gross, there shouldn't be one generic status changed
  // method and three individual other state change methods.
  virtual void commandStatusChanged(Command*, CommandStatusKind) = 0;

  /// Called by the build system to report that a declared command is preparing
  /// to run.
  ///
  /// This method is called before the command starts, when the system has
  /// identified that it will eventually need to run (after all of its inputs
  /// have been satisfied).
  ///
  /// The system guarantees that all such calls will be paired with a
  /// corresponding \see commandFinished() call.
  ///
  /// The system only makes this callback for commands explicitly declared in
  /// the build manifest (i.e., not for any work implicitly spawned by those
  /// commands).
  virtual void commandPreparing(Command*) = 0;

  /// Called by the build system to allow the delegate to skip a command without
  /// implicitly skipping its dependents.
  ///
  /// WARNING: Clients need to take special care when using this. Skipping
  /// commands without considering their dependencies or dependents can easily
  /// produce an inconsistent build.
  ///
  /// This method is called before the command starts, when the system has
  /// identified that it will eventually need to run (after all of its inputs
  /// have been satisfied).
  ///
  /// The system guarantees that all such calls will be paired with a
  /// corresponding \see commandFinished() call.
  virtual bool shouldCommandStart(Command*) = 0;

  /// Called by the build system to report that a declared command has started.
  ///
  /// The system guarantees that all such calls will be paired with a
  /// corresponding \see commandFinished() call.
  ///
  /// The system only makes this callback for commands explicitly declared in
  /// the build manifest (i.e., not for any work implicitly spawned by those
  /// commands).
  virtual void commandStarted(Command*) = 0;

  /// Called to report an error during the execution of a command.
  ///
  /// \param data - The error message.
  virtual void commandHadError(Command*, StringRef data) = 0;

  /// Called to report a note during the execution of a command.
  ///
  /// \param data - The note message.
  virtual void commandHadNote(Command*, StringRef data) = 0;

  /// Called to report a warning during the execution of a command.
  ///
  /// \param data - The warning message.
  virtual void commandHadWarning(Command*, StringRef data) = 0;

  /// Called by the build system to report a command has completed.
  ///
  /// \param result - The result of command (e.g. success, failure, etc).
  virtual void commandFinished(Command*, CommandResult result) = 0;
};

/// The BuildSystem class is used to perform builds using the native build
/// system.
class BuildSystem {
private:
  void *impl;

  // Copying is disabled.
  BuildSystem(const BuildSystem&) LLBUILD_DELETED_FUNCTION;
  void operator=(const BuildSystem&) LLBUILD_DELETED_FUNCTION;

public:
  /// Create a build system with the given delegate.
  explicit BuildSystem(BuildSystemDelegate& delegate);
  ~BuildSystem();

  /// Return the delegate the engine was configured with.
  BuildSystemDelegate& getDelegate();

  /// @name Client API
  /// @{

  /// Load the build description from a file.
  ///
  /// \returns True on success.
  bool loadDescription(StringRef mainFilename);

  /// Load an explicit build description. from a file.
  void loadDescription(std::unique_ptr<BuildDescription> description);
  
  /// Attach (or create) the database at the given path.
  ///
  /// \returns True on success.
  bool attachDB(StringRef path, std::string* error_out);

  /// Enable low-level engine tracing into the given output file.
  ///
  /// \returns True on success.
  bool enableTracing(StringRef path, std::string* error_out);

  /// Build the named target.
  ///
  /// A build description *must* have been loaded before calling this method.
  ///
  /// \returns True on success, or false if the build was aborted (for example,
  /// if a cycle was discovered).
  bool build(StringRef target);

  /// Build a specific key directly.
  ///
  /// A build description *must* have been loaded before calling this method.
  ///
  /// \returns The result of computing the value, or nil if the build failed.
  llvm::Optional<BuildValue> build(BuildKey target);

  /// Reset mutable build state before a new build operation.
  void resetForBuild();

  /// Cancel the current build.
  void cancel();

  /// @}
};

}
}

#endif
