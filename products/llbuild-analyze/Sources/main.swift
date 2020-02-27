// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import ArgumentParser

private func binaryName() -> String {
    CommandLine.arguments.first?.components(separatedBy: "/").last ?? ""
}

struct Analyze: ParsableCommand {
    static var configuration = CommandConfiguration(commandName: binaryName(),
                                                    shouldDisplay: false,
                                                    subcommands: [
                                                        CriticalPathTool.self
    ])
}

Analyze.main()
