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
    let db = llbuild3.makeInMemoryCASDatabase()
    let sp = TTempDirSandboxProvider(basedir: "testInitialization", casDB: db.asTCASDatabase)
    let exe = TExecutor(casDB: db, sandboxProvider: sp)
    XCTAssertNoThrow(try TEngine(casDB: db, executor: exe, baseRuleProvider: TBasicRuleProvider()), "initial registration succeeds");
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

    let db = llbuild3.makeInMemoryCASDatabase()
    let sp = TTempDirSandboxProvider(basedir: "testRuleProviderRegistration", casDB: db.asTCASDatabase)
    let exe = TExecutor(casDB: db, sandboxProvider: sp)

    var cfg = TEngineConfig()
    cfg.initRule = TLabel.with { $0.components = ["init", "working"] }
    let engine = try TEngine(config: cfg, casDB: db, executor: exe, baseRuleProvider: TestRuleProvider())
    _ = try await engine.build(TLabel.with { $0.components = ["null"] })

    cfg.initRule = TLabel.with { $0.components = ["init", "failing"] }
    let engine2 = try TEngine(config: cfg, casDB: db, executor: exe, baseRuleProvider: TestRuleProvider())
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

    let db = llbuild3.makeInMemoryCASDatabase()
    let sp = TTempDirSandboxProvider(basedir: "testNamedRegistration", casDB: db.asTCASDatabase)
    let exe = TExecutor(casDB: db, sandboxProvider: sp)
    let engine = try TEngine(casDB: db, executor: exe, baseRuleProvider: TestRuleProvider())
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
    let db = llbuild3.makeInMemoryCASDatabase()
    let sp = TTempDirSandboxProvider(basedir: "testBuild_NoProviders", casDB: db.asTCASDatabase)
    let exe = TExecutor(casDB: db, sandboxProvider: sp)
    let engine = try TEngine(casDB: db, executor: exe, baseRuleProvider: TBasicRuleProvider())

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

    let db = llbuild3.makeInMemoryCASDatabase()
    let sp = TTempDirSandboxProvider(basedir: "testBuild_BasicRule", casDB: db.asTCASDatabase)
    let exe = TExecutor(casDB: db, sandboxProvider: sp)
    let engine = try TEngine(casDB: db, executor: exe, baseRuleProvider: TestRuleProvider())

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

    let db = llbuild3.makeInMemoryCASDatabase()
    let sp = TTempDirSandboxProvider(basedir: "testBuild_3NodeGraph", casDB: db.asTCASDatabase)
    let exe = TExecutor(casDB: db, sandboxProvider: sp)
    let engine = try TEngine(casDB: db, executor: exe, baseRuleProvider: TestRuleProvider())

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

    let db = llbuild3.makeInMemoryCASDatabase()
    let actionCache = llbuild3.makeInMemoryActionCache()
    let sp = TTempDirSandboxProvider(basedir: "testBuild_SingleCachedRule", casDB: db.asTCASDatabase)
    let exe = TExecutor(casDB: db, sandboxProvider: sp)
    let rp = try TestRuleProvider(counter: counter)

    let engine = try TEngine(casDB: db, actionCache: actionCache, executor: exe, baseRuleProvider: rp)

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
    let engine2 = try TEngine(casDB: db, actionCache: actionCache, executor: exe, baseRuleProvider: rp)
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

  func testBuild_Action_BasicSubprocess() async throws {
    let rp = TMappedRuleProvider([
      try .init("//test") {
        TSimpleRule($0, arts: $1) {
          TStateMachineTask<EchoAction>($0, arts: $1)
        }
      }
    ])

    struct EchoAction: TStateMachine {
      enum State: Int {
        case actionComplete = 1
      }

      mutating func initialize(_ ti: TTaskInterface, task: TTask) throws -> TSMTransition<State> {
        let action = try TAction.with {
          $0.subprocess = TSubprocess.with {
            $0.arguments = ["/bin/echo", "a", "string"]
          }
          $0.function = try TLabel("//builtin/subprocess")
        }

        let taskID = try ti.requestAction(action)
        return .wait(.actionComplete, [taskID])
      }

      mutating func compute(state: StateType, _ ti: TTaskInterface, task: TTask, inputs: TTaskInputs, subtaskResults: TSubtaskResults) throws -> TSMTransition<State> {
        let sres = try inputs.getSubprocessResult(0)

        guard let artName = task.produces().first else {
          throw TClientError.unclassified("no product label")
        }
        return .result(TTaskResult.with {
          $0.artifacts = [TArtifact.with {
            $0.label = artName
            $0.type = .blob
            $0.casObject = sres.stdout
          }]
        })
      }
    }

    let db = llbuild3.makeInMemoryCASDatabase()
    let sp = TTempDirSandboxProvider(basedir: "testBuild_Action", casDB: db.asTCASDatabase)
    let exe = TExecutor(casDB: db, sandboxProvider: sp)
    let engine = try TEngine(casDB: db, executor: exe, baseRuleProvider: rp)

    let art = try TLabel("//test")
    do {
      let result = try await engine.build(art)
      if case .casObject(let id) = result.value {
        let db = engine.cas
        guard let obj = try await db.get(id) else {
          XCTFail("object not found")
          return
        }

        XCTAssertEqual(obj.refs.count, 1)

        guard let chunk = try await db.get(obj.refs[0]) else {
          XCTFail("file chunk not found")
          return
        }

        XCTAssertEqual(chunk.data, Data("a string\n".utf8))
      } else {
        XCTFail("invalid artifact type found \(result.value.debugDescription)")
      }
    } catch {
      XCTFail("build failed: \(error)")
    }
  }

  func testBuild_Action_SubprocessWithInput() async throws {
    let rp = TMappedRuleProvider([
      try .init("//test") {
        TSimpleRule($0, arts: $1) {
          TStateMachineTask<CatAction>($0, arts: $1)
        }
      }
    ])

    struct CatAction: TStateMachine {
      enum State: Int {
        case inputUploaded = 1
        case actionComplete
      }

      var subtaskID: UInt64 = 0

      mutating func initialize(_ ti: TTaskInterface, task: TTask) throws -> TSMTransition<State> {
        subtaskID = try ti.spawnSubtask() { si in
          let obj = TCASObject.with { $0.data = Data("a string".utf8) }
          return try await si.cas.put(obj)
        }
        return .wait(.inputUploaded, [subtaskID])
      }

      mutating func compute(state: StateType, _ ti: TTaskInterface, task: TTask, inputs: TTaskInputs, subtaskResults: TSubtaskResults) throws -> TSMTransition<State> {
        switch state {
        case .inputUploaded:
          guard let inputID: TCASID = subtaskResults[id: subtaskID] else {
            throw TClientError.badSubtaskResult
          }
          let action = try TAction.with {
            $0.subprocess = TSubprocess.with {
              $0.arguments = ["/bin/cat", "input-1"]
              $0.inputs = [
                TFileObject.with {
                  $0.path = "input-1"
                  $0.type = .plainFile
                  $0.object = inputID
                }
              ]
            }
            $0.function = try TLabel("//builtin/subprocess")
          }

          let taskID = try ti.requestAction(action)
          return .wait(.actionComplete, [taskID])

        case .actionComplete:
          let sres = try inputs.getSubprocessResult(0)

          guard let artName = task.produces().first else {
            throw TClientError.unclassified("no product label")
          }
          return .result(TTaskResult.with {
            $0.artifacts = [TArtifact.with {
              $0.label = artName
              $0.type = .blob
              $0.casObject = sres.stdout
            }]
          })
        }
      }
    }

    let db = llbuild3.makeInMemoryCASDatabase()
    let sp = TTempDirSandboxProvider(basedir: "testBuild_Action", casDB: db.asTCASDatabase)
    let exe = TExecutor(casDB: db, sandboxProvider: sp)
    let engine = try TEngine(casDB: db, executor: exe, baseRuleProvider: rp)

    let art = try TLabel("//test")
    do {
      let result = try await engine.build(art)
      if case .casObject(let id) = result.value {
        let db = engine.cas
        guard let obj = try await db.get(id) else {
          XCTFail("object not found")
          return
        }

        XCTAssertEqual(obj.refs.count, 1)

        guard let chunk = try await db.get(obj.refs[0]) else {
          XCTFail("file chunk not found")
          return
        }

        XCTAssertEqual(chunk.data, Data("a string".utf8))
      } else {
        XCTFail("invalid artifact type found \(result.value.debugDescription)")
      }
    } catch {
      XCTFail("build failed: \(error)")
    }
  }

  func testBuild_Action_BasicFunction() async throws {

    let rp = TMappedRuleProvider([
      try .init("//test") {
        TSimpleRule($0, arts: $1) {
          TStateMachineTask<EchoAction>($0, arts: $1)
        }
      }
    ])


    struct EchoAction: TStateMachine {
      enum State: Int {
        case inputUploaded = 1
        case actionComplete = 2
      }

      var v1: UInt64 = 0

      mutating func initialize(_ ti: TTaskInterface, task: TTask) throws -> TSMTransition<State> {
        v1 = try ti.spawnSubtask { si in
          return try await si.cas.put(TCASObject.with { $0.data = Data("a string".utf8) })
        }

        return .wait(.inputUploaded, [v1])
      }

      mutating func compute(state: StateType, _ ti: TTaskInterface, task: TTask, inputs: TTaskInputs, subtaskResults: TSubtaskResults) throws -> TSMTransition<State> {
        switch state {
        case .inputUploaded:
          guard let testStringID: TCASID = subtaskResults[id: v1] else {
            throw TClientError.badSubtaskResult
          }
          let action = try TAction.with {
            $0.casObject = testStringID
            $0.function = try TLabel("//bin/echo")
          }

          let taskID = try ti.requestAction(action)
          return .wait(.actionComplete, [taskID])
        case .actionComplete:
          let r = try inputs.getActionCASResult(0)

          guard let artName = task.produces().first else {
            throw TClientError.unclassified("no product label")
          }
          return .result(TTaskResult.with {
            $0.artifacts = [TArtifact.with {
              $0.label = artName
              $0.type = .blob
              $0.casObject = r
            }]
          })
        }
      }
    }

    class EchoProvider: TActionProvider {
      func prefixes() -> [TLabel] { return [try! TLabel("//bin/echo")] }
      func resolve(_ lbl: TLabel) throws -> TLabel? { return lbl }
      func actionDescriptor(_ lbl: TLabel) throws -> TActionDescriptor? {
        return TActionDescriptor(
          name: lbl,
          platform: TPlatform(),
          executable: "/bin/echo"
        )
      }
    }

    let db = llbuild3.makeInMemoryCASDatabase()
    let sp = TTempDirSandboxProvider(basedir: "testBuild_Action_BasicFunction", casDB: db.asTCASDatabase)
    let exe = TExecutor(casDB: db, sandboxProvider: sp)
    try exe.registerProvider(EchoProvider())
    let engine = try TEngine(casDB: db, executor: exe, baseRuleProvider: rp)

    let art = try TLabel("//test")
    do {
      let result = try await engine.build(art)
      if case .casObject(let id) = result.value {
        let db = engine.cas
        guard let obj = try await db.get(id) else {
          XCTFail("object not found")
          return
        }

        XCTAssertEqual(obj.data, Data("a string".utf8))
      } else {
        XCTFail("invalid artifact type found \(result.value.debugDescription)")
      }
    } catch {
      XCTFail("build failed: \(error)")
    }
  }


  func testBuild_Subtask() async throws {
    let rp = TMappedRuleProvider([
      try .init("//test") { TSimpleRule($0, arts: $1) { SubtaskTask($0, arts: $1) } }
    ])

    class SubtaskTask: TBasicTask {
      var v1: UInt64 = 0

      func doSomethingAsync(_ si: TSubtaskInterface) async throws -> String {
        return "a string"
      }

      override func compute(_ ti: TTaskInterface, ctx: TTaskContext, inputs: TTaskInputs, subtaskResults: TSubtaskResults) throws -> TTaskNextState {
        guard ctx.taskState != nil else {
          v1 = try ti.spawnSubtask(doSomethingAsync)
          return TTaskNextState.with {
            $0.wait = TTaskWait.with {
              $0.ids = [v1]
              $0.context = TTaskContext.with {
                $0.intState = 1
              }
            }
          }
        }

        guard let val: String = subtaskResults[id: v1] else {
          throw TClientError.badSubtaskResult
        }

        guard let artName = produces().first else {
          throw TClientError.unclassified("no product label")
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

    let db = llbuild3.makeInMemoryCASDatabase()
    let sp = TTempDirSandboxProvider(basedir: "testBuild_Subtask", casDB: db.asTCASDatabase)
    let exe = TExecutor(casDB: db, sandboxProvider: sp)
    let engine = try TEngine(casDB: db, executor: exe, baseRuleProvider: rp)

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

  func testBuild_Logger() async throws {
    let rp = TMappedRuleProvider([
      try .init("//test") {
        NullRule($0, arts: $1)
      }
    ])

    class TestLogger: TLogger {
      private let queue = DispatchQueue(label: "testBuild_Logger")
      private var events_: [([TStat], TLoggingContext)] = []

      func error(_ err: TError, _ ctx: TLoggingContext) {
        XCTFail("error logged \(err)")
      }
      func event(_ stats: [TStat], _ ctx: TLoggingContext) {
        queue.sync {
          events_.append((stats, ctx))
        }
      }

      var events: [([TStat], TLoggingContext)] {
        return queue.sync { self.events_ }
      }
    }

    class TestClientContext: TClientContext {
      let val = "some context"
    }

    let db = llbuild3.makeInMemoryCASDatabase()
    let sp = TTempDirSandboxProvider(basedir: "testBuild_Logger", casDB: db.asTCASDatabase)
    let exe = TExecutor(casDB: db, sandboxProvider: sp)
    let logger = TestLogger()
    let engine = try TEngine(casDB: db, executor: exe, logger: logger, clientContext: TestClientContext(), baseRuleProvider: rp)

    let art = try TLabel("//test")
    do {
      _ = try await engine.build(art)
      // check that we got events
      XCTAssertEqual(logger.events.count, 2)
      guard let (e1, c1) = logger.events.first else {
        XCTFail("start event not found")
        return
      }

      // Check the client context
      if let c = c1.clientContext {
        if let cc = c as? TestClientContext {
          XCTAssertEqual(cc.val, "some context")
        } else {
          XCTFail("not a TestClientContext")
        }
      } else {
        XCTFail("clientContext not found")
      }

      // Check the contents of the start message
      var messageFound = false
      for s in e1 {
        switch s.name {
        case "log.message":
          messageFound = true
          if case .stringValue(let v) = s.value {
            XCTAssertEqual(v, "build_started")
          } else {
            XCTFail("log.message not a string")
          }
        default:
          continue
        }
      }
      XCTAssertTrue(messageFound)

      // Check the contents of the completed message
      guard let (e2, _) = logger.events.last else {
        XCTFail("completed event not found")
        return
      }
      messageFound = false
      var statusFound = false
      for s in e2 {
        switch s.name {
        case "log.message":
          messageFound = true
          if case .stringValue(let v) = s.value {
            XCTAssertEqual(v, "build_completed")
          } else {
            XCTFail("log.message not a string")
          }
        case "status":
          statusFound = true
          if case .stringValue(let v) = s.value {
            XCTAssertEqual(v, "success")
          } else {
            XCTFail("status not a string")
          }
        default:
          continue
        }
      }
      XCTAssertTrue(messageFound)
      XCTAssertTrue(statusFound)
    } catch {
      XCTFail("build failed: \(error)")
    }
  }

  func testBuild_Ackermann() async throws {
    class AckRuleProvider: TBasicRuleProvider {
      init() {
        super.init(
          rules: [],
          artifacts: [
            TLabel.with {
              $0.components = ["ackermann"]
            },
          ]
        )
      }

      override func ruleForArtifact(_ lbl: TLabel) -> TRule? {
        guard lbl.components.count == 1 && lbl.components[0] == "ackermann" else {
          return nil
        }
        return TSimpleRule(lbl, arts: [lbl]) {
          TStateMachineTask<Ackermann>($0, arts: $1)
        }
      }
    }

    struct Ackermann: TStateMachine {
      enum State: Int {
        case input1Available = 1
        case input2Available = 2
      }

      enum AckError: Error {
        case badInput
      }

      var m: Int = 0
      var n: Int = 0

      mutating func initialize(_ ti: TTaskInterface, task: TTask) throws -> TSMTransition<State> {
        guard let args = task.produces().first?.name.split(separator: ",") else {
          throw AckError.badInput
        }
        guard args.count == 2, let am = Int(args[0]), let an = Int(args[1]) else {
          throw AckError.badInput
        }
        m = am
        n = an

        if m == 0 {
          return .result(TTaskResult.with {
            $0.artifacts = [TArtifact.with {
              $0.label = task.produces().first!
              $0.type = .blob
              $0.blob = Data("\(n + 1)".utf8)
            }]
          })
        }

        let inputID: UInt64
        if n == 0 {
          inputID = try ti.requestArtifact(TLabel("//ackermann:\(m - 1),1"))
        } else {
          inputID = try ti.requestArtifact(TLabel("//ackermann:\(m),\(n - 1)"))
        }

        return .wait(.input1Available, [inputID])
      }

      mutating func compute(state: StateType, _ ti: TTaskInterface, task: TTask, inputs: TTaskInputs, subtaskResults: TSubtaskResults) throws -> TSMTransition<State> {
        guard inputs.inputs.count == 1, let input = Int(String(decoding: inputs.inputs[0].artifact.blob, as: UTF8.self)) else {
          throw AckError.badInput
        }

        switch state {
        case .input1Available:
          if (m != 0 && n != 0) {
            let inputID = try ti.requestArtifact(TLabel("//ackermann:\(m - 1),\(input)"))
            return .wait(.input2Available, [inputID])
          }

          assert(input != 0)
          assert(n == 0)
          return .result(TTaskResult.with {
            $0.artifacts = [TArtifact.with {
              $0.label = task.produces().first!
              $0.type = .blob
              $0.blob = Data("\(input)".utf8)
            }]
          })

        case .input2Available:
          return .result(TTaskResult.with {
            $0.artifacts = [TArtifact.with {
              $0.label = task.produces().first!
              $0.type = .blob
              $0.blob = Data("\(input)".utf8)
            }]
          })
        }
      }
    }

    let rp = AckRuleProvider()

    let db = llbuild3.makeInMemoryCASDatabase()
    let sp = TTempDirSandboxProvider(basedir: "testBuild_Ackermann", casDB: db.asTCASDatabase)
    let exe = TExecutor(casDB: db, sandboxProvider: sp)
    let engine = try TEngine(casDB: db, executor: exe, baseRuleProvider: rp)

    let art = try TLabel("//ackermann:3,4")
    do {
      let result = try await engine.build(art)
      if case .blob(let data) = result.value {
        let value = Int(String(decoding: data, as: UTF8.self))
        XCTAssertEqual(value, 125)
      } else {
        XCTFail("invalid artifact type found \(result.value.debugDescription)")
      }
    } catch {
      XCTFail("build failed: \(error)")
    }
  }
}

