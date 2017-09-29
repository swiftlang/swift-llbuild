//===- Tracing.h ------------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_BASIC_TRACING_H
#define LLBUILD_BASIC_TRACING_H

#include "llvm/ADT/StringRef.h"

namespace llbuild {

/// Tracing Kind Codes
///
/// These are currently global across the entire library, please take care to
/// not unnecessarily cause them to reorder as it will prevent use of prior
/// tracing data.
enum class TraceEventKind {
  // Execution Queue
    
  /// An individual job execution interval.
  ExecutionQueueJob = 0,
      
  /// A subprocess launch.
  ExecutionQueueSubprocess = 1,

  /// A callback from the task, \see EngineTaskCallbackKind.
  EngineTaskCallback = 2,

  /// An event on the engine processing queue, \see EngineQueueItemKind.
  EngineQueueItemEvent = 3,

  /// A point event to track the depth of the execution queue
  ExecutionQueueDepth = 4,
};

// Engine Task Callbacks
enum class EngineTaskCallbackKind {
  Start = 0,
  ProvidePriorValue,
  ProvideValue,
  InputsAvailable,
};

// Engine Queue Processing
enum class EngineQueueItemKind {
  RuleToScan = 0,
  InputRequest,
  FinishedInputRequest,
  ReadyTask,
  FinishedTask,
  Waiting,
};

/// An RAII type to define an individual tracing point.
struct TracingPoint {
  const uint32_t kind;
  const uint64_t arg1;
  const uint64_t arg2;
  const uint64_t arg3;
  const uint64_t arg4;

  TracingPoint(TraceEventKind kind, uint64_t arg1 = 0, uint64_t arg2 = 0,
                uint64_t arg3 = 0, uint64_t arg4 = 0)
    : kind(uint32_t(kind)), arg1(arg1), arg2(arg2), arg3(arg3), arg4(arg4)
  {
  }
};

/// An RAII type to define an individual tracing interval.
///
/// The client may modify the values of the arguments after initialization, for
/// example to submit additional metrics for the event kind as part of the
/// interval completion event.
struct TracingInterval {
  const uint32_t kind;
  uint64_t arg1;
  uint64_t arg2;
  uint64_t arg3;
  uint64_t arg4;

  TracingInterval(TraceEventKind kind, uint64_t arg1 = 0, uint64_t arg2 = 0,
                  uint64_t arg3 = 0, uint64_t arg4 = 0)
      : kind(uint32_t(kind)), arg1(arg1), arg2(arg2), arg3(arg3), arg4(arg4)
  {
  }
  ~TracingInterval() {
  }

  // MARK: Utility Wrappers
  
  TracingInterval(EngineTaskCallbackKind arg1)
    : TracingInterval(TraceEventKind::EngineTaskCallback, uint64_t(arg1)) {}

  TracingInterval(EngineQueueItemKind arg1)
    : TracingInterval(TraceEventKind::EngineQueueItemEvent, uint64_t(arg1)) {}
};

/// An RAII type to define a string.
struct TracingString {
  const uint32_t kind;

  /// The integer code for the string, which can be provided to a trace point or
  /// interval.
  const uint64_t value;

  TracingString(TraceEventKind kind, llvm::StringRef str)
      : kind(uint32_t(kind)), value(0) {}

  operator uint64_t() const { return value; }
};
  
}

#endif
