//===- Defer.h --------------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2019 - 2024 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef TRITIUM_SUPPORT_DEFER_H
#define TRITIUM_SUPPORT_DEFER_H

#include <functional>
#include <utility>
#include <type_traits>

namespace tritium {
namespace support {

template <typename T>
class ScopeDefer {
  T deferredWork;
  void operator=(ScopeDefer&) = delete;
public:
  ScopeDefer(T&& work) : deferredWork(std::move(work)) { }
  ~ScopeDefer() { deferredWork(); }
};

template <typename T>
ScopeDefer<T> makeScopeDefer(T&& work) {
  return ScopeDefer<typename std::decay<T>::type>(std::forward<T>(work));
}

namespace impl {
  struct ScopeDeferTask {};
  template<typename T>
  ScopeDefer<typename std::decay<T>::type> operator+(ScopeDeferTask, T&& work) {
    return ScopeDefer<typename std::decay<T>::type>(std::forward<T>(work));
  }
}

}
}

// These generate a unique variable name for each use of defer in the
// translation unit.
#define DEFER_VAR_NAME(C) _defer_##C
#define DEFER_INTERMEDIATE(C) DEFER_VAR_NAME(C)
#define DEFER_UNIQUE_VAR_NAME DEFER_INTERMEDIATE(__COUNTER__)

/// Runs the following function/lambda body when the current scope exits.
/// Typical use looks like:
///
///   llbuild_defer {
///     deferred work
///   };
///
#define tritium_defer \
  auto DEFER_UNIQUE_VAR_NAME = tritium::support::impl::ScopeDeferTask() + [&]()

#endif
