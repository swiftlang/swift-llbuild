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

// os_signpost is included in mac OS 10.14, if that header is not available, we don't trace at all.
#if __has_include(<os/signpost.h>)
#include <os/signpost.h>

/// Returns the singleton instance of os_log_t to use with os_log and os_signpost API.
API_AVAILABLE(macos(10.12), ios(10.0), watchos(3.0), tvos(10.0))
static os_log_t getLog() {
  static os_log_t generalLog;
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    generalLog = os_log_create("org.swift.llbuild", "tracing");
  });
  return generalLog;
}

/// Returns a singleton id to use for all signpost calls during the lifetime of the program.
API_AVAILABLE(macos(10.14), ios(12.0), tvos(12.0), watchos(5.0))
static os_signpost_id_t signpost_id() {
  static os_signpost_id_t signpost_id;
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
      signpost_id = os_signpost_id_generate(getLog());
  });
  return signpost_id;
}

/// Begin an interval if tracing is enabled.
#define LLBUILD_TRACE_INTERVAL_BEGIN(name, ...) { \
if (__builtin_available(macOS 10.14, *)) os_signpost_interval_begin(getLog(), signpost_id(), name, ##__VA_ARGS__); \
}

/// End an interval if tracing is enabled.
#define LLBUILD_TRACE_INTERVAL_END(name, ...) { \
if (__builtin_available(macOS 10.14, *)) os_signpost_interval_end(getLog(), signpost_id(), name, ##__VA_ARGS__); \
}

/// Trace an event without duration at a point in time.
#define LLBUILD_TRACE_POINT(name, ...) { \
if (__builtin_available(macOS 10.12, *)) os_log(getLog(), ##__VA_ARGS__); \
}

#else

// Define dummy definitions to do nothing
#define LLBUILD_TRACE_POINT(name, ...) { (void)(name); }
#define LLBUILD_TRACE_INTERVAL_BEGIN(name, ...) { (void)(name); }
#define LLBUILD_TRACE_INTERVAL_END(name, ...) { (void)(name); }
#define LLBUILD_TRACING_SET_ENABLED(on) { (void)on; }

#endif // __has_include(<os/signpost.h>)

namespace llbuild {
  
extern bool TracingEnabled;
  
struct TracingExecutionQueueJob {
  TracingExecutionQueueJob(int laneNumber, llvm::StringRef commandName) {
    if (!TracingEnabled) return;
    LLBUILD_TRACE_INTERVAL_BEGIN("execution_queue_job", "lane:%d;command:%s", laneNumber, commandName.str().c_str());
  }
  
  ~TracingExecutionQueueJob() {
    if (!TracingEnabled) return;
    LLBUILD_TRACE_INTERVAL_END("execution_queue_job");
  }
};

struct TracingExecutionQueueSubprocess {
  TracingExecutionQueueSubprocess(uint32_t laneNumber,
                                  llvm::StringRef commandName)
      : laneNumber(laneNumber), pid(0), utime(0), stime(0), maxrss(0) {
    if (!TracingEnabled) return;
    LLBUILD_TRACE_INTERVAL_BEGIN("execution_queue_subprocess", "lane:%d;command:%s", laneNumber, commandName.str().c_str());
  }
  
  void update(pid_t pid, uint64_t utime, uint64_t stime, long maxrss) {
    this->pid = pid;
    this->utime = utime;
    this->stime = stime;
    this->maxrss = maxrss;
  }
  
  ~TracingExecutionQueueSubprocess() {
    if (!TracingEnabled) return;
    LLBUILD_TRACE_INTERVAL_END("execution_queue_subprocess", "lane:%d;pid:%d;utime:%llu;stime:%llu;maxrss:%ld", laneNumber, pid, utime, stime, maxrss);
  }
  
private:
  uint32_t laneNumber;
  pid_t pid;
  uint64_t utime;
  uint64_t stime;
  long maxrss;
};

// Engine Task Callbacks
enum class EngineTaskCallbackKind {
  Start = 0,
  ProvidePriorValue,
  ProvideValue,
  InputsAvailable,
};

struct TracingEngineTaskCallback {
  TracingEngineTaskCallback(EngineTaskCallbackKind kind, uint64_t key)
      : key(key) {
    if (!TracingEnabled) return;
    LLBUILD_TRACE_INTERVAL_BEGIN("engine_task_callback", "key:%llu;kind:%d", key, uint32_t(kind));
  }
  
  ~TracingEngineTaskCallback() {
    if (!TracingEnabled) return;
    LLBUILD_TRACE_INTERVAL_END("engine_task_callback", "key:%llu", key);
  }
private:
  uint64_t key;
};

// Engine Queue Processing
enum class EngineQueueItemKind {
  RuleToScan = 0,
  InputRequest,
  FinishedInputRequest,
  ReadyTask,
  FinishedTask,
  Waiting,
  FindingCycle,
  BreakingCycle,
};

struct TracingEngineQueueItemEvent {
  TracingEngineQueueItemEvent(EngineQueueItemKind kind, char const *key)
      : key(key) {
    if (!TracingEnabled) return;
    LLBUILD_TRACE_INTERVAL_BEGIN("engine_queue_item_event", "key:%s;kind:%d", key, kind);
  }
  
  ~TracingEngineQueueItemEvent() {
    if (!TracingEnabled) return;
    LLBUILD_TRACE_INTERVAL_END("engine_queue_item_event", "key:%s", key)
  }
private:
  char const *key;
};
  
inline void TracingExecutionQueueDepth(uint64_t depth) {
  if (!TracingEnabled) return;
  LLBUILD_TRACE_POINT("execution_queue_depth", "depth:%llu", depth);
}
  
}

#endif
