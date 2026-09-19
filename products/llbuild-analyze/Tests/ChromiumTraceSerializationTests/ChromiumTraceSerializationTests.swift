// This source file is part of the Swift.org open source project
//
// Copyright 2026 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import XCTest
import llbuildAnalysis
import llbuildSwift
import class Foundation.JSONEncoder
import class Foundation.JSONSerialization
import class Foundation.FileManager
import struct Foundation.UUID

@testable import llbuildAnalyzeSupport

private final class TraceRule: Rule {
    let inputs: [Key]

    init(inputs: [Key]) {
        self.inputs = inputs
    }

    func createTask() -> Task {
        return TraceTask(inputs: inputs)
    }
}

private final class TraceTask: Task {
    let inputs: [Key]

    init(inputs: [Key]) {
        self.inputs = inputs
    }

    func start(_ engine: TaskBuildEngine) {
        for (inputID, input) in inputs.enumerated() {
            engine.taskNeedsInput(input, inputID: inputID)
        }
    }

    func provideValue(_ engine: TaskBuildEngine, inputID: Int, value: Value) {}

    func inputsAvailable(_ engine: TaskBuildEngine) {
        let value = BuildValue.SuccessfulCommand(outputInfos: [BuildValue.FileInfo()])
        engine.taskIsComplete(Value(value.valueData))
    }
}

private final class TraceBuildEngineDelegate: BuildEngineDelegate {
    private let rules: [Key: TraceRule]

    init(rules: [Key: TraceRule]) {
        self.rules = rules
    }

    func lookupRule(_ key: Key) -> Rule {
        guard let rule = rules[key] else {
            fatalError("Unexpected key requested by test build: \(key)")
        }
        return rule
    }
}

final class ChromiumTraceSerializationTests: XCTestCase {
    func testChromiumTraceSerializesBuildDatabaseResults() throws {
        let dependencyABuildKey = BuildKey.Command(name: "dependency-a")
        let dependencyBBuildKey = BuildKey.Command(name: "dependency-b")
        let rootBuildKey = BuildKey.Command(name: "root")
        let dependencyA = Key(dependencyABuildKey)
        let dependencyB = Key(dependencyBBuildKey)
        let root = Key(rootBuildKey)
        let rules = [
            dependencyA: TraceRule(inputs: []),
            dependencyB: TraceRule(inputs: []),
            root: TraceRule(inputs: [dependencyA, dependencyB]),
        ]
        let databasePath = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString)
            .path
        defer {
            try? FileManager.default.removeItem(atPath: databasePath)
        }

        do {
            let engine = BuildEngine(delegate: TraceBuildEngineDelegate(rules: rules))
            try engine.attachDB(path: databasePath, schemaVersion: 9)
            _ = engine.build(key: root)
            engine.close()
        }

        let database = try BuildDB(path: databasePath, clientSchemaVersion: 9)
        let allKeyResults = try database.getKeysWithResult()
        XCTAssertEqual(allKeyResults.count, 3)

        let solver = CriticalBuildPath.Solver(keys: allKeyResults)
        let path = solver.run()
        let data = try chromiumTrace(path, allKeyResults: allKeyResults)
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        let events = try XCTUnwrap(json["traceEvents"] as? [[String: Any]])

        XCTAssertEqual(events.count, 3)
        XCTAssertEqual(events.filter { $0["cat"] as? String == "critical-path" }.count, 2)
        XCTAssertEqual(events.filter { $0["cat"] as? String == "build" }.count, 1)

        let rootEvent = try XCTUnwrap(events.first { $0["name"] as? String == rootBuildKey.description })
        XCTAssertEqual(rootEvent["cat"] as? String, "critical-path")
        let rootArgs = try XCTUnwrap(rootEvent["args"] as? [String: Any])
        XCTAssertEqual(rootArgs["buildKeyKind"] as? String, "command")
        XCTAssertEqual(rootArgs["buildKey"] as? String, "root")
        XCTAssertEqual(rootArgs["onCriticalPath"] as? Bool, true)
        XCTAssertEqual(
            Set(rootArgs["dependencies"] as? [String] ?? []),
            Set([dependencyABuildKey.description, dependencyBBuildKey.description])
        )

        for event in events {
            XCTAssertEqual(event["ph"] as? String, "X")
            XCTAssertEqual(event["pid"] as? Int, 0)
            XCTAssertEqual(event["tid"] as? Int, 0)
            let args = try XCTUnwrap(event["args"] as? [String: Any])
            XCTAssertEqual(args["onCriticalPath"] as? Bool, event["cat"] as? String == "critical-path")
        }
    }

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
        let encodedEvent = try XCTUnwrap(events.first)
        XCTAssertEqual(
            Set(encodedEvent.keys),
            Set(["args", "cat", "dur", "name", "ph", "pid", "tid", "ts"])
        )
        XCTAssertEqual(encodedEvent["name"] as? String, "<BuildKey.Command name=compile-main>")
        XCTAssertEqual(encodedEvent["cat"] as? String, "critical-path")
        XCTAssertEqual(encodedEvent["ph"] as? String, "X")
        XCTAssertEqual(encodedEvent["ts"] as? Int, 1_250_000)
        XCTAssertEqual(encodedEvent["dur"] as? Int, 1_500_000)
        XCTAssertEqual(encodedEvent["pid"] as? Int, 0)
        XCTAssertEqual(encodedEvent["tid"] as? Int, 0)

        let args = try XCTUnwrap(encodedEvent["args"] as? [String: Any])
        XCTAssertEqual(
            Set(args.keys),
            Set([
                "buildKey", "buildKeyKind", "dependencies", "durationSeconds",
                "onCriticalPath", "valueKind",
            ])
        )
        XCTAssertEqual(args["buildKeyKind"] as? String, "command")
        XCTAssertEqual(args["buildKey"] as? String, "compile-main")
        XCTAssertEqual(args["valueKind"] as? String, "skipped-command")
        XCTAssertEqual(args["durationSeconds"] as? Double, 1.5)
        XCTAssertEqual(args["onCriticalPath"] as? Bool, true)
        XCTAssertEqual(
            args["dependencies"] as? [String],
            ["<BuildKey.Node name=main.swift>"]
        )
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

    func testChromiumTraceEventUsesBuildCategoryForNonCriticalEvents() {
        let event = chromiumTraceEvent(
            name: "<BuildKey.Command name=compile-main>",
            buildKeyKind: "command",
            buildKey: "compile-main",
            valueKind: "successful-command",
            start: 1.25,
            duration: 1.5,
            isOnCriticalPath: false,
            dependencies: []
        )

        XCTAssertEqual(event.cat, "build")
        XCTAssertEqual(event.args.onCriticalPath, false)
        XCTAssertEqual(event.args.dependencies, [])
    }

    func testChromiumTraceEventClampsNegativeTiming() {
        let event = chromiumTraceEvent(
            name: "<BuildKey.Command name=compile-main>",
            buildKeyKind: "command",
            buildKey: "compile-main",
            valueKind: "failed-command",
            start: -1,
            duration: -0.5,
            isOnCriticalPath: false,
            dependencies: []
        )

        XCTAssertEqual(event.ts, 0)
        XCTAssertEqual(event.dur, 0)
    }
}
