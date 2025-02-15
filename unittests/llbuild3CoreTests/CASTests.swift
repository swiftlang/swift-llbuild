//===- CASTests.swift -----------------------------------------*- Swift -*-===//
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

import CryptoKit
import SwiftProtobuf
import llbuild3
import XCTest

final actor InMemoryCASDatabase: TCASDatabase {

  var content = [TCASObjectID: TCASObject]()

  func contains(_ id: TCASObjectID) async throws -> Bool {
    return content.keys.contains(id)
  }

  public func get(_ id: TCASObjectID) async throws -> TCASObject? {
    return content[id]
  }

  nonisolated public func identify(_ obj: TCASObject) throws -> TCASObjectID {
    return try calcIDForObject(obj)
  }

  public func put(_ obj: TCASObject) async throws -> TCASObjectID {
    let id = try calcIDForObject(obj)
    content[id] = obj
    return id
  }


  nonisolated func calcIDForObject(_ obj: TCASObject) throws -> TCASObjectID {
    let bytes = try obj.serializedData()
    let hash = SHA256.hash(data: bytes)
    return TCASObjectID.with { $0.bytes = Data(hash) }
  }
}


final class CASTests: XCTestCase {

  func testAdaptedSwiftInMemory() async throws {
    let sdb = InMemoryCASDatabase()
    let tdbref = llbuild3.makeExtCASDatabase(sdb.extCASDatabase)
    let tdb = tdbref.asTCASDatabase

    let data = Data("a string".utf8)

    let id = try await sdb.put(TCASObject.with { $0.data = data })
    let obj = try await tdb.get(id)

    XCTAssertEqual(obj?.data, data)

    let data2 = Data("b string".utf8)
    let id2 = try await tdb.put(TCASObject.with { $0.data = data2 })
    let obj2 = try await sdb.get(id2)

    XCTAssertEqual(obj2?.data, data2)
  }

  func testAdaptedNativeInMemory() async throws {
    let nref = llbuild3.makeInMemoryCASDatabase()

    let sdb = nref.asTCASDatabase
    let tdbref = llbuild3.makeExtCASDatabase(sdb.extCASDatabase)
    let tdb = tdbref.asTCASDatabase

    let data = Data("a string".utf8)

    let id = try await sdb.put(TCASObject.with { $0.data = data })
    let obj = try await tdb.get(id)

    XCTAssertEqual(obj?.data, data)

    let data2 = Data("b string".utf8)
    let id2 = try await tdb.put(TCASObject.with { $0.data = data2 })
    let obj2 = try await sdb.get(id2)

    XCTAssertEqual(obj2?.data, data2)
  }
}
