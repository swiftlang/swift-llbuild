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
//
// This file contains abstractions for creating a process and redirecting
// input and output of that process.
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_BASIC_REDIRECTEDPROCESS_H
#define LLBUILD_BASIC_REDIRECTEDPROCESS_H

#include "llvm/ADT/SmallString.h"

#include <mutex>
#include <vector>

namespace llbuild {
namespace basic {
namespace sys {
class RedirectedProcess {
  bool shouldCaptureOutput;
  struct ProcessInfo *innerProcessInfo;

public:
  RedirectedProcess(bool shouldCaptureOutput);

  bool openPipe();

  bool execute(const char *path, bool setGroupFlags, char *const *args,
               char *const *envp, std::mutex &spawnedProcessMutex);

  bool readPipe(llvm::SmallString<1024> &output);

  bool waitForCompletion(int *exitStatus);

  bool kill(int signal);

  bool operator==(const RedirectedProcess &rhs) const;
  size_t hash() const;

  ~RedirectedProcess();
  
  static int sigkill();

  static bool isProcessCancelledStatus(int status);
};
}
}
}

namespace std {
template <> struct hash<llbuild::basic::sys::RedirectedProcess> {
  size_t operator()(const llbuild::basic::sys::RedirectedProcess &x) const {
    return x.hash();
  };
};
}

#endif
