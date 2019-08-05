// This source file is part of the Swift.org open source project
//
// Copyright 2015-2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

// This file contains Swift bindings for the llbuild C API.

import Foundation

// We don't need this import if we're building
// this file as part of the llbuild framework.
#if !LLBUILD_FRAMEWORK
import llbuild
#endif

enum DatabaseError: Error {
    case AttachFailure(message: String)
}

private func stringFromUInt8Array(_ data: [UInt8]) -> String {
    // Convert as a UTF8 string, if possible.
    if let str = String(data: Data(data), encoding: String.Encoding.utf8) {
        return str
    }

    // Otherwise, return a string representation of the bytes.
    return String(describing: data)
}

/// Key objects are used to identify rules that can be built.
public struct Key: CustomStringConvertible, Equatable, Hashable {
    public let data: [UInt8]

    // MARK: CustomStringConvertible Conformance

    public var description: String {
        return "<Key: '\(toString())'>"
    }

    // MARK: Implementation

    public init(_ data: [UInt8]) { self.data = data }
    public init(_ str: String) { self.init(Array(str.utf8)) }
    public init(_ buildKey: BuildKey) { self.init(buildKey.keyData) }

    /// Convert to a string representation.
    public func toString() -> String {
        return stringFromUInt8Array(self.data)
    }

    /// Create a Key object from an llb_data_t.
    fileprivate static func fromInternalData(_ data: llb_data_t) -> Key {
        return Key([UInt8](UnsafeBufferPointer(start: data.data, count: Int(data.length))))
    }

    /// Provide a Key contents as an llb_data_t pointer.
    fileprivate func withInternalDataPtr<T>(closure: (UnsafePointer<llb_data_t>) -> T) -> T {
        return data.withUnsafeBufferPointer { (dataPtr: UnsafeBufferPointer<UInt8>) -> T in
            var value = llb_data_t(length: UInt64(self.data.count), data: dataPtr.baseAddress)
            return withUnsafePointer(to: &value, closure)
        }
    }
}

/// Value objects are the result of building rules.
public struct Value: CustomStringConvertible, Equatable, Hashable {
    public let data: [UInt8]

    public var description: String {
        return "<Value: '\(toString())'>"
    }

    public init(_ data: [UInt8]) { self.data = data }
    public init(_ str: String) { self.init(Array(str.utf8)) }

    /// Convert to a string representation.
    public func toString() -> String {
        return stringFromUInt8Array(self.data)
    }

    /// Create a Value object from an llb_data_t.
    fileprivate static func fromInternalData(_ data: llb_data_t) -> Value {
        return Value([UInt8](UnsafeBufferPointer(start: data.data, count: Int(data.length))))
    }

    /// Provide a Value contents as an llb_data_t pointer.
    fileprivate func withInternalDataPtr<T>(closure: (UnsafePointer<llb_data_t>) -> T) -> T {
        return data.withUnsafeBufferPointer { (dataPtr: UnsafeBufferPointer<UInt8>) -> T in
            var value = llb_data_t(length: UInt64(self.data.count), data: dataPtr.baseAddress)
            return withUnsafePointer(to: &value, closure)
        }
    }

    /// Create a Value object by providing an pointer to write the output into.
    ///
    /// \param closure The closure to execute with a pointer to a llb_data_t to
    /// use. The structure *must* be filled in by the closure.
    ///
    /// \return The output Value.
    fileprivate static func fromInternalDataOutputPtr(closure: (UnsafeMutablePointer<llb_data_t>) -> Void) -> Value {
        var data = llb_data_t()
        withUnsafeMutablePointer(to: &data, closure)
        return Value.fromInternalData(data)
    }
}

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
public protocol Rule {
    /// Called to create the task to build the rule, when necessary.
    func createTask() -> Task

    /// Called to check whether the previously computed value for this rule is
    /// still valid.
    ///
    /// This callback is designed for use in synchronizing values which represent
    /// state managed externally to the build engine. For example, a rule which
    /// computes something on the file system may use this to verify that the
    /// computed output has not changed since it was built.
    func isResultValid(_ priorValue: Value) -> Bool

    /// Called to indicate a change in the rule status.
    func updateStatus(_ status: RuleStatus)
}

/// Protocol extension for default Rule methods.
public extension Rule {
    func isResultValid(_ priorValue: Value) -> Bool { return true }
    func updateStatus(_ status: RuleStatus) { }
}

/// A task object represents an abstract in-progress computation in the build
/// engine.
///
/// The task represents not just the primary computation, but also the process
/// of starting the computation and necessary input dependencies. Tasks are
/// expected to be created in response to \see BuildEngine requests to initiate
/// the production of particular result value.
///
/// The creator may use \see TaskBuildEngine.taskNeedsInput() to specify input
/// dependencies on the Task. The Task itself may also specify additional input
/// dependencies dynamically during the execution of \see Task.start() or \see
/// Task.provideValue().
///
/// Once a task has been created and registered, the BuildEngine will invoke
/// \see Task::start() to initiate the computation. The BuildEngine will provide
/// the in progress task with its requested inputs via \see
/// Task.provideValue().
///
/// After all inputs requested by the Task have been delivered, the BuildEngine
/// will invoke \see Task.inputsAvailable() to instruct the Task it should
/// complete its computation and provide the output. The Task is responsible for
/// providing the engine with the computed value when ready using \see
/// TaskBuildEngine.taskIsComplete().
public protocol Task {
    /// Executed by the build engine when the task should be started.
    func start(_ engine: TaskBuildEngine)

    /// Invoked by the build engine to provide an input value as it becomes
    /// available.
    ///
    /// \param inputID The unique identifier provided to the build engine to
    /// represent this input when requested in \see
    /// TaskBuildEngine.taskNeedsInput().
    ///
    /// \param value The computed value for the given input.
    func provideValue(_ engine: TaskBuildEngine, inputID: Int, value: Value)

    /// Executed by the build engine to indicate that all inputs have been
    /// provided, and the task should begin its computation.
    ///
    /// The task is expected to call \see TaskBuildEngine.taskIsComplete() when it is
    /// done with its computation.
    ///
    /// It is an error for any client to request an additional input for a task
    /// after the last requested input has been provided by the build engine.
    func inputsAvailable(_ engine: TaskBuildEngine)
}

/// Delegate interface for use with the build engine.
public protocol BuildEngineDelegate {
    /// Get the rule to use for the given Key.
    ///
    /// The delegate *must* provide a rule for any possible key that can be
    /// requested (either by a client, through \see BuildEngine.build(), or via a
    /// Task through mechanisms such as \see TaskBuildEngine.taskNeedsInput(). If a
    /// requested Key cannot be supplied, the delegate should provide a dummy rule
    /// that the client can translate into an error.
    func lookupRule(_ key: Key) -> Rule

    /// Handle error message reported by the engine
    ///
    /// The delegate should handle this, but a default implementation is
    /// is provided that does nothing for backward compatiblity.
    func error(_ message: String)
}

public extension BuildEngineDelegate {
    func error(_ message: String) {
    }
}

/// Wrapper to allow passing an opaque pointer to a protocol type.
//
// FIXME: Why do we need this, why can't we get a pointer to the protocol
// typed object?
private class Wrapper<T> {
    let item: T
    init(_ item: T) { self.item = item }
}

/// This protocol encapsulates the API that a task can use to communicate with
/// the build engine.
public protocol TaskBuildEngine {
    var engine: BuildEngine { get }

    /// Specify that the task depends upon the result of computing \arg key.
    ///
    /// The result, when available, will be provided to the task via \see
    /// Task.provideValue(), supplying the provided \arg inputID to allow the task
    /// to identify the particular input.
    ///
    /// NOTE: It is an unchecked error for a task to request the same input value
    /// multiple times.
    ///
    /// \param inputID An arbitrary value that may be provided by the client to
    /// use in efficiently associating this input.
    func taskNeedsInput(_ key: Key, inputID: Int)

    /// Specify that the task must be built subsequent to the computation of \arg
    /// key.
    ///
    /// The value of the computation of \arg key is not available to the task, and
    /// the only guarantee the engine provides is that if \arg key is computed
    /// during a build, then task will not be computed until after it.
    func taskMustFollow(_ key: Key)

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
    func taskDiscoveredDependency(_ key: Key)

    /// Indicate that the task has completed and provide its resulting value.
    ///
    /// It is legal to call this method from any thread.
    ///
    /// \param value The new value for the task's rule.
    ///
    /// \param forceChange If true, treat the value as changed and trigger
    /// dependents to rebuild, even if the value itself is not different from the
    /// prior result.
    func taskIsComplete(_ result: Value, forceChange: Bool)
}

extension TaskBuildEngine {
    /// Indicate that the task has completed and provide its resulting value.
    ///
    /// It is legal to call this method from any thread.
    ///
    /// - Parameter result: value The new value for the task's rule.
    public func taskIsComplete(_ result: Value) {
        self.taskIsComplete(result, forceChange: false)
    }
}

/// Single concrete implementation of the TaskBuildEngine protocol.
private class TaskWrapper: CustomStringConvertible, TaskBuildEngine {
    let engine: BuildEngine
    let task: Task
    var taskInternal: OpaquePointer?

    var description: String {
        return "<TaskWrapper engine:\(engine), task:\(task)>"
    }

    init(_ engine: BuildEngine, _ task: Task) {
        self.engine = engine
        self.task = task
    }

    func taskNeedsInput(_ key: Key, inputID: Int) {
        engine.taskNeedsInput(self, key: key, inputID: inputID)
    }

    func taskMustFollow(_ key: Key) {
        engine.taskMustFollow(self, key: key)
    }

    func taskDiscoveredDependency(_ key: Key) {
        engine.taskDiscoveredDependency(self, key: key)
    }

    func taskIsComplete(_ result: Value, forceChange: Bool = false) {
        engine.taskIsComplete(self, result: result, forceChange: forceChange)
    }
}

/// A build engine supports fast, incremental, persistent, and parallel
/// execution of computational graphs.
///
/// Computational elements in the graph are modeled by \see Rule objects, which
/// are assocated with a specific \see Key, and which can be executed to produce
/// an output \see Value for that key.
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
public class BuildEngine {
    /// The client delegate.
    private var delegate: BuildEngineDelegate

    /// The internal llbuild build engine.
    private var _engine: OpaquePointer?

    /// Our llbuild engine delegate object.
    private var _delegate = llb_buildengine_delegate_t()

    /// The number of rules which have been defined.
    public var numRules: Int = 0

    public init(delegate: BuildEngineDelegate) {
        self.delegate = delegate

        // Initialize the delegate.
        _delegate.context = unsafeBitCast(Unmanaged.passUnretained(self), to: UnsafeMutableRawPointer.self)

        _delegate.lookup_rule = { BuildEngine.toEngine($0!).lookupRule($1!, $2!) }
        // FIXME: Include cycleDetected callback.

        _delegate.error = { BuildEngine.toEngine($0!).delegate.error(String(cString: $1!)) }

        // Create the engine.
        _engine = llb_buildengine_create(_delegate)
    }

    deinit {
        if _engine != nil {
            close()
        }
    }

    public func close() {
        assert(_engine != nil)
        llb_buildengine_destroy(_engine)
        _engine = nil
    }

    /// Build the result for a particular key.
    public func build(key: Key) -> Value {
        return Value.fromInternalDataOutputPtr { resultPtr in
            key.withInternalDataPtr { llb_buildengine_build(self._engine, $0, resultPtr) }
        }
    }

    /// Attach a database for persisting build state.
    ///
    /// A database should only be attached immediately after creating the engine,
    /// it is an error to attach a database after adding rules or initiating any
    /// builds, or to attempt to attach multiple databases.
    public func attachDB(path: String, schemaVersion: Int = 0) throws {
        let errorPtr = UnsafeMutablePointer<UnsafeMutablePointer<Int8>?>.allocate(capacity: 1)
        defer { errorPtr.deallocate() }

        // FIXME: Why do I have to name the closure signature here?
        var errorMsgOpt: String? = nil
        Key(path).withInternalDataPtr { (ptr) -> Void in
            if !llb_buildengine_attach_db(self._engine, ptr, UInt32(schemaVersion), errorPtr) {
                // If there was an error, report it.
                if let errorPointee = errorPtr.pointee {
                    defer { errorPointee.deallocate() }
                    errorMsgOpt = String(cString: errorPointee)
                }
            }
        }
        // Throw the error, if found.
        if let errorMsg = errorMsgOpt {
            throw DatabaseError.AttachFailure(message: errorMsg)
        }
    }

    /// MARK: Internal Task-Only API

    fileprivate func taskNeedsInput(_ taskWrapper: TaskWrapper, key: Key, inputID: Int) {
        key.withInternalDataPtr { keyPtr in
            llb_buildengine_task_needs_input(self._engine, taskWrapper.taskInternal, keyPtr, UInt(inputID))
        }
    }

    fileprivate func taskMustFollow(_ taskWrapper: TaskWrapper, key: Key) {
        key.withInternalDataPtr { keyPtr in
            llb_buildengine_task_must_follow(self._engine, taskWrapper.taskInternal, keyPtr)
        }
    }

    fileprivate func taskDiscoveredDependency(_ taskWrapper: TaskWrapper, key: Key) {
        key.withInternalDataPtr { keyPtr in
            llb_buildengine_task_discovered_dependency(self._engine, taskWrapper.taskInternal, keyPtr)
        }
    }

    fileprivate func taskIsComplete(_ taskWrapper: TaskWrapper, result: Value, forceChange: Bool = false) {
        result.withInternalDataPtr { dataPtr in
            llb_buildengine_task_is_complete(self._engine, taskWrapper.taskInternal, dataPtr, forceChange)
        }
    }

    /// MARK: Internal Delegate Implementation

    /// Helper function for getting the engine from the delegate context.
    static fileprivate func toEngine(_ context: UnsafeMutableRawPointer) -> BuildEngine {
        return Unmanaged<BuildEngine>.fromOpaque(context).takeUnretainedValue()
    }

    /// Helper function for getting the rule from a rule delegate context.
    static fileprivate func toRule(_ context: UnsafeMutableRawPointer) -> Rule {
        return Unmanaged<Wrapper<Rule>>.fromOpaque(context).takeUnretainedValue().item
    }

    /// Helper function for getting the task from a task delegate context.
    static fileprivate func toTaskWrapper(_ context: UnsafeMutableRawPointer) -> TaskWrapper {
        return Unmanaged<TaskWrapper>.fromOpaque(context).takeUnretainedValue()
    }

    fileprivate func lookupRule(_ key: UnsafePointer<llb_data_t>, _ ruleOut: UnsafeMutablePointer<llb_rule_t>) {
        numRules += 1

        // Get the rule from the client.
        let rule = delegate.lookupRule(Key.fromInternalData(key.pointee))

        // Fill in the output structure.
        //
        // FIXME: We need a deallocation callback in order to ensure this is released.
        ruleOut.pointee.context = unsafeBitCast(Unmanaged.passRetained(Wrapper(rule)), to: UnsafeMutableRawPointer.self)
        ruleOut.pointee.create_task = { (context, engineContext) -> OpaquePointer? in
            let rule = BuildEngine.toRule(context!)
            let engine = BuildEngine.toEngine(engineContext!)
            return engine.ruleCreateTask(rule)
        }
        ruleOut.pointee.is_result_valid = { (context, engineContext, internalRule, value) -> Bool in
            let rule = BuildEngine.toRule(context!)
            return rule.isResultValid(Value.fromInternalData(value!.pointee))
        }
        ruleOut.pointee.update_status =  { (context, engineContext, status) in
            let rule = BuildEngine.toRule(context!)
            return rule.updateStatus(status)
        }
    }

    private func ruleCreateTask(_ rule: Rule) -> OpaquePointer {
        // Create the task.
        let task = rule.createTask()

        // Create the task wrapper.
        //
        // Note that the wrapper here is serving two purposes, it is providing a way
        // to communicate the internal task object to clients, and it is providing a
        // way to segregate the Task-only API from the rest of the BuildEngine API.
        let taskWrapper = TaskWrapper(self, task)

        // Create the task delegate.
        //
        // FIXME: Separate the delegate from the context pointer.
        var taskDelegate = llb_task_delegate_t()
        taskDelegate.context = unsafeBitCast(Unmanaged.passRetained(taskWrapper), to: UnsafeMutableRawPointer.self)
        taskDelegate.destroy_context = { (context) in
            Unmanaged<TaskWrapper>.fromOpaque(context!).release()
        }
        taskDelegate.start = { (context, engineContext, internalTask) in
            let taskWrapper = BuildEngine.toTaskWrapper(context!)
            taskWrapper.task.start(taskWrapper)
        }
        taskDelegate.provide_value = { (context, engineContext, internalTask, inputID, value) in
            let taskWrapper = BuildEngine.toTaskWrapper(context!)
            taskWrapper.task.provideValue(taskWrapper, inputID: Int(inputID), value: Value.fromInternalData(value!.pointee))
        }
        taskDelegate.inputs_available = { (context, engineContext, internalTask) in
            let taskWrapper = BuildEngine.toTaskWrapper(context!)
            taskWrapper.task.inputsAvailable(taskWrapper)
        }

        // Create the internal task.
        taskWrapper.taskInternal = llb_task_create(taskDelegate)

        // FIXME: Why do we have both of these, it is kind of annoying. It makes
        // some amount of sense in the C++ API, but the C API should probably just
        // collapse them.
        return llb_buildengine_register_task(self._engine, taskWrapper.taskInternal)
    }
}

