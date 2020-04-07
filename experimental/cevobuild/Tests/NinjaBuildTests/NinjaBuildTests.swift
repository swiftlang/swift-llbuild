// This source file is part of the Swift.org open source project
//
// Copyright (c) 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors

import XCTest

import NIO
import NIOConcurrencyHelpers
import TSCBasic

import CevoCore
import NinjaBuild

extension Int: NinjaValue {}

final class NinjaBuildTests: XCTestCase {
    func buildAndLog(manifest: String) throws -> [String] {
        return try withTemporaryFile { tmp in
            try localFileSystem.writeFileContents(tmp.path, bytes: ByteString((manifest + "\n").utf8))
            let logger = LoggingNinjaDelegate()
            let nb = try NinjaBuild(manifest: tmp.path.pathString, delegate: logger)
            _ = try nb.build(target: "all", as: Int.self)
            return logger.log
        }
    }
    
    func testBasics() throws {
        let log = try buildAndLog(manifest: """
            rule TOUCH
                command = touch $out
            build all: TOUCH
            """)
        XCTAssertEqual(log, [
                "build command 'touch all'"])
    }
    
    func testMultipleOutputCommands() throws {
        let log = try buildAndLog(manifest: """
            rule TOUCH
                command = touch $out
            rule CAT
                command = cat $in > $out
            build a b: TOUCH
            build all: CAT a b
            """)
        XCTAssertEqual(log, [
                "build command 'touch a b'",
                "build command 'cat a b > all'"])
    }
}

private class LoggingNinjaDelegate: NinjaBuildDelegate {
    var log: [String] = []
    let lock = NIOConcurrencyHelpers.Lock()
    
    func build(group: EventLoopGroup, path: String) -> EventLoopFuture<NinjaValue> {
        lock.withLockVoid { log.append("build input '\(path)'") }
        return group.next().makeSucceededFuture(0)
    }
    
    func build(group: EventLoopGroup, command: Command, inputs: [NinjaValue]) -> EventLoopFuture<NinjaValue> {
        lock.withLockVoid { log.append("build command '\(command.command)'") }
        return group.next().makeSucceededFuture(0)
    }
}
