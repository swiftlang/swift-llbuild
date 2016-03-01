//===- BuildSystem.h --------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BUILDSYSTEM_BUILDSYSTEM_H
#define LLBUILD_BUILDSYSTEM_BUILDSYSTEM_H

#include "llbuild/Basic/Compiler.h"
#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <memory>
#include <string>

namespace llbuild {
namespace basic {

class FileSystem;

}

namespace buildsystem {

class BuildExecutionQueue;
class Command;
class Tool;
  
class BuildSystemDelegate {
  // DO NOT COPY
  BuildSystemDelegate(const BuildSystemDelegate&)
    LLBUILD_DELETED_FUNCTION;
  void operator=(const BuildSystemDelegate&)
    LLBUILD_DELETED_FUNCTION;
  BuildSystemDelegate &operator=(BuildSystemDelegate&& rhs)
    LLBUILD_DELETED_FUNCTION;
  
public:
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

  /// Called by the build system to determine if the build has been cancelled.
  ///
  /// This is checked before starting each new command.
  virtual bool isCancelled() = 0;
  
  /// Called by the build system to report a command failure.
  virtual void hadCommandFailure() = 0;

  /// Called by the build system to report that a declared command has started.
  ///
  /// The system guarantees that all such calls will be paired with a
  /// corresponding \see commandFinished() call.
  ///
  /// The system only makes this callback for commands explicitly declared in
  /// the build manifest (i.e., not for any work implicitly spawned by those
  /// commands).
  virtual void commandStarted(Command*) = 0;

  /// Called by the build system to report a command has completed.
  virtual void commandFinished(Command*) = 0;
};

/// The BuildSystem class is used to perform builds using the native build
/// system.
class BuildSystem {
private:
  void *impl;

public:
  /// Create a build system with the given delegate.
  ///
  /// \arg mainFilename The path of the main build file.
  explicit BuildSystem(BuildSystemDelegate& delegate,
                       StringRef mainFilename);
  ~BuildSystem();

  /// Return the delegate the engine was configured with.
  BuildSystemDelegate& getDelegate();

  /// @name Client API
  /// @{

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
  /// \returns True on success.
  bool build(StringRef target);

  /// @}
};

}
}

#endif
