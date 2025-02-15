//===- Engine.swift -------------------------------------------*- Swift -*-===//
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

import Foundation
import SwiftProtobuf

public enum TClientError: Error {
  case indexOutOfBounds
  case badInputType
  case badActionResult
  case badSubtaskResult
  case unimplemented
  case unclassified(String)
}

public class TTaskInterface {
  private var ti: llbuild3.ExtTaskInterface

  init(_ ti: llbuild3.ExtTaskInterface) {
    self.ti = ti
  }

  public func registerRuleProvider(_ provider: TRuleProvider) throws {
    let res = ti.registerRuleProvider(provider.extRuleProvider)
    if res.size() > 0 {
      let error = try TError(serializedBytes: res)
      throw error
    }
  }

  public func requestArtifact(_ lbl: TLabel) throws -> UInt64 {
    let res = ti.requestArtifact(try lbl.llbuild3Serialized())

    if (res.has_error()) {
      let errorData = res.error()
      let error = try TError(serializedBytes: errorData)
      throw error
    }

    return res.pointee
  }

  public func requestRule(_ lbl: TLabel) throws -> UInt64 {
    let res = ti.requestRule(try lbl.llbuild3Serialized())

    if (res.has_error()) {
      let errorData = res.error()
      let error = try TError(serializedBytes: errorData)
      throw error
    }

    return res.pointee
  }

  public func requestAction(_ action: TAction) throws -> UInt64 {
    let res = ti.requestAction(try action.llbuild3Serialized())

    if (res.has_error()) {
      let errorData = res.error()
      let error = try TError(serializedBytes: errorData)
      throw error
    }

    return res.pointee
  }

  class Capsule {
    let perform: (TSubtaskInterface) async throws -> Any

    init(perform: @escaping (TSubtaskInterface) async throws -> Any) {
      self.perform = perform
    }
  }

  public func spawnSubtask<T>(_ subtask: @escaping (TSubtaskInterface) async throws -> T) throws -> UInt64 {
    let obj = Capsule(perform: subtask)
    var est = llbuild3.ExtSubtask()
    est.ctx = Unmanaged<AnyObject>.passRetained(obj as AnyObject).toOpaque()
    est.perform = { ctx, si, handler in
      let f = Unmanaged<AnyObject>.fromOpaque(ctx!).takeRetainedValue() as! Capsule
      Task {
        do {
          let v = try await f.perform(TSubtaskInterface(si))
          let vp = Unmanaged<AnyObject>.passRetained(v as AnyObject).toOpaque()
          handler(vp, std.string())
        } catch {
          let err: TError
          if let terr = error as? TError {
            err = terr
          } else {
            err = TError.with {
              $0.type = .engine
              $0.code = llbuild3.EngineError.Unknown.rawValue
              $0.description_p = "\(error)"
            }
          }
          guard let bytes = try? err.serializedData() else {
            handler(nil, std.string("failed error serialization"))
            return
          }

          handler(nil, std.string(fromData: bytes))
        }
      }
    }
    let res = ti.spawnSubtask(est)

    if (res.has_error()) {
      let errorData = res.error()
      let error = try TError(serializedBytes: errorData)
      throw error
    }

    return res.pointee
  }
}

public class TSubtaskInterface {
  private var si: llbuild3.ExtSubtaskInterface

  init(_ si: llbuild3.ExtSubtaskInterface) {
    self.si = si
  }

  public var cas: TCASDatabase {
    return si.cas().asTCASDatabase
  }
}

public typealias TSubtaskResults = [UInt64: Any]

public extension TSubtaskResults {
  subscript<T>(id id: UInt64) -> T? {
    guard let value = self[id] else {
      return nil
    }
    return value as? T
  }
}


public protocol TTask {
  func name() -> TLabel
  func signature() -> TSignature

  var isInit: Bool { get }

  func produces() -> [TLabel]
  func compute(_ ti: TTaskInterface, ctx: TTaskContext, inputs: TTaskInputs,
               subtaskResults: TSubtaskResults) throws -> TTaskNextState
}

open class TBasicTask: TTask {
  let lbl: TLabel
  let arts: [TLabel]

  public let isInit: Bool

  public init(_ lbl: TLabel, arts: [TLabel], init isInit: Bool = false) {
    self.lbl = lbl
    self.arts = arts
    self.isInit = isInit
  }

  public func name() -> TLabel {
    return lbl
  }

  open func signature() -> TSignature {
    return TSignature()
  }

  public func produces() -> [TLabel] {
    return arts
  }

  open func compute(_ ti: TTaskInterface, ctx: TTaskContext, inputs: TTaskInputs,
                    subtaskResults: TSubtaskResults) throws -> TTaskNextState {
    throw TClientError.unimplemented
  }
}

public enum TSMTransition<StateType> {
  case wait(StateType, [UInt64])
  case result(TTaskResult)
  case fail(TError)
}

public protocol TStateMachine: Codable {
  associatedtype StateType: RawRepresentable<Int>

  init()
  mutating func initialize(_ ti: TTaskInterface, task: TTask) throws -> TSMTransition<StateType>
  mutating func compute(state: StateType, _ ti: TTaskInterface, task: TTask, inputs: TTaskInputs,
                        subtaskResults: TSubtaskResults) throws -> TSMTransition<StateType>
}

public enum TStateMachineTaskError: Error {
  case badContext
  case badState
}

public class TStateMachineTask<T: TStateMachine>: TBasicTask {
  public override func compute(_ ti: TTaskInterface, ctx: TTaskContext, inputs: TTaskInputs, subtaskResults: TSubtaskResults) throws -> TTaskNextState {
    guard ctx.taskState != nil else {
      var sm = T()
      let transition = try sm.initialize(ti, task: self)
      return try makeNext(sm, transition: transition)
    }

    // unwrap context
    guard ctx.protoState.isA(Llbuild3_TaskStateMachineContext.self) else {
      throw TStateMachineTaskError.badContext
    }
    let tctx = try Llbuild3_TaskStateMachineContext(unpackingAny: ctx.protoState)
    guard let s = T.StateType(rawValue: Int(tctx.next)) else {
      throw TStateMachineTaskError.badState
    }

    var sm = try JSONDecoder().decode(T.self, from: tctx.data)
    let transition = try sm.compute(state: s, ti, task: self, inputs: inputs, subtaskResults: subtaskResults)
    return try makeNext(sm, transition: transition)
  }

  private func makeNext(_ sm: T, transition: TSMTransition<T.StateType>) throws -> TTaskNextState {
    switch transition {
    case .wait(let next, let ids):

      let encoder = JSONEncoder()
      encoder.outputFormatting = [.sortedKeys]
      let encodedsm = try encoder.encode(sm)

      let tctx = Llbuild3_TaskStateMachineContext.with {
        $0.next = Int64(next.rawValue)
        $0.data = encodedsm
      }

      return try TTaskNextState.with {
        $0.wait = try TTaskWait.with {
          $0.ids = ids
          $0.context.protoState = try Google_Protobuf_Any(message: tctx)
        }
      }
    case .result(let tres):
      return TTaskNextState.with {
        $0.result = tres
      }

    case .fail(let err):
      return TTaskNextState.with {
        $0.error = err
      }
    }
  }
}

public extension TTask {
  var isInit: Bool {
    return false
  }
}

public extension TTaskInputs {
  func getSubprocessResult(_ idx: Int) throws -> TSubprocessResult {
    guard idx >= 0, inputs.count > idx else {
      throw TClientError.indexOutOfBounds
    }

    if case .error(let err) = inputs[idx].inputObject {
      throw err
    }

    guard case .action(let res) = inputs[idx].inputObject else {
      throw TClientError.badInputType
    }

    guard case .subprocess(let sres) = res.actionResultValue else {
      throw TClientError.badActionResult
    }

    return sres
  }
}

extension TTask {
  func extTask() throws -> llbuild3.ExtTask {
    var task = llbuild3.ExtTask()

    let namebytes = try name().serializedData()
    task.name = std.string(fromData: namebytes)
    let sigbytes = try signature().serializedData()
    task.signature = std.string(fromData: sigbytes)

    task.isInit = isInit

    task.ctx = Unmanaged.passRetained(self as AnyObject).toOpaque()

    task.producesFn = { ctx, lblp in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TTask
      var lblvector = lblp.pointee
      do {
        for rn in sp.produces() {
          let bytes = try rn.serializedData()
          lblvector.push_back(std.string(fromData: bytes))
        }
      } catch {
        return
      }
      lblp?.update(from: &lblvector, count: 1)
    }

    task.computeFn = { ctx, eti, tcp, tip, smap, tnsp in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TTask

      let ti = TTaskInterface(eti)

      do {
        let tctx = try TTaskContext(serializedBytes: tcp.pointee)
        let inputs = try TTaskInputs(serializedBytes: tip.pointee)

        var sres: [UInt64: Any] = [:]
        for v in smap.pointee {
          let ap = Unmanaged<AnyObject>.fromOpaque(v.second.pointee!).takeRetainedValue() as Any
          sres[v.first] = ap
        }

        let ns = try sp.compute(ti, ctx: tctx, inputs: inputs, subtaskResults: sres)

        let bytes = try ns.serializedData()
        var res = std.string(fromData: bytes)
        tnsp?.update(from: &res, count: 1)
      } catch {
        let err: TError
        if let terr = error as? TError {
          err = terr
        } else {
          err = TError.with {
            $0.type = .client
            $0.code = llbuild3.EngineError.Unknown.rawValue
            $0.description_p = "\(error)"
          }
        }
        let ns = TTaskNextState.with {
          $0.error = err
        }
        guard let bytes = try? ns.serializedData() else {
          return false
        }
        var res = std.string(fromData: bytes)
        tnsp?.update(from: &res, count: 1)
        return true
      }

      return true
    }

    return task
  }
}

public protocol TRule {
  func name() -> TLabel
  func signature() -> TSignature

  func produces() -> [TLabel]
  func configure() throws -> TTask
}

open class TBasicRule: TRule {
  let lbl: TLabel
  let arts: [TLabel]

  public init(_ lbl: TLabel, arts: [TLabel]) {
    self.lbl = lbl
    self.arts = arts
  }

  public func name() -> TLabel {
    return lbl
  }

  open func signature() -> TSignature {
    return TSignature()
  }

  public func produces() -> [TLabel] {
    return arts
  }

  open func configure() throws -> TTask {
    throw TClientError.unimplemented
  }
}

public class TSimpleRule: TBasicRule {
  let method: (TLabel, [TLabel]) throws -> TTask
  public init(_ lbl: TLabel, arts: [TLabel], _ method: @escaping (TLabel, [TLabel]) throws -> TTask) {
    self.method = method
    super.init(lbl, arts: arts)
  }

  public override func configure() throws -> TTask {
    return try method(name(), produces())
  }
}

extension TRule {
  func extRule() throws -> llbuild3.ExtRule {
    var rule = llbuild3.ExtRule()

    let namebytes = try name().serializedData()
    rule.name = std.string(fromData: namebytes)
    let sigbytes = try signature().serializedData()
    rule.signature = std.string(fromData: sigbytes)

    rule.ctx = Unmanaged.passRetained(self as AnyObject).toOpaque()

    rule.producesFn = { ctx, lblp in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TRule
      var lblvector = lblp.pointee
      do {
        for rn in sp.produces() {
          let bytes = try rn.serializedData()
          lblvector.push_back(std.string(fromData: bytes))
        }
      } catch {
        return
      }
      lblp?.update(from: &lblvector, count: 1)
    }

    rule.configureTaskFn = { ctx, etp -> Bool in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TRule

      do {
        let task = try sp.configure()
        var et = try task.extTask()
        etp?.update(from: &et, count: 1)
        return true
      } catch {
        return false
      }
    }

    return rule
  }
}

public protocol TRuleProvider {
  func rulePrefixes() -> [TLabel]
  func artifactPrefixes() -> [TLabel]

  func ruleByName(_ lbl: TLabel) -> TRule?
  func ruleForArtifact(_ lbl: TLabel) -> TRule?
}

open class TBasicRuleProvider: TRuleProvider {
  let ruleLbls: [TLabel]
  let artLbls: [TLabel]

  public init(rules: [TLabel] = [], artifacts: [TLabel] = []) {
    self.ruleLbls = rules
    self.artLbls = artifacts
  }

  public func rulePrefixes() -> [TLabel] {
    return ruleLbls
  }
  public func artifactPrefixes() -> [TLabel] {
    return artLbls
  }

  open func ruleByName(_ lbl: TLabel) -> TRule? {
    return nil
  }
  open func ruleForArtifact(_ lbl: TLabel) -> TRule? {
    return nil
  }
}

public class TMappedRuleProvider: TBasicRuleProvider {
  public struct MappedRule {
    let name: TLabel
    let arts: [TLabel]
    let method: (TLabel, [TLabel]) -> TRule

    public init(_ name: String, arts: [String] = [],  _ method: @escaping (TLabel, [TLabel]) -> TRule) throws {
      self.name = try TLabel(name)
      if arts.count == 0 {
        self.arts = [self.name]
      } else {
        self.arts = try arts.map { try TLabel($0) }
      }
      self.method = method
    }

    public init(_ name: TLabel, arts: [TLabel] = [], _ method: @escaping (TLabel, [TLabel]) -> TRule) {
      self.name = name
      if arts.count == 0 {
        self.arts = [self.name]
      } else {
        self.arts = arts
      }
      self.method = method
    }
  }
  let rules: [MappedRule]
  let artMap: [TLabel: MappedRule]


  public init(_ rules: [MappedRule] = []) {
    self.rules = rules
    let ruleNames = rules.map { $0.name }

    var artNames: [TLabel] = []
    var artMap: [TLabel: MappedRule] = [:]
    for rule in rules {
      artNames.append(contentsOf: rule.arts)
      for art in rule.arts {
        artMap[art] = rule
      }
    }
    self.artMap = artMap
    super.init(rules: ruleNames, artifacts: artNames)
  }

  public override func ruleByName(_ lbl: TLabel) -> TRule? {
    for rule in rules {
      if lbl == rule.name {
        return rule.method(lbl, rule.arts)
      }
    }
    return nil
  }

  public override func ruleForArtifact(_ lbl: TLabel) -> TRule? {
    guard let rule = artMap[lbl] else {
      return nil
    }
    return rule.method(rule.name, rule.arts)
  }
}

extension TRuleProvider {
  var extRuleProvider: llbuild3.ExtRuleProvider {
    var rp = llbuild3.ExtRuleProvider()
    rp.ctx = Unmanaged.passRetained(self as AnyObject).toOpaque()

    rp.rulePrefixesFn = { ctx, lblp in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TRuleProvider
      var lblvector = lblp.pointee
      do {
        for rn in sp.rulePrefixes() {
          let bytes = try rn.serializedData()
          lblvector.push_back(std.string(fromData: bytes))
        }
      } catch {
        return
      }
      lblp?.update(from: &lblvector, count: 1)
    }

    rp.artifactPrefixesFn = { ctx, lblp in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TRuleProvider
      var lblvector = lblp.pointee
      do {
        for rn in sp.artifactPrefixes() {
          let bytes = try rn.serializedData()
          lblvector.push_back(std.string(fromData: bytes))
        }
      } catch {
        return
      }
      lblp?.update(from: &lblvector, count: 1)
    }

    rp.ruleByNameFn = { ctx, lblpb, rulep -> Bool in
      let lbl: TLabel
      do {
        let lblData = lblpb.pointee
        lbl = try TLabel(serializedBytes: lblData)
      } catch {
        return false
      }

      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TRuleProvider

      guard let rule = sp.ruleByName(lbl) else {
        return false
      }

      guard let erule = try? rule.extRule() else {
        return false
      }
      var merule = erule
      rulep?.update(from: &merule, count: 1)
      return true
    }

    rp.ruleForArtifactFn = { ctx, lblpb, rulep -> Bool in
      let lbl: TLabel
      do {
        let lblData = lblpb.pointee
        lbl = try TLabel(serializedBytes: lblData)
      } catch {
        return false
      }

      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TRuleProvider

      guard let rule = sp.ruleForArtifact(lbl) else {
        return false
      }

      guard let erule = try? rule.extRule() else {
        return false
      }
      var merule = erule
      rulep?.update(from: &merule, count: 1)
      return true
    }

    return rp
  }
}

public struct TEngineConfig {
  public var initRule: TLabel? = nil

  public init() { }
}

extension TEngineConfig {
  func extEngineConfig() throws -> llbuild3.ExtEngineConfig {
    var extcfg = llbuild3.ExtEngineConfig()
    if let initRule = self.initRule {
      let bytes = try initRule.serializedData()
      extcfg.setInitRule(std.string(fromData: bytes))
    }
    return extcfg
  }
}

public class TExecutor {
  let executor: llbuild3.ActionExecutorRef

  convenience public init(casDB: TCASDatabase, actionCache: TActionCache? = nil, sandboxProvider: TLocalSandboxProvider) {
    let tcas = llbuild3.makeExtCASDatabase(casDB.extCASDatabase)
    let lsp = llbuild3.makeExtLocalSandboxProvider(sandboxProvider.extLocalSandboxProvider)

    let tcache: llbuild3.ActionCacheRef
    if let cache = actionCache {
      tcache = llbuild3.makeExtActionCache(cache.extActionCache())
    } else {
      tcache = llbuild3.ActionCacheRef()
    }

    self.init(casDB: tcas, actionCache: tcache, sandboxProvider: lsp)
  }

  convenience public init(casDB: llbuild3.CASDatabaseRef, actionCache: llbuild3.ActionCacheRef? = nil, sandboxProvider: TLocalSandboxProvider) {
    let lsp = llbuild3.makeExtLocalSandboxProvider(sandboxProvider.extLocalSandboxProvider)

    let tcache = actionCache ?? llbuild3.ActionCacheRef()
    self.init(casDB: casDB, actionCache: tcache, sandboxProvider: lsp)
  }

  public init(casDB: llbuild3.CASDatabaseRef, actionCache: llbuild3.ActionCacheRef, sandboxProvider: llbuild3.LocalSandboxProviderRef) {
    let execlocal = llbuild3.makeLocalExecutor(sandboxProvider)
    let execremote = llbuild3.makeRemoteExecutor()

    executor = llbuild3.makeActionExecutor(casDB, actionCache, execlocal, execremote)
  }
}

public class TEngine {
  private var eng: llbuild3.EngineRef

  convenience public init (config: TEngineConfig = TEngineConfig(), casDB: TCASDatabase, actionCache: TActionCache? = nil, executor: TExecutor, baseRuleProvider: TRuleProvider) throws {
    let tcas = llbuild3.makeExtCASDatabase(casDB.extCASDatabase)

    let tcache: llbuild3.ActionCacheRef
    if let cache = actionCache {
      tcache = llbuild3.makeExtActionCache(cache.extActionCache())
    } else {
      tcache = llbuild3.ActionCacheRef()
    }

    try self.init(config: config, casDB: tcas, actionCache: tcache, executor: executor.executor, baseRuleProvider: baseRuleProvider)
  }

  convenience public init (config: TEngineConfig = TEngineConfig(), casDB: llbuild3.CASDatabaseRef, actionCache: llbuild3.ActionCacheRef = llbuild3.ActionCacheRef(), executor: TExecutor, baseRuleProvider: TRuleProvider) throws {
    try self.init(config: config, casDB: casDB, actionCache: actionCache, executor: executor.executor, baseRuleProvider: baseRuleProvider)
  }

  init (config: TEngineConfig = TEngineConfig(), casDB: llbuild3.CASDatabaseRef, actionCache: llbuild3.ActionCacheRef, executor: llbuild3.ActionExecutorRef, baseRuleProvider: TRuleProvider) throws {

    eng = llbuild3.makeEngine(try config.extEngineConfig(), casDB, actionCache, executor, baseRuleProvider.extRuleProvider)
  }


  public var cas: TCASDatabase {
    let db = eng.cas()
    if let ctx = llbuild3.getRawCASDatabaseContext(db),
       let sp = Unmanaged<AnyObject>.fromOpaque(ctx).takeUnretainedValue() as? TCASDatabase {
      return sp
    }

    return AdaptedCASDatabase(db: db)
  }

  public func build(_ lbl: TLabel) async throws -> TArtifact {
    var build = eng.build(try lbl.llbuild3Serialized())

    return try await withCheckedThrowingContinuation { continuation in
      let ctx = Unmanaged.passRetained(continuation as AnyObject).toOpaque()
      build.addCompletionHandler(ctx, { ctx, result in
        let completion = Unmanaged<AnyObject>.fromOpaque(ctx!).takeRetainedValue() as! CheckedContinuation<TArtifact, any Error>

        if result.pointee.has_error() {
          do {
            let errorData = result.pointee.error()
            let error = try TError(serializedBytes: errorData)
            completion.resume(throwing: error)
          } catch {
            completion.resume(throwing: error)
          }
        } else {
          do {
            let artData = result.pointee.pointee
            let art = try TArtifact(serializedBytes: artData)
            completion.resume(returning: art)
          } catch {
            completion.resume(throwing: error)
          }
        }
      })
    }
  }
}
