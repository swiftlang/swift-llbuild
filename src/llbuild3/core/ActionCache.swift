//===- ActionCache.swift --------------------------------------*- Swift -*-===//
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

public protocol TActionCache {
  func get(key: TCacheKey) async throws -> TCacheValue?
  func update(key: TCacheKey, value: TCacheValue) async
}

extension TActionCache {
  func extActionCache() -> llbuild3.core.ExtActionCache {
    var extcache = llbuild3.core.ExtActionCache()
    extcache.ctx = Unmanaged.passRetained(self as AnyObject).toOpaque()

    extcache.getFn = { ctx, kpb, handler in
      let key: TCacheKey
      do {
        key = try TCacheKey(serializedBytes: kpb)
      } catch {
        let err = TError.with {
          $0.type = .engine
          $0.code = llbuild3.core.InternalProtobufSerialization.rawValue
          $0.description_p = "cache key get"
        }
        guard let bytes = try? err.serializedData() else {
          return
        }
        handler(std.string(), std.string(fromData: bytes))
        return
      }

      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TActionCache
      Task {
        do {
          if let value = try await sp.get(key: key) {
            let bytes = try value.serializedData()
            handler(std.string(fromData: bytes), std.string())
            return
          }
        } catch {
          let err: TError
          if let terr = error as? TError {
            err = terr
          } else {
            err = TError.with {
              $0.type = .client
              $0.code = llbuild3.core.Unknown.rawValue
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

    extcache.updateFn = { ctx, kpb, vpb in
      let key: TCacheKey
      let value: TCacheValue
      do {
        key = try TCacheKey(serializedBytes: kpb)
        value = try TCacheValue(serializedBytes: vpb)
      } catch {
        return
      }

      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TActionCache
      Task {
        await sp.update(key: key, value: value)
      }
    }

    return extcache;
  }
}
