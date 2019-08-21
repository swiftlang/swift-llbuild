// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

/// Defines the interface for options
public protocol OptionsType {
    init()
    func bind(to binder: inout Binder<Self>)
}

/// If a command has no options, EmptyOptions can be used
public struct EmptyOptions: OptionsType {
    public init() {}
    public func bind(to binder: inout Binder<EmptyOptions>) {}
}

/// Runnable type erases a Command
public protocol Runnable {
    static var toolName: String { get }
    static var usage: String { get }
    static func run(args: [String]) throws
}

/// A Command gets default implementation for it's Runnable requirements and needs
/// to implement its execution in `run(options:)`
public protocol Command: Runnable {
    associatedtype Options : OptionsType
    static func run(options: Options) throws
}

extension Command {
    public static var toolName: String {
        let mirror = Mirror(reflecting: self)
        guard let name = "\(mirror.subjectType)".components(separatedBy: ".").first else {
            preconditionFailure("toolName needs to be overriden by \(type(of: self))")
        }
        
        return name.lowercased()
    }
    
    public static var usage: String {
        return "\(CommandLine.arguments[0]) \(toolName) [options] where options are:\n\(Binder<Options>.usage)"
    }
    
    public static func run(args: [String]) throws {
        try run(options: try parseCommandLine(args: args))
    }
}
