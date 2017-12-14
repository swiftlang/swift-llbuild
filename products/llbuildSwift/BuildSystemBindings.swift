// This source file is part of the Swift.org open source project
//
// Copyright 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

// This file contains Swift bindings for the llbuild C API.

#if os(Linux)
import Glibc
#else
import Darwin.C
#endif

import Foundation

import llbuild

private func bytesFromData(_ data: llb_data_t) -> [UInt8] {
    return Array(UnsafeBufferPointer(start: data.data, count: Int(data.length)))
}

/// Create a new `llb_data_t` instance containing an allocated copy of the given `bytes`.
private func copiedDataFromBytes(_ bytes: [UInt8]) -> llb_data_t {
    // Create the data.
    let buf = UnsafeMutableBufferPointer(start: UnsafeMutablePointer<UInt8>.allocate(capacity: bytes.count), count: bytes.count)

    // Copy the data.
    memcpy(buf.baseAddress!, UnsafePointer<UInt8>(bytes), buf.count)

    // Fill in the result structure.
    return llb_data_t(length: UInt64(buf.count), data: unsafeBitCast(buf.baseAddress, to: UnsafePointer<UInt8>.self))
}

// FIXME: We should eventually eliminate the need for this.
private func stringFromData(_ data: llb_data_t) -> String {
    return String(decoding: UnsafeBufferPointer(start: data.data, count: Int(data.length)), as: Unicode.UTF8.self)
}


public protocol Tool: class {
    /// Called to create a specific command instance of this tool.
    func createCommand(_ name: String) -> ExternalCommand
}

private final class ToolWrapper {
    let tool: Tool

    init(tool: Tool) {
        self.tool = tool
    }
    
    /// The owning list of all created commands.
    //
    // FIXME: This is lame, we should be able to destroy these naturally.
    private var commandWrappers: [CommandWrapper] = []
    func createCommand(_ name: UnsafePointer<llb_data_t>) -> OpaquePointer? {
        let command = tool.createCommand(stringFromData(name.pointee))
        let wrapper = CommandWrapper(command: command)
        self.commandWrappers.append(wrapper)
        var _delegate = llb_buildsystem_external_command_delegate_t()
        _delegate.context = Unmanaged.passUnretained(wrapper).toOpaque()
        _delegate.get_signature = { return BuildSystem.toCommandWrapper($0!).getSignature($1!, $2!) }
        _delegate.execute_command = { return BuildSystem.toCommandWrapper($0!).executeCommand($1!, $2!, $3!, $4!) }
            
        // Create the low-level command.
        wrapper._command = Command(llb_buildsystem_external_command_create(name, _delegate))

        return wrapper._command.handle
    }
}

public protocol ExternalCommand: class {
    /// Get a signature used to identify the internal state of the command.
    ///
    /// This is checked to determine if the command needs to rebuild versus the last time it was run.
    func getSignature(_ command: Command) -> [UInt8]

    /// Called to execute the given command.
    ///
    /// - command: A handle to the executing command.
    /// - returns: True on success.
    func execute(_ command: Command) -> Bool
}

// FIXME: The terminology is really confusing here, we have ExternalCommand which is divorced from the actual internal command implementation of the same name.
private final class CommandWrapper {
    let command: ExternalCommand
    var _command: Command
    
    init(command: ExternalCommand) {
        self.command = command
        self._command = Command(nil)
    }

    func getSignature(_: OpaquePointer, _ data: UnsafeMutablePointer<llb_data_t>) {
        data.pointee = copiedDataFromBytes(command.getSignature(_command))
    }

    func executeCommand(_: OpaquePointer, _ bsci: OpaquePointer, _ task: OpaquePointer, _ jobContext: OpaquePointer) -> Bool {
        return command.execute(_command)
    }
}

/// Encapsulates a diagnostic as reported by the build system.
public struct Diagnostic {
    public enum Kind: CustomStringConvertible {
        case note
        case warning
        case error
        
        init(_ kind: llb_buildsystem_diagnostic_kind_t) {
            switch kind {
            case llb_buildsystem_diagnostic_kind_note:
                self = .note
            case llb_buildsystem_diagnostic_kind_warning:
                self = .warning
            default:
                self = .error
            }
        }
        
        public var description: String {
            switch self {
            case .note:
                return "note"
            case .warning:
                return "warning"
            case .error:
                return "error"
            }
        }
    }

    /// The kind of diagnostic.
    public let kind: Kind

    /// The diagnostic location, if provided.
    public let location: (filename: String, line: Int, column: Int)?

    /// The diagnostic text.
    public let message: String
}

/// Handle for a command as invoked by the low-level BuildSystem.
public struct Command: Hashable {
    fileprivate let handle: OpaquePointer?

    fileprivate init(_ handle: OpaquePointer?) {
        self.handle = handle
    }

    /// The command name.
    //
    // FIXME: We shouldn't need to expose this to use for mapping purposes, we should be able to use something more efficient.
    public var name: String {
        var data = llb_data_t()
        withUnsafeMutablePointer(to: &data) { (ptr: UnsafeMutablePointer<llb_data_t>) in
            llb_buildsystem_command_get_name(handle, ptr)
        }
        return stringFromData(data)
    }

    /// The description provided by the command.
    public var description: String {
        let name = llb_buildsystem_command_get_description(handle)!
        defer { free(name) }

        return String(cString: name)
    }

    public var hashValue: Int {
        return handle!.hashValue
    }
}
public func ==(lhs: Command, rhs: Command) -> Bool {
    return lhs.handle == rhs.handle
}

/// Handle for a process which has been launched by a command.
//
// FIXME: We would like to call this Process, but then it conflicts with Swift's builtin Process. Maybe there is another name?
public struct ProcessHandle: Hashable {
    fileprivate let handle: OpaquePointer

    fileprivate init(_ handle: OpaquePointer) {
        self.handle = handle
    }

    public var hashValue: Int {
        return handle.hashValue
    }
}
public func ==(lhs: ProcessHandle, rhs: ProcessHandle) -> Bool {
    return lhs.handle == rhs.handle
}

/// Result of a command execution.
public enum CommandResult {
  case succeeded
  case failed
  case cancelled
  case skipped

  init(_ result: llb_buildsystem_command_result_t) {
    switch result {
    case llb_buildsystem_command_result_succeeded:
      self = .succeeded
    case llb_buildsystem_command_result_failed:
      self = .failed
    case llb_buildsystem_command_result_cancelled:
      self = .cancelled
    case llb_buildsystem_command_result_skipped:
      self = .skipped
    default:
      fatalError("unknown command result")
    }
  }
}

/// Status change event kinds.
public enum CommandStatusKind {
    case isScanning
    case isUpToDate
    case isComplete
            
    init(_ kind: llb_buildsystem_command_status_kind_t) {
        switch kind {
        case llb_buildsystem_command_status_kind_is_scanning:
            self = .isScanning
        case llb_buildsystem_command_status_kind_is_up_to_date:
            self = .isUpToDate
        case llb_buildsystem_command_status_kind_is_complete:
            self = .isComplete
        default:
            fatalError("unknown status kind")
        }
    }
}

/// The BuildKey encodes the key space used by the BuildSystem when using the
/// core BuildEngine.
public struct BuildKey {
    public enum Kind {
        /// A key used to identify a command.
        case command
        /// A key used to identify a custom task.
        case customTask
        /// A key used to identify directory contents.
        case directoryContents
        /// A key used to identify the signature of a complete directory tree.
        case directoryTreeSignature
        /// A key used to identify a node.
        case node
        /// A key used to identify a target.
        case target
        /// An invalid key kind.
        case unknown

        init(_ kind: llb_build_key_kind_t) {
            switch (kind) {
            case llb_build_key_kind_command:
                self = .command
            case llb_build_key_kind_custom_task:
                self = .customTask
            case llb_build_key_kind_directory_contents:
                self = .directoryContents
            case llb_build_key_kind_directory_tree_signature:
                self = .directoryTreeSignature
            case llb_build_key_kind_node:
                self = .node
            case llb_build_key_kind_target:
                self = .target
            case llb_build_key_kind_unknown:
                self = .unknown
            default:
                fatalError("unknown build key kind")
            }
        }
    }

    /// The kind of key
    public let kind: Kind

    /// The actual key data
    public let key: String

    public init(kind: Kind, key: String) {
        self.kind = kind
        self.key = key
    }
}

/// Delegate interface for use with the build system.
public protocol BuildSystemDelegate {
    /// Called in response to requests for new tools.
    ///
    /// The client should return an appropriate tool implementation if recognized.
    func lookupTool(_ name: String) -> Tool?
    
    /// Called to report any form of command failure.
    ///
    /// This can may be called to report the failure of a command which has
    /// executed, but may also be used to report the inability of a command to
    /// run. It is expected to be used by the client in making decisions with
    /// regard to cancelling the build.
    func hadCommandFailure()
    
    /// Called to report an unassociated diagnostic from the build system.
    func handleDiagnostic(_ diagnostic: Diagnostic)

    /// Called when a command has changed state.
    ///
    /// The system guarantees that any commandStart() call will be paired with
    /// exactly one \see commandFinished() call.
    func commandStatusChanged(_ command: Command, kind: CommandStatusKind)

    /// Called when a command is preparing to start.
    ///
    /// The system guarantees that any commandStart() call will be paired with
    /// exactly one \see commandFinished() call.
    func commandPreparing(_ command: Command)

    /// Called when a command has been started.
    ///
    /// The system guarantees that any commandStart() call will be paired with
    /// exactly one \see commandFinished() call.
    func commandStarted(_ command: Command)
    
    /// Called to allow the delegate to skip commands without cancelling their
    /// dependents. See llbuild's should_command_start.
    func shouldCommandStart(_ command: Command) -> Bool

    /// Called when a command has been finished.
    func commandFinished(_ command: Command, result: CommandResult)

    /// Called to report an error during the execution of a command.
    func commandHadError(_ command: Command, message: String)

    /// Called to report a note during the execution of a command.
    func commandHadNote(_ command: Command, message: String)

    /// Called to report a warning during the execution of a command.
    func commandHadWarning(_ command: Command, message: String)

    /// Called when a command's job has started executing an external process.
    ///
    /// The system guarantees that any commandProcessStarted() call will be paired
    /// with exactly one \see commandProcessFinished() call.
    ///
    /// - parameter process: A unique handle used in subsequent delegate calls
    /// to identify the process. This handle should only be used to associate
    /// different status calls relating to the same process. It is only
    /// guaranteed to be unique from when it has been provided here to when it
    /// has been provided to the \see commandProcessFinished() call.
    func commandProcessStarted(_ command: Command, process: ProcessHandle)

    /// Called to report an error in the management of a command process.
    ///
    /// - parameter process: The process handle.
    /// - parameter message: The error message.
    func commandProcessHadError(_ command: Command, process: ProcessHandle, message: String)

    /// Called to report a command processes' (merged) standard output and error.
    ///
    /// - parameter process: The process handle.
    /// - parameter data: The process output.
    func commandProcessHadOutput(_ command: Command, process: ProcessHandle, data: [UInt8])

    /// Called when a command's job has finished executing an external process.
    ///
    /// - parameter process: The handle used to identify the process. This
    /// handle will become invalid as soon as the client returns from this API
    /// call.
    ///
    /// - parameter result: Whether the process suceeded, failed or was cancelled.
    /// - parameter exitStatus: The raw exit status of the process.
    func commandProcessFinished(_ command: Command, process: ProcessHandle, result: CommandResult, exitStatus: Int)

    /// Called when a cycle is detected by the build engine and it cannot make
    /// forward progress.
    func cycleDetected(rules: [BuildKey])
}

/// Utility class for constructing a C-style environment.
private final class CStyleEnvironment {
    /// The list of individual bindings, which must be deallocated.
    private let bindings: [UnsafeMutablePointer<CChar>]

    /// The environment array, which will be a valid C-style environment pointer
    /// for the lifetime of the instance.
    let envp: [UnsafePointer<CChar>?]
        
    init(_ environment: [String: String]) {
        // Allocate the individual binding strings.
        self.bindings = environment.map{ "\($0.0)=\($0.1)".withCString(strdup)! }

        // Allocate the envp array.
        self.envp = self.bindings.map{ UnsafePointer($0) } + [nil]
    }

    deinit {
        bindings.forEach{ free($0) }
    }
}

/// This class allows building using llbuild's native BuildSystem component.
public final class BuildSystem {
    /// The build file that the system is configured with.
    public let buildFile: String

    /// The delegate used by the system.
    public let delegate: BuildSystemDelegate
    
    /// The internal llbuild build system.
    private var _system: OpaquePointer? = nil

    /// The C environment, if used.
    private let _cEnvironment: CStyleEnvironment?

    public init(buildFile: String, databaseFile: String, delegate: BuildSystemDelegate, environment: [String: String]? = nil, serial: Bool = false, traceFile: String? = nil) {
        self.buildFile = buildFile
        self.delegate = delegate

        // Create a stable C string path.
        let pathPtr = strdup(buildFile)
        defer { free(pathPtr) }
        
        let dbPathPtr = strdup(databaseFile)
        defer { free(dbPathPtr) }

        let tracePathPtr = strdup(traceFile ?? "")
        defer { free(tracePathPtr) }

        // Allocate a C style environment, if necessary.
        _cEnvironment = environment.map{ CStyleEnvironment($0) }
        
        var _invocation = llb_buildsystem_invocation_t()
        _invocation.buildFilePath = UnsafePointer(pathPtr)
        _invocation.dbPath = UnsafePointer(dbPathPtr)
        _invocation.traceFilePath = UnsafePointer(tracePathPtr)
        _invocation.environment = _cEnvironment.map{ UnsafePointer($0.envp) }
        _invocation.showVerboseStatus = true
        _invocation.useSerialBuild = serial

        // Construct the system delegate.
        var _delegate = llb_buildsystem_delegate_t()
        _delegate.context = Unmanaged.passUnretained(self).toOpaque()
        _delegate.lookup_tool = { return BuildSystem.toSystem($0!).lookupTool($1!) }
        _delegate.had_command_failure = { BuildSystem.toSystem($0!).hadCommandFailure() }
        _delegate.handle_diagnostic = { BuildSystem.toSystem($0!).handleDiagnostic($1, String(cString: $2!), Int($3), Int($4), String(cString: $5!)) }
        _delegate.command_status_changed = { BuildSystem.toSystem($0!).commandStatusChanged(Command($1), $2) }
        _delegate.command_preparing = { BuildSystem.toSystem($0!).commandPreparing(Command($1)) }
        _delegate.command_started = { BuildSystem.toSystem($0!).commandStarted(Command($1)) }
        _delegate.should_command_start = { BuildSystem.toSystem($0!).shouldCommandStart(Command($1)) }
        _delegate.command_finished = { BuildSystem.toSystem($0!).commandFinished(Command($1), CommandResult($2)) }
        _delegate.command_had_error = { BuildSystem.toSystem($0!).commandHadError(Command($1), $2!) }
        _delegate.command_had_note = { BuildSystem.toSystem($0!).commandHadNote(Command($1), $2!) }
        _delegate.command_had_warning = { BuildSystem.toSystem($0!).commandHadWarning(Command($1), $2!) }
        _delegate.command_process_started = { BuildSystem.toSystem($0!).commandProcessStarted(Command($1), ProcessHandle($2!)) }
        _delegate.command_process_had_error = { BuildSystem.toSystem($0!).commandProcessHadError(Command($1), ProcessHandle($2!), $3!) }
        _delegate.command_process_had_output = { BuildSystem.toSystem($0!).commandProcessHadOutput(Command($1), ProcessHandle($2!), $3!) }
        _delegate.command_process_finished = { BuildSystem.toSystem($0!).commandProcessFinished(Command($1), ProcessHandle($2!), CommandResult($3), Int($4)) }
        _delegate.cycle_detected = {
            var rules = [BuildKey]()
            UnsafeBufferPointer(start: $1, count: Int($2)).forEach {
                rules.append(BuildKey(kind: BuildKey.Kind($0.kind), key: String(cString: $0.key)))
            }
            BuildSystem.toSystem($0!).cycleDetected(rules)
        }

        // Create the system.
        _system = llb_buildsystem_create(_delegate, _invocation)
    }

    deinit {
        assert(_system != nil)
        llb_buildsystem_destroy(_system)
    }

    /// Build the default target.
    ///
    /// The client is responsible for ensuring only one build is ever executing concurrently.
    public func build() -> Bool {
        var data = llb_data_t(length: 0, data: nil)
        return llb_buildsystem_build(_system, &data)
    }

    /// Cancel any running build.
    public func cancel() {
        llb_buildsystem_cancel(_system)
    }
    
    /// MARK: Internal Delegate Implementation

    /// Helper function for getting the system from the delegate context.
    static private func toSystem(_ context: UnsafeMutableRawPointer) -> BuildSystem {
        return Unmanaged<BuildSystem>.fromOpaque(UnsafeRawPointer(context)).takeUnretainedValue()
    }

    /// Helper function for getting the tool wrapper from the delegate context.
    static private func toToolWrapper(_ context: UnsafeMutableRawPointer) -> ToolWrapper {
        return Unmanaged<ToolWrapper>.fromOpaque(UnsafeRawPointer(context)).takeUnretainedValue()
    }

    /// Helper function for getting the command wrapper from the delegate context.
    static fileprivate func toCommandWrapper(_ context: UnsafeMutableRawPointer) -> CommandWrapper {
        return Unmanaged<CommandWrapper>.fromOpaque(UnsafeRawPointer(context)).takeUnretainedValue()
    }

    /// The owning list of all created tools.
    //
    // FIXME: This is lame, we should be able to destroy these naturally.
    private var toolWrappers: [ToolWrapper] = []
    private func lookupTool(_ name: UnsafePointer<llb_data_t>) -> OpaquePointer? {
        // Look up the named tool.
        guard let tool = delegate.lookupTool(stringFromData(name.pointee)) else {
            return nil
        }
        
        // If we got a tool, save it and create an appropriate low-level instance.
        let wrapper = ToolWrapper(tool: tool)
        self.toolWrappers.append(wrapper)
        var _delegate = llb_buildsystem_tool_delegate_t()
        _delegate.context = Unmanaged.passUnretained(wrapper).toOpaque()
        _delegate.create_command = { return BuildSystem.toToolWrapper($0!).createCommand($1!) }
            
        // Create the tool.
        return llb_buildsystem_tool_create(name, _delegate)
    }

    private func hadCommandFailure() {
        delegate.hadCommandFailure()
    }
    
    private func handleDiagnostic(_ _kind: llb_buildsystem_diagnostic_kind_t, _ filename: String, _ line: Int, _ column: Int, _ message: String) {
        let kind: Diagnostic.Kind = Diagnostic.Kind(_kind)

        // Clean up the location.
        let location: (filename: String, line: Int, column: Int)?
        if filename == "<unknown>" {
            location = nil
        } else {
            location = (filename: filename, line: line, column: column)
        }
        
        delegate.handleDiagnostic(Diagnostic(kind: kind, location: location, message: message))
    }
    
    private func commandStatusChanged(_ command: Command, _ kind: llb_buildsystem_command_status_kind_t) {
        delegate.commandStatusChanged(command, kind: CommandStatusKind(kind))
    }
    
    private func commandPreparing(_ command: Command) {
        delegate.commandPreparing(command)
    }

    private func commandStarted(_ command: Command) {
        delegate.commandStarted(command)
    }
    
    private func shouldCommandStart(_ command: Command) -> Bool {
        return delegate.shouldCommandStart(command)
    }

    private func commandFinished(_ command: Command, _ result: CommandResult) {
        delegate.commandFinished(command, result: result)
    }

    private func commandHadError(_ command: Command, _ data: UnsafePointer<llb_data_t>) {
        delegate.commandHadError(command, message: stringFromData(data.pointee))
    }

    private func commandHadNote(_ command: Command, _ data: UnsafePointer<llb_data_t>) {
        delegate.commandHadNote(command, message: stringFromData(data.pointee))
    }

    private func commandHadWarning(_ command: Command, _ data: UnsafePointer<llb_data_t>) {
        delegate.commandHadWarning(command, message: stringFromData(data.pointee))
    }

    private func commandProcessStarted(_ command: Command, _ process: ProcessHandle) {
        delegate.commandProcessStarted(command, process: process)
    }

    private func commandProcessHadError(_ command: Command, _ process: ProcessHandle, _ data: UnsafePointer<llb_data_t>) {
        delegate.commandProcessHadError(command, process: process, message: stringFromData(data.pointee))
    }

    private func commandProcessHadOutput(_ command: Command, _ process: ProcessHandle, _ data: UnsafePointer<llb_data_t>) {
        delegate.commandProcessHadOutput(command, process: process, data: bytesFromData(data.pointee))
    }

    private func commandProcessFinished(_ command: Command, _ process: ProcessHandle, _ result: CommandResult, _ exitStatus: Int) {
        delegate.commandProcessFinished(command, process: process, result: result, exitStatus: exitStatus)
    }

    private func cycleDetected(_ rules: [BuildKey]) {
        delegate.cycleDetected(rules: rules)
    }
}
