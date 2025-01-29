//===- ActionExecutor.h -----------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2025 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD3_ACTIONEXECUTOR_H
#define LLBUILD3_ACTIONEXECUTOR_H

#include <cstdint>
#include <memory>
#include <optional>

#include <llbuild3/Result.hpp>

#include "llbuild3/Error.pb.h"
#include "llbuild3/Action.pb.h"
#include "llbuild3/Label.pb.h"
#include "llbuild3/Subtask.h"

#define UUID_SYSTEM_GENERATOR 1
#include "uuid.h"

namespace llbuild3 {

struct ActionOwner {
  uuids::uuid engineID = {};
  uint64_t buildID = 0;
  uint64_t workID = 0;
};

enum class ActionPriority: int64_t {
  Low = -1,
  Default = 0,
  High = 1,
};

struct ActionRequest {
  ActionOwner owner;
  ActionPriority priority;
  Action action;
};

struct ActionID {
  // Stable action identifer, when available from the executor.
  uuids::uuid stable;

  // Volatile action identifer. Always available, but valid for the current
  // process lifetime only.
  uint64_t vid;
};

struct SubtaskRequest {
  ActionOwner owner;
  ActionPriority priority;
  Subtask subtask;
  std::optional<SubtaskInterface> si;
};

class ActionExecutorListener {
public:
  ActionExecutorListener() { }
  virtual ~ActionExecutorListener();

  virtual void notifyActionStart(ActionID) = 0;
  virtual void notifyActionComplete(ActionID, result<ActionResult, Error>) = 0;

  virtual void notifySubtaskStart(uint64_t) = 0;
  virtual void notifySubtaskComplete(uint64_t, SubtaskResult) = 0;
};


struct ActionDescriptor {
  Label name;
  Platform platform;
};

class ActionProvider {
public:
  virtual ~ActionProvider();

  virtual std::vector<Label> prefixes() = 0;

  /// Resolve the action method for the given label
  virtual std::unique_ptr<ActionDescriptor> resolve(const Label& name) = 0;
};


class LocalExecutor;
class RemoteExecutor;

class ActionExecutor {
private:
  void* impl;

  // Copying is disabled.
  ActionExecutor(const ActionExecutor&) = delete;
  void operator=(const ActionExecutor&) = delete;

public:
  ActionExecutor(std::shared_ptr<LocalExecutor> localExecutor,
                 std::shared_ptr<RemoteExecutor> remoteExecutor,
                 unsigned maxLocalConcurrency = 0,
                 unsigned maxAsyncConcurrency = 0);
  ~ActionExecutor();

  void registerProvider(std::unique_ptr<ActionProvider>&& provider);

  void attachListener(ActionExecutorListener* listener);
  void detachListener(ActionExecutorListener* listener);

  result<Label, Error> resolveFunction(const Label& name);
  result<ActionID, Error> submit(ActionRequest request);
  result<uint64_t, Error> submit(SubtaskRequest request);
  std::optional<Error> cancel(ActionID aid, ActionOwner owner);
};


}

#endif
