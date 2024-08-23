//===- CoreTypes.swift ----------------------------------------*- Swift -*-===//
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

// ActionCache.proto
public typealias TKeyType = Tritium_Core_KeyType

// Artifact.proto
public typealias TArtifactType = Tritium_Core_ArtifactType
public typealias TDictObject = Tritium_Core_DictObject
public typealias TListObject = Tritium_Core_ListObject
public typealias TArtifact = Tritium_Core_Artifact

// Label.proto
public typealias TLabel = Tritium_Core_Label

// Rule.proto
public typealias TSignature = Tritium_Core_Signature
public typealias TTaskResult = Tritium_Core_TaskResult
public typealias TActionResult = Tritium_Core_ActionResult
public typealias TTaskInput = Tritium_Core_TaskInput
public typealias TTaskInputs = Tritium_Core_TaskInputs
public typealias TTaskContext = Tritium_Core_TaskContext
public typealias TTaskWait = Tritium_Core_TaskWait
public typealias TTaskNextState = Tritium_Core_TaskNextState

// SwiftProtobuf Extensions
public extension SwiftProtobuf.Message {
  func tritiumCoreSerialized() throws -> std.string {
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
}

