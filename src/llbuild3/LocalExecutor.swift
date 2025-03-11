//===- LocalExecutor.swift ------------------------------------*- Swift -*-===//
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
import System

public protocol TLocalSandbox {
  func workingDir() -> String
  func environment() -> [String: String]
  func prepareInput(_ path: String, type: TFileType, id: TCASID) throws
  func collectOutputs(_ paths: [String]) throws -> [TFileObject]
  func release()
}

public protocol TLocalSandboxProvider {
  func create(_ sandboxID: UInt64) throws -> TLocalSandbox
}

extension TLocalSandbox {
  var extLocalSandbox: llbuild3.ExtLocalSandbox {
    var ls = llbuild3.ExtLocalSandbox()

    ls.ctx = Unmanaged.passRetained(self as AnyObject).toOpaque()

    ls.releaseFn = { ctx in
      let _ = Unmanaged<AnyObject>.fromOpaque(ctx!).takeRetainedValue()
    }

    ls.dirFn = { ctx, dirp in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TLocalSandbox
      var path = std.string(sp.workingDir())
      dirp?.update(from: &path, count: 1)
    }

    ls.envFn = { ctx, envp in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TLocalSandbox
      var oenv = envp.pointee

      let env = sp.environment();
      for (k, v) in env {
        oenv.push_back(llbuild3.makeStringPair(std.string(k), std.string(v)))
      }

      envp?.update(from: &oenv, count: 1)
    }

    ls.prepareInputFn = { ctx, path, type, idbytes, errp in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TLocalSandbox

      let objID = TCASID.with { casid in
        idbytes.pointee.withUnsafeBytes { bp in
          casid.bytes = Data(buffer: bp.bindMemory(to: CChar.self))
        }
      }

      guard let ftype = TFileType(rawValue: Int(type)) else {
        let err = TError.with {
          $0.type = .client
          $0.code = llbuild3.ExecutorError.Unknown.rawValue
          $0.description_p = "invalid filetype \(type)"
        }
        guard let bytes = try? err.serializedData() else {
          var error = std.string(fromData: Data("serialization failed".utf8))
          errp?.update(from: &error, count: 1)
          return
        }
        var error = std.string(fromData: bytes)
        errp?.update(from: &error, count: 1)
        return
      }

      do {
        try sp.prepareInput(path.pointee.utf8String, type: ftype, id: objID)
        return
      } catch {
        let err = TError.with {
          $0.type = .client
          $0.code = llbuild3.ExecutorError.Unknown.rawValue
          $0.description_p = "\(error)"
        }
        guard let bytes = try? err.serializedData() else {
          var error = std.string(fromData: Data("serialization failed".utf8))
          errp?.update(from: &error, count: 1)
          return
        }
        var error = std.string(fromData: bytes)
        errp?.update(from: &error, count: 1)
        return
      }
    }

    ls.collectOutputsFn = { ctx, paths, rp, errp in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TLocalSandbox

      let spaths: [String] = paths.map { $0.utf8String }
      do {
        var fovector = rp.pointee
        let fov = try sp.collectOutputs(spaths)
        for fo in fov {
          let bytes = try fo.serializedData()
          fovector.push_back(std.string(fromData: bytes))
        }
        rp?.update(from: &fovector, count: 1)
        return
      } catch {
        let err = TError.with {
          $0.type = .client
          $0.code = llbuild3.ExecutorError.Unknown.rawValue
          $0.description_p = "\(error)"
        }
        guard let bytes = try? err.serializedData() else {
          var error = std.string(fromData: Data("serialization failed".utf8))
          errp?.update(from: &error, count: 1)
          return
        }
        var error = std.string(fromData: bytes)
        errp?.update(from: &error, count: 1)
        return
      }
    }

    ls.releaseSandboxFn = { ctx in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TLocalSandbox
      sp.release()
    }

    return ls
  }
}

extension TLocalSandboxProvider {
  var extLocalSandboxProvider: llbuild3.ExtLocalSandboxProvider {
    var lsp = llbuild3.ExtLocalSandboxProvider()

    lsp.ctx = Unmanaged.passRetained(self as AnyObject).toOpaque()

    lsp.releaseFn = { ctx in
      let _ = Unmanaged<AnyObject>.fromOpaque(ctx!).takeRetainedValue()
    }

    lsp.createFn = { ctx, sid, errp in
      let sp = Unmanaged<AnyObject>.fromOpaque(ctx!).takeUnretainedValue() as! TLocalSandboxProvider

      do {
        let ls = try sp.create(sid)
        return ls.extLocalSandbox
      } catch {
        let err = TError.with {
          $0.type = .client
          $0.code = llbuild3.ExecutorError.Unknown.rawValue
          $0.description_p = "\(error)"
        }
        guard let bytes = try? err.serializedData() else {
          var error = std.string("serialization failed")
          errp?.update(from: &error, count: 1)
          return llbuild3.ExtLocalSandbox()
        }
        var error = std.string(fromData: bytes)
        errp?.update(from: &error, count: 1)
        return llbuild3.ExtLocalSandbox()
      }
    }

    return lsp
  }
}


fileprivate func determineTempDirectory() -> FilePath {
  let env = ProcessInfo.processInfo.environment
  let override = env["TMPDIR"] ?? env["TEMP"] ?? env["TMP"]
  return FilePath(stringLiteral: override ?? NSTemporaryDirectory())
}

fileprivate class Box<T> {
  enum Error: Swift.Error {
    case incompleteOperation
  }

  var value: Result<T, Swift.Error>?
}

fileprivate func synchronous<T>(priority: TaskPriority? = nil, operation: @escaping @Sendable () async throws -> T) throws -> T {
  let semaphore = DispatchSemaphore(value: 0)
  let box = Box<T>()

  Task(priority: priority) {
    defer { semaphore.signal() }
    do {
      box.value = .success(try await operation())
    } catch {
      box.value = .failure(error)
    }
  }

  semaphore.wait()

  guard let value = box.value else {
    throw Box<T>.Error.incompleteOperation
  }
  return try value.get()
}


public enum TTempDirSandboxError: Error {
  case notFound
  case ioError
  case badFileType
  case unimplemented
  case complexBlobUnsupported
}

class TTempDirSandbox: TLocalSandbox {
  let casDB: TCASDatabase
  let workDir: FilePath

  public init(basedir: String, sandboxID: UInt64, casDB: TCASDatabase) throws {
    self.casDB = casDB

    // make sandbox temp dir
    var tempDir = determineTempDirectory()
    tempDir.append(basedir)
    tempDir.append("task-\(sandboxID)")
    try FileManager.default.createDirectory(atPath: tempDir.string,
                                            withIntermediateDirectories: true)
    workDir = tempDir
  }

  public func workingDir() -> String {
    return workDir.string
  }

  public func environment() -> [String: String] {
    return [:]
  }

  public func prepareInput(_ path: String, type: TFileType, id: TCASID) throws {
    let inputPath = workDir.appending(path).string

    switch type {
    case .plainFile, .executable:
      let fileData: Data = try synchronous {
        guard let obj = try await self.casDB.get(id) else {
          throw TTempDirSandboxError.notFound
        }
        if obj.refs.count > 0 {
          throw TTempDirSandboxError.complexBlobUnsupported
        }
        return obj.data
      }

      let attrs: [FileAttributeKey : Any] = [.posixPermissions: 0x755]
      guard FileManager.default.createFile(atPath: inputPath, contents: fileData, attributes: attrs) else {
        throw TTempDirSandboxError.notFound
      }

    case .directory:
      // FIXME: directories unimplemented
      throw TTempDirSandboxError.unimplemented

    case .symlink:
      let fileData: Data = try synchronous {
        guard let obj = try await self.casDB.get(id) else {
          throw TTempDirSandboxError.notFound
        }
        if obj.refs.count > 0 {
          throw TTempDirSandboxError.complexBlobUnsupported
        }
        return obj.data
      }

      let linkTarget = String(decoding: fileData, as: UTF8.self)
      try FileManager.default.createSymbolicLink(atPath: inputPath, withDestinationPath: linkTarget)

    default:
      throw TTempDirSandboxError.badFileType
    }
  }

  public func collectOutputs(_ paths: [String]) throws -> [TFileObject] {
    var fos: [TFileObject] = []
    for path in paths {
      let apath = workDir.appending(path).string
      guard let attr = try? FileManager.default.attributesOfItem(atPath: apath) else {
        continue
      }

      switch attr[.type] as? FileAttributeType {
      case .typeRegular:
        let executable = FileManager.default.isExecutableFile(atPath: apath)

        guard let data = FileManager.default.contents(atPath: apath) else {
          throw TTempDirSandboxError.ioError
        }

        let obj = TCASObject.with {
          $0.data = data
        }

        let casID = try synchronous { try await self.casDB.put(obj) }

        fos.append(TFileObject.with {
          $0.path = path
          $0.type = executable ? .executable : .plainFile
          $0.object = casID
        })

      case .typeDirectory:
        // FIXME: directories unimplemented
        throw TTempDirSandboxError.unimplemented

      case .typeSymbolicLink:
        guard let target = try? FileManager.default.destinationOfSymbolicLink(atPath: apath) else {
          throw TTempDirSandboxError.ioError
        }

        let obj = TCASObject.with {
          $0.data = Data(target.utf8)
        }

        let casID = try synchronous { try await self.casDB.put(obj) }

        fos.append(TFileObject.with {
          $0.path = path
          $0.type = .symlink
          $0.object = casID
        })

      default:
        continue
      }
    }
    return fos
  }

  public func release() {
    _ = try? FileManager.default.removeItem(atPath: workDir.string)
  }
}

public class TTempDirSandboxProvider: TLocalSandboxProvider {
  let basedir: String
  let casDB: TCASDatabase

  public init(basedir: String, casDB: TCASDatabase) {
    self.basedir = basedir
    self.casDB = casDB
  }

  public func create(_ sandboxID: UInt64) throws -> TLocalSandbox {
    return try TTempDirSandbox(basedir: basedir, sandboxID: sandboxID, casDB: casDB)
  }
}
