// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import TSCBasic
import TSCLibc
import func Foundation.exit
import TSCUtility

public protocol Option {
    init()
}

/// This class is used to write on the underlying stream.
///
/// If underlying stream is a not tty, the string will be written in without any
/// formatting.
private final class InteractiveWriter {

    /// The standard error writer.
    static let stderr = InteractiveWriter(stream: stderrStream)

    /// The standard output writer.
    static let stdout = InteractiveWriter(stream: stdoutStream)

    /// The terminal controller, if present.
    let term: TerminalController?

    /// The output byte stream reference.
    let stream: OutputByteStream

    /// Create an instance with the given stream.
    init(stream: OutputByteStream) {
        self.term = TerminalController(stream: stream)
        self.stream = stream
    }

    /// Write the string to the contained terminal or stream.
    func write(_ string: String, inColor color: TerminalController.Color = .noColor, bold: Bool = false) {
        if let term = term {
            term.write(string, inColor: color, bold: bold)
        } else {
            stream <<< string
            stream.flush()
        }
    }
}

func print(diagnostic: Diagnostic, stdoutStream: OutputByteStream) {

    let writer = InteractiveWriter.stderr

    if !(diagnostic.location is UnknownLocation) {
        writer.write(diagnostic.location.description)
        writer.write(": ")
    }

    switch diagnostic.message.behavior {
    case .error:
        writer.write("error: ", inColor: .red, bold: true)
    case .warning:
        writer.write("warning: ", inColor: .yellow, bold: true)
    case .note:
        writer.write("note: ", inColor: .yellow, bold: true)
    case .ignored:
        break
    }

    writer.write(diagnostic.description)
    writer.write("\n")
}

private final class DiagnosticsEngineHandler {

    /// The standard output stream.
    var stdoutStream = TSCBasic.stdoutStream

    /// The default instance.
    static let `default` = DiagnosticsEngineHandler()

    private init() {}

    func diagnosticsHandler(_ diagnostic: Diagnostic) {
        print(diagnostic: diagnostic, stdoutStream: stderrStream)
    }
}

public protocol Runnable {
    static var toolName: String { get }
    init(args: [String]) throws
    func run()
}

open class Tool<Options: Option>: Runnable {
    
    /// An enum indicating the execution status of run commands.
    public enum ExecutionStatus {
        case success
        case failure
    }
    
    public let options: Options
    
    public let diagnostics: DiagnosticsEngine = DiagnosticsEngine(handlers: [DiagnosticsEngineHandler.default.diagnosticsHandler])
    
    /// The execution status of the tool.
    public var executionStatus: ExecutionStatus = .success
    
    public required convenience init(args: [String]) throws {
        try self.init(usage: "", overview: "", args: args)
    }
    
    public class var toolName: String {
        fatalError("Must be implemented by subclasses")
    }
    
    init(usage: String, overview: String, args: [String]) throws {
        var options = Options()
        
        let parser = ArgumentParser(commandName: type(of: self).toolName, usage: usage, overview: overview)
        
        let binder = ArgumentBinder<Options>()
        
        type(of: self).defineArguments(parser: parser, binder: binder)
        
        let result = try parser.parse(args)
        try binder.fill(parseResult: result, into: &options)
        
        self.options = options
    }
    
    class func defineArguments(parser: ArgumentParser, binder: ArgumentBinder<Options>) {
        fatalError("Must be implemented by subclasses")
    }
    
    open func runImpl() throws {
        fatalError("Must be implemented by subclasses")
    }
    
    public func run() {
        do {
            try runImpl()
            
            if diagnostics.hasErrors {
                throw Diagnostics.fatalError
            }
        } catch {
            handle(error: error)
            executionStatus = .failure
        }
        
        
    }
    
    /// Exit the tool with the given execution status.
    static func exit(status: ExecutionStatus) -> Never {
        switch status {
        case .success: TSCLibc.exit(0)
        case .failure: TSCLibc.exit(1)
        }
    }
}

public extension Sequence where Element == Runnable.Type {
    func run(args: [String] = CommandLine.arguments) throws {
        var args = args
        args.removeFirst() // remove the binary name
        guard let toolName = args.first?.lowercased() else {
            throw StringError("Pass tool name to determine which command should run.")
        }
        for toolType in self {
            guard toolType.toolName.lowercased() == toolName else { continue }
            let tool = try toolType.init(args: Array(args.dropFirst()))
            return tool.run()
        }
        throw ArgumentParserError.unknownOption(toolName, suggestion: map { $0.toolName }.joined(separator: ", "))
    }
}

public func handle(error: Any) {
    switch error {

    // If we got instance of any error, handle the underlying error.
    case let anyError as AnyError:
        handle(error: anyError.underlyingError)

    default:
        _handle(error)
    }
}

// The name has underscore because of SR-4015.
private func _handle(_ error: Any) {

    switch error {
    case Diagnostics.fatalError:
        break

    case ArgumentParserError.expectedArguments(let parser, _):
        print(error: error)
        parser.printUsage(on: stderrStream)

    default:
        print(error: error)
    }
}

func print(error: Any) {
    let writer = InteractiveWriter.stderr
    writer.write("error: ", inColor: .red, bold: true)
    writer.write("\(error)")
    writer.write("\n")
}
