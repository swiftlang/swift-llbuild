//===- SerialQueue.h --------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BASIC_SERIALQUEUE_H
#define LLBUILD_BASIC_SERIALQUEUE_H

#include <functional>

namespace llbuild {
namespace basic {

/// A basic serial operation queue.
class SerialQueue {
  void *impl;

public:
  SerialQueue();
  ~SerialQueue();

  /// Add an operation and wait for it to complete.
  void sync(std::function<void(void)> fn);

  /// Add an operation to the queue and return.
  void async(std::function<void(void)> fn);
};

}
}

#endif
