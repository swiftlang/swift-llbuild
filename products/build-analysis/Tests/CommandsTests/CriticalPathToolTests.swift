//// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import XCTest
import llbuildAnalysis
import llbuildSwift
import Commands

private extension Data {
    var string: String {
        String(data: self, encoding: .utf8) ?? "nil"
    }
}

class CriticalPathToolTests: XCTestCase {
    
    func createSimplePath() throws -> CriticalPath? {
        let key = BuildKey.Command(name: "A")
        let result = RuleResult(value: BuildValue.SkippedCommand(), signature: 1234, computedAt: 0, builtAt: 0, start: 1, end: 2, dependencies: [])
        return try CriticalPath.construct([(key, result)])
    }
    
    func testJsonEncoding() throws {
        let tool = try CriticalPathTool(args: ["-d", ""])
        
        XCTAssertEqual(try tool.json(nil).string, #"{"error":"No path calculated."}"#)
        
        enum Error: Swift.Error {
            case someError
        }
        
        let data = try tool.json(createSimplePath())
        // since the keys can have any order we need to test differently
        let obj = try JSONSerialization.jsonObject(with: data, options: []) as? [String: [String: Any]]
        guard let path = obj?["path"] else { return XCTFail() }
        XCTAssertEqual(path["cost"] as? Double, 1)
        XCTAssertEqual(path["realisticCost"] as? Double, 1)
        guard let elements = path["elements"] as? [[String: Any]] else { return XCTFail() }
        XCTAssertEqual(elements.count, 1)
        let first = elements[0]
        guard let key = first["key"] as? [String: String] else { return XCTFail() }
        XCTAssertEqual(key["kind"], "command")
        XCTAssertEqual(key["key"], "A")
        guard let result = first["result"] as? [String: Any] else { return XCTFail() }
        XCTAssertEqual(result["signature"] as? UInt64, 1234)
        XCTAssertEqual(result["start"] as? Double, 1)
        XCTAssertEqual(result["end"] as? Double, 2)
        XCTAssertEqual(result["dependencies"] as? [[String: String]], [])
        XCTAssertEqual(result["value"] as? [String: String], ["kind": "skippedCommand"])
    }
    
}
