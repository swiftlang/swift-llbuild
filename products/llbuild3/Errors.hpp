//===- Errors.hpp -----------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD3_CORE_ERRORS_H
#define LLBUILD3_CORE_ERRORS_H

#include <cstdint>

namespace llbuild3 {
namespace core {

enum EngineError: uint64_t {
  // 100 - Graph Errors
  NoArtifactProducer = 100,
  NoProviderForRule = 101,

  // 200 - Client Implementation Errors
  DuplicateRuleProvider = 200,
  DuplicateArtifactProvider = 201,
  DuplicateRule = 202,
  DuplicateRuleArtifact = 203,
  DuplicateTask = 204,
  DuplicateTaskArtifact = 205,
  UnrequestedInput = 206,
  InvalidNextState = 207,
  RuleConstructionFailed = 208,
  TaskConstructionFailed = 209,
  TaskPropertyViolation = 210,
  InvalidEngineState = 211,

  // 1000 - Engine Internal Errors
  Unimplemented = 1000,
  InternalInconsistency = 1001,
  InternalProtobufSerialization = 1002,

  // Unknown
  Unknown = 0
};

}
}

#endif /* Header_h */
