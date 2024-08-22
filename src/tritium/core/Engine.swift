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

public enum TClientError: Error {
  case unimplemented
}

public class TTaskInterface {
  private var ti: tritium.core.ExtTaskInterface

  init(_ ti: tritium.core.ExtTaskInterface) {
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
    let res = ti.requestArtifact(try lbl.tritiumCoreSerialized())

    if (res.has_error()) {
      let errorData = res.error()
      let error = try TError(serializedBytes: errorData)
      throw error
    }

    return res.pointee
  }

  public func requestRule(_ lbl: TLabel) throws -> UInt64 {
    let res = ti.requestRule(try lbl.tritiumCoreSerialized())

    if (res.has_error()) {
      let errorData = res.error()
      let error = try TError(serializedBytes: errorData)
      throw error
    }

    return res.pointee
  }

  public func requestAction() throws -> UInt64 {
    let res = ti.requestAction()

    if (res.has_error()) {
      let errorData = res.error()
      let error = try TError(serializedBytes: errorData)
      throw error
    }

    return res.pointee
  }
}

public protocol TTask {
  func name() -> TLabel

  var isInit: Bool { get }

  func produces() -> [TLabel]
  func compute(_ ti: TTaskInterface, ctx: TTaskContext, inputs: TTaskInputs) throws -> TTaskNextState
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

  public func produces() -> [TLabel] {
    return arts
  }

  open func compute(_ ti: TTaskInterface, ctx: TTaskContext, inputs: TTaskInputs) throws -> TTaskNextState {
    throw TClientError.unimplemented
  }
}

public extension TTask {
  var isInit: Bool {
    return false
  }
}

extension TTask {
  func extTask() throws -> tritium.core.ExtTask {
    var task = tritium.core.ExtTask()

    let namebytes = try name().serializedData()
    task.name = std.string(fromData: namebytes)

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

    task.computeFn = { ctx, eti, tcp, tip, tnsp in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TTask

      let ti = TTaskInterface(eti)

      do {
        let tctx = try TTaskContext(serializedBytes: tcp.pointee)
        let inputs = try TTaskInputs(serializedBytes: tip.pointee)
        let ns = try sp.compute(ti, ctx: tctx, inputs: inputs)

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
            $0.code = tritium.core.Unknown.rawValue
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

extension TRule {
  func extRule() throws -> tritium.core.ExtRule {
    var rule = tritium.core.ExtRule()

    let namebytes = try name().serializedData()
    rule.name = std.string(fromData: namebytes)

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

  public init(rules: [TLabel], artifacts: [TLabel]) {
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

extension TRuleProvider {
  var extRuleProvider: tritium.core.ExtRuleProvider {
    var rp = tritium.core.ExtRuleProvider()
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
  func extEngineConfig() throws -> tritium.core.ExtEngineConfig {
    var extcfg = tritium.core.ExtEngineConfig()
    if let initRule = self.initRule {
      let bytes = try initRule.serializedData()
      extcfg.setInitRule(std.string(fromData: bytes))
    }
    return extcfg
  }
}

public class TEngine {
  private var eng: tritium.core.EngineRef

  public init (config: TEngineConfig = TEngineConfig(), baseRuleProvider: TRuleProvider) throws {

    eng = tritium.core.makeEngine(try config.extEngineConfig(), baseRuleProvider.extRuleProvider)
  }

  public func build(_ lbl: TLabel) async throws -> TArtifact {
    var build = eng.build(try lbl.tritiumCoreSerialized())

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
