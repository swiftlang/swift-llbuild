//===- Engine.h -------------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2025 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD3_ENGINE_H
#define LLBUILD3_ENGINE_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <llbuild3/Result.hpp>

#include "llbuild3/Action.pb.h"
#include "llbuild3/Engine.pb.h"
#include "llbuild3/Error.pb.h"
#include "llbuild3/Label.pb.h"
#include "llbuild3/Logging.h"
#include "llbuild3/Subtask.h"


namespace llbuild3 {

class ActionCache;
class ActionExecutor;
class CASDatabase;
class EngineInterface;
class Rule;
class RuleProvider;

namespace internal {
class IntTaskInterface;
}

class TaskInterface {
  friend class ExtTaskAdaptor;
  friend class internal::IntTaskInterface;
private:
  void* impl;
  uint64_t ctx;

public:
  TaskInterface(void* impl, uint64_t ctx) : impl(impl), ctx(ctx) {}

  /// Add a rule provider which the engine can use to produce outputs.
  /// Only valid during initialization, will return error otherwise.
  std::optional<Error> registerRuleProvider(std::unique_ptr<RuleProvider>&& provider);

  /// Request the production of a specific artifact
  result<uint64_t, Error> requestArtifact(const Label& label /* config */);

  /// Request all of the artifact(s) produced by a rule
  result<uint64_t, Error> requestRule(const Label& label /* config */);

  /// Request the result of a fully defined action
  result<uint64_t, Error> requestAction(const Action& action);

  /// Spawn an in-process subtask
  result<uint64_t, Error> spawnSubtask(const Subtask& subtask);
};


/// A task object represents an abstract in-progress computation in the build
/// engine.
class Task {
public:
  struct Properties {
    bool init = false;
    bool cacheable = true;

    Properties() { }
    Properties(bool init, bool cacheable) : init(init), cacheable(cacheable) { }
  };

public:
  const Properties props;

  Task(Properties props) : props(props) {}
  virtual ~Task();

  virtual const Label& name() const = 0;
  virtual const Signature& signature() const = 0;
  virtual std::vector<Label> produces() const = 0;

  /// Executed by the build engine to process a state transition
  virtual TaskNextState compute(TaskInterface, const TaskContext&, const TaskInputs&, const SubtaskResults&) = 0;
};

/// A rule represents an individual element of computation that can be performed
/// by the build engine.
class Rule {
private:
  Rule(const Rule&) = delete;
  void operator=(const Rule&) = delete;

public:
  Rule() { }
  virtual ~Rule() = 0;

  virtual const Label& name() const = 0;
  virtual const Signature& signature() const = 0;

  /// Returns the set of (potentially abstract) Artifacts produced by the rule
  // FIXME: Should this be ArtifactID?
  virtual std::vector<Label> produces() const = 0;

  /// Called to create the task to build the rule, when necessary.
  virtual std::unique_ptr<Task> configureTask(/* FIXME - config context */) = 0;
};


class RuleProvider {
public:
  virtual ~RuleProvider();

  /// Get the sets of supported rule and artifact label prefixes
  virtual std::vector<Label> rulePrefixes() = 0;
  virtual std::vector<Label> artifactPrefixes() = 0;

  /// Get the rule to use for the given name.
  virtual std::unique_ptr<Rule> ruleByName(const Label& name) = 0;

  /// Get the rule that produces a specific artifact
  virtual std::unique_ptr<Rule> ruleForArtifact(const Label& artifact) = 0;
};


namespace internal {

struct BuildContext;

}

class Build {
  std::shared_ptr<internal::BuildContext> impl;

public:
  Build(std::shared_ptr<internal::BuildContext> impl) : impl(impl) {}

  void cancel();
  void addCompletionHandler(std::function<void(result<Artifact, Error>)>);
};


struct EngineConfig {
  std::optional<Label> initRule;
};


class Engine {
  void* impl;

  // Copying is disabled.
  Engine(const Engine&) = delete;
  void operator=(const Engine&) = delete;

public:
  /// Create a build engine
  Engine(EngineConfig cfg,
         std::shared_ptr<CASDatabase> casDB,
         std::shared_ptr<ActionCache> cache,
         std::shared_ptr<ActionExecutor> executor,
         std::shared_ptr<Logger> logger,
         std::shared_ptr<ClientContext> clientContext,
         std::unique_ptr<RuleProvider>&& init);
  ~Engine();

  /// @name Client API
  /// @{

  /// Get the configuration
  const EngineConfig& config();

  std::shared_ptr<CASDatabase> cas();

  /// Build the requested artifact.
  Build build(const Label& artifact);

  /// @}
};

}

#endif
