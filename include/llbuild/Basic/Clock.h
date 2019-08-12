//===- Clock.h --------------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_BASIC_CLOCK_h
#define LLBUILD_BASIC_CLOCK_h

#include <algorithm>
#include <chrono>

namespace llbuild {
namespace basic {

class Clock {
public:
  /// A timestamp is the number of seconds since the clock's epoch time.
  typedef double Timestamp;
  
  Clock() LLBUILD_DELETED_FUNCTION;
  
  /// Returns a global timestamp that represents the current time in seconds since a reference data.
  /// *NOTE*: This function uses a monotonic clock, so don't compare between systems.
  inline static Timestamp now() {
    // steady_clock is monotonic
    auto now = std::chrono::steady_clock::now();
    auto difference = std::chrono::duration_cast<std::chrono::duration<double>>(now.time_since_epoch());
    return difference.count();
  }
};

}
}

#endif
