//===- ActionExecutor.swift -----------------------------------*- Swift -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2025 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//


public struct TActionDescriptor {
  var name: TLabel
  var platform: TPlatform
  var executable: String

  public init(name: TLabel, platform: TPlatform, executable: String) {
    self.name = name
    self.platform = platform
    self.executable = executable
  }
}

public protocol TActionProvider {
  func prefixes() -> [TLabel]
  func resolve(_ lbl: TLabel) throws -> TLabel?
  func actionDescriptor( _ lbl: TLabel) throws -> TActionDescriptor?
}

extension TActionProvider {
  var extActionProvider: llbuild3.ExtActionProvider {
    var p = llbuild3.ExtActionProvider()

    p.ctx = Unmanaged.passRetained(self as AnyObject).toOpaque()

    p.releaseFn = { ctx in
      let _ = Unmanaged<AnyObject>.fromOpaque(ctx!).takeRetainedValue()
    }

    p.prefixesFn = { ctx, lblp in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TActionProvider
      var lblvector = lblp.pointee
      do {
        for rn in sp.prefixes() {
          let bytes = try rn.serializedData()
          lblvector.push_back(std.string(fromData: bytes))
        }
      } catch {
        return
      }
      lblp?.update(from: &lblvector, count: 1)
    }

    p.resolveFn = { ctx, lblpb, errp in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TActionProvider
      do {
        let lbl = try TLabel(serializedBytes: lblpb)
        guard let resolved = try sp.resolve(lbl) else {
          throw TError.with {
            $0.type = .executor
            $0.code = llbuild3.ExecutorError.NoProvider.rawValue
            $0.description_p = "resolve failed"
          }
        }

        return std.string(fromData: try resolved.serializedData())
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
          var serr = std.string("failed error serialization")
          errp?.update(from: &serr, count: 1)
          return std.string()
        }

        var serr = std.string(fromData: bytes)
        errp?.update(from: &serr, count: 1)
        return std.string()
      }
    }

    p.descriptorFn = { ctx, lblpb, errp in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TActionProvider
      do {
        let lbl = try TLabel(serializedBytes: lblpb)
        guard let desc = try sp.actionDescriptor(lbl) else {
          throw TError.with {
            $0.type = .executor
            $0.code = llbuild3.ExecutorError.NoProvider.rawValue
            $0.description_p = "action descriptor failed"
          }
        }

        var edesc = llbuild3.ExtActionDescriptor()
        edesc.name = std.string(try desc.name.serializedData())
        edesc.platform = std.string(try desc.platform.serializedData())
        edesc.executable = std.string(desc.executable)

        return edesc
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
          var serr = std.string("failed error serialization")
          errp?.update(from: &serr, count: 1)
          return llbuild3.ExtActionDescriptor()
        }

        var serr = std.string(fromData: bytes)
        errp?.update(from: &serr, count: 1)
        return llbuild3.ExtActionDescriptor()
      }
    }

    return p
  }
}

public class TExecutor {
  let executor: llbuild3.ActionExecutorRef

  convenience public init(casDB: TCASDatabase, actionCache: TActionCache? = nil, sandboxProvider: TLocalSandboxProvider, remoteExecutor: TRemoteExecutor? = nil, logger: TLogger? = nil) {
    let tcas = llbuild3.makeExtCASDatabase(casDB.extCASDatabase)
    let lsp = llbuild3.makeExtLocalSandboxProvider(sandboxProvider.extLocalSandboxProvider)

    let tcache: llbuild3.ActionCacheRef
    if let cache = actionCache {
      tcache = llbuild3.makeExtActionCache(cache.extActionCache())
    } else {
      tcache = llbuild3.ActionCacheRef()
    }

    let tremoteexec: llbuild3.RemoteExecutorRef
    if let remoteexec = remoteExecutor {
      tremoteexec = llbuild3.makeRemoteExecutor(remoteexec.extRemoteExecutor)
    } else {
      tremoteexec = llbuild3.RemoteExecutorRef()
    }

    let tlogger: llbuild3.LoggerRef
    if let logger = logger {
      tlogger = llbuild3.makeExtLogger(logger.extLogger)
    } else {
      tlogger = llbuild3.LoggerRef()
    }

    self.init(casDB: tcas, actionCache: tcache, sandboxProvider: lsp, remoteExecutor: tremoteexec, logger: tlogger)
  }

  convenience public init(casDB: llbuild3.CASDatabaseRef, actionCache: llbuild3.ActionCacheRef? = nil, sandboxProvider: TLocalSandboxProvider, remoteExecutor: llbuild3.RemoteExecutorRef? = nil, logger: TLogger? = nil) {
    let lsp = llbuild3.makeExtLocalSandboxProvider(sandboxProvider.extLocalSandboxProvider)

    let tcache = actionCache ?? llbuild3.ActionCacheRef()
    let tremoteexec = remoteExecutor ?? llbuild3.RemoteExecutorRef()

    let tlogger: llbuild3.LoggerRef
    if let logger = logger {
      tlogger = llbuild3.makeExtLogger(logger.extLogger)
    } else {
      tlogger = llbuild3.LoggerRef()
    }

    self.init(casDB: casDB, actionCache: tcache, sandboxProvider: lsp, remoteExecutor: tremoteexec, logger: tlogger)
  }

  public init(casDB: llbuild3.CASDatabaseRef, actionCache: llbuild3.ActionCacheRef, sandboxProvider: llbuild3.LocalSandboxProviderRef, remoteExecutor: llbuild3.RemoteExecutorRef, logger: llbuild3.LoggerRef) {
    let execlocal = llbuild3.makeLocalExecutor(sandboxProvider)
    executor = llbuild3.makeActionExecutor(casDB, actionCache, execlocal, remoteExecutor, logger)
  }

  public func registerProvider(_ provider: TActionProvider) throws {
    let p = llbuild3.makeExtActionProvider(provider.extActionProvider)
    let err = llbuild3.registerProviderWithExecutor(executor, p)

    if err.size() > 0 {
      throw try TError(serializedBytes: err)
    }
  }
}
