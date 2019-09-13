// This source file is part of the Swift.org open source project
//
// Copyright 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for Swift project authors

import XCTest
import llbuildAnalysis

class CriticalPathTests: XCTestCase {
  struct Data: Hashable {
    let name: String
    let start: Double
    let end: Double
    let deps: [String]
    
    init(_ name: String, _ start: Double, _ end: Double, _ deps: [String]) {
      self.name = name
      self.start = start
      self.end = end
      self.deps = deps
    }
  }
  typealias InputData = [Data]
  struct Input {
    private let data: [String: InputData.Element]
    private let lookup: IdentifierFactory<InputData.Element>
    let elements: [CriticalPath.Element]
    
    init(data: InputData) {
      let map = Dictionary(uniqueKeysWithValues: zip(data.map({ $0.name }), data))
      self.data = map
      let lookup = IdentifierFactory(data)
      self.lookup = lookup
      self.elements = data.map { d in
        return CriticalPath.Element(identifier: lookup.identifier(element: d), weight: d.end - d.start, dependencies: d.deps.map({
          return lookup.identifier(element: map[$0]!)
        }))
      }
    }
    
    func path(with keys: [String]) throws -> CriticalPath {
      let elements = keys.map { self.lookup.identifier(element: self.data[$0]!) }
      return try CriticalPath(elements.map({ self.elements[$0] }))
    }
  }
  
  func testEmptyKeys() throws {
    XCTAssertEqual(calculateCriticalPath([]), .empty())
  }
  
  func testMissingDependency() throws {
    let input = Input(data: [
      Data("A", 0, 1, []),
      Data("B", 1, 2, []),
    ])
    
    let expectedError = CriticalPath.Error.missingDependency(previous: CriticalPath.Element(identifier: 0, weight: 1, dependencies: []), following: CriticalPath.Element(identifier: 1, weight: 1, dependencies: []))
    XCTAssertThrowsError(try input.path(with: ["A", "B"]), expectedError)
    XCTAssertEqual(expectedError.description, #"Can't create a connection in the critical build path between element "0" and "1" because "1" doesn't have a dependency on "0". Found dependencies are []."#)
  }
  
  func testCalculation() throws {
    do {
      
      let input = Input(data: [
        Data("A", 0, 1, []),
        Data("B", 1, 2, ["A"]),
        Data("C", 1, 2, []),
        Data("D", 2, 3, ["B", "C"]),
        Data("E", 2.1, 3.1, []),
        Data("F", 3.5, 4.5, ["D", "E"]),
      ])
      let gotPath = calculateCriticalPath(input.elements)
      let expectedPath = try input.path(with: ["A", "B", "D", "F"])
      XCTAssertEqual(gotPath, expectedPath)
      XCTAssertEqual(gotPath.weight, 4)
    } catch {
      XCTFail("\(error)")
    }
  }
  
  func testAmbiguousPath() throws {
    // Both, A -> C and B -> C are critical paths for C as they have the same cost (2 seconds)
    let input = Input(data: [
      Data("A", 0, 1, []),
      Data("B", 0, 1, []),
      Data("C", 1, 2, ["A", "B"]),
    ])
    let path = calculateCriticalPath(input.elements)
    let possibleSolutions = try [["A", "C"], ["B", "C"]].compactMap(input.path(with:))
    XCTAssertTrue(possibleSolutions.contains(path))
  }
}

func XCTAssertThrowsError<E, T>(_ expression: @autoclosure () throws -> T, _ error: E, _ message: String = "", file: StaticString = #file, line: UInt = #line) where E: Error & Equatable {
  XCTAssertThrowsError(try expression(), message) { err in
    XCTAssertNotNil(err as? E, "\(err) is not of type \(E.self).")
    XCTAssertEqual(err as? E, error)
  }
}
