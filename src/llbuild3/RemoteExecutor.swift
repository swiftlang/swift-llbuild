//===- RemoteExecutor.swift -----------------------------------*- Swift -*-===//
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

public protocol TRemoteExecutor {
  var builtinExecutable: String { get }

  func prepare(_ path: String) async throws -> TCASID
  func execute(_ f: TCASID, action: TAction,
               dispatched: @escaping (Result<UUID, Error>) -> Void,
               result: @escaping (Result<TActionResult, Error>) -> Void) async
}

extension TRemoteExecutor {
  var extRemoteExecutor: llbuild3.ExtRemoteExecutor {
    var re = llbuild3.ExtRemoteExecutor()

    re.ctx = Unmanaged.passRetained(self as AnyObject).toOpaque()

    re.builtinExecutable = std.string(self.builtinExecutable)

    re.releaseFn = { ctx in
      let _ = Unmanaged<AnyObject>.fromOpaque(ctx!).takeRetainedValue()
    }

    re.prepareFn = { ctx, path, handler in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TRemoteExecutor

      Task {
        do {
          let id = try await sp.prepare(path.utf8String)
          handler(std.string(fromData: id.bytes), std.string())
        } catch {
          let err: TError
          if let terr = error as? TError {
            err = terr
          } else {
            err = TError.with {
              $0.type = .cas
              $0.code = llbuild3.CASError.Unknown.rawValue
              $0.description_p = "\(error)"
            }
          }
          guard let bytes = try? err.serializedData() else {
            handler(std.string(), std.string("failed error serialization"))
            return
          }

          handler(std.string(), std.string(fromData: bytes))
        }
      }
    }

    re.executeFn = { ctx, fidbytes, apb, dispatchedHandler, resultHandler in
      let casid = TCASID.with { casid in
        fidbytes.withUnsafeBytes { bp in
          casid.bytes = Data(buffer: bp.bindMemory(to: CChar.self))
        }
      }

      let act: TAction
      do {
        act = try TAction(serializedBytes: apb)
      } catch {
        let err = TError.with {
          $0.type = .engine
          $0.code = llbuild3.EngineError.InternalProtobufSerialization.rawValue
          $0.description_p = "remote executor execute"
        }
        guard let bytes = try? err.serializedData() else {
          dispatchedHandler(std.string(), std.string("serialized data failure"))
          return
        }
        dispatchedHandler(std.string(), std.string(fromData: bytes))
        return
      }


      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TRemoteExecutor
      Task {
        await sp.execute(casid, action: act, dispatched: { res in
          do {
            let r = try res.get()
            var tid = std.string();
            tid.push_back(std.string.value_type(bitPattern: r.uuid.0))
            tid.push_back(std.string.value_type(bitPattern: r.uuid.1))
            tid.push_back(std.string.value_type(bitPattern: r.uuid.2))
            tid.push_back(std.string.value_type(bitPattern: r.uuid.3))
            tid.push_back(std.string.value_type(bitPattern: r.uuid.4))
            tid.push_back(std.string.value_type(bitPattern: r.uuid.5))
            tid.push_back(std.string.value_type(bitPattern: r.uuid.6))
            tid.push_back(std.string.value_type(bitPattern: r.uuid.7))
            tid.push_back(std.string.value_type(bitPattern: r.uuid.8))
            tid.push_back(std.string.value_type(bitPattern: r.uuid.9))
            tid.push_back(std.string.value_type(bitPattern: r.uuid.10))
            tid.push_back(std.string.value_type(bitPattern: r.uuid.11))
            tid.push_back(std.string.value_type(bitPattern: r.uuid.12))
            tid.push_back(std.string.value_type(bitPattern: r.uuid.13))
            tid.push_back(std.string.value_type(bitPattern: r.uuid.14))
            tid.push_back(std.string.value_type(bitPattern: r.uuid.15))
            dispatchedHandler(tid, std.string())
          } catch {
            let err = TError.with {
              $0.type = .engine
              $0.code = llbuild3.EngineError.InternalProtobufSerialization.rawValue
              $0.description_p = "remote executor execute"
            }
            guard let bytes = try? err.serializedData() else {
              dispatchedHandler(std.string(), std.string("serialized data failure"))
              return
            }
            dispatchedHandler(std.string(), std.string(fromData: bytes))
            return
          }
        }, result: { res in
          do {
            let r = try res.get()
            guard let bytes = try? r.serializedData() else {
              resultHandler(std.string(), std.string("failed data serialization"))
              return
            }
            resultHandler(std.string(fromData: bytes), std.string())
          } catch {
            let err = TError.with {
              $0.type = .engine
              $0.code = llbuild3.EngineError.InternalProtobufSerialization.rawValue
              $0.description_p = "remote executor execute"
            }
            guard let bytes = try? err.serializedData() else {
              resultHandler(std.string(), std.string("serialized data failure"))
              return
            }
            resultHandler(std.string(), std.string(fromData: bytes))
            return
          }

        })
      }
    }

    return re
  }
}
