//===- BuildEngine.h --------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_CORE_BUILDENGINE_H
#define LLBUILD_CORE_BUILDENGINE_H

#include "llbuild/Basic/Clock.h"
#include "llbuild/Basic/Compiler.h"
#include "llbuild/Basic/ExecutionQueue.h"
#include "llbuild/Basic/Hashing.h"

#include "llbuild/Core/AttributedKeyIDs.h"
#include "llbuild/Core/KeyID.h"

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

class KeyType {
private:
  std::string key;

public:
  KeyType() { }
  KeyType(const std::string& key) : key(key) { }
  KeyType(const char* key) : key(key) { }
  KeyType(llvm::StringRef ref) : key(ref) { }
  KeyType(const char* data, size_t length) : key(data, length) { }

  bool operator==(const KeyType& rhs) const { return key == rhs.key; }
  bool operator<(const KeyType& rhs) const { return key < rhs.key; }

  const std::string& str() const { return key; }
  const char* data() const { return key.data(); }
  const char* c_str() const { return key.c_str(); }
  size_t size() const { return key.size(); }
};

typedef std::vector<uint8_t> ValueType;

class BuildDB;
class BuildEngine;
class BuildEngineDelegate;

/// A monotonically increasing number identifying which iteration of a build
/// an event occurred during.
typedef uint64_t Epoch;

/// This object contains the result of executing a task to produce the value for
/// a key.
struct Result {
  /// The last value that resulted from executing the task.
  ValueType value = {};

  /// The signature of the node that generated the result.
  basic::CommandSignature signature;

  /// The build timestamp during which the result \see Value was computed.
  Epoch computedAt = 0;

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
  Epoch builtAt = 0;

  /// The explicit dependencies required by the generation.
  AttributedKeyIDs dependencies;
  
  /// The start of the command as a timestamp since a reference time
  basic::Clock::Timestamp start;
  
  /// The timestamp of when the command finished computing
  basic::Clock::Timestamp end;
};


class TaskInterface {
private:
  void* impl;
  void* ctx;

public:
  TaskInterface(void* impl, void* ctx) : impl(impl), ctx(ctx) {}

  /// @name Accessors
  /// @{

  Epoch currentEpoch();
  bool isCancelled();
  BuildEngineDelegate* delegate();

  /// @}


  /// @name Task Management APIs
  /// @{

  /// Specify the task depends upon the result of computing \arg Key.
  /// The order in which these inputs are requested will be preserved in
  /// subsequent builds when scanning dependencies.
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
  void request(const KeyType& key, uintptr_t inputID);

  /// Specify that the task must be built subsequent to the
  /// computation of \arg Key.
  ///
  /// The value of the computation of \arg Key is not available to the task, and
  /// the only guarantee the engine provides is that if \arg Key is computed
  /// during a build, then task will not be computed until after it.
  void mustFollow(const KeyType& key);

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
  void discoveredDependency(const KeyType& key);

  /// Called by a task to indicate it has completed and to provide its value.
  ///
  /// It is legal to call this method from any thread.
  ///
  /// \param value The new value for the task's rule.
  ///
  /// \param forceChange If true, treat the value as changed and trigger
  /// dependents to rebuild, even if the value itself is not different from the
  /// prior result.
  void complete(ValueType&& value, bool forceChange = false);

  /// Called by a task to run an asynchronous computation
  void spawn(basic::QueueJob&&);

  /// Called by a task to spawn an external process.
  void spawn(basic::QueueJobContext* context,
             ArrayRef<StringRef> commandLine,
             ArrayRef<std::pair<StringRef, StringRef>> environment,
             basic::ProcessAttributes attributes = {true},
             llvm::Optional<basic::ProcessCompletionFn> completionFn = {llvm::None});

  basic::ProcessStatus spawn(basic::QueueJobContext* context,
                             ArrayRef<StringRef> commandLine);
  /// @}
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
///
/// A task which has been cancelled may be destroyed without any of the above
/// behaviors having been completed.
class Task {
public:
  Task() {}
  virtual ~Task();

  /// Executed by the build engine when the task should be started.
  virtual void start(TaskInterface) = 0;

  /// Invoked by the build engine to provide the prior result for the task's
  /// output, if present.
  ///
  /// This callback will always be invoked immediately after the task is
  /// started, and prior to its receipt of any other callbacks.
  virtual void providePriorValue(TaskInterface, const ValueType& value) {};

  /// Invoked by the build engine to provide an input value as it becomes
  /// available.
  ///
  /// \param inputID The unique identifier provided to the build engine to
  /// represent this input when requested in \see
  /// BuildEngine::taskNeedsInput().
  ///
  /// \param value The computed value for the given input.
  virtual void provideValue(TaskInterface, uintptr_t inputID,
                            const ValueType& value) = 0;

  /// Executed by the build engine to indicate that all inputs have been
  /// provided, and the task should begin its computation. If the client will
  /// perform non-trivial work for this computation, it should be executed
  /// asynchronously on separate thread/work queue to prevent stalling the core
  /// engine.
  ///
  /// The task is expected to call \see BuildEngine::taskIsComplete() when it is
  /// done with its computation.
  ///
  /// It is an error for any client to request an additional input for a task
  /// after the last requested input has been provided by the build engine.
  virtual void inputsAvailable(TaskInterface) = 0;
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

  enum class CycleAction {
    /// Indicates a rule will be forced to build
    ForceBuild = 0,

    /// Indicates a rule's prior value will be supplied to a downstream rule
    SupplyPriorValue = 1
  };

public:
  /// The key computed by the rule.
  const KeyType key;

  /// The signature of the rule.
  const basic::CommandSignature signature;

private:
  Rule(const Rule&) LLBUILD_DELETED_FUNCTION;
  void operator=(const Rule&) LLBUILD_DELETED_FUNCTION;

public:
  Rule(const KeyType& key, const basic::CommandSignature& signature = {})
    : key(key), signature(signature) { }
  virtual ~Rule() = 0;

  /// Called to create the task to build the rule, when necessary.
  virtual Task* createTask(BuildEngine&) = 0;

  /// Called to check whether the previously computed value for this rule is
  /// still valid.
  ///
  /// This callback is designed for use in synchronizing values which represent
  /// state managed externally to the build engine. For example, a rule which
  /// computes something on the file system may use this to verify that the
  /// computed output has not changed since it was built.
  virtual bool isResultValid(BuildEngine&, const ValueType&) = 0;

  /// Called to indicate a change in the rule status.
  virtual void updateStatus(BuildEngine&, StatusKind);
};

/// Delegate interface for use with the build engine.
class BuildEngineDelegate {
public:
  virtual ~BuildEngineDelegate();  

  /// Called by the build engine to get create the object used to dispatch work.
  virtual std::unique_ptr<basic::ExecutionQueue> createExecutionQueue() = 0;

  /// Get the rule to use for the given Key.
  ///
  /// The delegate *must* provide a rule for any possible key that can be
  /// requested (either by a client, through \see BuildEngine::build(), or via a
  /// Task through mechanisms such as \see BuildEngine::taskNeedsInput(). If a
  /// requested Key cannot be supplied, the delegate should provide a dummy rule
  /// that the client can translate into an error.
  virtual std::unique_ptr<Rule> lookupRule(const KeyType& key) = 0;

  /// Called when a cycle is detected by the build engine to check if it should
  /// attempt to resolve the cycle and continue
  ///
  /// \param items The ordered list of items comprising the cycle, starting from
  /// the node which was requested to build and ending with the first node in
  /// the cycle (i.e., the node participating in the cycle will appear twice).
  /// \param candidateRule The rule the engine will use to attempt to break the
  /// cycle.
  /// \param action The action the engine will take on the candidateRule.
  /// \returns True if the engine should attempt to resolve the cycle, false
  /// otherwise. Resolution is attempted by either forcing items to be built, or
  /// supplying a previously built result to a node in the cycle. The latter
  /// action may yield unexpected results and thus this should be opted into
  /// with care.
  virtual bool shouldResolveCycle(const std::vector<Rule*>& items,
                                  Rule* candidateRule,
                                  Rule::CycleAction action);

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

/// Abstract class for visiting the rule results of a node and its dependencies.
class RuleResultsWalker {
public:
  /// Specifies how node visitation should proceed.
  enum class ActionKind {
    /// Continue visiting the rule results of the current node dependencies.
    VisitDependencies = 0,

    /// Continue visiting but skip the current node dependencies.
    SkipDependencies = 1,

    /// Stop visitation.
    Stop = 2
  };

  /// Accepts the rule result for a node.
  ///
  /// \param key The key for the currenly visited node.
  /// \param key The rule result for the currenly visited node.
  /// \returns An action kind to indicate how visitation should proceed.
  virtual ActionKind visitResult(const KeyType& key, const core::Result& result) = 0;
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

  /// Get the current build epoch used by the engine.
  ///
  /// The iteration is a monotonically increasing value which is incremented
  /// with each requested build.
  Epoch getCurrentEpoch();

  /// @name Rule Definition
  /// @{

  /// Add a rule which the engine can use to produce outputs.
  void addRule(std::unique_ptr<Rule>&& rule);

  /// @}

  /// @name Client API
  /// @{

  /// Build the result for a particular key.
  ///
  /// \param resultsWalker Optional walker for receiving the rule results of the node and its dependencies.
  /// \returns The result of computing the key, or the empty value if the key
  /// could not be computed; the latter case only happens if a cycle was
  /// discovered currently.
  const ValueType& build(const KeyType& key, RuleResultsWalker* resultsWalker = nullptr);

  /// Cancel the currently running build.
  ///
  /// The engine guarantees that it will not *start* any task after processing
  /// the current engine work loop iteration after it has been cancelled.
  ///
  /// This method is thread-safe.
  ///
  /// This method should only be used when a build is actively running. Invoking
  /// this method before a build has started will have no effect.
  //
  // FIXME: This method is hard to use correctly, we should modify build to
  // return an explicit object to represent an in-flight build, and then expose
  // cancellation on that.
  void cancelBuild();

  void resetForBuild();
  bool isCancelled();
  
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

  /// The maximum allowed input ID.
  static const uintptr_t kMaximumInputID = ~(uintptr_t)0xFF;
};

}
}

namespace std
{
  template<>
  struct hash<llbuild::core::KeyType>
  {
    size_t operator()(const llbuild::core::KeyType& key) const
    {
      return hash<std::string>{}(key.str());
    }
  };
}

#endif
