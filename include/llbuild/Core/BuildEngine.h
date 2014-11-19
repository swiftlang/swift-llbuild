//===- BuildEngine.h --------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_CORE_BUILDENGINE_H
#define LLBUILD_CORE_BUILDENGINE_H

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace llbuild {
namespace core {

// FIXME: Need to abstract KeyType;
typedef std::string KeyType;
// FIXME: Need to abstract ValueType;
typedef int ValueType;

class BuildDB;
class BuildEngine;

/// This object contains the result of executing a task to produce the value for
/// a key.
struct Result {
  /// The last value that resulted from executing the task.
  ValueType Value = {};

  /// The build timestamp during which the result \see Value was computed.
  uint64_t ComputedAt = 0;

  /// The build timestamp at which this result was last checked to be
  /// up-to-date.
  ///
  /// \invariant BuiltAt >= ComputedAt
  //
  // FIXME: Think about this representation more. The problem with storing this
  // field here in this fashion is that every build will result in bringing all
  // of the BuiltAt fields up to date. That is unfortunate from a persistence
  // perspective, where it would be ideal if we didn't touch any disk state for
  // null builds.
  uint64_t BuiltAt = 0;

  /// The explicit dependencies required by the generation.
  //
  // FIXME: At some point, figure out the optimal representation for this field,
  // which is likely to be a lot of the resident memory size.
  std::vector<KeyType> Dependencies;
};

/// A task object represents an abstract in progress computation in the build
/// engine.
///
/// The task represents not just the primary computation, but also the process
/// of starting the computation and necessary input dependencies. Tasks are
/// expected to be created in response to \see BuildEngine requests to initiate
/// the production of particular result value.
///
/// The creator may use \see BuildEngine::taskNeedsInput() to specify input
/// dependencies on the Task. The Task itself may also specify additional input
/// dependencies dynamically during the execution of \see Task::start() or \see
/// Task::provideValue().
///
/// Once a task has been created and registered, the BuildEngine will invoke
/// \see Task::start() to initiate the computation. The BuildEngine will provide
/// the in progress task with its requested inputs via \see
/// Task::provideValue().
///
/// After all inputs requested by the Task have been delivered, the BuildEngine
/// will invoke \see Task::inputsAvailable() to instruct the Task it should
/// complete its computation and provide the output. The Task is responsible for
/// providing the engine with the computed value when ready using \see
/// BuildEngine::taskIsComplete().
//
// FIXME: Define parallel execution semantics.
class Task {
public:
  /// The name of the task, for debugging purposes.
  //
  // FIXME: Eliminate this?
  std::string Name;

public:
  Task(const std::string& Name) : Name(Name) {}

  virtual ~Task();

  /// Executed by the build engine when the task should be started.
  virtual void start(BuildEngine&) = 0;

  /// Invoked by the build engine to provide an input value as it becomes
  /// available.
  ///
  /// \param InputID The unique identifier provided to the build engine to
  /// represent this input when requested in \see
  /// BuildEngine::taskNeedsInput().
  ///
  /// \param Value The computed value for the given input.
  virtual void provideValue(BuildEngine&, uintptr_t InputID,
                            const ValueType& Value) = 0;

  /// Executed by the build engine to indicate that all inputs have been
  /// provided, and the task should begin its computation.
  ///
  /// The task is expected to call \see BuildEngine::taskIsComplete() when it is
  /// done with its computation.
  ///
  /// It is an error for any client to request an additional input for a task
  /// after the last requested input has been provided by the build engine.
  virtual void inputsAvailable(BuildEngine&) = 0;
};

class Rule {
public:
  KeyType Key;
  std::function<Task*(BuildEngine&)> Action;
  std::function<bool(const Rule&, const ValueType&)> IsResultValid;
};

/// Delegate interface for use with the build engine.
class BuildEngineDelegate {
public:
  virtual ~BuildEngineDelegate();  

  /// Get the rule to use for the given Key.
  ///
  /// The delegate *must* provide a rule for any possible key that can be
  /// requested (either by a client, through \see BuildEngine::build(), or via a
  /// Task through mechanisms such as \see BuildEngine::taskNeedsInput()). If a
  /// requested Key cannot be supplied, the delegate should provide a dummy rule
  /// that the client can translate into an error.
  virtual Rule lookupRule(const KeyType& Key) = 0;
};

/// A build engine supports fast, incremental, persistent, and parallel
/// execution of computational graphs.
///
/// Computational elements in the graph are modeled by \see Rule objects, which
/// are assocated with a specific \see KeyType, and which can be executed to
/// produce an output \see ValueType for that key.
///
/// Rule objects are evaluated by first invoking their action to produce a \see
/// Task object which is responsible for the live execution of the
/// computation. The Task object can interact with the BuildEngine to request
/// inputs or to notify the engine of its completion, and receives various
/// callbacks from the engine as the computation progresses.
///
/// The engine itself executes using a deterministic, serial operation, but it
/// supports parallel computation by allowing the individual Task objects to
/// defer their own computation to signal the BuildEngine of its completion on
/// alternate threads.
///
/// To support persistence, the engine allows attaching a database (\see
/// attachDB()) which can be used to record the prior results of evaluating Rule
/// instances.
class BuildEngine {
  void *Impl;

public:
  /// Create a build engine with the given delegate.
  explicit BuildEngine(BuildEngineDelegate& Delegate);
  ~BuildEngine();

  /// @name Rule Definition
  /// @{

  /// Add a rule which the engine can use to produce outputs.
  void addRule(Rule &&Rule);

  /// @}

  /// @name Client API
  /// @{

  /// Build the result for a particular key.
  const ValueType& build(const KeyType& Key);

  /// Attach a database for persisting build state.
  ///
  /// A database should only be attached immediately after creating the engine,
  /// it is an error to attach a database after adding rules or initiating any
  /// builds, or to attempt to attach multiple databases.
  void attachDB(std::unique_ptr<BuildDB> Database);

  /// Enable tracing into the given output file.
  ///
  /// \returns True on success.
  bool enableTracing(const std::string& Path, std::string* Error_Out);

  /// Dump the build state to a file in Graphviz DOT format.
  void dumpGraphToFile(const std::string &Path);

  /// @}

  /// @name Task Management APIs
  /// @{

  /// Register the given task, in response to a Rule evaluation.
  ///
  /// The engine tasks ownership of the \arg Task, and it is expected to
  /// subsequently be returned as the task to execute for a Rule evaluation.
  ///
  /// \returns The provided task, for the convenience of the client.
  Task* registerTask(Task* Task);

  /// The maximum allowed input ID.
  static const uintptr_t kMaximumInputID = ~(uintptr_t)0xFF;

  /// Specify the given \arg Task depends upon the result of computing \arg Key.
  ///
  /// The result, when available, will be provided to the task via \see
  /// Task::provideValue(), supplying the provided \arg InputID to allow the
  /// task to identify the particular input.
  ///
  /// NOTE: It is an unchecked error for a task to request the same input value
  /// multiple times.
  ///
  /// \param InputID An arbitrary value that may be provided by the client to
  /// use in efficiently associating this input. The range of this parameter is
  /// intentionally chosen to allow a pointer to be provided, but note that all
  /// input IDs greater than \see kMaximumInputID are reserved for internal use
  /// by the engine.
  void taskNeedsInput(Task* Task, const KeyType& Key, uintptr_t InputID);

  /// Specify that the given \arg Task must be built subsequent to the
  /// computation of \arg Key.
  ///
  /// The value of the computation of \arg Key is not available to the task, and
  /// the only guarantee the engine provides is that if \arg Key is computed
  /// during a build, then \arg Task will not be computed until after it.
  void taskMustFollow(Task* Task, const KeyType& Key);

  /// Inform the engine of an input dependency that was discovered by the task
  /// during its execution, a la compiler generated dependency files.
  ///
  /// This call may only be made after a task has received all of its inputs;
  /// inputs discovered prior to that point should simply be requested as normal
  /// input dependencies.
  ///
  /// Such a dependency is not used to provide additional input to the task,
  /// rather it is a way for the task to report an additional input which should
  /// be considered the next time the rule is evaluated. The expected use case
  /// for a discovered dependency is is when a processing task cannot predict
  /// all of its inputs prior to being run, but can presume that any unknown
  /// inputs already exist. In such cases, the task can go ahead and run and can
  /// report the all of the discovered inputs as it executes. Once the task is
  /// complete, these inputs will be recorded as being dependencies of the task
  /// so that it will be recomputed when any of the inputs change.
  ///
  /// It is legal to call this method from any thread, but the caller is
  /// responsible for ensuring that it is never called concurrently for the same
  /// task.
  void taskDiscoveredDependency(Task* Task, const KeyType& Key);

  /// Called by a task to indicate it has completed and to provide its value.
  ///
  /// It is legal to call this method from any thread.
  void taskIsComplete(Task* Task, ValueType&& Value);
  
  /// @}
};

}
}

#endif
