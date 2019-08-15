// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import Commands
import func Foundation.exit

enum ExitCode: Int32 {
    case success = 0, noCommand, unknownCommand, wrongArguments, commandFailed
}

func exit(_ exitCode: ExitCode) -> Never {
    exit(exitCode.rawValue)
}

func run(using commands: [Runnable.Type]) {
    func printUsage(command: Runnable.Type? = nil) {
        if let command = command {
            print(command.usage)
            return
        }
        print("""
            \(CommandLine.arguments[0]) [command] options
            
            where command can be one of [\(commands.map({ $0.toolName }).joined(separator: ", "))]
            """)
    }
    
    var args = Array(CommandLine.arguments.dropFirst())
    guard let toolName = args.first else {
        print("No command found.")
        printUsage()
        exit(.noCommand)
    }
    args.removeFirst()
    
    guard let command = commands.first(where: { $0.toolName == toolName }) else {
        print("Unknown command: \(toolName)")
        printUsage()
        exit(.unknownCommand)
    }
    
    let tool: Runnable
    do {
        tool = try command.init(args: args)
    } catch {
        printUsage(command: command)
        exit(.wrongArguments)
    }
    
    do {
        try tool.run()
    } catch {
        print("error: ", error)
        exit(.commandFailed)
    }
    
    exit(.success)
}
