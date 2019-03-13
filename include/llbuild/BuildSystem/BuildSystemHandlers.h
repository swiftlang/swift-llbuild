//===- BuildSystemHandlers.h ------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BUILDSYSTEM_BUILDSYSTEMHANDLERS_H
#define LLBUILD_BUILDSYSTEM_BUILDSYSTEMHANDLERS_H

#include "llbuild/Basic/Subprocess.h"

#include <memory>

namespace llbuild {
namespace basic {
  class QueueJobContext;
}
namespace core {
  class Task;
}

namespace buildsystem {

class BuildSystemCommandInterface;
class ExternalCommand;

class HandlerState {
public:
  explicit HandlerState();
  virtual ~HandlerState();
};
  
class ShellCommandHandler {
public:
  explicit ShellCommandHandler();
  virtual ~ShellCommandHandler();
  
  virtual std::unique_ptr<HandlerState>
  start(BuildSystemCommandInterface&, ExternalCommand* command) const = 0;

  virtual void
  execute(HandlerState*, ExternalCommand* command, BuildSystemCommandInterface&,
          core::Task* task, basic::QueueJobContext* context,
          basic::ProcessCompletionFn) const = 0;
};

}
}

#endif
