//// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import func Foundation.exit

enum ExitCode: Int32 {
  case noCommand = 1, unknownCommand, wrongArguments, commandFailed
}

func exit(_ exitCode: ExitCode) -> Never {
  exit(exitCode.rawValue)
}

private let toolName = "llbuild-analysis"

/// Tries to find the specified command and runs it with the given options.
/// - Parameter commands: Commands that can be specified with the toolName in the invocation
/// - Parameter arguments: Command line arguments, default are the environmental command line arguments
public func run(using commands: [Runnable.Type], arguments: [String] = CommandLine.arguments) {
  func printUsage(error: Error? = nil, command: Runnable.Type? = nil) {
    if let error = error {
      print("error: ", error)
    }
    if let command = command {
      print(command.usage)
      return
    }
    print("""
      \(arguments.first ?? toolName) [command] options
      
      where command can be one of [\(commands.map({ $0.toolName }).joined(separator: ", "))]
      """)
  }
  
  // Remove the binary path if first argument
  var args = arguments.first?.contains(toolName) ?? false ? Array(arguments.dropFirst()) : arguments
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
  
  do {
    try command.run(args: args)
  } catch {
    printUsage(error: error, command: command)
    exit(.commandFailed)
  }
}
