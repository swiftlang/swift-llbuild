//===- Types.swift ---0----------------------------------------*- Swift -*-===//
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

// Action.proto
public typealias TActionResult = Llbuild3_ActionResult
public typealias TFileObject = Llbuild3_FileObject

// ActionCache.proto
public typealias TAction = Llbuild3_Action
public typealias TPlatform = Llbuild3_Platform
public typealias TSubprocess = Llbuild3_Subprocess
public typealias TSubprocessResult = Llbuild3_SubprocessResult

// ActionCache.proto
public typealias TCacheKey = Llbuild3_CacheKey
public typealias TCacheValue = Llbuild3_CacheValue

// Artifact.proto
public typealias TArtifactType = Llbuild3_ArtifactType
public typealias TDictObject = Llbuild3_DictObject
public typealias TListObject = Llbuild3_ListObject
public typealias TArtifact = Llbuild3_Artifact

// CAS.proto
public typealias TCASObject = Llbuild3_CASObject
public typealias TCASID = Llbuild3_CASID

// CASTree.proto
public typealias TFileType = Llbuild3_FileType

// Common.proto
public typealias TStat = Llbuild3_Stat

// Engine.proto
public typealias TSignature = Llbuild3_Signature
public typealias TTaskResult = Llbuild3_TaskResult
public typealias TTaskInput = Llbuild3_TaskInput
public typealias TTaskInputs = Llbuild3_TaskInputs
public typealias TTaskContext = Llbuild3_TaskContext
public typealias TTaskWait = Llbuild3_TaskWait
public typealias TTaskNextState = Llbuild3_TaskNextState

// Error.proto

public typealias TErrorType = Llbuild3_ErrorType
public typealias TError = Llbuild3_Error

extension TError: Swift.Error {

}

// Label.proto
public typealias TLabel = Llbuild3_Label

// SwiftProtobuf Extensions
public extension SwiftProtobuf.Message {
  func llbuild3Serialized() throws -> std.string {
    return std.string(fromData: try self.serializedData())
  }
}

// Swift-C++ Interop Extensions
public extension std.string {
  init(fromData data: Data) {
    self.init()
    self.reserve(data.count)
    for char in data {
      self.push_back(value_type(bitPattern: char))
    }
  }
}

extension std.string: ContiguousBytes, SwiftProtobufContiguousBytes {
  public init(repeating char: UInt8, count: Int) {
    self.init()
    self.reserve(count)
    for _ in 1...count {
      self.push_back(value_type(bitPattern: char))
    }
  }

  public init<S>(_ sequence: S) where S : Sequence, S.Element == UInt8 {
    self.init()
    for c in sequence {
      self.push_back(value_type(bitPattern: c))
    }
  }

  public mutating func withUnsafeMutableBytes<R>(_ body: (UnsafeMutableRawBufferPointer) throws -> R) rethrows -> R {
    try body(UnsafeMutableRawBufferPointer(start:self.__dataMutatingUnsafe(), count: self.size()))
  }

  borrowing public func withUnsafeBytes<R>(_ body: (UnsafeRawBufferPointer) throws -> R) rethrows -> R {
    try body(UnsafeRawBufferPointer(start:self.__dataUnsafe(), count: self.size()))
  }

  public var utf8String: String {
    return withUnsafeBytes {
      return String(decoding: Data(buffer: $0.bindMemory(to: CChar.self)), as: UTF8.self)
    }
  }
}
