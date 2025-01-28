//===- Subtask.h ------------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2025 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD3_SUBTASK_H
#define LLBUILD3_SUBTASK_H

#include <any>
#include <functional>
#include <unordered_map>
#include <variant>

#include <llbuild3/Result.hpp>

#include "llbuild3/Error.pb.h"

namespace llbuild3 {

class CASDatabase;

class SubtaskInterface {
  friend class ExtTaskInterface;
private:
  void* impl;
  uint64_t ctx;

public:
  SubtaskInterface(void* impl, uint64_t ctx) : impl(impl), ctx(ctx) {}

  std::shared_ptr<CASDatabase> cas();
};

typedef std::function<result<std::any, Error> (SubtaskInterface)> SyncSubtask;

typedef std::function<void(result<std::any, Error>)> SubtaskCallback;
typedef std::function<void (SubtaskInterface, SubtaskCallback)> AsyncSubtask;

typedef std::variant<SyncSubtask, AsyncSubtask> Subtask;

typedef result<std::any, Error> SubtaskResult;

typedef std::unordered_map<uint64_t, SubtaskResult> SubtaskResults;

}

#endif
