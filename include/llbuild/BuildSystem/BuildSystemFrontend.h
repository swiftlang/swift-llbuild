//===- BuildSystemFrontend.h ------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BUILDSYSTEM_BUILDSYSTEMFRONTEND_H
#define LLBUILD_BUILDSYSTEM_BUILDSYSTEMFRONTEND_H

#include "llbuild/Basic/LLVM.h"
#include "llbuild/BuildSystem/BuildSystem.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include <string>
#include <vector>

namespace llvm {

class SourceMgr;

}

namespace llbuild {
namespace buildsystem {

class BuildSystemFrontendDelegate;
class BuildSystemInvocation;

/// This provides a standard "frontend" to the build system features, for use in
/// building bespoke build systems that can still take advantage of desirable
/// shared behavior.
///
/// The frontend glues together various parts of the build system functionality
/// to provide:
///   o Support for common command line options.
///   o Support for parallel, persistent builds.
//    o Support for command line diagnostics and status reporting.
class BuildSystemFrontend {
  BuildSystemFrontendDelegate& delegate;
  const BuildSystemInvocation& invocation;

public:
  BuildSystemFrontend(BuildSystemFrontendDelegate& delegate,
                      const BuildSystemInvocation& invocation);

  /// @name Accessors
  /// @{

  BuildSystemFrontendDelegate& getDelegate() { return delegate; }
  const BuildSystemFrontendDelegate& getDelegate() const { return delegate; }

  const BuildSystemInvocation& getInvocation() { return invocation; }
  
  /// @}

  /// @name Client API
  /// @{

  /// Build the named target using the specified by the invocation parameters.
  ///
  /// \returns True on success, or false if there were errors.
  bool build(StringRef targetToBuild);

  /// @}
};

/// The frontend-specific delegate, which provides some shared behaviors.
class BuildSystemFrontendDelegate : public BuildSystemDelegate {
private:
  void* impl;

  /// Default implementation, cannot be overriden by subclasses.
  virtual void setFileContentsBeingParsed(StringRef buffer) override;

  /// Provides a default error implementation which will delegate to the
  /// provided source manager. Cannot be overriden by subclasses.
  virtual void error(StringRef filename, const Token& at,
                     const Twine& message) override;
  
public:
  /// Create a frontend delegate.
  ///
  /// \param sourceMgr The source manager to use for reporting diagnostics.
  /// \param invocation The invocation parameters.
  /// \param name The name of build system client.
  /// \param version The version of the build system client.
  BuildSystemFrontendDelegate(llvm::SourceMgr& sourceMgr,
                              const BuildSystemInvocation& invocation,
                              StringRef name,
                              uint32_t version);
  virtual ~BuildSystemFrontendDelegate();
  
  /// Called by the build system to get a tool definition, must be provided by
  /// subclasses.
  virtual std::unique_ptr<Tool> lookupTool(StringRef name) override = 0;

  /// Provides an appropriate execution queue based on the invocation options.
  virtual std::unique_ptr<BuildExecutionQueue> createExecutionQueue() override;

  /// Provides a default cancellation implementation that will cancel when any
  /// command has failed.
  virtual bool isCancelled() override;
  
  /// Provides a default handler.
  ///
  /// Subclass should call this method if overridden.
  virtual void hadCommandFailure() override;

  /// @name Frontend-specific APIs
  /// @{

  /// Report a non-file specific error message.
  void error(const Twine& message);
  
  /// @}

  /// @name Accessors
  /// @{

  llvm::SourceMgr& getSourceMgr();

  /// Get the number of reported errors.
  unsigned getNumErrors();

  /// Get the number of failed commands.
  unsigned getNumFailedCommands();

  /// @}
};


/// This class wraps the common options which are used by the frontend.
class BuildSystemInvocation {
public:
  /// Whether command usage should be printed.
  bool showUsage = false;

  /// Whether to use a serial build.
  bool useSerialBuild = false;
  
  /// The path of the database file to use, if any.
  std::string dbPath = "build.db";

  /// The path of a directory to change into before anything else, if any.
  std::string chdirPath = "";

  /// The name of the build file to use.
  std::string buildFilePath = "build.llbuild";

  /// The name of the build trace output file to use, if any.
  std::string traceFilePath = "";

  /// The positional arguments.
  std::vector<std::string> positionalArgs;

  /// Whether there were any parsing errors.
  bool hadErrors = false;
  
public:
  /// Get the appropriate "usage" text to use for the built in arguments.
  static void getUsage(int optionWidth, raw_ostream& os);
  
  /// Parse the invocation parameters from the given arguments.
  ///
  /// \param sourceMgr The source manager to use for diagnostics.
  void parse(ArrayRef<std::string> args, llvm::SourceMgr& sourceMgr);
};

}
}

#endif
