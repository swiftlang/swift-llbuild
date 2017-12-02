//===- BuildEngine.h --------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_CORE_BUILDENGINE_H
#define LLBUILD_CORE_BUILDENGINE_H

#include "llbuild/Basic/Compiler.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llbuild {
namespace core {

// FIXME: Need to abstract KeyType;
typedef std::string KeyType;
typedef uint64_t KeyID;
typedef std::vector<uint8_t> ValueType;

class BuildDB;
class BuildEngine;

/// A monotonically increasing timestamp identifying which iteration of a build
/// an event occurred during.
typedef uint64_t Timestamp;

/// This object contains the result of executing a task to produce the value for
/// a key.
struct Result {
  /// The last value that resulted from executing the task.
  ValueType value = {};

  /// The build timestamp during which the result \see Value was computed.
  uint64_t computedAt = 0;

  /// The build timestamp at which this result was last checked to be
  /// up-to-date.
  ///
  /// \invariant builtAt >= computedAt
  //
  // FIXME: Think about this representation more. The problem with storing this
  // field here in this fashion is that every build will result in bringing all
  // of the \see builtAt fields up to date. That is unfortunate from a
  // persistence perspective, where it would be ideal if we didn't touch any
  // disk state for null builds.
  uint64_t builtAt = 0;

  /// The explicit dependencies required by the generation.
  //
  // FIXME: At some point, figure out the optimal representation for this field,
  // which is likely to be a lot of the resident memory size.
  std::vector<KeyID> dependencies;
};

/// A task object represents an abstract in-progress computation in the build
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
class Task {
public:
  Task() {}
  virtual ~Task();

  /// Executed by the build engine when the task should be started.
  virtual void start(BuildEngine&) = 0;

  /// Invoked by the build engine to provide the prior result for the task's
  /// output, if present.
  ///
  /// This callback will always be invoked immediately after the task is
  /// started, and prior to its receipt of any other callbacks.
  virtual void providePriorValue(BuildEngine&, const ValueType& value) {};

  /// Invoked by the build engine to provide an input value as it becomes
  /// available.
  ///
  /// \param inputID The unique identifier provided to the build engine to
  /// represent this input when requested in \see
  /// BuildEngine::taskNeedsInput().
  ///
  /// \param value The computed value for the given input.
  virtual void provideValue(BuildEngine&, uintptr_t inputID,
                            const ValueType& value) = 0;

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

/// A rule represents an individual element of computation that can be performed
/// by the build engine.
///
/// Each rule is identified by a unique key and the value for that key can be
/// computed to produce a result, and supplies a set of callbacks that are used
/// to implement the rule's behavior.
///
/// The computation for a rule is done by invocation of its \see Action
/// callback, which is responsible for creating a Task object which will manage
/// the computation.
///
/// All callbacks for the Rule are always invoked synchronously on the primary
/// BuildEngine thread.
//
// FIXME: The intent of having a callback like Rule structure and a decoupled
// (virtual) Task is that the Rule objects (of which there can be very many) can
// be optimized for being lightweight. We don't currently make much of an
// attempt in this direction with the std::functions, but we should investigate
// if this can be lighter weight -- especially since many clients are likely to
// need little more than a place to stuff a context object and register their
// callbacks.
//
// FIXME: We also need to figure out if a richer concurrency model is needed for
// the callbacks. The current intent is that they should be lightweight and
// Tasks should be used when real concurrency is needed.
class Rule {
public:
  enum class StatusKind {
    /// Indicates the rule is being scanned.
    IsScanning = 0,

    /// Indicates the rule is up-to-date, and doesn't need to run.
    IsUpToDate = 1,

    /// Indicates the rule was run, and is now complete.
    IsComplete = 2
  };

  /// The key computed by the rule.
  KeyType key;

  /// Called to create the task to build the rule, when necessary.
  std::function<Task*(BuildEngine&)> action;

  /// Called to check whether the previously computed value for this rule is
  /// still valid.
  ///
  /// This callback is designed for use in synchronizing values which represent
  /// state managed externally to the build engine. For example, a rule which
  /// computes something on the file system may use this to verify that the
  /// computed output has not changed since it was built.
  std::function<bool(BuildEngine&, const Rule&,
                     const ValueType&)> isResultValid;

  /// Called to indicate a change in the rule status.
  std::function<void(BuildEngine&, StatusKind)> updateStatus;
};

/// Delegate interface for use with the build engine.
class BuildEngineDelegate {
public:
  virtual ~BuildEngineDelegate();  

  /// Get the rule to use for the given Key.
  ///
  /// The delegate *must* provide a rule for any possible key that can be
  /// requested (either by a client, through \see BuildEngine::build(), or via a
  /// Task through mechanisms such as \see BuildEngine::taskNeedsInput(). If a
  /// requested Key cannot be supplied, the delegate should provide a dummy rule
  /// that the client can translate into an error.
  virtual Rule lookupRule(const KeyType& key) = 0;

  /// Called when a cycle is detected by the build engine and it cannot make
  /// forward progress.
  ///
  /// \param items The ordered list of items comprising the cycle, starting from
  /// the node which was requested to build and ending with the first node in
  /// the cycle (i.e., the node participating in the cycle will appear twice).
  virtual void cycleDetected(const std::vector<Rule*>& items) = 0;

  /// Called when a fatal error is encountered by the build engine.
  ///
  /// \param message The diagnostic message.
  virtual void error(const llvm::Twine& message) = 0;

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
  void *impl;

  // Copying is disabled.
  BuildEngine(const BuildEngine&) LLBUILD_DELETED_FUNCTION;
  void operator=(const BuildEngine&) LLBUILD_DELETED_FUNCTION;

public:
  /// Create a build engine with the given delegate.
  explicit BuildEngine(BuildEngineDelegate& delegate);
  ~BuildEngine();

  /// Return the delegate the engine was configured with.
  BuildEngineDelegate* getDelegate();

  /// Get the current build timestamp used by the engine.
  ///
  /// The timestamp is a monotonically increasing value which is incremented
  /// with each requested build.
  Timestamp getCurrentTimestamp();

  /// @name Rule Definition
  /// @{

  /// Add a rule which the engine can use to produce outputs.
  void addRule(Rule&& rule);

  /// @}

  /// @name Client API
  /// @{

  /// Build the result for a particular key.
  ///
  /// \returns The result of computing the key, or the empty value if the key
  /// could not be computed; the latter case only happens if a cycle was
  /// discovered currently.
  const ValueType& build(const KeyType& key);

  /// Attach a database for persisting build state.
  ///
  /// A database should only be attached immediately after creating the engine,
  /// it is an error to attach a database after adding rules or initiating any
  /// builds, or to attempt to attach multiple databases.
  ///
  /// \param error_out [out] Error string if return value is false.
  /// \returns false if the build database could not be attached.
  bool attachDB(std::unique_ptr<BuildDB> database, std::string* error_out);

  /// Enable tracing into the given output file.
  ///
  /// \returns True on success.
  bool enableTracing(const std::string& path, std::string* error_out);

  /// Dump the build state to a file in Graphviz DOT format.
  void dumpGraphToFile(const std::string &path);

  /// @}

  /// @name Task Management APIs
  /// @{

  /// Register the given task, in response to a Rule evaluation.
  ///
  /// The engine tasks ownership of the \arg Task, and it is expected to
  /// subsequently be returned as the task to execute for a Rule evaluation.
  ///
  /// \returns The provided task, for the convenience of the client.
  Task* registerTask(Task* task);

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
  /// \param inputID An arbitrary value that may be provided by the client to
  /// use in efficiently associating this input. The range of this parameter is
  /// intentionally chosen to allow a pointer to be provided, but note that all
  /// input IDs greater than \see kMaximumInputID are reserved for internal use
  /// by the engine.
  void taskNeedsInput(Task* task, const KeyType& key, uintptr_t inputID);

  /// Specify that the given \arg Task must be built subsequent to the
  /// computation of \arg Key.
  ///
  /// The value of the computation of \arg Key is not available to the task, and
  /// the only guarantee the engine provides is that if \arg Key is computed
  /// during a build, then \arg Task will not be computed until after it.
  void taskMustFollow(Task* task, const KeyType& key);

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
  void taskDiscoveredDependency(Task* task, const KeyType& key);

  /// Called by a task to indicate it has completed and to provide its value.
  ///
  /// It is legal to call this method from any thread.
  ///
  /// \param value The new value for the task's rule.
  ///
  /// \param forceChange If true, treat the value as changed and trigger
  /// dependents to rebuild, even if the value itself is not different from the
  /// prior result.
  void taskIsComplete(Task* task, ValueType&& value, bool forceChange = false);
  
  /// @}
};

}
}

#endif
