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

#include "llbuild3/Action.pb.h"
#include "llbuild3/CAS.h"
#include "llbuild3/Common.h"
#include "llbuild3/Error.pb.h"
#include "llbuild3/Label.pb.h"
#include "llbuild3/Subtask.h"

namespace llbuild3 {

struct ClientActionID {
  uint64_t buildID = 0;
  uint64_t workID = 0;
};

enum class ActionPriority: int64_t {
  Low = -1,
  Default = 0,
  High = 1,
};

struct ActionRequest {
  EngineID owner;
  ClientActionID id;
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
  EngineID owner;
  ClientActionID id;
  ActionPriority priority;
  Subtask subtask;
  std::optional<SubtaskInterface> si;
};

class ActionExecutorListener {
public:
  ActionExecutorListener() { }
  virtual ~ActionExecutorListener();

  virtual void notifyActionStart(ClientActionID, ActionID) = 0;
  virtual void notifyActionComplete(ClientActionID, result<ActionResult, Error>) = 0;

  virtual void notifySubtaskStart(ClientActionID) = 0;
  virtual void notifySubtaskComplete(ClientActionID, SubtaskResult) = 0;
};

class PlatformPropertyKey {
public:
  static inline std::string const Architecture = "__arch__";
  static inline std::string const Platform = "__platform__";
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
  virtual result<Label, Error> resolve(const Label& name) = 0;

  virtual result<ActionDescriptor, Error> actionDescriptor(const Label& function) = 0;
};

class ActionCache;
class LocalExecutor;
class RemoteExecutor;

class ActionExecutor {
private:
  void* impl;

  // Copying is disabled.
  ActionExecutor(const ActionExecutor&) = delete;
  void operator=(const ActionExecutor&) = delete;

public:
  ActionExecutor(std::shared_ptr<CASDatabase> db,
                 std::shared_ptr<ActionCache> actionCache,
                 std::shared_ptr<LocalExecutor> localExecutor,
                 std::shared_ptr<RemoteExecutor> remoteExecutor,
                 unsigned maxLocalConcurrency = 0,
                 unsigned maxAsyncConcurrency = 0);
  ~ActionExecutor();

  std::optional<Error> registerProvider(std::unique_ptr<ActionProvider>&& provider);

  void attachListener(EngineID engineID, ActionExecutorListener* listener);
  void detachListener(EngineID engineID);

  result<Label, Error> resolveFunction(const Label& name);
  result<ActionID, Error> submit(ActionRequest request);
  result<uint64_t, Error> submit(SubtaskRequest request);
  std::optional<Error> cancel(EngineID owner, ClientActionID aid);
};


}

#endif
