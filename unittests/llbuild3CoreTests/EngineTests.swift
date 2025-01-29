//===- EngineTests.swift --------------------------------------*- Swift -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2024 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

import SwiftProtobuf
import llbuild3
import XCTest

enum TestErrors: Error {
  case unimplemented
}

class BasicTestRuleProvider: TBasicRuleProvider {
  override init(rules: [TLabel] = [], artifacts: [TLabel] = []) {
    super.init(rules: rules, artifacts: artifacts)
  }
}

class NullTask: TBasicTask {
  override func compute(_ ti: TTaskInterface, ctx: TTaskContext, inputs: TTaskInputs, subtaskResults: TSubtaskResults) throws -> TTaskNextState {
    return TTaskNextState.with {
      $0.result = TTaskResult.with {
        $0.artifacts = produces().map { lbl in
          TArtifact.with {
            $0.label = lbl
            $0.type = .blob
            $0.blob = Data(lbl.name.utf8)
          }
        }
      }
    }
  }
}

class NullRule: TBasicRule {
  override func configure() throws -> TTask {
    return NullTask(name(), arts: produces())
  }
}

final class EngineTests: XCTestCase {

  func testInitialization() {
    XCTAssertNoThrow(try TEngine(baseRuleProvider: BasicTestRuleProvider()), "initial registration succeeds");
  }

  func testRuleProviderRegistration() async throws {

    class WorkingInitTask: TBasicTask {
      override func compute(_ ti: TTaskInterface, ctx: TTaskContext, inputs: TTaskInputs, subtaskResults: TSubtaskResults) throws -> TTaskNextState {

        return TTaskNextState.with {
          $0.result = TTaskResult()
        }
      }
    }

    class WorkingInitRule: TBasicRule {
      override func configure() throws -> TTask {
        return WorkingInitTask(name(), arts: [], init: true)
      }
    }

    class FailingInitTask: TBasicTask {
      override func compute(_ ti: TTaskInterface, ctx: TTaskContext, inputs: TTaskInputs, subtaskResults: TSubtaskResults) throws -> TTaskNextState {

        try ti.registerRuleProvider(TestRuleProvider())

        return TTaskNextState.with {
          $0.result = TTaskResult()
        }
      }
    }

    class FailingInitRule: TBasicRule {
      override func configure() throws -> TTask {
        return FailingInitTask(name(), arts: [], init: true)
      }
    }

    class TestRuleProvider: TBasicRuleProvider {
      init() {
        super.init(
          rules: [
            TLabel.with { $0.components = ["init", "working"] },
            TLabel.with { $0.components = ["init", "failing"] },
            TLabel.with { $0.components = ["null"] },
          ],
          artifacts: [
            TLabel.with { $0.components = ["null"] },
          ]
        )
      }

      override func ruleForArtifact(_ lbl: TLabel) -> TRule? {
        if lbl.components.count != 1 || lbl.components[0] != "null" {
          return nil
        }

        return NullRule(lbl, arts: [lbl])
      }

      override func ruleByName(_ lbl: TLabel) -> TRule? {
        if lbl.components.count != 2 || lbl.components[0] != "init" {
          return nil
        }

        switch lbl.components[1] {
        case "working":
          return WorkingInitRule(lbl, arts: [])
        case "failing":
          return FailingInitRule(lbl, arts: [])
        default:
          return nil
        }
      }
    }

    var cfg = TEngineConfig()
    cfg.initRule = TLabel.with { $0.components = ["init", "working"] }
    let engine = try TEngine(config: cfg, baseRuleProvider: TestRuleProvider())
    _ = try await engine.build(TLabel.with { $0.components = ["null"] })

    cfg.initRule = TLabel.with { $0.components = ["init", "failing"] }
    let engine2 = try TEngine(config: cfg, baseRuleProvider: TestRuleProvider())
    do {
      _ = try await engine2.build(TLabel.with { $0.components = ["null"] })
      XCTFail("duplicate registration succeeded")
    } catch {
      guard let terr = error as? TError else {
        throw error
      }
      XCTAssertEqual(llbuild3.EngineError(rawValue: terr.code), llbuild3.EngineError.DuplicateRuleProvider)
    }
  }

  func testNamedTargetRegistration() async throws {
    class TestRuleProvider: TBasicRuleProvider {
      init() {
        super.init(
          rules: [
            TLabel.with {
              $0.components = ["value"]
              $0.name = "one"
            },
            TLabel.with {
              $0.components = ["value"]
              $0.name = "two"
            },
          ],
          artifacts: [
            TLabel.with {
              $0.components = ["value"]
              $0.name = "one"
            },
            TLabel.with {
              $0.components = ["value"]
              $0.name = "two"
            },
          ]
        )
      }

      override func ruleForArtifact(_ lbl: TLabel) -> TRule? {
        if lbl.components.count != 1 || lbl.components[0] != "value" {
          return nil
        }

        return NullRule(lbl, arts: [lbl])
      }

      override func ruleByName(_ lbl: TLabel) -> TRule? {
        return nil
      }
    }

    let engine = try TEngine(baseRuleProvider: TestRuleProvider())
    for val in ["one", "two"] {
      let result = try await engine.build(TLabel.with {
        $0.components = ["value"]
        $0.name = val
      })
      if case .blob(let data) = result.value {
        XCTAssertEqual(data, Data(val.utf8))
      } else {
        XCTFail("invalid artifact type found \(result.value.debugDescription)")
      }
    }
  }



  func testBuild_NoProviders() async throws {
    let engine = try TEngine(baseRuleProvider: BasicTestRuleProvider())

    let art = try TLabel("//test")
    do {
      _ = try await engine.build(art)
      XCTFail("build should fail")
    } catch {
      guard let terr = error as? TError else {
        throw error
      }
      XCTAssertEqual(llbuild3.EngineError(rawValue: terr.code), llbuild3.EngineError.NoArtifactProducer)
    }
  }

  func testBuild_BasicRule() async throws {
    class TestRuleProvider: TBasicRuleProvider {
      init() {
        super.init(
          rules: [],
          artifacts: [try! TLabel("//test")]
        )
      }

      override func ruleForArtifact(_ lbl: TLabel) -> TRule? {
        if lbl.components.count != 1 || lbl.components[0] != "test" {
          return nil
        }
        return NullRule(lbl, arts: [lbl])
      }
    }

    let engine = try TEngine(baseRuleProvider: TestRuleProvider())

    let art = try TLabel("//test:result")
    do {
      let result = try await engine.build(art)
      if case .blob(let data) = result.value {
        XCTAssertEqual(data, Data("result".utf8))
      } else {
        XCTFail("invalid artifact type found \(result.value.debugDescription)")
      }

      // expect that we can build the same object again
      let result2 = try await engine.build(art)
      if case .blob(let data) = result2.value {
        XCTAssertEqual(data, Data("result".utf8))
      } else {
        XCTFail("invalid artifact type found \(result.value.debugDescription)")
      }

    } catch {
      XCTFail("build failed: \(error)")
    }
  }

  func testBuild_3NodeGraph() async throws {
    class MultTask: TBasicTask {
      enum TaskError: Error {
        case unexpectedState
        case badInput
      }

      init(_ lbl: TLabel) {
        let taskName = TLabel.with {
          $0.components = ["compute-mult", lbl.components[1], lbl.components[2]]
        }
        super.init(taskName, arts: [lbl])
      }

      override func compute(_ ti: TTaskInterface, ctx: TTaskContext, inputs: TTaskInputs, subtaskResults: TSubtaskResults) throws -> TTaskNextState {

        guard ctx.taskState != nil else {
          let v1 = try ti.requestArtifact(TLabel.with {
            $0.components = ["value", name().components[1]]
          })
          let v2 = try ti.requestArtifact(TLabel.with {
            $0.components = ["value", name().components[2]]
          })
          return TTaskNextState.with {
            $0.wait = TTaskWait.with {
              $0.ids = [v1,v2]
              $0.context = TTaskContext.with {
                $0.intState = 1
              }
            }
          }
        }

        guard inputs.inputs.count == 2 else {
          throw TaskError.unexpectedState
        }
        guard let v1 = Int(String(decoding: inputs.inputs[0].artifact.blob, as: UTF8.self)) else {
          throw TaskError.badInput
        }
        guard let v2 = Int(String(decoding: inputs.inputs[1].artifact.blob, as: UTF8.self)) else {
          throw TaskError.badInput
        }
        guard let artName = produces().first else {
          throw TaskError.unexpectedState
        }
        return TTaskNextState.with {
          $0.result = TTaskResult.with {
            $0.artifacts = [TArtifact.with {
              $0.label = artName
              $0.type = .blob
              $0.blob = Data("\(v1 * v2)".utf8)
            }]
          }
        }
      }
    }

    class ValueTask: TBasicTask {
      enum TaskError: Error {
        case notANumber
        case unexpectedState
      }

      init(_ lbl: TLabel) {
        let taskName = TLabel.with {
          $0.components = ["produce-value", lbl.components[1]]
        }
        super.init(taskName, arts: [lbl])
      }
      override func compute(_ ti: TTaskInterface, ctx: TTaskContext, inputs: TTaskInputs, subtaskResults: TSubtaskResults) throws -> TTaskNextState {
        guard let val = Int(name().components[1]) else {
          throw TaskError.notANumber
        }
        guard let artName = produces().first else {
          throw TaskError.unexpectedState
        }
        return TTaskNextState.with {
          $0.result = TTaskResult.with {
            $0.artifacts = [TArtifact.with {
              $0.label = artName
              $0.type = .blob
              $0.blob = Data("\(val)".utf8)
            }]
          }
        }
      }
    }

    class MultRule: TBasicRule {
      init(_ lbl: TLabel) {
        let ruleName = TLabel.with {
          $0.components = ["mult-rule", lbl.components[1], lbl.components[2]]
        }
        super.init(ruleName, arts: [lbl])
      }

      override func configure() -> TTask {
        return MultTask(produces().first!)
      }
    }

    class ValueRule: TBasicRule {
      init(_ lbl: TLabel) {
        let ruleName = TLabel.with {
          $0.components = ["input-value", lbl.components[1]]
        }
        super.init(ruleName, arts: [lbl])
      }

      override func configure() -> TTask {
        return ValueTask(produces().first!)
      }
    }

    class TestRuleProvider: TBasicRuleProvider {
      init() {
        super.init(
          rules: [],
          artifacts: [
            TLabel.with {
              $0.components = ["mult"]
            },
            TLabel.with {
              $0.components = ["value"]
            },
          ]
        )
      }

      override func ruleForArtifact(_ lbl: TLabel) -> TRule? {
        guard lbl.components.count > 1 else {
          return nil
        }

        switch lbl.components[0] {
        case "mult":
          guard lbl.components.count > 2 else {
            return nil
          }
          return MultRule(lbl)
        case "value":
          return ValueRule(lbl)
        default:
          return nil
        }
      }
    }

    let engine = try TEngine(baseRuleProvider: TestRuleProvider())

    let art = TLabel.with {
      $0.components = ["mult", "4", "5"]
    }
    do {
      let result = try await engine.build(art)
      if case .blob(let data) = result.value {
        XCTAssertEqual(data, Data("20".utf8))
      } else {
        XCTFail("invalid artifact type found \(result.value.debugDescription)")
      }
    } catch {
      XCTFail("build failed: \(error)")
    }
  }

  func testBuild_SingleCachedRule() async throws {
    class Counter {
      private let queue = DispatchQueue(label: "testBuild_SingleCachedRule")
      private var counter = 0

      func increment() {
        queue.sync { counter += 1 }
      }
      func load() -> Int {
        return queue.sync { return counter }
      }
    }
    let counter = Counter()

    class TestRuleProvider: TBasicRuleProvider {
      let counter: Counter

      init(counter: Counter) throws {
        self.counter = counter
        super.init(
          rules: [],
          artifacts: [
            try TLabel("//test"),
            try TLabel("//input")
          ]
        )
      }

      override func ruleForArtifact(_ lbl: TLabel) -> TRule? {
        if lbl.components.count != 1 {
          return nil
        }

        if lbl.components[0] == "input" {
          return NullRule(lbl, arts: [lbl])
        }
        if lbl.components[0] == "test" {
          return CountedNullRule(lbl, arts: [lbl], counter: counter)
        }

        return nil
      }
    }

    class CountedNullTask: TBasicTask {
      let counter: Counter

      enum TaskError: Error {
        case unexpectedState
      }

      init( _ lbl: TLabel, arts: [TLabel], counter: Counter) {
        self.counter = counter
        super.init(lbl, arts: arts)
      }

      override func compute(_ ti: TTaskInterface, ctx: TTaskContext, inputs: TTaskInputs, subtaskResults: TSubtaskResults) throws -> TTaskNextState {
        guard ctx.taskState != nil else {
          let v1 = try ti.requestArtifact(TLabel.with {
            $0.components = ["input"]
            $0.name = "value1"
          })
          return TTaskNextState.with {
            $0.wait = TTaskWait.with {
              $0.ids = [v1]
              $0.context = TTaskContext.with {
                $0.intState = 1
              }
            }
          }
        }

        guard inputs.inputs.count == 1 else {
          throw TaskError.unexpectedState
        }
        guard let artName = produces().first else {
          throw TaskError.unexpectedState
        }
        counter.increment()
        return TTaskNextState.with {
          $0.result = TTaskResult.with {
            $0.artifacts = [TArtifact.with {
              $0.label = artName
              $0.type = .blob
              $0.blob = inputs.inputs[0].artifact.blob
            }]
          }
        }
      }
    }

    class CountedNullRule: TBasicRule {
      let counter: Counter

      init( _ lbl: TLabel, arts: [TLabel], counter: Counter) {
        self.counter = counter
        super.init(lbl, arts: arts)
      }

      override func configure() throws -> TTask {
        return CountedNullTask(name(), arts: produces(), counter: counter)
      }
    }

    let casDB = llbuild3.makeInMemoryCASDatabase()
    let actionCache = llbuild3.makeInMemoryActionCache()
    let executor = llbuild3.makeActionExecutor()
    let rp = try TestRuleProvider(counter: counter)

    let engine = try TEngine(casDB: casDB, actionCache: actionCache, executor: executor, baseRuleProvider: rp)

    let art = try TLabel("//test:result")
    do {
      let result = try await engine.build(art)
      if case .blob(let data) = result.value {
        XCTAssertEqual(data, Data("value1".utf8))
      } else {
        XCTFail("invalid artifact type found \(result.value.debugDescription)")
      }
      XCTAssertEqual(counter.load(), 1)
    } catch {
      XCTFail("first build failed: \(error)")
    }

    // Construct new engine with the same CAS and action cache
    let engine2 = try TEngine(casDB: casDB, actionCache: actionCache, executor: executor, baseRuleProvider: rp)
    do {
      let result2 = try await engine2.build(art)
      if case .blob(let data) = result2.value {
        XCTAssertEqual(data, Data("value1".utf8))
      } else {
        XCTFail("invalid artifact type found \(result2.value.debugDescription)")
      }
      // Expect that we got an action cache hit
      XCTAssertEqual(counter.load(), 1)
    } catch {
      XCTFail("second build failed: \(error)")
    }
  }

  func testBuild_Subtask() async throws {
    class TestRuleProvider: TBasicRuleProvider {
      init() throws {
        super.init(
          rules: [],
          artifacts: [
            try TLabel("//test"),
          ]
        )
      }

      override func ruleForArtifact(_ lbl: TLabel) -> TRule? {
        if lbl.components.count != 1 {
          return nil
        }

        if lbl.components[0] == "test" {
          return SubtaskRule(lbl, arts: [lbl])
        }

        return nil
      }
    }

    class SubtaskRule: TBasicRule {
      override func configure() throws -> TTask {
        return SubtaskTask(name(), arts: produces())
      }
    }


    class SubtaskTask: TBasicTask {
      enum TaskError: Error {
        case unexpectedState
      }

      func doSomethingAsync(_ si: TSubtaskInterface) async throws -> String {
        return "a string"
      }

      override func compute(_ ti: TTaskInterface, ctx: TTaskContext, inputs: TTaskInputs, subtaskResults: TSubtaskResults) throws -> TTaskNextState {
        guard ctx.taskState != nil else {
          let v1 = try ti.spawnSubtask(doSomethingAsync)
          return TTaskNextState.with {
            $0.wait = TTaskWait.with {
              $0.ids = [v1]
              $0.context = TTaskContext.with {
                $0.intState = 1
              }
            }
          }
        }

        guard subtaskResults.count == 1 else {
          throw TaskError.unexpectedState
        }

        guard let val = subtaskResults.first?.value as? String else {
          throw TaskError.unexpectedState
        }
        guard let artName = produces().first else {
          throw TaskError.unexpectedState
        }
        return TTaskNextState.with {
          $0.result = TTaskResult.with {
            $0.artifacts = [TArtifact.with {
              $0.label = artName
              $0.type = .blob
              $0.blob = Data(val.utf8)
            }]
          }
        }
      }
    }

    let engine = try TEngine(baseRuleProvider: TestRuleProvider())

    let art = try TLabel("//test")
    do {
      let result = try await engine.build(art)
      if case .blob(let data) = result.value {
        XCTAssertEqual(data, Data("a string".utf8))
      } else {
        XCTFail("invalid artifact type found \(result.value.debugDescription)")
      }
    } catch {
      XCTFail("build failed: \(error)")
    }
  }


}

