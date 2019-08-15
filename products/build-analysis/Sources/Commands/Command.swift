// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import Core

/// Defines the interface for options
public protocol OptionsType {
    init(values: ArgumentsParser.Values)
    static var options: [ArgumentsParser.Option] { get }
}

/// If a command has no options, EmptyOptions can be used
public struct EmptyOptions: OptionsType {
    public init(values: ArgumentsParser.Values) {}
    public static let options: [ArgumentsParser.Option] = []
}

/// Runnable type erases a Tool
public protocol Runnable: class {
    static var toolName: String { get }
    static var usage: String { get }
    init(args: [String]) throws
    func run() throws
}

/// Command is the abstract base class for all commands
/// Subclasses need to provide their Options type and override run() and toolName
open class Command<Options : OptionsType>: Runnable {
    public enum Error: Swift.Error {
        case usage(description: String)
    }
    
    public let options: Options
    open class var toolName: String { preconditionFailure("`toolName` needs to be overriden by \(type(of: self)).") }
    public static var usage: String {
        let options = Options.options
        let optionsUsage = options.map { option -> String in
            let kindUsage: String
            switch option.kind {
            case let .short(short): kindUsage = "-\(short)"
            case let .long(long): kindUsage = "--\(long)"
            case let .both(short, long): kindUsage = "[-\(short)|--\(long)]"
            }
            
            return "\t\(kindUsage) \(option.name) (\(option.type)) \(option.required ? " [required]" : "")"
        }.joined(separator: "\n")
        
        return "\(CommandLine.arguments[0]) \(toolName) [options]\n\nwhere options are:\n\(optionsUsage)"
    }
    
    required public init(args: [String]) throws {
        do {
            let parser = ArgumentsParser(options: Options.options)
            let values = try parser.parse(args: args)
            self.options = Options.init(values: values)
        } catch {
            throw Error.usage(description: "Argument parsing failed: \(error)")
        }
    }
    
    open func run() throws {}
}
