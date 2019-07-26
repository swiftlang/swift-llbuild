//
//  BuildSystemBindingsTests.swift
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

extension Command {
  func with(name: String? = nil, shouldShowStatus: Bool? = nil, description: String? = nil, verboseDescription: String? = nil) -> Command {
    return Command(name: name ?? self.name, shouldShowStatus: shouldShowStatus ?? self.shouldShowStatus, description: description ?? self.description, verboseDescription: verboseDescription ?? self.verboseDescription)
  }
}

class BuildSystemBindingsTests: XCTestCase {
  
  func testCommand() {
    let command = Command(name: "aName", shouldShowStatus: true, description: "a description", verboseDescription: "a verbose description")
    XCTAssertEqual(command.name, "aName")
    XCTAssertTrue(command.shouldShowStatus)
    XCTAssertEqual(command.description, "a description")
    XCTAssertEqual(command.verboseDescription, "a verbose description")
    
    XCTAssertEqual(command, command.with())
    XCTAssertNotEqual(command, command.with(name: "foobar"))
    XCTAssertNotEqual(command, command.with(shouldShowStatus: false))
    XCTAssertNotEqual(command, command.with(description: "another description"))
    XCTAssertNotEqual(command, command.with(verboseDescription: "another verbose description"))
    
    XCTAssertEqual(command.hashValue, command.with().hashValue)
    XCTAssertNotEqual(command.hashValue, command.with(name: "foobar").hashValue)
    XCTAssertNotEqual(command.hashValue, command.with(shouldShowStatus: false).hashValue)
    XCTAssertNotEqual(command.hashValue, command.with(description: "another description").hashValue)
    XCTAssertNotEqual(command.hashValue, command.with(verboseDescription: "another verbose description").hashValue)
  }
  
  func testCommandMetrics() {
    let metrics = CommandMetrics(utime: 1, stime: 2, maxRSS: 3)
    XCTAssertEqual(metrics.utime, 1)
    XCTAssertEqual(metrics.stime, 2)
    XCTAssertEqual(metrics.maxRSS, 3)
  }
  
  func testCommandExtendedResult() {
    let metrics = CommandMetrics(utime: 1, stime: 2, maxRSS: 3)
    
    let result = CommandExtendedResult(result: .succeeded, exitStatus: 0, pid: 123, metrics: metrics)
    XCTAssertEqual(result.result, .succeeded)
    XCTAssertEqual(result.exitStatus, 0)
    XCTAssertEqual(result.pid, 123)
    XCTAssertEqual(result.metrics?.utime, 1)
    XCTAssertEqual(result.metrics?.stime, 2)
    XCTAssertEqual(result.metrics?.maxRSS, 3)
  }
  
  func testKey() {
    let key1 = Key("foobar")
    XCTAssertEqual(key1.toString(), "foobar")
    XCTAssertEqual(key1.data, Array("foobar".utf8))
    XCTAssertEqual(key1.description, "<Key: 'foobar'>")
    XCTAssertEqual(key1.hashValue, Key("foobar").hashValue)
    
    let key2 = Key([102, 111, 111, 98, 97, 114])
    XCTAssertEqual(key1, key2)
  }
  
  func testValue() {
    let value1 = Value("foobar")
    XCTAssertEqual(value1.toString(), "foobar")
    XCTAssertEqual(value1.data, Array("foobar".utf8))
    XCTAssertEqual(value1.description, "<Value: 'foobar'>")
    XCTAssertEqual(value1.hashValue, Key("foobar").hashValue)
    
    let value2 = Value([102, 111, 111, 98, 97, 114])
    XCTAssertEqual(value1, value2)
  }
  
  func testBuildEngine() {
    class Delegate: BuildEngineDelegate {
      struct DummyTask: Task {
        func start(_ engine: TaskBuildEngine) {
          
        }
        func provideValue(_ engine: TaskBuildEngine, inputID: Int, value: Value) {
          
        }
        func inputsAvailable(_ engine: TaskBuildEngine) {
          engine.taskIsComplete(Value("bar"))
        }
      }
      
      struct DummyRule: Rule, Equatable {
        let key: Key
        func createTask() -> Task {
          return DummyTask()
        }
      }
      
      private(set) var rules: [Key: DummyRule] = [:]
      private(set) var errors: [String] = []
      
      func lookupRule(_ key: Key) -> Rule {
        if let rule = rules[key] {
          return rule
        }
        let rule = DummyRule(key: key)
        rules[key] = rule
        return rule
      }
      
      func error(_ message: String) {
        self.errors.append(message)
      }
    }
    
    let delegate = Delegate()
    let engine = BuildEngine(delegate: delegate)
    
    let value = engine.build(key: Key("foo"))
    
    XCTAssertEqual(value, Value("bar"))
    XCTAssertEqual(delegate.rules, [Key("foo"): Delegate.DummyRule(key: Key("foo"))])
    XCTAssertTrue(delegate.errors.isEmpty)
  }
}
