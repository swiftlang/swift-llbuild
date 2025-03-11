//===- Logging.h ------------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD3_LOGGING_H
#define LLBUILD3_LOGGING_H

#include <memory>
#include <optional>
#include <string>

#include "llbuild3/Common.h"
#include "llbuild3/Common.pb.h"
#include "llbuild3/Error.pb.h"


namespace llbuild3 {

class ClientContext {
private:
  void* ctx;
  std::function<void (void*)> releaseFn;

public:
  ClientContext(void* ctx, std::function<void (void*)> releaseFn)
    : ctx(ctx), releaseFn(releaseFn) { }
  ~ClientContext() {
    if (ctx && releaseFn) releaseFn(ctx);
  }

  void* get() const { return ctx; }
};

struct LoggingContext {
  std::optional<EngineID> engine;
  std::shared_ptr<ClientContext> clientContext;
};

class Logger {
public:
  virtual ~Logger() = 0;

  virtual void error(LoggingContext, Error) = 0;
  virtual void event(LoggingContext, const std::vector<Stat>&) = 0;
};

class NullLogger: public Logger {
public:
  NullLogger() { }
  ~NullLogger() { }

  void error(LoggingContext, Error) override;
  void event(LoggingContext, const std::vector<Stat>&) override;
};

}

#endif
