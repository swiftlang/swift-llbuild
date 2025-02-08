//===- CAS.swift ----------------------------------------------*- Swift -*-===//
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

public protocol TCASDatabase {
  /// Check if the database contains the given `id`.
  func contains(_ id: TCASObjectID) async throws -> Bool

  /// Get the object corresponding to the given `id`.
  ///
  /// - Parameters:
  ///   - id: The id of the object to look up
  /// - Returns: The object, or nil if not present in the database.
  func get(_ id: TCASObjectID) async throws -> TCASObject?

  /// Calculate the DataID for the given CAS object.
  ///
  /// The implementation *MUST* return a valid content-address, such
  /// that a subsequent call to `put(...` will return an identical
  /// `identify`. This method should be implemented as efficiently as possible,
  /// ideally locally.
  ///
  /// NOTE: The implementations *MAY* store the content, as if it were `put`.
  /// Clients *MAY NOT* assume the data has been written.
  ///
  ///
  /// - Parameters:
  ///    - refs: The list of objects references.
  ///    - data: The object contents.
  /// - Returns: The id representing the combination of contents and refs.
  func identify(_ obj: TCASObject) throws -> TCASObjectID

  /// Store an object.
  ///
  /// - Parameters:
  ///    - refs: The list of objects references.
  ///    - data: The object contents.
  /// - Returns: The id representing the combination of contents and refs.
  func put(_ obj: TCASObject) async throws -> TCASObjectID
}

class AdaptedCASDatabase: TCASDatabase {
  let db: llbuild3.CASDatabaseRef

  init(db: llbuild3.CASDatabaseRef) {
    self.db = db
  }

  /// Check if the database contains the given `id`.
  func contains(_ id: TCASObjectID) async throws -> Bool {
    let sid = std.string(fromData: id.bytes)
    return try await withCheckedThrowingContinuation { continuation in
      let ctx = Unmanaged.passRetained(continuation as AnyObject).toOpaque()
      llbuild3.adaptedCASDatabaseContains(db, sid, ctx, { ctx, result in
        let completion = Unmanaged<AnyObject>.fromOpaque(ctx!).takeRetainedValue() as! CheckedContinuation<Bool, any Error>
        if result.pointee.has_error() {
          do {
            let errorData = result.pointee.error()
            let error = try TError(serializedBytes: errorData)
            completion.resume(throwing: error)
          } catch {
            completion.resume(throwing: error)
          }
        } else {
          completion.resume(returning: result.pointee.pointee)
        }
      })
    }
  }

  /// Get the object corresponding to the given `id`.
  ///
  /// - Parameters:
  ///   - id: The id of the object to look up
  /// - Returns: The object, or nil if not present in the database.
  func get(_ id: TCASObjectID) async throws -> TCASObject? {
    let sid = std.string(fromData: id.bytes)
    return try await withCheckedThrowingContinuation { continuation in
      let ctx = Unmanaged.passRetained(continuation as AnyObject).toOpaque()
      llbuild3.adaptedCASDatabaseGet(db, sid, ctx, { ctx, result in
        let completion = Unmanaged<AnyObject>.fromOpaque(ctx!).takeRetainedValue() as! CheckedContinuation<TCASObject?, any Error>
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
            let objData = result.pointee.pointee
            let obj = try TCASObject(serializedBytes: objData)
            if obj.data.count > 0 {
              completion.resume(returning: obj)
            } else {
              completion.resume(returning: nil)
            }
          } catch {
            completion.resume(throwing: error)
          }
        }
      })
    }
  }

  /// Calculate the DataID for the given CAS object.
  ///
  /// The implementation *MUST* return a valid content-address, such
  /// that a subsequent call to `put(...` will return an identical
  /// `identify`. This method should be implemented as efficiently as possible,
  /// ideally locally.
  ///
  /// NOTE: The implementations *MAY* store the content, as if it were `put`.
  /// Clients *MAY NOT* assume the data has been written.
  ///
  ///
  /// - Parameters:
  ///    - refs: The list of objects references.
  ///    - data: The object contents.
  /// - Returns: The id representing the combination of contents and refs.
  func identify(_ obj: TCASObject) throws -> TCASObjectID {
    let sobj = try obj.llbuild3Serialized()
    let idbytes = llbuild3.adaptedCASDatabaseIdentify(db, sobj);
    let casid = TCASObjectID.with { casid in
      idbytes.withUnsafeBytes { bp in
        casid.bytes = Data(buffer: bp.bindMemory(to: CChar.self))
      }
    }
    return casid
  }

  /// Store an object.
  ///
  /// - Parameters:
  ///    - refs: The list of objects references.
  ///    - data: The object contents.
  /// - Returns: The id representing the combination of contents and refs.
  func put(_ obj: TCASObject) async throws -> TCASObjectID {
    let sobj = try obj.llbuild3Serialized()
    return try await withCheckedThrowingContinuation { continuation in
      let ctx = Unmanaged.passRetained(continuation as AnyObject).toOpaque()
      llbuild3.adaptedCASDatabasePut(db, sobj, ctx, { ctx, result in
        let completion = Unmanaged<AnyObject>.fromOpaque(ctx!).takeRetainedValue() as! CheckedContinuation<TCASObjectID, any Error>
        if result.pointee.has_error() {
          do {
            let errorData = result.pointee.error()
            let error = try TError(serializedBytes: errorData)
            completion.resume(throwing: error)
          } catch {
            completion.resume(throwing: error)
          }
        } else {
          let idbytes = result.pointee.pointee
          let casid = TCASObjectID.with { casid in
            idbytes.withUnsafeBytes { bp in
              casid.bytes = Data(buffer: bp.bindMemory(to: CChar.self))
            }
          }
          completion.resume(returning: casid)
        }
      })
    }
  }
}

public extension TCASDatabase {
  var extCASDatabase: llbuild3.ExtCASDatabase {
    var extCASDB = llbuild3.ExtCASDatabase()
    extCASDB.ctx = Unmanaged.passRetained(self as AnyObject).toOpaque()

    extCASDB.containsFn = { ctx, id, handler in
      let casid = TCASObjectID.with { casid in
        id.withUnsafeBytes { bp in
          casid.bytes = Data(buffer: bp.bindMemory(to: CChar.self))
        }
      }

      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TCASDatabase
      Task {
        do {
          handler(try await sp.contains(casid), std.string())
          return
        } catch {
          let err: TError
          if let terr = error as? TError {
            err = terr
          } else {
            err = TError.with {
              $0.type = .client
              $0.code = llbuild3.CASError.Unknown.rawValue
              $0.description_p = "\(error)"
            }
          }
          guard let bytes = try? err.serializedData() else {
            handler(false, std.string("failed error serialization"))
            return
          }

          handler(false, std.string(fromData: bytes))
        }
      }
    }

    extCASDB.getFn = { ctx, id, handler in
      let casid = TCASObjectID.with { casid in
        id.withUnsafeBytes { bp in
          casid.bytes = Data(buffer: bp.bindMemory(to: CChar.self))
        }
      }

      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TCASDatabase
      Task {
        do {
          if let obj = try await sp.get(casid) {
            guard let bytes = try? obj.serializedData() else {
              handler(std.string(), std.string("failed error serialization"))
              return
            }

            handler(std.string(fromData: bytes), std.string())
            return
          }

          let err = TError.with {
            $0.type = .cas
            $0.code = llbuild3.CASError.ObjectNotFound.rawValue
          }
          guard let bytes = try? err.serializedData() else {
            handler(std.string(), std.string("failed error serialization"))
            return
          }

          handler(std.string(), std.string(fromData: bytes))
          return
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

    extCASDB.putFn = { ctx, opb, handler in
      let obj: TCASObject
      do {
        obj = try TCASObject(serializedBytes: opb)
      } catch {
        let err = TError.with {
          $0.type = .engine
          $0.code = llbuild3.EngineError.InternalProtobufSerialization.rawValue
          $0.description_p = "cas put"
        }
        guard let bytes = try? err.serializedData() else {
          return
        }
        handler(std.string(), std.string(fromData: bytes))
        return
      }


      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TCASDatabase
      Task {
        do {
          let casid = try await sp.put(obj)
          handler(std.string(fromData: casid.bytes), std.string())
          return
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

    extCASDB.identifyFn = { ctx, opb in
      let obj: TCASObject
      do {
        obj = try TCASObject(serializedBytes: opb)
      } catch {
        // FIXME: propagate error
        return std.string()
      }

      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TCASDatabase
      do {
        let casid = try sp.identify(obj)
        return std.string(fromData: casid.bytes)
      } catch {
        // FIXME: propagate error
        return std.string()
      }
    }

    return extCASDB;
  }
}

public extension llbuild3.CASDatabaseRef {
  var asTCASDatabase: TCASDatabase {
    if let ctx = llbuild3.getRawCASDatabaseContext(self),
       let sp = Unmanaged<AnyObject>.fromOpaque(ctx).takeRetainedValue() as? TCASDatabase {
      return sp
    }

    return AdaptedCASDatabase(db: self)
  }
}
