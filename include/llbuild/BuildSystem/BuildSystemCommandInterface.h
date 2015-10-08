//===- BuildSystemCommandInterface.h ----------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BUILDSYSTEM_BUILDSYSTEMCOMMANDINTERFACE_H
#define LLBUILD_BUILDSYSTEM_BUILDSYSTEMCOMMANDINTERFACE_H

namespace llbuild {
namespace core {

class BuildEngine;
class Task;

}

namespace buildsystem {

class BuildExecutionQueue;
class BuildKey;
class BuildSystemDelegate;
class BuildValue;
class QueueJob;

/// This is an abstract interface class which defines the API available to
/// Commands when being invoked by the BuildSystem for the purposes of
/// execution.
//
// FIXME: This could avoid using virtual dispatch.
class BuildSystemCommandInterface {
public:
  virtual ~BuildSystemCommandInterface();

  /// @name Accessors
  /// @{

  virtual BuildSystemDelegate& getDelegate() = 0;
  
  virtual core::BuildEngine& getBuildEngine() = 0;

  virtual BuildExecutionQueue& getExecutionQueue() = 0;
  
  /// @}
  
  /// @name BuildEngine Task API
  /// @{
  
  virtual void taskNeedsInput(core::Task* task, const BuildKey& key,
                              uintptr_t inputID) = 0;

  virtual void taskMustFollow(core::Task* task, const BuildKey& key) = 0;

  virtual void taskDiscoveredDependency(core::Task* task,
                                        const BuildKey& key) = 0;

  virtual void taskIsComplete(core::Task* task, const BuildValue& value,
                              bool forceChange = false) = 0;

  /// @}

  /// @name BuildSystem API
  /// @{

  /// Add a job to be executed.
  virtual void addJob(QueueJob&&) = 0;

  /// @}
};

}
}

#endif
