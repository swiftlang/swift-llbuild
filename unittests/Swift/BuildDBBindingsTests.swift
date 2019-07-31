//
//  BuildDBBindingsTests.swift
//  llbuildSwiftTests
//
//  Copyright © 2019 Apple Inc. All rights reserved.
//

import XCTest

// The Swift package has llbuildSwift as module
#if SWIFT_PACKAGE
import llbuild
import llbuildSwift
#else
import llbuild
#endif

private func createExampleBuildDB(at path: String, file: StaticString = #file, line: UInt = #line) throws {
  typealias Compute = ([BuildValue]) -> BuildValue
  
  enum Keys {
    static let A = Key(BuildKey.Command(name: "A"))
    static let B = Key(BuildKey.Target(name: "B"))
    static let C = Key(BuildKey.Node(path: "C"))
  }
  
  enum FileInfo {
    static let A = BuildValue.FileInfo()
    static let B = BuildValue.FileInfo()
    static let C = BuildValue.FileInfo()
  }
  
  class ExampleTask: Task {
    let inputs: [Key]
    private var values: [Int: BuildValue] = [:]
    let compute: Compute
    
    init(inputs: [Key], compute: @escaping Compute) {
      self.inputs = inputs
      self.compute = compute
    }
    
    func start(_ engine: TaskBuildEngine) {
      for (index, input) in inputs.enumerated() {
        engine.taskNeedsInput(input, inputID: index)
      }
    }
    func provideValue(_ engine: TaskBuildEngine, inputID: Int, value: Value) {
      values[inputID] = BuildValue.construct(from: value)
    }
    func inputsAvailable(_ engine: TaskBuildEngine) {
      let inputValues = inputs.indices.map { self.values[$0]! }
      engine.taskIsComplete(Value(self.compute(inputValues).valueData))
    }
  }
  
  class ExampleRule: Rule {
    let inputs: [Key]
    let compute: Compute
    
    init(inputs: [Key] = [], compute: @escaping Compute) {
      self.inputs = inputs
      self.compute = compute
    }
    
    func createTask() -> Task {
      return ExampleTask(inputs: inputs, compute: compute)
    }
  }
  
  class ExampleDelegate: BuildEngineDelegate {
    func lookupRule(_ key: Key) -> Rule {
      switch key {
      case Keys.A: return ExampleRule { _ in BuildValue.SuccessfulCommand(outputInfos: [FileInfo.A]) }
      case Keys.B: return ExampleRule { _ in BuildValue.SuccessfulCommand(outputInfos: [FileInfo.B]) }
      case Keys.C: return ExampleRule(inputs: [Keys.A, Keys.B]) { values in
        BuildValue.SuccessfulCommand(outputInfos: values.flatMap { ($0 as? BuildValue.SuccessfulCommand)?.outputInfos ?? [FileInfo.C] })
        }
      default: fatalError("Unexpected key: \(key.toString())")
      }
    }
  }
  
  let delegate = ExampleDelegate()
  let engine = BuildEngine(delegate: delegate)
  try engine.attachDB(path: path, schemaVersion: 9)
  
  let result = engine.build(key: Keys.C)
  XCTAssertNotNil(BuildValue.construct(from: result) as? BuildValue.SuccessfulCommand, file: file, line: line)
}

class BuildDBBindingsTests: XCTestCase {
  
  let exampleBuildDBClientSchemaVersion: UInt32 = 9
  
  var exampleBuildDBPath: String {
    return "\(self.tmpDirectory!)/build.db"
  }
  
  func exampleDB(path: String, file: StaticString = #file, line: UInt = #line) throws -> BuildDB {
    try createExampleBuildDB(at: path, file: file, line: line)
    return try BuildDB(path: path, clientSchemaVersion: exampleBuildDBClientSchemaVersion)
  }
  
  var tmpDirectory: String!
  
  override func setUp() {
    super.setUp()
    let tmpDir = "/tmp/llbuild-test/\(UUID())"
    
    do {
      try FileManager.default.createDirectory(atPath: tmpDir, withIntermediateDirectories: true)
    } catch {
      fatalError("Could not create temporary directory for test case BuildDBBindingsTests at path: \(tmpDir)")
    }
    self.tmpDirectory = tmpDir
  }
  
  override func tearDown() {
    super.tearDown()
    do {
      try FileManager.default.removeItem(atPath: self.tmpDirectory)
    } catch {
      // intentionally left empty as we don't care about already removed directories
    }
  }
  
  func testCouldNotOpenDatabaseErrors() {
    func expectCouldNotOpenError(path: String, clientSchemaVersion: UInt32 = 0, expectedError: String, file: StaticString = #file, line: UInt = #line) {
      do {
        _ = try BuildDB(path: path, clientSchemaVersion: clientSchemaVersion)
        XCTFail("Expected to throw error with not existing database path.", file: file, line: line)
      } catch BuildDB.Error.couldNotOpenDB(error: let got) {
        XCTAssertEqual(got, expectedError, file: file, line: line)
      } catch {
        XCTFail("Unexpected error while opening non existing database: \(error)", file: file, line: line)
      }
    }
    
    expectCouldNotOpenError(path: "/tmp/invalid/path",
                            expectedError: "Database at path '/tmp/invalid/path' does not exist.")
    expectCouldNotOpenError(path: "/tmp",
                            expectedError: "Path '/tmp' exists, but is a directory.")
    
    // Create the example database for the following tests
    XCTAssertNoThrow(try createExampleBuildDB(at: exampleBuildDBPath))
    
    expectCouldNotOpenError(path: exampleBuildDBPath,
                            clientSchemaVersion: 8,
                            expectedError: "Version mismatch. (database-schema: 11 requested schema: 11. database-client: \(exampleBuildDBClientSchemaVersion) requested client: 8)")
    XCTAssertNoThrow(try BuildDB(path: exampleBuildDBPath, clientSchemaVersion: exampleBuildDBClientSchemaVersion))
  }
  
  func testGetKeys() throws {
    let db = try exampleDB(path: exampleBuildDBPath)
    let keys = try db.getKeys()
    
    XCTAssertEqual(keys.count, 3)
    XCTAssertEqual(keys[0], BuildKey.Target(name: "B"))
    XCTAssertEqual(keys[1], BuildKey.Command(name: "A"))
    XCTAssertEqual(keys[2], BuildKey.Node(path: "C"))
  }
  
  func testGetResults() throws {
    let db = try exampleDB(path: exampleBuildDBPath)
    
    guard let result1 = try db.lookupRuleResult(buildKey: BuildKey.Node(path: "C")) else {
      return XCTFail("Expected to get result for build key C.")
    }
    
    XCTAssertGreaterThan(result1.start, 0)
    XCTAssertGreaterThanOrEqual(result1.end, result1.start)
    XCTAssertEqual(result1.dependencies.count, 2)
    XCTAssertEqual(result1.dependencies[0], BuildKey.Command(name: "A"))
    XCTAssertEqual(result1.dependencies[1], BuildKey.Target(name: "B"))
    XCTAssertEqual(result1.value, BuildValue.SuccessfulCommand(outputInfos: [BuildValue.SuccessfulCommand.FileInfo(), BuildValue.SuccessfulCommand.FileInfo()]))
    
    guard let result2 = try db.lookupRuleResult(buildKey: BuildKey.Target(name: "B")) else {
      return XCTFail("Expected to get result for build key B.")
    }
    
    XCTAssertGreaterThan(result2.start, 0)
    XCTAssertGreaterThanOrEqual(result2.end, result2.start)
    XCTAssertTrue(result2.dependencies.isEmpty)
    XCTAssertEqual(result2.value, BuildValue.SuccessfulCommand(outputInfos: [BuildValue.SuccessfulCommand.FileInfo()]))
    
    guard let result3 = try db.lookupRuleResult(buildKey: BuildKey.Command(name: "A")) else {
      return XCTFail("Expected to get result for build key A.")
    }
    
    XCTAssertGreaterThan(result3.start, 0)
    XCTAssertGreaterThanOrEqual(result3.end, result3.start)
    XCTAssertTrue(result3.dependencies.isEmpty)
    XCTAssertEqual(result3.value, BuildValue.SuccessfulCommand(outputInfos: [BuildValue.SuccessfulCommand.FileInfo()]))
  }
  
}
