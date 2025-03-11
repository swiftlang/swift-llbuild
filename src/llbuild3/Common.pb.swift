// DO NOT EDIT.
// swift-format-ignore-file
//
// Generated by the Swift generator plugin for the protocol buffer compiler.
// Source: llbuild3/Common.proto
//
// For information on using the generated types, please see the documentation:
//   https://github.com/apple/swift-protobuf/

// This source file is part of the Swift.org open source project
//
// Copyright (c) 2024-2025 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors

import Foundation
import SwiftProtobuf

// If the compiler emits an error on this type, it is because this file
// was generated by a version of the `protoc` Swift plug-in that is
// incompatible with the version of SwiftProtobuf to which you are linking.
// Please ensure that you are building against the same version of the API
// that was used to generate this file.
fileprivate struct _GeneratedWithProtocGenSwiftVersion: SwiftProtobuf.ProtobufAPIVersionCheck {
  struct _2: SwiftProtobuf.ProtobufAPIVersion_2 {}
  typealias Version = _2
}

public struct Llbuild3_Stat: Sendable {
  // SwiftProtobuf.Message conformance is added in an extension below. See the
  // `Message` and `Message+*Additions` files in the SwiftProtobuf library for
  // methods supported on all messages.

  public var name: String = String()

  public var value: Llbuild3_Stat.OneOf_Value? = nil

  public var intValue: Int64 {
    get {
      if case .intValue(let v)? = value {return v}
      return 0
    }
    set {value = .intValue(newValue)}
  }

  public var uintValue: UInt64 {
    get {
      if case .uintValue(let v)? = value {return v}
      return 0
    }
    set {value = .uintValue(newValue)}
  }

  public var stringValue: String {
    get {
      if case .stringValue(let v)? = value {return v}
      return String()
    }
    set {value = .stringValue(newValue)}
  }

  public var boolValue: Bool {
    get {
      if case .boolValue(let v)? = value {return v}
      return false
    }
    set {value = .boolValue(newValue)}
  }

  public var doubleValue: Double {
    get {
      if case .doubleValue(let v)? = value {return v}
      return 0
    }
    set {value = .doubleValue(newValue)}
  }

  public var casObject: Llbuild3_CASID {
    get {
      if case .casObject(let v)? = value {return v}
      return Llbuild3_CASID()
    }
    set {value = .casObject(newValue)}
  }

  public var errorValue: Llbuild3_Error {
    get {
      if case .errorValue(let v)? = value {return v}
      return Llbuild3_Error()
    }
    set {value = .errorValue(newValue)}
  }

  public var unknownFields = SwiftProtobuf.UnknownStorage()

  public enum OneOf_Value: Equatable, Sendable {
    case intValue(Int64)
    case uintValue(UInt64)
    case stringValue(String)
    case boolValue(Bool)
    case doubleValue(Double)
    case casObject(Llbuild3_CASID)
    case errorValue(Llbuild3_Error)

  }

  public init() {}
}

// MARK: - Code below here is support for the SwiftProtobuf runtime.

fileprivate let _protobuf_package = "llbuild3"

extension Llbuild3_Stat: SwiftProtobuf.Message, SwiftProtobuf._MessageImplementationBase, SwiftProtobuf._ProtoNameProviding {
  public static let protoMessageName: String = _protobuf_package + ".Stat"
  public static let _protobuf_nameMap: SwiftProtobuf._NameMap = [
    1: .same(proto: "name"),
    2: .standard(proto: "int_value"),
    3: .standard(proto: "uint_value"),
    4: .standard(proto: "string_value"),
    5: .standard(proto: "bool_value"),
    6: .standard(proto: "double_value"),
    7: .standard(proto: "cas_object"),
    8: .standard(proto: "error_value"),
  ]

  public mutating func decodeMessage<D: SwiftProtobuf.Decoder>(decoder: inout D) throws {
    while let fieldNumber = try decoder.nextFieldNumber() {
      // The use of inline closures is to circumvent an issue where the compiler
      // allocates stack space for every case branch when no optimizations are
      // enabled. https://github.com/apple/swift-protobuf/issues/1034
      switch fieldNumber {
      case 1: try { try decoder.decodeSingularStringField(value: &self.name) }()
      case 2: try {
        var v: Int64?
        try decoder.decodeSingularInt64Field(value: &v)
        if let v = v {
          if self.value != nil {try decoder.handleConflictingOneOf()}
          self.value = .intValue(v)
        }
      }()
      case 3: try {
        var v: UInt64?
        try decoder.decodeSingularUInt64Field(value: &v)
        if let v = v {
          if self.value != nil {try decoder.handleConflictingOneOf()}
          self.value = .uintValue(v)
        }
      }()
      case 4: try {
        var v: String?
        try decoder.decodeSingularStringField(value: &v)
        if let v = v {
          if self.value != nil {try decoder.handleConflictingOneOf()}
          self.value = .stringValue(v)
        }
      }()
      case 5: try {
        var v: Bool?
        try decoder.decodeSingularBoolField(value: &v)
        if let v = v {
          if self.value != nil {try decoder.handleConflictingOneOf()}
          self.value = .boolValue(v)
        }
      }()
      case 6: try {
        var v: Double?
        try decoder.decodeSingularDoubleField(value: &v)
        if let v = v {
          if self.value != nil {try decoder.handleConflictingOneOf()}
          self.value = .doubleValue(v)
        }
      }()
      case 7: try {
        var v: Llbuild3_CASID?
        var hadOneofValue = false
        if let current = self.value {
          hadOneofValue = true
          if case .casObject(let m) = current {v = m}
        }
        try decoder.decodeSingularMessageField(value: &v)
        if let v = v {
          if hadOneofValue {try decoder.handleConflictingOneOf()}
          self.value = .casObject(v)
        }
      }()
      case 8: try {
        var v: Llbuild3_Error?
        var hadOneofValue = false
        if let current = self.value {
          hadOneofValue = true
          if case .errorValue(let m) = current {v = m}
        }
        try decoder.decodeSingularMessageField(value: &v)
        if let v = v {
          if hadOneofValue {try decoder.handleConflictingOneOf()}
          self.value = .errorValue(v)
        }
      }()
      default: break
      }
    }
  }

  public func traverse<V: SwiftProtobuf.Visitor>(visitor: inout V) throws {
    // The use of inline closures is to circumvent an issue where the compiler
    // allocates stack space for every if/case branch local when no optimizations
    // are enabled. https://github.com/apple/swift-protobuf/issues/1034 and
    // https://github.com/apple/swift-protobuf/issues/1182
    if !self.name.isEmpty {
      try visitor.visitSingularStringField(value: self.name, fieldNumber: 1)
    }
    switch self.value {
    case .intValue?: try {
      guard case .intValue(let v)? = self.value else { preconditionFailure() }
      try visitor.visitSingularInt64Field(value: v, fieldNumber: 2)
    }()
    case .uintValue?: try {
      guard case .uintValue(let v)? = self.value else { preconditionFailure() }
      try visitor.visitSingularUInt64Field(value: v, fieldNumber: 3)
    }()
    case .stringValue?: try {
      guard case .stringValue(let v)? = self.value else { preconditionFailure() }
      try visitor.visitSingularStringField(value: v, fieldNumber: 4)
    }()
    case .boolValue?: try {
      guard case .boolValue(let v)? = self.value else { preconditionFailure() }
      try visitor.visitSingularBoolField(value: v, fieldNumber: 5)
    }()
    case .doubleValue?: try {
      guard case .doubleValue(let v)? = self.value else { preconditionFailure() }
      try visitor.visitSingularDoubleField(value: v, fieldNumber: 6)
    }()
    case .casObject?: try {
      guard case .casObject(let v)? = self.value else { preconditionFailure() }
      try visitor.visitSingularMessageField(value: v, fieldNumber: 7)
    }()
    case .errorValue?: try {
      guard case .errorValue(let v)? = self.value else { preconditionFailure() }
      try visitor.visitSingularMessageField(value: v, fieldNumber: 8)
    }()
    case nil: break
    }
    try unknownFields.traverse(visitor: &visitor)
  }

  public static func ==(lhs: Llbuild3_Stat, rhs: Llbuild3_Stat) -> Bool {
    if lhs.name != rhs.name {return false}
    if lhs.value != rhs.value {return false}
    if lhs.unknownFields != rhs.unknownFields {return false}
    return true
  }
}
