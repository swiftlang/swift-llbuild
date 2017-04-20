//===- RedirectedProcess.h --------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_BASIC_REDIRECTEDPROCESS_H
#define LLBUILD_BASIC_REDIRECTEDPROCESS_H

#include <signal.h>

namespace llbuild {
namespace basic {
/// This is an abstraction that spawns, reads, waits and kills a process.
/// The implementation is platform specific.
class RedirectedProcess {
 public:
  using ProcessId = pid_t;
  ProcessId processId;

  RedirectedProcess(ProcessId processId) { this->processId = processId; }

  /// Sends the specified signal to the process.
  void sendSignal(int signal) const;

  struct Hash {
    size_t operator()(const llbuild::basic::RedirectedProcess& x) const {
      return x.processId;
    }
  };

  bool operator==(const RedirectedProcess& rhs) const {
    return processId == rhs.processId;
  }
};
}
}

#endif
