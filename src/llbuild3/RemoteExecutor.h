//===- RemoteExecutor.h -----------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD3_REMOTEEXECUTOR_H
#define LLBUILD3_REMOTEEXECUTOR_H

#include <functional>

#include <llbuild3/Result.hpp>

#include "llbuild3/ActionExecutor.h"
#include "llbuild3/CAS.pb.h"
#include "llbuild3/Common.h"
#include "llbuild3/Error.pb.h"

namespace llbuild3 {

typedef uuids::uuid RemoteActionID;

class RemoteExecutor {
public:
  RemoteExecutor() { }
  virtual ~RemoteExecutor() = 0;

  virtual std::string builtinExecutable() const = 0;

  virtual void prepare(std::string execPath,
                       std::function<void(result<CASID, Error>)> res) = 0;

  virtual void execute(
    const CASID& functionID,
    const Action& action,
    std::function<void(result<RemoteActionID, Error>)> dispatchedFn,
    std::function<void(result<ActionResult, Error>)> resultFn
  ) = 0;
};

}

#endif
