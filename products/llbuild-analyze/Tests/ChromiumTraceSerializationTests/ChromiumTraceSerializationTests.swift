// This source file is part of the Swift.org open source project
//
// Copyright 2026 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import XCTest
import class Foundation.JSONEncoder
import class Foundation.JSONSerialization

@testable import llbuildAnalyzeSupport

final class ChromiumTraceSerializationTests: XCTestCase {
    func testChromiumTraceFileWrapsEvents() throws {
        let event = chromiumTraceEvent(
            name: "<BuildKey.Command name=compile-main>",
            buildKeyKind: "command",
            buildKey: "compile-main",
            valueKind: "skipped-command",
            start: 1.25,
            duration: 1.5,
            isOnCriticalPath: true,
            dependencies: ["<BuildKey.Node name=main.swift>"]
        )
        let data = try JSONEncoder().encode(ChromiumTraceFile(traceEvents: [event]))
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])

        let events = try XCTUnwrap(json["traceEvents"] as? [[String: Any]])
        XCTAssertEqual(events.count, 1)
        XCTAssertEqual(events.first?["name"] as? String, "<BuildKey.Command name=compile-main>")
    }

    func testChromiumTraceEventUsesCompleteEventFormat() {
        let event = chromiumTraceEvent(
            name: "<BuildKey.Command name=compile-main>",
            buildKeyKind: "command",
            buildKey: "compile-main",
            valueKind: "skipped-command",
            start: 1.25,
            duration: 1.5,
            isOnCriticalPath: true,
            dependencies: ["<BuildKey.Node name=main.swift>"]
        )

        XCTAssertEqual(event.name, "<BuildKey.Command name=compile-main>")
        XCTAssertEqual(event.cat, "critical-path")
        XCTAssertEqual(event.ph, "X")
        XCTAssertEqual(event.ts, 1_250_000)
        XCTAssertEqual(event.dur, 1_500_000)
        XCTAssertEqual(event.pid, 0)
        XCTAssertEqual(event.tid, 0)
        XCTAssertEqual(event.args.buildKeyKind, "command")
        XCTAssertEqual(event.args.buildKey, "compile-main")
        XCTAssertEqual(event.args.valueKind, "skipped-command")
        XCTAssertEqual(event.args.durationSeconds, 1.5)
        XCTAssertEqual(event.args.onCriticalPath, true)
        XCTAssertEqual(event.args.dependencies, ["<BuildKey.Node name=main.swift>"])
    }
}
