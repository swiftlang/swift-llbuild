// This source file is part of the Swift.org open source project
//
// Copyright (c) 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors

import ArgumentParser
import NIO

import CevoCore
import NinjaBuild

struct NinjaBuildTool: ParsableCommand {
    static var configuration = CommandConfiguration(
        abstract: "NinjaBuild tool")

    @Flag(help: "Print verbose output")
    var verbose: Bool

    @Option(help: "Path to the Ninja manifest file")
    var manifest: String

    @Option(help: "The name of the target to build")
    var target: String

    func run() throws {
        let dryRunDelegate = NinjaDryRunDelegate()
        let nb = try NinjaBuild(manifest: manifest, delegate: dryRunDelegate)
        _ = try nb.build(target: target, as: Int.self)
    }
}

extension Int: NinjaValue {}

private class NinjaDryRunDelegate: NinjaBuildDelegate {
    func build(group: EventLoopGroup, path: String) -> EventLoopFuture<NinjaValue> {
        print("build input: \(path)")
        return group.next().makeSucceededFuture(0)
    }
    
    func build(group: EventLoopGroup, command: Command, inputs: [NinjaValue]) -> EventLoopFuture<NinjaValue> {
        print("build command: \(command.command)")
        return group.next().makeSucceededFuture(0)
    }
}
