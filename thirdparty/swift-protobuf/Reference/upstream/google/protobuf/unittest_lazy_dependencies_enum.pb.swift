// DO NOT EDIT.
// swift-format-ignore-file
//
// Generated by the Swift generator plugin for the protocol buffer compiler.
// Source: google/protobuf/unittest_lazy_dependencies_enum.proto
//
// For information on using the generated types, please see the documentation:
//   https://github.com/apple/swift-protobuf/

// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

// Author: trafacz@google.com (Todd Rafacz)
//  Based on original Protocol Buffers design by
//  Sanjay Ghemawat, Jeff Dean, and others.
//
// A proto file we will use for unit testing.

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

enum ProtobufUnittest_LazyImports_LazyEnum: SwiftProtobuf.Enum, Swift.CaseIterable {
  typealias RawValue = Int
  case lazyEnum0 // = 0
  case lazyEnum1 // = 1

  init() {
    self = .lazyEnum0
  }

  init?(rawValue: Int) {
    switch rawValue {
    case 0: self = .lazyEnum0
    case 1: self = .lazyEnum1
    default: return nil
    }
  }

  var rawValue: Int {
    switch self {
    case .lazyEnum0: return 0
    case .lazyEnum1: return 1
    }
  }

}

// MARK: - Code below here is support for the SwiftProtobuf runtime.

extension ProtobufUnittest_LazyImports_LazyEnum: SwiftProtobuf._ProtoNameProviding {
  static let _protobuf_nameMap: SwiftProtobuf._NameMap = [
    0: .same(proto: "LAZY_ENUM_0"),
    1: .same(proto: "LAZY_ENUM_1"),
  ]
}