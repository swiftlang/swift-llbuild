// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import XCTest
// The Swift package has llbuildSwift as module
#if SWIFT_PACKAGE
import llbuild
import llbuildSwift
#else
import llbuild
#endif
import llbuildAnalysis

class CriticalPathSolverTests: XCTestCase {
    struct Input {
        let keys: [BuildKey]
        let lookup: [BuildKey: RuleResult]
        let keyByName: [String: BuildKey]
        
        func path(with names: [String]) throws -> CriticalPath? {
            let expectedKeys = names.compactMap { keyByName[$0] }
            let rules = expectedKeys.compactMap { lookup[$0] }
            guard expectedKeys.count == names.count, rules.count == names.count else { return nil }
            return try CriticalPath.construct(Array(zip(expectedKeys, rules)))
        }
    }
    
    typealias InputData = [(key: String, deps: [String], start: Double, end: Double)]
    
    func createInput(data: InputData) -> Input {
        let keys = data.map { BuildKey.Command(name: $0.key) }
        let keyByName: [String: BuildKey] = Dictionary(uniqueKeysWithValues: keys.map { ($0.name, $0) })
        let ruleResults: [RuleResult] = data.map {
            let deps = $0.deps.map { BuildKey.Command(name: $0) }
            let result = RuleResult(value: BuildValue.DirectoryTreeSignature(signature: 0x1234), signature: 0x1234, computedAt: 1, builtAt: 2, start: $0.start, end: $0.end, dependencies: deps)
            return result
        }
        return Input(keys: keys, lookup: [BuildKey: RuleResult](zip(keys, ruleResults), uniquingKeysWith: {a, b in a}), keyByName: keyByName)
    }
    
    func createSolver(input: Input) -> CriticalPathSolver {
        return CriticalPathSolver(keys: input.keys, ruleResultLookup: { input.lookup[$0] })
    }
    
    func testEmptyKeys() throws {
        let solver = CriticalPathSolver(keys: [], ruleResultLookup: { _ in nil})
        XCTAssertNil(try solver.calculateCriticalPath())
    }
    
    func testNilLookup() throws {
        let key = BuildKey.Command(name: "A")
        let solver = CriticalPathSolver(keys: [key], ruleResultLookup: { _ in nil })
        XCTAssertThrowsError(try solver.calculateCriticalPath(), CriticalPathSolver.Error.noResultGiven(key: key), "A solver without lookup for every built key should throw an error.")
    }
    
    func testCalculation() throws {
        let input = createInput(data: [
            ("A", [], 0, 1),
            ("B", ["A"], 1, 2),
            ("C", [], 1, 2),
            ("D", ["B"], 2, 3),
            ("E", [], 2.1, 3.1),
            ("F", ["D", "E"], 3.5, 4.5),
        ])
        let gotPath = try createSolver(input: input).calculateCriticalPath()
        let expectedPath = try input.path(with: ["A", "B", "D", "F"])
        XCTAssertEqual(gotPath, expectedPath)
        XCTAssertEqual(gotPath?.cost, 4)
        XCTAssertEqual(gotPath?.realisticCost, 4.5)
    }
    
    func testAllPaths() throws {
        let data: InputData = [
            ("A", [], 0, 1),
            ("B", [], 0, 0.9),
            ("C", ["A", "B"], 1, 2),
        ]
        
        let input = createInput(data: data)
        let solver = createSolver(input: input)
        let paths = try solver.calculateAllCriticalPaths()
        XCTAssertEqual(paths.count, 3)
        XCTAssertEqual(paths[input.keyByName["A"]!], try input.path(with: ["A"]))
        XCTAssertEqual(paths[input.keyByName["B"]!], try input.path(with: ["B"]))
        XCTAssertEqual(paths[input.keyByName["C"]!], try input.path(with: ["A", "C"]))
        XCTAssertEqual(try solver.calculateCriticalPath(), try input.path(with: ["A", "C"]))
    }
    
    func testAmbiguousPath() throws {
        // Both, A -> C and B -> C are critical paths for C as they have the same cost (2 seconds)
        let data: InputData = [
            ("A", [], 0, 1),
            ("B", [], 0, 1),
            ("C", ["A", "B"], 1, 2),
        ]
        
        let input = createInput(data: data)
        let solver = createSolver(input: input)
        guard let path = try solver.calculateCriticalPath() else {
            return XCTFail("Expected a path to be calculated.")
        }
        let possibleSolutions = try [["A", "C"], ["B", "C"]].compactMap(input.path(with:))
        XCTAssertTrue(possibleSolutions.contains(path))
    }
    
    func testMissingDependencyError() throws {
        let key1 = BuildKey.Command(name: "A")
        let key2 = BuildKey.Command(name: "B")
        
        let result1 = RuleResult(value: .SkippedCommand(), signature: 0, computedAt: 0, builtAt: 0, start: 0, end: 0, dependencies: [])
        let result2 = RuleResult(value: .SkippedCommand(), signature: 0, computedAt: 0, builtAt: 0, start: 0, end: 0, dependencies: [])
        
        let path = CriticalPath(key: key1, result: result1, next: nil)
        let end = CriticalPath(key: key2, result: result2, next: nil)
        XCTAssertThrowsError(try path.appending(end: end)) { err in
            XCTAssertEqual(err as? CriticalPath.Error, .missingDependency(self: path, end: end))
            XCTAssertEqual("\(err)", "Can't append \(end) to \(path) since \(key1) is not a dependency of \(key2). Found dependencies: [].")
        }
    }
    
    func testConstruct() throws {
        let inputData: InputData = [
            ("A", [], 0, 1),
            ("B", ["A"], 1, 2),
        ]
        
        let input = createInput(data: inputData)
        
        XCTAssertEqual(try CriticalPath.construct(Array(zip(input.keys, input.keys.map({ input.lookup[$0]! })))), try input.path(with: ["A", "B"]))
        XCTAssertNil(try CriticalPath.construct([]))
    }
}

func XCTAssertThrowsError<E, T>(_ expression: @autoclosure () throws -> T, _ error: E, _ message: String = "", file: StaticString = #file, line: UInt = #line) where E: Error & Equatable {
    XCTAssertThrowsError(try expression(), message) { err in
        XCTAssertNotNil(err as? E, "\(err) is not of type \(E.self).")
        XCTAssertEqual(err as? E, error)
    }
}
