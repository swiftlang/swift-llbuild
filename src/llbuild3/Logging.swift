//===- Logging.swift ------------------------------------------*- Swift -*-===//
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

import Foundation

open class TClientContext {
  public init() { }
}

public struct TLoggingContext {
  public var engineID: UUID?
  public var clientContext: TClientContext?
}

public protocol TLogger {
  func error(_ err: TError, _ ctx: TLoggingContext)
  func event(_ stats: [TStat], _ ctx: TLoggingContext)
}

extension TLogger {
  var extLogger: llbuild3.ExtLogger {
    var el = llbuild3.ExtLogger()

    el.ctx = Unmanaged.passRetained(self as AnyObject).toOpaque()

    el.releaseFn = { ctx in
      let _ = Unmanaged<AnyObject>.fromOpaque(ctx!).takeRetainedValue()
    }

    el.errorFn = { ctx, lctx, epb in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TLogger

      var tctx = TLoggingContext()
      if lctx.engineID.size() == 16 {
        lctx.engineID.withUnsafeBytes { bp in
          tctx.engineID = bp.load(as: UUID.self)
        }
      }
      guard let err = try? TError(serializedBytes: epb) else {
        return
      }
      if let clientContext = lctx.ctx {
        tctx.clientContext = Unmanaged.fromOpaque(clientContext).takeUnretainedValue() as TClientContext
      }

      sp.error(err, tctx)
    }

    el.eventFn = { ctx, lctx, psv in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TLogger

      var tctx = TLoggingContext()
      if lctx.engineID.size() == 16 {
        lctx.engineID.withUnsafeBytes { bp in
          tctx.engineID = bp.load(as: UUID.self)
        }
      }
      if let clientContext = lctx.ctx {
        tctx.clientContext = Unmanaged.fromOpaque(clientContext).takeUnretainedValue() as TClientContext
      }

      let sv = psv.pointee

      do {
        sp.event(try sv.map { try TStat(serializedBytes: $0) }, tctx)
      } catch {
        return
      }
    }

    return el
  }
}
